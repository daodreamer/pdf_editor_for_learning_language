#include "pdfworker.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <cpp/fpdf_scopers.h>
#include <fpdfview.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
constexpr qint64 maxImageBytes = 64 * 1024 * 1024;
constexpr int maxDimension = ReadingConfig::maxDimension;
constexpr int previewWidth = ReadingConfig::previewWidth;
std::atomic<int> nativeHandles{0};
template<typename Scoped>
class TrackedHandle {
    using Handle = decltype(std::declval<Scoped>().get());
public:
    explicit TrackedHandle(Handle handle = nullptr) : handle_(handle) { if (handle_) ++nativeHandles; }
    ~TrackedHandle() { reset(); }
    TrackedHandle(const TrackedHandle&) = delete;
    TrackedHandle& operator=(const TrackedHandle&) = delete;
    void reset(Handle handle = nullptr) {
        const bool owned = bool(handle_);
        handle_.reset(handle);
        if (owned) --nativeHandles;
        if (handle_) ++nativeHandles;
    }
    Handle get() const { return handle_.get(); }
    explicit operator bool() const { return bool(handle_); }
private:
    Scoped handle_;
};
int readBlock(void* context, unsigned long offset, unsigned char* data, unsigned long size) {
    auto& file = *static_cast<QFile*>(context);
    return file.seek(offset) && file.read(reinterpret_cast<char*>(data), size) == size;
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

struct PdfWorker::Engine {
    QThread* owner = QThread::currentThread();
    QFile file;
    FPDF_FILEACCESS access{};
    TrackedHandle<ScopedFPDFDocument> document;
    quint64 session = 0;
    Engine() { FPDF_InitLibrary(); }
    ~Engine() {
        Q_ASSERT(QThread::currentThread() == owner);
        document.reset();
        file.close();
        FPDF_DestroyLibrary();
    }
    void close() {
        Q_ASSERT(QThread::currentThread() == owner);
        document.reset();
        file.close();
        session = 0;
    }
};
PdfWorker::PdfWorker(QObject* parent) : QObject(parent) { qRegisterMetaType<PdfFrame>(); }
PdfWorker::~PdfWorker() { Q_ASSERT(!engine_); }
bool PdfWorker::current(quint64 revision) const {
    return !stopping_.load() && latest_.load() == revision;
}
void PdfWorker::submit(PdfRequest request) {
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    latest_.store(request.revision);
    pending_ = std::move(request);
    if (!scheduled_) {
        scheduled_ = true;
        QMetaObject::invokeMethod(this, &PdfWorker::drain, Qt::QueuedConnection);
    }
}
void PdfWorker::cancel(quint64 revision) {
    std::lock_guard lock(mutex_);
    latest_.store(revision);
    pending_.reset();
}
void PdfWorker::shutdown() {
    std::lock_guard lock(mutex_);
    stopping_.store(true);
    pending_.reset();
    if (!scheduled_) {
        scheduled_ = true;
        QMetaObject::invokeMethod(this, &PdfWorker::drain, Qt::QueuedConnection);
    }
}
void PdfWorker::drain() {
    if (QThread::currentThread() != thread() || QThread::currentThread() == qApp->thread())
        qFatal("PDFium worker thread violation");
    for (;;) {
        std::optional<PdfRequest> request;
        {
            std::lock_guard lock(mutex_);
            request.swap(pending_);
            if (!request && !stopping_) { scheduled_ = false; return; }
        }
        if (stopping_) {
            const auto session = engine_ ? engine_->session : 0;
            engine_.reset();
            qInfo("pdf_shutdown native_handles=%d worker=%llu", nativeHandles.load(), quint64(quintptr(QThread::currentThreadId())));
            emit nativeClosed(session, nativeHandles.load());
            emit stopped();
            return;
        }
        emit requestStarted(request->revision);
        execute(*request);
        emit requestFinished(request->revision);
        if (!current(request->revision)) {
            qInfo("pdf_discarded revision=%llu worker=%llu", request->revision, quint64(quintptr(QThread::currentThreadId())));
            emit discarded(request->revision);
        }
    }
}
void PdfWorker::execute(const PdfRequest& r) {
    PdfFrame result;
    result.session = r.session;
    result.revision = r.revision;
    result.page = r.page;
    QElapsedTimer timer;
    timer.start();
    try {
        if (engine_ && (engine_->session != r.session || r.path.isEmpty())) {
            const auto previous = engine_->session;
            engine_->close();
            qInfo("pdf_closed session=%llu native_handles=%d", previous, nativeHandles.load());
            emit nativeClosed(previous, nativeHandles.load());
        }
        if (r.path.isEmpty() || !current(r.revision)) return;
        if (!engine_) engine_ = std::make_unique<Engine>();
        auto& e = *engine_;
        if (!e.document) {
            e.file.setFileName(r.path);
            result.errorCode = QStringLiteral("file");
            require(e.file.open(QIODevice::ReadOnly), "无法以只读方式打开文件。");
            result.errorCode = QStringLiteral("size");
            require(e.file.size() > 0 && quint64(e.file.size()) <= std::numeric_limits<unsigned long>::max(),
                    "文件为空或超过当前 PDFium 文件接口的大小上限（Windows 为 4 GiB）。");
            e.access.m_FileLen = static_cast<unsigned long>(e.file.size());
            e.access.m_GetBlock = readBlock;
            e.access.m_Param = &e.file;
            e.document.reset(FPDF_LoadCustomDocument(&e.access, nullptr));
            if (!e.document) {
                const auto error = FPDF_GetLastError();
                if (error == FPDF_ERR_PASSWORD) {
                    result.errorCode = QStringLiteral("password");
                    throw std::runtime_error("此 PDF 需要密码，当前版本暂不支持输入密码。");
                }
                result.errorCode = error == FPDF_ERR_SECURITY ? QStringLiteral("security") : QStringLiteral("format");
                throw std::runtime_error("PDF 无法解析：文件损坏、格式无效或安全方案不受支持。");
            }
            e.session = r.session;
            qInfo("pdf_opened session=%llu bytes=%lld open_ms=%.3f worker=%llu", r.session,
                e.file.size(), timer.nsecsElapsed() / 1e6, quint64(quintptr(QThread::currentThreadId())));
        }
        if (!current(r.revision)) return;
        result.pages = FPDF_GetPageCount(e.document.get());
        if (r.metadataStart >= 0) {
            result.metadataStart = r.metadataStart;
            QElapsedTimer slice; slice.start();
            for (int p=r.metadataStart; p<std::min(result.pages,r.metadataStart+ReadingConfig::metadataBatch); ++p) {
                if (!current(r.revision)) return;
                FS_SIZEF size{};
                if (FPDF_GetPageSizeByIndexF(e.document.get(),p,&size)) result.sizes.append(QSizeF(size.width,size.height));
                else result.sizes.append(QSizeF());
                if (slice.elapsed() >= ReadingConfig::metadataSliceMs) break;
            }
            emit frameReady(std::move(result));
            return;
        }
        result.errorCode = QStringLiteral("page");
        require(r.page >= 0 && r.page < result.pages, "页码越界或文档没有页面。");
        TrackedHandle<ScopedFPDFPage> page(FPDF_LoadPage(e.document.get(), r.page));
        require(bool(page), "无法加载此 PDF 页面。");
        const double w = FPDF_GetPageWidthF(page.get()), h = FPDF_GetPageHeightF(page.get());
        require(std::isfinite(w) && std::isfinite(h) && w > 0 && h > 0, "页面尺寸无效。");
        result.points = QSizeF(w, h);
        result.errorCode = QStringLiteral("render");
        require(QSysInfo::ByteOrder == QSysInfo::LittleEndian, "当前图像路径需要 little-endian BGRA。");
        const qint64 imageBudget = std::clamp(r.maxBytes,qint64(4),maxImageBytes);
        const auto render = [&](int wanted, bool preview) {
            if (!current(r.revision)) return;
            const double ratio = h / w;
            const double cap = std::min({double(maxDimension), double(maxDimension) / ratio,
                std::sqrt(double(imageBudget) / (4 * ratio))});
            require(std::isfinite(cap) && cap >= 1, "页面比例超过渲染上限。");
            int width = std::max(1, int(std::min(double(wanted), std::floor(cap))));
            // The continuous area bound must also hold after rounding height up.
            while (width > 1 && qint64(width) * qint64(std::ceil(width * ratio)) * 4 > imageBudget)
                --width;
            const double pixelHeight = std::ceil(width * ratio);
            require(std::isfinite(pixelHeight) && pixelHeight > 0 && pixelHeight <= maxDimension, "页面比例超过渲染上限。");
            const int height = int(pixelHeight);
            require(qint64(width) * height * 4 <= imageBudget, "页面像素超过 64 MiB 上限。");
            QImage image(width, height, QImage::Format_ARGB32);
            require(!image.isNull(), "页面图像内存分配失败。");
            image.fill(Qt::white);
            TrackedHandle<ScopedFPDFBitmap> bitmap(FPDFBitmap_CreateEx(width, height, FPDFBitmap_BGRA,
                image.bits(), int(image.bytesPerLine())));
            require(bool(bitmap), "PDFium 位图创建失败。");
            FPDF_RenderPageBitmap(bitmap.get(), page.get(), 0, 0, width, height, 0, FPDF_ANNOT);
            bitmap.reset();
            if (!current(r.revision)) return;
            auto frame = result;
            frame.image = std::move(image);
            frame.errorCode.clear();
            frame.preview = preview;
            frame.limited = width < wanted;
            qInfo("pdf_frame session=%llu revision=%llu page=%d preview=%d width=%d height=%d bytes=%lld worker_ms=%.3f",
                r.session, r.revision, r.page, preview, width, height, qint64(frame.image.sizeInBytes()), timer.nsecsElapsed() / 1e6);
            emit frameReady(std::move(frame));
        };
        if (r.width > previewWidth && !r.skipPreview) render(previewWidth, true);
        render(r.width, r.previewOnly);
    } catch (const std::exception& error) {
        result.error = QString::fromUtf8(error.what());
        qInfo("pdf_error session=%llu revision=%llu code=%s", r.session, r.revision, qPrintable(result.errorCode));
        if (engine_ && !engine_->document) {
            engine_->close();
            emit nativeClosed(r.session, nativeHandles.load());
        }
        if (current(r.revision)) emit frameReady(std::move(result));
    }
}
