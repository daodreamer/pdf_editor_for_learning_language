#include "shellstate.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStyleHints>
#include <QInputMethod>
#include <algorithm>
#include <cmath>

ShellState::ShellState(QObject* parent) : QObject(parent) {
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this] { if (themeMode_ == 0) emit themeChanged(); });
    worker_ = new PdfWorker;
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &PdfWorker::stopped, &thread_, &QThread::quit, Qt::DirectConnection);
    connect(worker_, &PdfWorker::frameReady, this, &ShellState::acceptFrame);
    connect(worker_, &PdfWorker::nativeClosed, this, &ShellState::documentClosed);
    connect(worker_, &PdfWorker::requestStarted, this, &ShellState::renderStarted);
    connect(worker_, &PdfWorker::discarded, this, &ShellState::renderDiscarded);
    connect(worker_,&PdfWorker::scanProgress,this,[this](quint64 session,int page,int pages){
        if(session!=session_)return;
        exportStatus_=tr("扫描优化 %1 / %2 页…").arg(page).arg(pages);emit storageChanged();
    });
    connect(worker_,&PdfWorker::scanFinished,this,[this](quint64 session,const QString& target,const QString& error){
        scanning_=false;
        if(session!=session_)return;
        exportStatus_=error.isEmpty()?tr("扫描优化完成，已创建新 PDF"):error;emit storageChanged();
        if(error.isEmpty()&&!shuttingDown_)selectFile(QUrl::fromLocalFile(target));
    });
    connect(&thread_, &QThread::finished, this, [this] {
        if(!ocrThread_.isRunning()) {shutdownComplete_ = true;emit shutdownChanged();}
    });
    connect(&ocrThread_,&QThread::finished,this,[this] {
        if(shuttingDown_ && !thread_.isRunning()) {shutdownComplete_=true;emit shutdownChanged();}
    });
    scheduleTimer_.setSingleShot(true);
    scheduleTimer_.setInterval(ReadingConfig::viewportDelayMs);
    connect(&scheduleTimer_, &QTimer::timeout, this, &ShellState::schedule);
    connect(&model_, &ReadingModel::geometryChanged, this, [this] {
        syncAnnotations();
        emit layoutChanged();
    });
    connect(&textSelection_, &TextSelection::demandChanged, this, [this] {
        updateOcrDemand();
        if (!inFlight_ && !scheduleTimer_.isActive()) scheduleTimer_.start();
    });
    connect(worker_, &PdfWorker::requestFinished, this, [this](quint64 revision) {
        if (revision != revision_ || shuttingDown_) return;
        inFlight_ = false;
        scheduleTimer_.start();
    });
    initializeStorage();
    thread_.setObjectName(QStringLiteral("PDFium"));
    thread_.start();
}

ShellState::~ShellState() {
    if(!shuttingDown_ && ocr_)ocr_->invalidate(++ocrRevision_);
    ocrThread_.quit(); ocrThread_.wait();
    storageThread_.quit(); storageThread_.wait();
    if (thread_.isRunning()) {
        if (!shuttingDown_) worker_->shutdown();
        thread_.wait(); // safety fallback; normal window closing is asynchronous
    }
}

