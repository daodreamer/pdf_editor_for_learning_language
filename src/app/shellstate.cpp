#include "shellstate.h"

#include <QFileInfo>
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
    connect(&thread_, &QThread::finished, this, [this] {
        shutdownComplete_ = true;
        emit shutdownChanged();
    });
    scheduleTimer_.setSingleShot(true);
    scheduleTimer_.setInterval(ReadingConfig::viewportDelayMs);
    connect(&scheduleTimer_, &QTimer::timeout, this, &ShellState::schedule);
    connect(&model_, &ReadingModel::geometryChanged, this, [this] {
        syncAnnotations();
        emit layoutChanged();
    });
    connect(&textSelection_, &TextSelection::demandChanged, this, [this] {
        if (!inFlight_ && !scheduleTimer_.isActive()) scheduleTimer_.start();
    });
    connect(worker_, &PdfWorker::requestFinished, this, [this](quint64 revision) {
        if (revision != revision_ || shuttingDown_) return;
        inFlight_ = false;
        scheduleTimer_.start();
    });
    thread_.setObjectName(QStringLiteral("PDFium"));
    thread_.start();
}

ShellState::~ShellState() {
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
    if (url.isEmpty() || shuttingDown_) return;
    const QFileInfo file(url.toLocalFile());
    if (!url.isLocalFile() || url.toLocalFile().isEmpty()) {
        statusText_ = QStringLiteral("请选择可读取的本地 PDF 文件。");
        errorCode_ = QStringLiteral("file");
    } else {
        ++session_;
        QGuiApplication::inputMethod()->reset();
        annotations_.reset();
        textSelection_.reset();
        path_ = file.absoluteFilePath();
        selectedName_ = file.fileName();
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
    if (shuttingDown_) return;
    ++session_;
    QGuiApplication::inputMethod()->reset();
    annotations_.reset();
    textSelection_.reset();
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
    PdfRequest r{session_,++revision_,path_,std::max(0,candidate),width};
    if (textTask) { r.page=textPage; r.textOnly=true; }
    r.maxBytes = model_.pageBudget();
    r.skipPreview = viewportAttached_;
    if (viewportAttached_ && candidate >= 0 && model_.image(candidate).isNull() && width > ReadingConfig::previewWidth) {
        r.width = ReadingConfig::previewWidth;
        r.previewOnly = true;
    }
    if (textTask) {
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
    viewportAttached_ = true;
    viewportWidth_ = width; viewportHeight_ = height; dpr_ = dpr;
    model_.setViewport(y,height,width,zoom_);
    page_ = model_.pageAt(model_.offset());
    if (oldPages != model_.priority()) syncAnnotations();
    busy_ = pageCount_ > 0 && !failed_.contains(page_) && !model_.ready(page_,model_.targetWidth(page_,dpr_));
    emit selectionChanged(); emit imageChanged();
    if (oldPages != model_.priority() || oldWidth != model_.targetWidth(page_,dpr_)) {
        for (int p : model_.priority()) if (model_.visible(p)) model_.recordDemand(p,model_.targetWidth(p,dpr_));
        invalidate();
    }
    else if (!inFlight_ && !scheduleTimer_.isActive()) scheduleTimer_.start();
}
void ShellState::setZoom(double zoom) {
    if (shuttingDown_ || !std::isfinite(zoom)) return;
    zoom = std::clamp(zoom,0.25,4.0);
    if (zoom_ == zoom) return;
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
    if (frame.textOnly) {
        model_.setSizes(frame.page,{frame.points});
        if (frame.text) textSelection_.put(frame.page,frame.text);
        else {
            auto failure=std::make_shared<TextPage>(); failure->error=frame.error;
            textSelection_.put(frame.page,std::move(failure));
        }
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
        model_.put(frame.page,frame.image,requestedWidth_,!frame.preview);
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
    shuttingDown_ = true;
    QGuiApplication::inputMethod()->reset();
    annotations_.reset();
    textSelection_.reset();
    busy_ = false;
    statusText_ = QStringLiteral("正在关闭文档…");
    emit selectionChanged();
    worker_->shutdown();
}

bool ShellState::beginTextSelection(double x,double y) {
    if (shuttingDown_ || !pageCount_ || !std::isfinite(x) || !std::isfinite(y)) return false;
    const int page=model_.pageAt(y);
    const double width=model_.pageWidth();
    const double left=(model_.contentWidth()-width)/2;
    if (x<left || x>left+width || y<model_.top(page) || y>model_.top(page)+model_.height(page)) {
        textSelection_.clear(); return false;
    }
    textSelection_.begin(page,(x-left)/width,(y-model_.top(page))/model_.height(page),3/width,3/model_.height(page));
    return textSelection_.canStart();
}
void ShellState::extendTextSelection(double x,double y) {
    if (shuttingDown_ || !pageCount_ || !std::isfinite(x) || !std::isfinite(y)) return;
    const int page=model_.pageAt(y);
    const double width=model_.pageWidth();
    const double left=(model_.contentWidth()-width)/2;
    textSelection_.extend(page,std::clamp((x-left)/width,0.0,1.0),std::clamp((y-model_.top(page))/model_.height(page),0.0,1.0));
}

void ShellState::cycleTheme() {
    themeMode_ = (themeMode_ + 1) % 3;
    emit themeChanged();
}

void ShellState::finishTextEditing() {
    QGuiApplication::inputMethod()->commit();
    annotations_.finishEditing();
}
QString ShellState::createTextBox(double x,double y) {
    if(shuttingDown_ || !pageCount_ || !std::isfinite(x) || !std::isfinite(y)) return {};
    const int page=model_.pageAt(y);
    const double w=model_.pageWidth(), h=model_.height(page), left=(model_.contentWidth()-w)/2;
    if(x<left || x>=left+w || y<model_.top(page) || y>=model_.top(page)+h) return {};
    finishTextEditing(); textSelection_.clear();
    return annotations_.create(page,(x-left)/w,(y-model_.top(page))/h);
}

bool ShellState::containsPage(double x,double y) const {
    if(shuttingDown_ || !pageCount_ || !std::isfinite(x) || !std::isfinite(y)) return false;
    const int page=model_.pageAt(y);
    const double w=model_.pageWidth(), left=(model_.contentWidth()-w)/2;
    return x>=left && x<left+w && y>=model_.top(page) && y<model_.top(page)+model_.height(page);
}

void ShellState::commitInput() { QGuiApplication::inputMethod()->commit(); }
void ShellState::resetInput() { QGuiApplication::inputMethod()->reset(); }

void ShellState::syncAnnotations() {
    const auto active=model_.priority();
    if (!annotations_.selectedId().isEmpty() && !active.contains(annotations_.selectedPage())) finishTextEditing();
    annotations_.setActivePages(active);
}
