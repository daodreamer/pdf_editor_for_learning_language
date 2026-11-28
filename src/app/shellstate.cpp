#include "shellstate.h"
#include "runtimepaths.h"
#include "answerfonts.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStyleHints>
#include <QInputMethod>
#include <algorithm>
#include <limits>
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
    // Test executables set LANGLERN_NO_BACKGROUND_OCR so unrelated suites keep their timing.
    bookAllowed_ = qEnvironmentVariableIsEmpty("LANGLERN_NO_BACKGROUND_OCR");
    // Diagnostic builds/benchmarks only: worst case with no idle gap (benchmarks/run-ocr-book.py).
    bool idleSet=false;
    if (const int idle=qEnvironmentVariableIntValue("LANGLERN_OCR_BOOK_IDLE_MS",&idleSet); idleSet && idle>=0) bookIdleMs_=idle;
    bookQueue_.setEnabled(OcrBookConfig::enabledByDefault);
    clock_.start();
    backgroundTimer_.setSingleShot(true);
    connect(&backgroundTimer_,&QTimer::timeout,this,[this] { if(!inFlight_ && !scheduleTimer_.isActive()) scheduleTimer_.start(); });
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
        // A background probe that ended without an accepted frame goes back into the queue.
        if (revision == backgroundPdfRevision_) {
            backgroundPdfRevision_ = 0;
            if (backgroundStage_ == BackgroundStage::Pdf) cancelBackground();
        }
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
        noteInteraction(); // a freshly opened book waits a full idle period before background OCR
        QGuiApplication::inputMethod()->reset();
        restoring_=true; annotations_.reset(); restoring_=false;
        storageReady_=false; loadingStorage_=true; editRevision_=savedRevision_=0; identity_.clear();
        exportStatus_.clear();
        storageStatus_=QStringLiteral("正在校验 PDF 并恢复练习…"); emit storageChanged();
        textSelection_.reset();
        resetOcr();
        closeBookSession();
        emit answerLinesChanged();
        path_ = file.absoluteFilePath();
        resetDocumentLanguage();
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
    closeBookSession();
    emit answerLinesChanged();
    path_.clear();
    resetDocumentLanguage();
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
    // PDFium cannot stop a render; the probe's frame will be stale. The page is queued again.
    if (backgroundStage_ == BackgroundStage::Pdf) cancelBackground();
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
    if(ocrPage>=0 && bookQueue_.isDone(ocrPage) && !ocrForced_.contains(ocrPage)) {
        // Whole-book store has this page: no PDFium render, the OCR thread only reads the file.
        const auto pending=textSelection_.snapshot(ocrPage);
        if(!pending || pending->region==QRectF(0,0,1,1)) {
            ocrDemand_.remove(ocrPage);ocrBusyPage_=ocrPage;cancelBackground();
            OcrRequest load;load.session=session_;load.revision=++ocrRevision_;load.page=ocrPage;load.identity=identity_;
            load.language=ocrLanguage_;load.book=bookToken_;load.storeOnly=true;
            submitOcr(std::move(load));
            ocrPage=-1;
        }
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
    r.detectBoxes = !textTask && !ocrTask && !r.previewOnly && candidate >= 0 && !model_.hasCheckBoxes(r.page);
    if (textTask || ocrTask) {
        // No bitmap allocation, and no renderJobs increment for text extraction.
    } else if (candidate < 0) {
        while (metadataCursor_ < pageCount_ && model_.known(metadataCursor_)) ++metadataCursor_;
        if (metadataCursor_ >= pageCount_) {
            busy_ = false;
            emit selectionChanged();
            // Last of all: one background whole-book page (probe, then OCR image if no text layer).
            if (const int page = backgroundCandidate(); page >= 0) {
                r.page = page; r.ocrOnly = true; r.ocrProbe = true; r.width = ReadingConfig::ocrWidth;
                r.skipPreview = true; r.previewOnly = false; r.detectBoxes = false; r.maxBytes = ReadingConfig::ocrImageBytes;
                bookQueue_.set(page, OcrBookQueue::State::Busy);
                backgroundPage_ = page; backgroundStage_ = BackgroundStage::Pdf; backgroundPdfRevision_ = r.revision;
                updateBookStatus();
                requestedWidth_ = r.width;
                inFlight_ = true;
                worker_->submit(std::move(r));
            }
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
    noteInteraction();
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
    if(scrolled || width!=viewportWidth_ || height!=viewportHeight_) noteInteraction();
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
    noteInteraction();
    annotations_.cancelGesture();
    zoom_ = zoom;
    model_.setViewport(model_.offset(),viewportHeight_,viewportWidth_,zoom_);
    emit layoutChanged();
    model_.recordDemand(page_,model_.targetWidth(page_,dpr_));
    request();
}
QPointF ShellState::zoomAt(double zoom,double x,double y,double viewX,double viewY) {
    const QPointF unchanged(x-viewX,model_.offset());
    if (shuttingDown_ || !std::isfinite(zoom) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(viewX) || !std::isfinite(viewY)) return unchanged;
    zoom = std::clamp(zoom,0.25,4.0);
    if (zoom_ == zoom) return unchanged;
    noteInteraction();
    const int page = pageCount_ > 0 ? model_.pageAtPosition(x,y,true) : -1;
    QPointF fraction;
    if (page >= 0) fraction = {(x-model_.left(page))/model_.pageWidth(),(y-model_.top(page))/model_.height(page)};
    annotations_.cancelGesture();
    zoom_ = zoom;
    model_.setViewport(model_.offset(),viewportHeight_,viewportWidth_,zoom_);
    QPointF origin(x-viewX,model_.offset());
    if (page >= 0) {
        origin = {model_.left(page)+fraction.x()*model_.pageWidth()-viewX,model_.top(page)+fraction.y()*model_.height(page)-viewY};
        model_.setViewport(origin.y(),viewportHeight_,viewportWidth_,zoom_); // same zoom: only scrolls, clamped
        origin.setY(model_.offset());
    }
    origin.setX(std::clamp(origin.x(),0.0,std::max(0.0,model_.contentWidth()-viewportWidth_)));
    emit layoutChanged();
    model_.recordDemand(page_,model_.targetWidth(page_,dpr_));
    request();
    return origin;
}
void ShellState::acceptFrame(const PdfFrame& frame) {
    // Tick boxes depend only on the document page, so a newer request does not void them.
    if (frame.boxesOnly) {
        if (shuttingDown_ || frame.session != session_ || frame.page < 0 || frame.page >= pageCount_) return;
        model_.setCheckBoxes(frame.page,frame.checkBoxes);
        emit answerLinesChanged();
        return;
    }
    if (shuttingDown_ || frame.session != session_ || frame.revision != revision_) {
        emit renderDiscarded(frame.revision);
        return;
    }
    if (frame.geometry.valid) {
        annotations_.setGeometry(frame.page,frame.geometry);
        annotations_.setActivePages(model_.priority());
    }
    if(frame.ocrProbe) {
        if(backgroundStage_!=BackgroundStage::Pdf || frame.page!=backgroundPage_)return;
        backgroundPdfRevision_=0;
        if(!frame.error.isEmpty() || frame.nativeText) {
            bookQueue_.set(frame.page,frame.error.isEmpty()?OcrBookQueue::State::Native:OcrBookQueue::State::Failed);
            if(frame.nativeText && ocr_) QMetaObject::invokeMethod(ocr_,[worker=ocr_.data(),book=bookToken_,page=frame.page]{worker->recordNative(book,page);},Qt::QueuedConnection);
            backgroundPage_=-1;backgroundStage_=BackgroundStage::None;updateBookStatus();
            return; // requestFinished schedules the next step
        }
        backgroundStage_=BackgroundStage::Ocr;
        OcrRequest request{session_,++ocrRevision_,frame.page,identity_,ocrLanguage_,frame.image};
        request.book=bookToken_;request.background=true;request.displayToPdf=frame.geometry.displayToPdf;
        submitOcr(std::move(request));
        updateBookStatus();
        return;
    }
    if(frame.ocrOnly) {
        if(!ocrDemand_.contains(frame.page))return;
        if(!frame.error.isEmpty()) {
            auto failure=std::make_shared<TextPage>();failure->ocr=true;failure->error=frame.error;
            ocrDemand_.remove(frame.page);ocrForced_.remove(frame.page);textSelection_.put(frame.page,failure);return;
        }
        cancelBackground(); // foreground OCR first: a running background page is cancelled and requeued
        ocrDemand_.remove(frame.page);ocrBusyPage_=frame.page;
        const auto pending=textSelection_.snapshot(frame.page);
        OcrRequest request{session_,++ocrRevision_,frame.page,identity_,ocrLanguage_,frame.image,pending?pending->region:QRectF(0,0,1,1)};
        request.book=bookToken_;request.force=ocrForced_.contains(frame.page);request.displayToPdf=frame.geometry.displayToPdf;
        ocrForced_.remove(frame.page);
        submitOcr(std::move(request));
        return;
    }
    if (frame.textOnly) {
        model_.setSizes(frame.page,{frame.points});
        if (frame.text) {
            textSelection_.put(frame.page,frame.text); noteLanguage(frame.page,frame.text);
            // A text layer seen in the foreground spares the background queue its probe.
            if(!frame.text->characters.isEmpty() && bookQueue_.ready() && bookQueue_.state(frame.page)==OcrBookQueue::State::Unknown) {
                bookQueue_.set(frame.page,OcrBookQueue::State::Native);
                if(ocr_) QMetaObject::invokeMethod(ocr_,[worker=ocr_.data(),book=bookToken_,page=frame.page]{worker->recordNative(book,page);},Qt::QueuedConnection);
                updateBookStatus();
            }
        }
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
            openBook();
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
    closeBookSession();
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
    noteInteraction();
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
    resetOcr();closeBookSession();ocrLanguage_=std::move(language);textSelection_.clearOcr();updateOcrDemand();emit ocrChanged();
    openBook(); // the new language has its own stored profile; the old one is never read for it
    scheduleTimer_.start();
    // Text recognized in the old language says little about the document.
    for(auto it=pageLanguages_.begin();it!=pageLanguages_.end();) it=it->ocr?pageLanguages_.erase(it):std::next(it);
    updateDocumentLanguage();
}
void ShellState::noteLanguage(int page,const TextSnapshot& text) {
    if(!text || text->pending || !text->error.isEmpty() || page<0) return;
    // An OCR pass over a selected region only covers part of the page; keep the larger sample.
    if(text->characters.isEmpty() && pageLanguages_.contains(page)) return;
    pageLanguages_.insert(page,{languageEvidence(*text),text->ocr});
    updateDocumentLanguage();
}
void ShellState::updateDocumentLanguage() {
    QString language;
    if(!path_.isEmpty()) {
        LanguageEvidence total;
        for(const auto& page:std::as_const(pageLanguages_)) total+=page.evidence;
        // Text on the pages first; scanned books without text fall back to the file name, then
        // to the OCR language the user set.
        language=total.dominant();
        if(language.isEmpty()) language=languageFromName(QFileInfo(path_).completeBaseName());
        if(language.isEmpty()) language=languageFromOcr(ocrLanguage_);
    }
    if(language==documentLanguage_) return;
    documentLanguage_=language;
    emit documentLanguageChanged();
}
void ShellState::resetDocumentLanguage() { pageLanguages_.clear(); updateDocumentLanguage(); }
void ShellState::resetOcr() {
    cancelBackground();
    if(ocr_)ocr_->invalidate(++ocrRevision_);
    ocrForced_.clear();
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
void ShellState::recognizeCurrentPage() { if(ocrBusyPage_!=page_ && !ocrDemand_.contains(page_))ocrForced_.insert(page_); ensureOcr(page_,true); }
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
        if(QRectF(anchor,QSizeF(n.width/g.units.width(),n.height/g.units.height())).adjusted(-10/width,-10/height,10/width,10/height).contains(p))return true;}
    return false;
}
QString ShellState::textBoxAt(double x,double y,double tolerance) const {
    if(!containsPage(x,y))return {};
    const int page=model_.pageAtPosition(x,y);
    if(page<0)return {};const auto g=annotations_.geometry(page);if(!g.valid)return {};
    const QPointF p((x-model_.left(page))/model_.pageWidth(),(y-model_.top(page))/model_.height(page));
    auto notes=annotations_.snapshot();std::sort(notes.begin(),notes.end(),[](const auto& a,const auto& b){return a.order>b.order;});
    for(const auto& n:notes)if(n.page==page){const auto anchor=g.pdfToDisplay.map(n.anchor);
        const double dx=tolerance/model_.pageWidth(),dy=tolerance/model_.height(page);
        if(QRectF(anchor,QSizeF(n.width/g.units.width(),n.height/g.units.height())).adjusted(-dx,-dy,dx,dy).contains(p)) return n.id;}
    return {};
}
bool ShellState::eraseTextAt(double x,double y) {
    // The eraser acts within its radius, for typed answers as for strokes.
    const auto id=textBoxAt(x,y,AnnotationConfig::eraserPixels);
    if(id.isEmpty())return false;
    focusPage(model_.pageAtPosition(x,y));annotations_.select(id);annotations_.removeSelected();
    return true;
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
ShellState::AnswerBlock ShellState::answerBlockAt(int page,QPointF point) const {
    const auto g=annotations_.geometry(page);
    const auto image=model_.image(page);
    if(!g.valid || image.isNull()) return {};
    const auto notes=annotations_.snapshot();
    auto occupied=[&](const QRectF& line) {
        for(const auto& n:notes) if(n.page==page && !annotations_.noteHidden(n)) {
            const auto rect=QRectF(g.pdfToDisplay.map(n.anchor),QSizeF(n.width/g.units.width(),n.height/g.units.height()));
            if(rect.intersects(line)) return true;
        }
        return false;
    };
    auto lines=model_.answerLines(page);
    std::sort(lines.begin(),lines.end(),[](const QRectF& a,const QRectF& b){ return a.top()<b.top(); });
    int start=-1;
    for(int i=0;i<lines.size() && start<0;++i) if(lines[i].contains(point) && !occupied(lines[i])) start=i;
    if(start<0) return {};
    const double pt=1/g.units.height(), ptX=1/g.units.width();
    // A printed label just left of a rule ("1", "a)", "Nom :") marks its own question.
    auto labelled=[&](const QRectF& line) {
        const double rule=line.bottom()-3*pt;
        const int x0=std::max(0,int((line.left()-22*ptX)*image.width())), x1=int((line.left()-1.5*ptX)*image.width());
        const int y0=std::max(0,int((rule-10*pt)*image.height())), y1=std::min(image.height()-1,int((rule+pt)*image.height()));
        auto darkIn=[&](int top,int bottom) {
            int dark=0;
            for(int y=std::max(0,top);y<=std::min(image.height()-1,bottom);++y) {
                const auto* row=reinterpret_cast<const QRgb*>(image.constScanLine(y));
                for(int x=x0;x<=x1 && x<image.width();++x) dark+=qGray(row[x])<140;
            }
            return dark;
        };
        const int band=darkIn(y0,y1);
        if(band<4) return false;
        // A label is text on the rule's own row; an illustration beside the rules
        // continues well above and below it.
        const int around=darkIn(int((rule-18*pt)*image.height()),y0-1)+darkIn(y1+1,int((rule+8*pt)*image.height()));
        return around<=band/2;
    };
    // The nearest rule above/below that overlaps horizontally (skipping other columns).
    auto neighbour=[&](int i,int step) {
        for(int k=i+step;k>=0 && k<lines.size();k+=step)
            if(lines[k].left()<lines[i].right() && lines[i].left()<lines[k].right() && std::abs(lines[k].top()-lines[i].top())>lines[i].height()*.5) return k;
        return -1;
    };
    // Rules of one answer have nothing printed between them; a question line between
    // two rules makes them separate answers even when aligned and evenly spaced.
    auto clearBetween=[&](const QRectF& upper,const QRectF& lower) {
        const double from=upper.bottom()-3*pt+2*pt, to=lower.bottom()-3*pt-2*pt;
        const int y0=std::max(0,int(from*image.height())), y1=std::min(image.height()-1,int(to*image.height()));
        const int x0=std::max(0,int(std::max(upper.left(),lower.left())*image.width()));
        const int x1=std::min(image.width()-1,int(std::min(upper.right(),lower.right())*image.width()));
        int dark=0,total=0;
        for(int y=y0;y<=y1;++y) {
            const auto* row=reinterpret_cast<const QRgb*>(image.constScanLine(y));
            for(int x=x0;x<=x1;x+=2) { ++total; dark+=qGray(row[x])<160; }
        }
        return dark<=std::max(2,total/300);
    };
    double pitch=0;
    // Same field type, aligned ends, a steady 12-40 pt pitch, still unanswered.
    auto joins=[&](int upper,int lower,int adding) {
        const auto& a=lines[upper]; const auto& b=lines[lower];
        const double gap=b.top()-a.top();
        if(std::abs(a.height()-b.height())>.5*pt || std::abs(a.left()-b.left())>.02 || std::abs(a.right()-b.right())>.02) return false;
        if(gap<std::max(a.height(),12*pt) || gap>40*pt || occupied(lines[adding]) || !clearBetween(a,b)) return false;
        return pitch==0 || std::abs(gap-pitch)<=pitch*.2;
    };
    int first=start,last=start;
    while(!labelled(lines[first])) {
        const int k=neighbour(first,-1);
        if(k<0 || !joins(k,first,k)) break;
        if(pitch==0) pitch=lines[first].top()-lines[k].top();
        first=k;
    }
    for(;;) {
        const int k=neighbour(last,1);
        if(k<0 || labelled(lines[k]) || !joins(last,k,k)) break;
        if(pitch==0) pitch=lines[k].top()-lines[last].top();
        last=k;
    }
    AnswerBlock block;
    block.first=lines[first];
    block.lines=1; block.area=lines[first];
    for(int k=first;k!=last;) { k=neighbour(k,1); block.area=block.area.united(lines[k]); ++block.lines; }
    block.pitch=block.lines>1?(lines[last].top()-lines[first].top())/(block.lines-1):0;
    return block;
}
QRectF ShellState::answerBlankAt(double x,double y) const {
    if(!containsPage(x,y)) return {};
    const int page=model_.pageAtPosition(x,y);
    const double w=model_.pageWidth(),h=model_.height(page);
    const QPointF point((x-model_.left(page))/w,(y-model_.top(page))/h);
    const auto block=answerBlockAt(page,point);
    if(!block.lines) return {};
    const auto& a=block.area;
    return {model_.left(page)+a.x()*w,model_.top(page)+a.y()*h,a.width()*w,a.height()*h};
}
QString ShellState::createAnswerAt(double x,double y) {
    if(!containsPage(x,y)) return {};
    const int page=model_.pageAtPosition(x,y);
    const QPointF point((x-model_.left(page))/model_.pageWidth(),(y-model_.top(page))/model_.height(page));
    const auto block=answerBlockAt(page,point);
    if(!block.lines) return {};
    finishTextEditing(); textSelection_.clear(); focusPage(page);
    const auto g=annotations_.geometry(page);
    // A block of rules becomes one answer: it wraps at the rule ends and each line
    // (wrapped or Enter-started) lands on the next rule.
    return annotations_.create(page,block.area.x(),block.area.y(),block.area.width()*g.units.width(),
        block.first.height()*g.units.height(),block.pitch*g.units.height(),block.lines);
}
ShellState::PageBox ShellState::checkBoxNear(double x,double y) const {
    if(!containsPage(x,y)) return {};
    const int page=model_.pageAtPosition(x,y);
    const double w=model_.pageWidth(),h=model_.height(page);
    if(!(w>0) || !(h>0)) return {};
    const QPointF point((x-model_.left(page))/w,(y-model_.top(page))/h);
    PageBox best; double distance=std::numeric_limits<double>::max();
    for(const auto& box:model_.checkBoxes(page)) {
        // Generous target: a quarter of the box around it, at least ~4 logical px.
        const double padX=std::max(box.width()*.25,4/w),padY=std::max(box.height()*.25,4/h);
        if(!box.adjusted(-padX,-padY,padX,padY).contains(point)) continue;
        const auto d=box.center()-point; const double dd=QPointF::dotProduct(d,d);
        if(dd<distance) { distance=dd; best={page,box}; }
    }
    return best;
}
QRectF ShellState::checkBoxAt(double x,double y) const {
    const auto hit=checkBoxNear(x,y);
    if(hit.page<0) return {};
    const double w=model_.pageWidth(),h=model_.height(hit.page);
    const auto& b=hit.box;
    const double pad=std::max(3.0,b.width()*w*.3);
    return QRectF(model_.left(hit.page)+b.x()*w-pad,model_.top(hit.page)+b.y()*h-pad,b.width()*w+2*pad,b.height()*h+2*pad);
}
bool ShellState::toggleCheckBox(double x,double y) {
    const auto hit=checkBoxNear(x,y);
    if(hit.page<0) return false;
    finishTextEditing(); textSelection_.clear(); focusPage(hit.page);
    annotations_.toggleBoxMark(hit.page,hit.box,crossMarks(hit.page),annotations_.defaultTextColor());
    return true;
}
bool ShellState::crossMarks(int page) const {
    if(checkMarkStyle_!=0) return checkMarkStyle_==2;
    // Common function words per language in the page text (PDF text layer or OCR).
    if(const auto text=textSelection_.snapshot(page); text && !text->characters.isEmpty()) {
        const auto evidence=languageEvidence(*text);
        const int cross=evidence.cross, tick=evidence.tick, han=evidence.han;
        if(cross+tick>=3) return cross>tick;
        if(han>=10) return false; // Chinese: 打勾
    }
    return !ocrLanguage_.startsWith(QLatin1String("eng")) && !ocrLanguage_.startsWith(QLatin1String("chi"));
}
QString ShellState::createTextBox(double x,double y) {
    if(shuttingDown_ || !storageReady_ || transitioning_ || !pageCount_ || !std::isfinite(x) || !std::isfinite(y)) return {};
    const int page=model_.pageAtPosition(x,y);
    if(page<0)return {};
    const double w=model_.pageWidth(), h=model_.height(page), left=model_.left(page);
    if(x<left || x>=left+w || y<model_.top(page) || y>=model_.top(page)+h) return {};
    finishTextEditing(); textSelection_.clear();
    focusPage(page);
    // The pointer marks the caret: left edge of the box, vertically centred on its first line.
    const auto g=annotations_.geometry(page);
    const double half=g.valid ? annotations_.defaultTextHeight()/2/g.units.height() : 0;
    return annotations_.create(page,(x-left)/w,std::max(0.0,(y-model_.top(page))/h-half));
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

static bool initializeAnnotationFont() { return AnswerFonts::registerAll(); }
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
        noteInteraction();
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
        openBook();
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
    return {annotations_.snapshot(),annotations_.inkSnapshot(),editRevision_,annotations_.rounds()};
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
void ShellState::exportPdf(const QUrl& target,bool includeHidden) {
    if(shuttingDown_ || transitioning_ || exporting_ || scanning_ || !storageReady_ || !pageCount_) return;
    finishTextEditing(); annotations_.cancelGesture();
    if(!target.isLocalFile()) { exportStatus_=QStringLiteral("请选择本地新 PDF 文件。"); emit storageChanged(); return; }
    exporting_=true; exportStatus_=QStringLiteral("正在导出当前修订；可以继续编辑…"); emit storageChanged();
    const auto session=session_; const auto path=path_; const auto destination=target.toLocalFile(); const auto identity=identity_;
    // Only the current round is exported; hidden answers are current data and included by default.
    AnnotationSnapshot snapshot{annotations_.snapshot(),annotations_.inkSnapshot(),editRevision_,{}};
    if(!includeHidden) {
        snapshot.notes.removeIf([this](const auto& n){ return annotations_.noteHidden(n); });
        snapshot.ink.removeIf([this](const auto& s){ return annotations_.inkHidden(s); });
    }
    QMetaObject::invokeMethod(worker_,[this,session,path,destination,identity,snapshot] { worker_->exportPdf(session,path,destination,identity,snapshot); },Qt::QueuedConnection);
}
void ShellState::optimizeScan(const QUrl& target,const QVariantMap& options) {
    if(shuttingDown_||transitioning_||exporting_||scanning_||!storageReady_||!pageCount_)return;
    if(!target.isLocalFile()||QFileInfo::exists(target.toLocalFile())){exportStatus_=tr("请选择尚未存在的新 PDF 文件名。");emit storageChanged();return;}
    finishTextEditing();annotations_.cancelGesture();scanning_=true;exportStatus_=tr("正在重建扫描 PDF…");emit storageChanged();
    worker_->optimizeScan(session_,target.toLocalFile(),identity_,options);
}

// ---- Whole-book background OCR (docs/OCR_SELECTION.md) ----
void ShellState::ensureOcrWorker() {
    if(ocr_ || shuttingDown_) return;
    const auto overrideRoot=qEnvironmentVariable("LANGLERN_WORKSPACE_DIR");
    const auto root=overrideRoot.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::CacheLocation):overrideRoot;
    ocr_=new OcrWorker(root+"/ocr-v1",OcrBook::defaultRoot());ocr_->moveToThread(&ocrThread_);
    connect(&ocrThread_,&QThread::finished,ocr_,&QObject::deleteLater);
    connect(ocr_,&OcrWorker::finished,this,[this](quint64 session,quint64 revision,int page,TextSnapshot text,bool cached,double ms,bool background){
        if(shuttingDown_ || session!=session_ || revision!=ocrRevision_)return;
        if(background) {
            if(page!=backgroundPage_ || backgroundStage_!=BackgroundStage::Ocr)return;
            backgroundPage_=-1;backgroundStage_=BackgroundStage::None;
            if(!text) bookQueue_.release(page);
            else if(!text->error.isEmpty()) {
                bookQueue_.set(page,OcrBookQueue::State::Failed);
                // A missing model or broken engine fails every page alike: stop instead of walking the book.
                if(++bookFailures_>=3) bookError_=tr("后台识别已停止：%1").arg(text->error);
            } else {
                bookFailures_=0;
                if(bookQueue_.state(page)==OcrBookQueue::State::Busy) bookQueue_.set(page,OcrBookQueue::State::Done);
            }
            if(text && text->error.isEmpty()) {
                // Share with the selection layer only for active pages that still lack OCR text.
                const auto current=textSelection_.snapshot(page);
                if(model_.priority().contains(page) && current && !current->ocr && !current->pending && current->characters.isEmpty() && current->error.isEmpty()) {
                    textSelection_.put(page,text);noteLanguage(page,text);
                }
            }
            qInfo("ocr_book_page session=%llu page=%d chars=%lld cached=%d elapsed_ms=%.3f done=%d native=%d pages=%d",session,page,
                text?qint64(text->characters.size()):-1,cached,ms,bookQueue_.done(),bookQueue_.native(),bookQueue_.pages());
            updateBookStatus();
            if(!inFlight_)scheduleTimer_.start();
            return;
        }
        ocrBusyPage_=-1;
        if(!text) {
            // Stored page vanished or failed its checksum: recognise it from a fresh render.
            if(bookQueue_.isDone(page)) { bookQueue_.set(page,OcrBookQueue::State::Unknown); updateBookStatus(); }
            ocrDemand_.insert(page);scheduleTimer_.start();return;
        }
        if(text->error.isEmpty() && text->region==QRectF(0,0,1,1) && bookQueue_.ready() && bookError_.isEmpty()) {
            const auto state=bookQueue_.state(page);
            if(state==OcrBookQueue::State::Unknown || state==OcrBookQueue::State::Busy) { bookQueue_.set(page,OcrBookQueue::State::Done); updateBookStatus(); }
        }
        textSelection_.put(page,text);
        noteLanguage(page,text);
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
    connect(ocr_,&OcrWorker::bookOpened,this,[this](quint64 book,QByteArray profile,QList<int> ocrPages,QList<int> nativePages,QString error){
        if(shuttingDown_ || book!=bookToken_)return;
        bookOpening_=false;bookProfile_=profile;
        bookQueue_.reset(pageCount_);
        for(int p:ocrPages)bookQueue_.set(p,OcrBookQueue::State::Done);
        for(int p:nativePages)bookQueue_.set(p,OcrBookQueue::State::Native);
        bookError_=error;bookQueue_.setReady(error.isEmpty());
        updateBookStatus();
        if(!inFlight_)scheduleTimer_.start();
    });
    connect(ocr_,&OcrWorker::bookCleared,this,[this](quint64 book,QString error){
        if(shuttingDown_ || book!=bookToken_)return;
        bookOpening_=false;
        openBook();
        bookError_=error;
        ocrStatus_=error.isEmpty()?tr("已清除本书识别结果"):error;emit ocrChanged();
        updateBookStatus();
    });
    connect(ocr_,&OcrWorker::bookWriteFailed,this,[this](quint64 book,int page,QString error){
        if(shuttingDown_ || book!=bookToken_)return;
        bookQueue_.set(page,OcrBookQueue::State::Failed);
        bookError_=error;qWarning("ocr_book_write_failed page=%d error=%s",page,qPrintable(error));
        updateBookStatus();
    });
    ocrThread_.setObjectName("Offline OCR");ocrThread_.start(QThread::LowPriority);
}
void ShellState::submitOcr(OcrRequest request) {
    ensureOcrWorker();
    if(ocr_)ocr_->submit(std::move(request));
}
void ShellState::openBook() {
    if(shuttingDown_ || identity_.isEmpty() || pageCount_<=0 || bookOpening_ || bookQueue_.ready()) return;
    ensureOcrWorker();
    if(!ocr_)return;
    bookQueue_.reset(pageCount_);bookError_.clear();bookFailures_=0;bookOpening_=true;
    QMetaObject::invokeMethod(ocr_,[worker=ocr_.data(),book=++bookToken_,identity=identity_,language=ocrLanguage_,pages=pageCount_]{
        worker->openBook(book,identity,language,pages);
    },Qt::QueuedConnection);
    updateBookStatus();
}
void ShellState::closeBookSession() {
    cancelBackground();
    ++bookToken_;bookOpening_=false;bookProfile_.clear();bookError_.clear();
    bookQueue_.reset(0);
    if(ocr_)QMetaObject::invokeMethod(ocr_,&OcrWorker::closeBook,Qt::QueuedConnection);
    updateBookStatus();
}
void ShellState::cancelBackground() {
    if(backgroundPage_<0)return;
    bookQueue_.release(backgroundPage_);
    // Only the OCR thread can actually stop (Tesseract cancel callback); a PDFium render ends on
    // its own and its frame is dropped as stale.
    if(backgroundStage_==BackgroundStage::Ocr && ocr_)ocr_->invalidate(++ocrRevision_);
    qInfo("ocr_book_yield page=%d stage=%s",backgroundPage_,backgroundStage_==BackgroundStage::Ocr?"ocr":"pdf");
    backgroundPage_=-1;backgroundStage_=BackgroundStage::None;backgroundPdfRevision_=0;
    updateBookStatus();
}
void ShellState::noteInteraction() {
    bookQueue_.noteInteraction(clock_.elapsed());
}
int ShellState::backgroundCandidate() {
    if(!bookAllowed_ || !bookQueue_.active() || !bookError_.isEmpty() || shuttingDown_ || !storageReady_ || identity_.isEmpty()
        || exporting_ || scanning_ || transitioning_ || backgroundPage_>=0 || ocrBusyPage_>=0 || !ocrDemand_.isEmpty()) return -1;
    const auto remaining=bookQueue_.idleRemaining(clock_.elapsed(),bookIdleMs_);
    if(remaining>0) { backgroundTimer_.start(int(remaining)+1); return -1; }
    return bookQueue_.next(page_);
}
void ShellState::setOcrBookEnabled(bool enabled) {
    if(bookQueue_.enabled()==enabled)return;
    bookQueue_.setEnabled(enabled);
    if(!enabled)cancelBackground();
    updateBookStatus();
    if(!inFlight_)scheduleTimer_.start();
}
void ShellState::setOcrBookPaused(bool paused) {
    if(bookQueue_.paused()==paused)return;
    bookQueue_.setPaused(paused);
    if(paused)cancelBackground();
    else if(bookQueue_.ready()) { bookError_.clear(); bookFailures_=0; } // resume retries after a write or model error
    updateBookStatus();
    if(!inFlight_)scheduleTimer_.start();
}
void ShellState::clearBookOcr() {
    if(shuttingDown_ || identity_.isEmpty())return;
    resetOcr();
    textSelection_.clearOcr();
    ++bookToken_;bookQueue_.reset(pageCount_);bookProfile_.clear();bookError_.clear();
    ensureOcrWorker();
    if(!ocr_)return;
    bookOpening_=true;
    QMetaObject::invokeMethod(ocr_,[worker=ocr_.data(),book=bookToken_,identity=identity_]{worker->clearBook(book,identity);},Qt::QueuedConnection);
    updateBookStatus();
    updateOcrDemand();
    scheduleTimer_.start();
}
OcrBookReader ShellState::ocrBookReader() const {
    return OcrBookReader(OcrBook::defaultRoot(),identity_,bookProfile_);
}
void ShellState::updateBookStatus() {
    QString status;
    if(path_.isEmpty() || identity_.isEmpty()) status.clear();
    else if(!bookError_.isEmpty()) status=bookError_;
    else if(!bookQueue_.ready()) status=tr("正在读取本书识别记录…");
    else if(bookQueue_.complete()) {
        status=tr("全书识别完成：%1 页 OCR，%2 页原生文字").arg(bookQueue_.done()).arg(bookQueue_.native());
        if(bookQueue_.failed()>0) status+=tr("，%1 页失败（重新打开本书后重试）").arg(bookQueue_.failed());
    }
    else if(!bookQueue_.enabled()) status=tr("后台识别已关闭");
    else if(bookQueue_.paused()) status=tr("后台识别已暂停");
    else if(backgroundPage_>=0) status=tr("正在后台识别第 %1 页…").arg(backgroundPage_+1);
    else status=tr("空闲时继续识别");
    bookStatus_=status;
    emit ocrBookChanged();
}