bool ShellState::dark() const {
    return themeMode_ == 2 || (themeMode_ == 0 &&
        QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
}

void ShellState::selectFile(const QUrl& url) {
    if (url.isEmpty() || shuttingDown_ || scanning_) return;
    if(!applyingTransition_) { flushBefore([this,url] { selectFile(url); }); return; }
    const QFileInfo file(url.toLocalFile());
    if (!url.isLocalFile() || url.toLocalFile().isEmpty()) {
        statusText_ = QStringLiteral("请选择可读取的本地 PDF 文件。");
        errorCode_ = QStringLiteral("file");
    } else {
        ++session_;
        QGuiApplication::inputMethod()->reset();
        restoring_=true; annotations_.reset(); restoring_=false;
        storageReady_=false; loadingStorage_=true; editRevision_=savedRevision_=0; identity_.clear();
        exportStatus_.clear();
        storageStatus_=QStringLiteral("正在校验 PDF 并恢复练习…"); emit storageChanged();
        textSelection_.reset();
        resetOcr();
        emit answerLinesChanged();
        path_ = file.absoluteFilePath();
        selectedName_ = file.fileName();
        const auto storageSession=session_; const auto storagePath=path_;
        QMetaObject::invokeMethod(storage_,[this,storageSession,storagePath] { storage_->open(storageSession,storagePath); },Qt::QueuedConnection);
        page_ = 0;
        pageCount_ = 0;
        model_.reset(0);
        failed_.clear();
        metadataCursor_ = 0;
        emit imageChanged();
        request();
    }
    emit selectionChanged();
}

void ShellState::clearSelection() {
    if (shuttingDown_ || scanning_) return;
    if(!applyingTransition_) { flushBefore([this] { clearSelection(); }); return; }
    storageReady_=false; loadingStorage_=false; editRevision_=savedRevision_=0;
    QMetaObject::invokeMethod(storage_,&StorageWorker::close,Qt::QueuedConnection);
    storageStatus_.clear(); exportStatus_.clear(); emit storageChanged();
    ++session_;
    QGuiApplication::inputMethod()->reset();
    restoring_=true; annotations_.reset(); restoring_=false;
    textSelection_.reset();
    resetOcr();
    emit answerLinesChanged();
    path_.clear();
    selectedName_.clear();
    statusText_.clear();
    errorCode_.clear();
    pageCount_ = 0;
    page_ = 0;
    model_.reset(0);
    busy_ = false;
    worker_->submit({session_, ++revision_, {}, 0, renderWidth_});
    emit imageChanged();
    emit selectionChanged();
}

void ShellState::invalidate() {
    ++revision_;
    worker_->cancel(revision_);
    inFlight_ = false;
    failed_.clear();
    if (!scheduleTimer_.isActive()) scheduleTimer_.start();
}
void ShellState::request() {
    invalidate();
    busy_ = pageCount_ == 0 || !model_.ready(page_,viewportAttached_ ? model_.targetWidth(page_,dpr_) : renderWidth_);
    errorCode_.clear();
    statusText_ = busy_ ? QStringLiteral("正在加载页面…") : QStringLiteral("已显示");
    schedule();
    emit selectionChanged();
}
void ShellState::schedule() {
    if (shuttingDown_ || path_.isEmpty() || inFlight_) return;
    int candidate = -1;
    int width = renderWidth_;
    if (!pageCount_) {
        if (failed_.contains(0)) return;
        candidate = 0;
    } else {
        const auto pages = model_.priority();
        for (const bool visibleOnly : {true,false}) {
            for (int p : pages) {
                if (visibleOnly != model_.visible(p) || failed_.contains(p)) continue;
                if (model_.image(p).isNull()) { candidate=p; break; }
            }
            if (candidate < 0) for (int p : pages) {
                const int target = viewportAttached_ ? model_.targetWidth(p,dpr_) : renderWidth_;
                if (visibleOnly == model_.visible(p) && !failed_.contains(p) && !model_.ready(p,target)) { candidate=p; break; }
            }
            if (candidate >= 0) break;
        }
        if (candidate >= 0) width = viewportAttached_ ? model_.targetWidth(candidate,dpr_) : renderWidth_;
    }
    // Missing visible previews come first. Text then precedes resolution refinement and idle metadata.
    const int textPage = pageCount_ ? textSelection_.nextPage(model_.priority()) : -1;
    const bool textTask = textPage >= 0 && (candidate < 0 || !model_.visible(candidate) || !model_.image(candidate).isNull());
    updateOcrDemand();
    int ocrPage=-1;
    if(ocrBusyPage_<0 && !ocrDemand_.isEmpty() && storageReady_) {
        for(int p : model_.priority()) if(ocrDemand_.contains(p)) { ocrPage=p; break; }
        if(ocrPage<0 && !textSelection_.selectedPages().isEmpty())
            for(int p : textSelection_.selectedPages()) if(ocrDemand_.contains(p)) { ocrPage=p; break; }
    }
    const bool ocrTask=ocrPage>=0 && !textTask && (candidate<0 || !model_.visible(candidate) || !model_.image(candidate).isNull());
    PdfRequest r{session_,++revision_,path_,std::max(0,candidate),width};
    if (textTask) { r.page=textPage; r.textOnly=true; }
    if (ocrTask) { r.page=ocrPage; r.width=ReadingConfig::ocrWidth; r.ocrOnly=true; }
    r.maxBytes = model_.pageBudget();
    r.skipPreview = viewportAttached_;
    if(ocrTask) { r.skipPreview=true; r.maxBytes=ReadingConfig::ocrImageBytes; }
    if (!textTask && !ocrTask && viewportAttached_ && candidate >= 0 && model_.image(candidate).isNull() && width > ReadingConfig::previewWidth) {
        r.width = ReadingConfig::previewWidth;
        r.previewOnly = true;
    }
    if (textTask || ocrTask) {
        // No bitmap allocation, and no renderJobs increment for text extraction.
    } else if (candidate < 0) {
        while (metadataCursor_ < pageCount_ && model_.known(metadataCursor_)) ++metadataCursor_;
        if (metadataCursor_ >= pageCount_) {
            busy_ = false;
            emit selectionChanged();
            return;
        }
        r.metadataStart = metadataCursor_;
    } else ++model_.renderJobs;
    requestedWidth_ = r.width;
    inFlight_ = true;
    worker_->submit(std::move(r));
}
void ShellState::goToPage(int page) {
    if (shuttingDown_ || page < 1 || page > pageCount_) return;
    annotations_.cancelGesture();
    page_ = page - 1;
    model_.setViewport(model_.top(page_),viewportHeight_,viewportWidth_,zoom_);
    finishTextEditing();
    annotations_.setActivePages(model_.priority());
    emit layoutChanged();
    emit imageChanged();
    model_.recordDemand(page_,viewportAttached_ ? model_.targetWidth(page_,dpr_) : renderWidth_);
    request();
}
void ShellState::setRenderWidth(int pixels) {
    pixels = qBound(64,pixels,8192);
    if (shuttingDown_ || renderWidth_ == pixels) return;
    renderWidth_ = pixels;
    if (!path_.isEmpty() && pageCount_ > 0) request();
}
void ShellState::updateViewport(double y,double height,double width,double dpr) {
    if (shuttingDown_ || !std::isfinite(y) || !std::isfinite(height) || !std::isfinite(width)
        || !std::isfinite(dpr) || dpr <= 0 || height <= 0 || width <= 0) return;
    const auto oldPages = model_.priority();
    const auto oldWidth = model_.targetWidth(page_,dpr_);
    const bool scrolled=std::abs(y-model_.offset())>0.5;
    viewportAttached_ = true;
    viewportWidth_ = width; viewportHeight_ = height; dpr_ = dpr;
    model_.setViewport(y,height,width,zoom_);
    const int first=model_.pageAt(model_.offset());
    const int columns=doublePage()?2:1;
    if(!model_.visible(page_) || (scrolled && page_/columns!=first/columns))page_=first;
    if (oldPages != model_.priority()) syncAnnotations();
    busy_ = pageCount_ > 0 && !failed_.contains(page_) && !model_.ready(page_,model_.targetWidth(page_,dpr_));
    emit selectionChanged(); emit imageChanged();
    if (oldPages != model_.priority() || oldWidth != model_.targetWidth(page_,dpr_)) {
        for (int p : model_.priority()) if (model_.visible(p)) model_.recordDemand(p,model_.targetWidth(p,dpr_));
        invalidate();
    }
    else if (!inFlight_ && !scheduleTimer_.isActive()) scheduleTimer_.start();
}
void ShellState::focusPage(int page) {
    if(shuttingDown_||page<0||page>=pageCount_||page_==page)return;
    page_=page;
    busy_=!model_.ready(page_,model_.targetWidth(page_,dpr_));
    emit selectionChanged();emit imageChanged();
}
void ShellState::setDoublePage(bool enabled) {
    if(shuttingDown_||doublePage()==enabled)return;
    const int anchor=page_;
    annotations_.cancelGesture();finishTextEditing();textSelection_.clear();
    if(enabled)singlePageZoom_=zoom_;
    model_.setDoublePage(enabled);
    zoom_=enabled?1.0:singlePageZoom_;
    model_.setViewport(model_.offset(),viewportHeight_,viewportWidth_,zoom_);
    if(pageCount_>0)goToPage(anchor+1);
    else { emit layoutChanged();emit selectionChanged(); }
}
void ShellState::setZoom(double zoom) {
    if (shuttingDown_ || !std::isfinite(zoom)) return;
    zoom = std::clamp(zoom,0.25,4.0);
    if (zoom_ == zoom) return;
    annotations_.cancelGesture();
    zoom_ = zoom;
    model_.setViewport(model_.offset(),viewportHeight_,viewportWidth_,zoom_);
    emit layoutChanged();
    model_.recordDemand(page_,model_.targetWidth(page_,dpr_));
    request();
}
void ShellState::acceptFrame(const PdfFrame& frame) {
    if (shuttingDown_ || frame.session != session_ || frame.revision != revision_) {
        emit renderDiscarded(frame.revision);
        return;
    }
    if (frame.geometry.valid) {
        annotations_.setGeometry(frame.page,frame.geometry);
        annotations_.setActivePages(model_.priority());
    }
    if(frame.ocrOnly) {
        if(!ocrDemand_.contains(frame.page))return;
        if(!frame.error.isEmpty()) {
            auto failure=std::make_shared<TextPage>();failure->ocr=true;failure->error=frame.error;
            ocrDemand_.remove(frame.page);textSelection_.put(frame.page,failure);return;
        }
        if(!ocr_) {
            const auto overrideRoot=qEnvironmentVariable("LANGLERN_WORKSPACE_DIR");
            const auto root=overrideRoot.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::CacheLocation):overrideRoot;
            ocr_=new OcrWorker(root+"/ocr-v1");ocr_->moveToThread(&ocrThread_);
            connect(&ocrThread_,&QThread::finished,ocr_,&QObject::deleteLater);
            connect(ocr_,&OcrWorker::finished,this,[this](quint64 session,quint64 revision,int page,TextSnapshot text,bool cached,double ms){
                if(shuttingDown_ || session!=session_ || revision!=ocrRevision_)return;
                ocrBusyPage_=-1;
                textSelection_.put(page,text);
                const bool selectRegion=ocrRegionSelections_.contains(page) && ocrRegionSelections_.take(page)==textSelection_.interactionRevision();
                if(selectRegion && model_.visible(page) && text->region!=QRectF(0,0,1,1) && text->error.isEmpty()) {
                    QPointF first,last;bool found=false;
                    for(const auto& c:text->characters)if(c.visible){if(!found){first=c.box.center();found=true;}last=c.box.center();}
                    if(found) {textSelection_.begin(page,first.x(),first.y(),0,0);textSelection_.extend(page,last.x(),last.y());}
                }
                ocrStatus_=text->error.isEmpty()?(text->characters.isEmpty()?tr("当前页未识别到文字，请调整 OCR 语言。"):tr("第 %1 页离线识别完成").arg(page+1)):text->error;
                qInfo("ocr_result session=%llu page=%d chars=%lld cached=%d elapsed_ms=%.3f",session,page,qint64(text->characters.size()),cached,ms);
                emit ocrChanged();scheduleTimer_.start();
            });
            ocrThread_.setObjectName("Offline OCR");ocrThread_.start(QThread::LowPriority);
        }
        ocrDemand_.remove(frame.page);ocrBusyPage_=frame.page;
        ocr_->invalidate(++ocrRevision_);
        const auto pending=textSelection_.snapshot(frame.page);
        OcrRequest request{session_,ocrRevision_,frame.page,identity_,ocrLanguage_,frame.image,pending?pending->region:QRectF(0,0,1,1)};
        ocr_->submit(std::move(request));
        return;
    }
    if (frame.textOnly) {
        model_.setSizes(frame.page,{frame.points});
        if (frame.text) textSelection_.put(frame.page,frame.text);
        else {
            auto failure=std::make_shared<TextPage>(); failure->error=frame.error;
            textSelection_.put(frame.page,std::move(failure));
        }
        updateOcrDemand();
        return;
    }
    if (frame.metadataStart >= 0) {
        metadataCursor_ = frame.metadataStart + int(frame.sizes.size());
        model_.setSizes(frame.metadataStart,frame.sizes);
        return;
    }
    errorCode_ = frame.errorCode;
    if (!frame.error.isEmpty()) {
        statusText_ = frame.error;
        failed_.insert(frame.page);
        busy_ = false;
    } else {
        if (pageCount_ != frame.pages) {
            pageCount_ = frame.pages;
            model_.reset(pageCount_);
        }
        model_.setSizes(frame.page,{frame.points});
        model_.put(frame.page,frame.image,requestedWidth_,!frame.preview,frame.answerLines,frame.answerLinesReady);
        if(frame.answerLinesReady) {
            emit answerLinesChanged();
        }
        busy_ = !model_.ready(page_,viewportAttached_ ? model_.targetWidth(page_,dpr_) : renderWidth_);
        statusText_ = frame.preview ? QStringLiteral("预览已显示，正在提高清晰度…")
                    : frame.limited ? QStringLiteral("已显示（受内存预算限制，已降低分辨率）。")
                                    : QStringLiteral("已显示");
    }
    emit imageChanged();
    emit selectionChanged();
}
void ShellState::beginShutdown() {
    if (shuttingDown_) return;
    worker_->cancelScan();
    if(!applyingTransition_) { flushBefore([this] { beginShutdown(); }); return; }
    QMetaObject::invokeMethod(storage_,&StorageWorker::close,Qt::QueuedConnection);
    shuttingDown_ = true;
    QGuiApplication::inputMethod()->reset();
    restoring_=true; annotations_.reset(); restoring_=false;
    textSelection_.reset();
    resetOcr();
    busy_ = false;
    statusText_ = QStringLiteral("正在关闭文档…");
    emit selectionChanged();
    ocrThread_.quit();
    worker_->shutdown();
}

