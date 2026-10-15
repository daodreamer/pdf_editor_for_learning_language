#include "shellstate.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QStyleHints>

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
        path_ = file.absoluteFilePath();
        selectedName_ = file.fileName();
        page_ = 0;
        pageCount_ = 0;
        image_ = {};
        ratio_ = 1;
        emit imageChanged();
        request();
    }
    emit selectionChanged();
}

void ShellState::clearSelection() {
    if (shuttingDown_) return;
    ++session_;
    path_.clear();
    selectedName_.clear();
    statusText_.clear();
    errorCode_.clear();
    pageCount_ = 0;
    page_ = 0;
    image_ = {};
    busy_ = false;
    worker_->submit({session_, ++revision_, {}, 0, renderWidth_});
    emit imageChanged();
    emit selectionChanged();
}

void ShellState::request() {
    busy_ = true;
    errorCode_.clear();
    statusText_ = QStringLiteral("正在加载页面…");
    worker_->submit({session_, ++revision_, path_, page_, renderWidth_});
    emit selectionChanged();
}
void ShellState::goToPage(int page) {
    if (shuttingDown_ || page < 1 || page > pageCount_ || page - 1 == page_) return;
    page_ = page - 1;
    image_ = {};
    emit imageChanged();
    request();
}
void ShellState::setRenderWidth(int pixels) {
    pixels = qBound(64, pixels, 8192);
    if (shuttingDown_ || renderWidth_ == pixels) return;
    renderWidth_ = pixels;
    if (!path_.isEmpty() && pageCount_ > 0) request();
}
void ShellState::acceptFrame(const PdfFrame& frame) {
    if (shuttingDown_ || frame.session != session_ || frame.revision != revision_) {
        emit renderDiscarded(frame.revision);
        return;
    }
    busy_ = frame.preview;
    errorCode_ = frame.errorCode;
    if (!frame.error.isEmpty()) {
        statusText_ = frame.error;
        image_ = {};
    } else {
        pageCount_ = frame.pages;
        ratio_ = frame.points.height() / frame.points.width();
        image_ = frame.image;
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
    busy_ = false;
    statusText_ = QStringLiteral("正在关闭文档…");
    emit selectionChanged();
    worker_->shutdown();
}

void ShellState::cycleTheme() {
    themeMode_ = (themeMode_ + 1) % 3;
    emit themeChanged();
}