bool ShellState::beginTextSelection(double x,double y) {
    if (shuttingDown_ || !pageCount_ || !std::isfinite(x) || !std::isfinite(y)) return false;
    const int page=model_.pageAtPosition(x,y);
    if(page<0){textSelection_.clear();return false;}
    const double width=model_.pageWidth();
    const double left=model_.left(page);
    if (x<left || x>left+width || y<model_.top(page) || y>model_.top(page)+model_.height(page)) {
        textSelection_.clear(); return false;
    }
    focusPage(page);
    const auto previous=textSelection_.snapshot(page);
    if(previous && previous->ocr && !previous->region.contains(QPointF((x-left)/width,(y-model_.top(page))/model_.height(page))))textSelection_.forgetPage(page);
    ensureOcr(page);
    textSelection_.begin(page,(x-left)/width,(y-model_.top(page))/model_.height(page),3/width,3/model_.height(page));
    return textSelection_.canStart();
}

void ShellState::setOcrSelectionEnabled(bool enabled) {
    if(ocrSelectionEnabled_==enabled)return;
    ocrSelectionEnabled_=enabled;updateOcrDemand();emit ocrChanged();scheduleTimer_.start();
}
void ShellState::setOcrLanguage(QString language) {
    if(language!="fra" && language!="eng" && language!="chi_sim" && language!="fra+eng")return;
    if(ocrLanguage_==language)return;
    resetOcr();ocrLanguage_=std::move(language);textSelection_.clearOcr();updateOcrDemand();emit ocrChanged();scheduleTimer_.start();
}
void ShellState::resetOcr() {
    if(ocr_)ocr_->invalidate(++ocrRevision_);
    ocrDemand_.clear();ocrRegionSelections_.clear();ocrBusyPage_=-1;ocrStatus_.clear();emit ocrChanged();
}
void ShellState::ensureOcr(int page,bool force,QRectF region) {
    if(shuttingDown_ || page<0 || page>=pageCount_ || ocrBusyPage_==page || ocrDemand_.contains(page))return;
    const auto text=textSelection_.snapshot(page);
    if(!force && (!text || text->ocr || !text->error.isEmpty() || !text->characters.isEmpty()))return;
    auto pending=std::make_shared<TextPage>();pending->ocr=true;pending->pending=true;
    pending->region=region;
    ocrDemand_.insert(page);textSelection_.put(page,pending);
    ocrStatus_=tr("正在离线识别第 %1 页…").arg(page+1);emit ocrChanged();scheduleTimer_.start();
}
void ShellState::updateOcrDemand() {
    const auto selected=textSelection_.selectedPages();
    for(auto it=ocrDemand_.begin();it!=ocrDemand_.end();) {
        if(!model_.visible(*it) && !selected.contains(*it)) {
            // Leave a native empty page so a later visit can request OCR again.
            textSelection_.put(*it,std::make_shared<TextPage>());it=ocrDemand_.erase(it);
        } else ++it;
    }
    for(int p:selected)ensureOcr(p);
    if(ocrSelectionEnabled_)for(int p:model_.priority())if(model_.visible(p))ensureOcr(p);
}
void ShellState::recognizeCurrentPage() { ensureOcr(page_,true); }
void ShellState::recognizeRegion(double x1,double y1,double x2,double y2) {
    if(shuttingDown_ || !std::isfinite(x1+y1+x2+y2))return;
    const int page=model_.pageAtPosition(x1,y1);if(page<0)return;
    const QRectF bounds(model_.left(page),model_.top(page),model_.pageWidth(),model_.height(page));
    const auto selected=QRectF(QPointF(x1,y1),QPointF(x2,y2)).normalized().intersected(bounds);
    if(selected.width()<4 || selected.height()<4)return;
    const QRectF region((selected.x()-bounds.x())/bounds.width(),(selected.y()-bounds.y())/bounds.height(),selected.width()/bounds.width(),selected.height()/bounds.height());
    if(ocrBusyPage_>=0 && ocr_)ocr_->invalidate(++ocrRevision_);
    ocrBusyPage_=-1;ocrDemand_.clear();textSelection_.clearOcr();
    focusPage(page);ensureOcr(page,true,region);
    ocrRegionSelections_.insert(page,textSelection_.interactionRevision());
}
bool ShellState::interactiveContentAt(double x,double y) const {
    if(!containsPage(x,y))return true;
    const int page=model_.pageAtPosition(x,y);
    if(page<0)return false;const double width=model_.pageWidth(),height=model_.height(page);
    const QPointF p((x-model_.left(page))/width,(y-model_.top(page))/height);
    const auto g=annotations_.geometry(page);if(!g.valid)return true;
    for(const auto& n:annotations_.snapshot())if(n.page==page){const auto anchor=g.pdfToDisplay.map(n.anchor);
        if(QRectF(anchor,QSizeF(n.width/g.units.width(),n.height/g.units.height())).adjusted(-10/width,-18/height,10/width,10/height).contains(p))return true;}
    return false;
}
bool ShellState::eraseTextAt(double x,double y) {
    if(!containsPage(x,y))return false;
    const int page=model_.pageAtPosition(x,y);
    if(page<0)return false;const auto g=annotations_.geometry(page);if(!g.valid)return false;
    const QPointF p((x-model_.left(page))/model_.pageWidth(),(y-model_.top(page))/model_.height(page));
    auto notes=annotations_.snapshot();std::sort(notes.begin(),notes.end(),[](const auto& a,const auto& b){return a.order>b.order;});
    for(const auto& n:notes)if(n.page==page){const auto anchor=g.pdfToDisplay.map(n.anchor);
        if(QRectF(anchor,QSizeF(n.width/g.units.width(),n.height/g.units.height())).contains(p)){
            focusPage(page);annotations_.select(n.id);annotations_.removeSelected();return true;}}
    return false;
}
void ShellState::extendTextSelection(double x,double y) {
    if (shuttingDown_ || !pageCount_ || !std::isfinite(x) || !std::isfinite(y)) return;
    const int page=model_.pageAtPosition(x,y,true);
    if(page<0)return;
    const double width=model_.pageWidth();
    const double left=model_.left(page);
    textSelection_.extend(page,std::clamp((x-left)/width,0.0,1.0),std::clamp((y-model_.top(page))/model_.height(page),0.0,1.0));
}

void ShellState::cycleTheme() {
    themeMode_ = (themeMode_ + 1) % 3;
    emit themeChanged();
}

void ShellState::finishTextEditing() {
    QGuiApplication::inputMethod()->commit();
    emit textCommitRequested();
    annotations_.finishEditing();
}
QRectF ShellState::answerBlankAt(double x,double y) const {
    if(!containsPage(x,y)) return {};
    const int page=model_.pageAtPosition(x,y);
    const double w=model_.pageWidth(),h=model_.height(page);
    if(model_.image(page).isNull()) return {};
    const QPointF point((x-model_.left(page))/w,(y-model_.top(page))/h);
    const auto g=annotations_.geometry(page);
    if(!g.valid) return {};
    for(const auto& line:model_.answerLines(page)) {
        if(!line.contains(point)) continue;
        bool occupied=false;
        for(const auto& n:annotations_.snapshot()) if(n.page==page) {
            const auto rect=QRectF(g.pdfToDisplay.map(n.anchor),QSizeF(n.width/g.units.width(),n.height/g.units.height()));
            if(rect.intersects(line)) { occupied=true; break; }
        }
        if(!occupied) return {model_.left(page)+line.x()*w,model_.top(page)+line.y()*h,line.width()*w,line.height()*h};
    }
    return {};
}
QString ShellState::createAnswerAt(double x,double y) {
    const auto area=answerBlankAt(x,y);
    if(area.isEmpty()) return {};
    const int page=model_.pageAtPosition(x,y);
    finishTextEditing(); textSelection_.clear(); focusPage(page);
    const auto g=annotations_.geometry(page);
    return annotations_.create(page,(area.x()-model_.left(page))/model_.pageWidth(),
        (area.y()-model_.top(page))/model_.height(page),area.width()/model_.pageWidth()*g.units.width(),area.height()/model_.height(page)*g.units.height());
}
QString ShellState::createTextBox(double x,double y) {
    if(shuttingDown_ || !storageReady_ || transitioning_ || !pageCount_ || !std::isfinite(x) || !std::isfinite(y)) return {};
    const int page=model_.pageAtPosition(x,y);
    if(page<0)return {};
    const double w=model_.pageWidth(), h=model_.height(page), left=model_.left(page);
    if(x<left || x>=left+w || y<model_.top(page) || y>=model_.top(page)+h) return {};
    finishTextEditing(); textSelection_.clear();
    focusPage(page);
    return annotations_.create(page,(x-left)/w,(y-model_.top(page))/h);
}

bool ShellState::containsPage(double x,double y) const {
    if(shuttingDown_ || !storageReady_ || transitioning_ || !pageCount_ || !std::isfinite(x) || !std::isfinite(y)) return false;
    const int page=model_.pageAtPosition(x,y);
    if(page<0)return false;
    const double w=model_.pageWidth(), left=model_.left(page);
    return x>=left && x<left+w && y>=model_.top(page) && y<model_.top(page)+model_.height(page);
}

void ShellState::commitInput() { QGuiApplication::inputMethod()->commit(); }
void ShellState::resetInput() { QGuiApplication::inputMethod()->reset(); }

void ShellState::syncAnnotations() {
    const auto active=model_.priority();
    if (!annotations_.selectedId().isEmpty() && !active.contains(annotations_.selectedPage())) finishTextEditing();
    annotations_.setActivePages(active);
}

static bool initializeAnnotationFont() {
    static int font=-1;
    if(font<0) font=QFontDatabase::addApplicationFont(QCoreApplication::applicationDirPath()+QStringLiteral("/fonts/NotoSansSC-Regular.ttf"));
    return font>=0;
}
void ShellState::initializeStorage() {
    qRegisterMetaType<AnnotationSnapshot>();
    const auto overrideRoot=qEnvironmentVariable("LANGLERN_WORKSPACE_DIR");
    const auto root=overrideRoot.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/workspaces" : overrideRoot;
    storage_=new StorageWorker(root);
    storage_->moveToThread(&storageThread_);
    connect(&storageThread_,&QThread::finished,storage_,&QObject::deleteLater);
    storageThread_.setObjectName(QStringLiteral("WorkspaceIO")); storageThread_.start(QThread::LowPriority);
    saveTimer_.setSingleShot(true); saveTimer_.setInterval(750);
    maxSaveTimer_.setSingleShot(true); maxSaveTimer_.setInterval(5000);
    connect(&saveTimer_,&QTimer::timeout,this,&ShellState::saveNow);
    connect(&maxSaveTimer_,&QTimer::timeout,this,&ShellState::saveNow);
    connect(&annotations_,&AnnotationModel::contentChanged,this,[this] {
        if(restoring_ || !storageReady_) return;
        ++editRevision_; storageStatus_=QStringLiteral("练习已编辑，等待自动保存…"); emit storageChanged();
        saveTimer_.start(); if(!maxSaveTimer_.isActive()) maxSaveTimer_.start();
    });
    connect(storage_,&StorageWorker::loaded,this,[this](quint64 session,AnnotationSnapshot snapshot,QByteArray identity,QString message,bool writable) {
        if(session!=session_ || shuttingDown_) return;
        if(writable && !initializeAnnotationFont()) { writable=false; message=QStringLiteral("缺少答案字体，请检查程序 fonts 目录；PDF 阅读仍可用。"); }
        loadingStorage_=false; identity_=identity; storageReady_=writable;
        if(writable) { restoring_=true; annotations_.restore(snapshot); restoring_=false; editRevision_=savedRevision_=snapshot.revision; }
        storageStatus_=message.isEmpty() ? QStringLiteral("练习已恢复，自动保存已就绪") : message;
        emit storageChanged();
        if(pendingTransition_) { auto action=std::move(pendingTransition_); pendingTransition_={}; transitioning_=false; flushBefore(std::move(action)); }
    });
    connect(storage_,&StorageWorker::saved,this,[this](quint64 session,quint64 revision,QString error) {
        if(session!=session_) return;
        saving_=false;
        if(!error.isEmpty()) {
            storageStatus_=error; pendingTransition_={}; transitioning_=false;
        } else {
            savedRevision_=revision;
            storageStatus_=savedRevision_==editRevision_ ? QStringLiteral("练习已保存 · 修订 %1").arg(revision) : QStringLiteral("仍有新修改等待保存…");
            if(savedRevision_!=editRevision_) saveNow();
            else if(pendingTransition_) {
                auto action=std::move(pendingTransition_); pendingTransition_={};
                transitioning_=false; applyingTransition_=true; action(); applyingTransition_=false;
            }
        }
        emit storageChanged();
    });
    connect(worker_,&PdfWorker::exportProgress,this,[this](quint64 session,int page,int pages) {
        if(session!=session_) return;
        exportStatus_=QStringLiteral("正在导出标注页 %1/%2；保存 PDF 期间页面任务可能等待…").arg(page).arg(pages); emit storageChanged();
    });
    connect(worker_,&PdfWorker::exportFinished,this,[this](quint64 session,quint64 revision,QString error) {
        exporting_=false;
        if(session==session_) exportStatus_=error.isEmpty() ? QStringLiteral("已导出修订 %1 为新 PDF").arg(revision) : error;
        emit storageChanged();
        if(pendingTransition_) { auto action=std::move(pendingTransition_); pendingTransition_={}; transitioning_=false; flushBefore(std::move(action)); }
    });
}
AnnotationSnapshot ShellState::annotationSnapshot() const {
    return {annotations_.snapshot(),annotations_.inkSnapshot(),editRevision_};
}
void ShellState::saveNow() {
    if(storageReady_) emit textCommitRequested();
    if(!storageReady_ || saving_ || editRevision_==savedRevision_) return;
    saveTimer_.stop(); maxSaveTimer_.stop(); saving_=true;
    storageStatus_=QStringLiteral("正在保存练习…"); emit storageChanged();
    const auto session=session_; const auto snapshot=annotationSnapshot();
    QMetaObject::invokeMethod(storage_,[this,session,snapshot] { storage_->save(session,snapshot); },Qt::QueuedConnection);
}
void ShellState::flushBefore(std::function<void()> action) {
    if(transitioning_) return;
    finishTextEditing(); annotations_.cancelGesture();
    if(loadingStorage_ || exporting_ || saving_ || (storageReady_ && editRevision_!=savedRevision_)) {
        pendingTransition_=std::move(action); transitioning_=true; emit storageChanged(); saveNow(); return;
    }
    applyingTransition_=true; action(); applyingTransition_=false;
}
void ShellState::exportPdf(const QUrl& target) {
    if(shuttingDown_ || transitioning_ || exporting_ || scanning_ || !storageReady_ || !pageCount_) return;
    finishTextEditing(); annotations_.cancelGesture();
    if(!target.isLocalFile()) { exportStatus_=QStringLiteral("请选择本地新 PDF 文件。"); emit storageChanged(); return; }
    exporting_=true; exportStatus_=QStringLiteral("正在导出当前修订；可以继续编辑…"); emit storageChanged();
    const auto session=session_; const auto path=path_; const auto destination=target.toLocalFile(); const auto identity=identity_; const auto snapshot=annotationSnapshot();
    QMetaObject::invokeMethod(worker_,[this,session,path,destination,identity,snapshot] { worker_->exportPdf(session,path,destination,identity,snapshot); },Qt::QueuedConnection);
}
void ShellState::optimizeScan(const QUrl& target,const QVariantMap& options) {
    if(shuttingDown_||transitioning_||exporting_||scanning_||!storageReady_||!pageCount_)return;
    if(!target.isLocalFile()||QFileInfo::exists(target.toLocalFile())){exportStatus_=tr("请选择尚未存在的新 PDF 文件名。");emit storageChanged();return;}
    finishTextEditing();annotations_.cancelGesture();scanning_=true;exportStatus_=tr("正在重建扫描 PDF…");emit storageChanged();
    worker_->optimizeScan(session_,target.toLocalFile(),identity_,options);
}
