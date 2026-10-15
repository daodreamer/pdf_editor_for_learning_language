#include "worker.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>
#include <QtEndian>
#include <cpp/fpdf_scopers.h>
#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fpdf_text.h>
#include <fpdfview.h>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace phase0 {
namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
struct Library {
    QThread* owner = QThread::currentThread();
    Library() { FPDF_InitLibrary(); }
    ~Library() { Q_ASSERT(owner == QThread::currentThread()); FPDF_DestroyLibrary(); }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
};
int readBlock(void* context, unsigned long offset, unsigned char* data, unsigned long size) {
    auto& file = *static_cast<QFile*>(context);
    return file.seek(offset) && file.read(reinterpret_cast<char*>(data), size) == size;
}
struct Writer : FPDF_FILEWRITE {
    QSaveFile file;
    explicit Writer(const QString& path) : file(path) {
        version = 1;
        WriteBlock = [](FPDF_FILEWRITE* self, const void* data, unsigned long size) {
            return static_cast<Writer*>(self)->file.write(static_cast<const char*>(data), size) == size ? 1 : 0;
        };
        require(file.open(QIODevice::WriteOnly), "export: cannot open new output");
    }
};
QString textOf(FPDF_PAGE page) {
    ScopedFPDFTextPage text(FPDFText_LoadPage(page));
    require(bool(text), "text: load failed");
    const int count = FPDFText_CountChars(text.get());
    QString result;
    for (int i = 0; i < count; ++i) {
        const char32_t cp = FPDFText_GetUnicode(text.get(), i);
        result += QString::fromUcs4(&cp, 1);
    }
    return result;
}
QJsonObject run(const Request& r, QImage& image) {
    require(QThread::currentThread() != qApp->thread(), "PDFium must run off UI thread");
    require(QSysInfo::ByteOrder == QSysInfo::LittleEndian, "BGRA probe needs little endian");
    QElapsedTimer timer;
    timer.start();
    Library library;
    QFile input(r.path);
    require(input.open(QIODevice::ReadOnly), "input: cannot open PDF");
    require(input.size() > 0 && quint64(input.size()) <= std::numeric_limits<unsigned long>::max(),
            "input: unsupported file size for FPDF_FILEACCESS");
    FPDF_FILEACCESS access{};
    access.m_FileLen = static_cast<unsigned long>(input.size());
    access.m_GetBlock = readBlock;
    access.m_Param = &input;
    ScopedFPDFDocument document(FPDF_LoadCustomDocument(&access, nullptr));
    require(bool(document), "PDFium open failed (damaged or password protected)");
    QJsonObject meta{{"generation", qint64(r.generation)}, {"worker_thread", QString::number(quintptr(QThread::currentThreadId()))},
                     {"file_bytes", input.size()}, {"pages", FPDF_GetPageCount(document.get())},
                     {"open_ms", timer.nsecsElapsed() / 1e6}};
    require(r.page >= 0 && r.page < FPDF_GetPageCount(document.get()), "page out of range");
    ScopedFPDFPage page(FPDF_LoadPage(document.get(), r.page));
    require(bool(page), "page load failed");
    const double w = FPDF_GetPageWidthF(page.get()), h = FPDF_GetPageHeightF(page.get());
    require(std::isfinite(w) && std::isfinite(h) && w > 0 && h > 0, "invalid page geometry");
    require(r.width > 0 && r.width <= Config::maxDimension, "width exceeds probe limit");
    const double height = std::ceil(r.width * h / w);
    require(height > 0 && height <= Config::maxDimension, "height exceeds probe limit");
    const int pixelsH = static_cast<int>(height);
    require(qint64(r.width) * pixelsH * 4 <= Config::maxImageBytes, "pixel budget exceeded before allocation");
    image = QImage(r.width, pixelsH, QImage::Format_ARGB32);
    require(!image.isNull(), "image allocation failed");
    image.fill(Qt::white);
    ScopedFPDFBitmap bitmap(FPDFBitmap_CreateEx(r.width, pixelsH, FPDFBitmap_BGRA,
                                               image.bits(), static_cast<int>(image.bytesPerLine())));
    require(bool(bitmap), "bitmap creation failed");
    const auto renderStart = timer.nsecsElapsed();
    FPDF_RenderPageBitmap(bitmap.get(), page.get(), 0, 0, r.width, pixelsH, 0, FPDF_ANNOT);
    meta["render_ms"] = (timer.nsecsElapsed() - renderStart) / 1e6;
    bitmap.reset(); // QImage owns storage; no borrowed PDFium buffer crosses threads.
    meta["width"] = r.width;
    meta["height"] = pixelsH;
    meta["page_width"] = w;
    meta["page_height"] = h;
    meta["image_bytes"] = image.sizeInBytes();
    meta["rotation"] = FPDFPage_GetRotation(page.get());
    float left = 0, bottom = 0, right = 0, top = 0;
    if (!FPDFPage_GetCropBox(page.get(), &left, &bottom, &right, &top)) {
        require(FPDFPage_GetMediaBox(page.get(), &left, &bottom, &right, &top), "page boxes unavailable");
        meta["crop_fallback_to_media"] = true;
    }
    meta["crop_box"] = QJsonArray{left, bottom, right, top};
    // Device APIs include the page's intrinsic rotation. Additional view rotation is separate.
    double maxPixelError = 0;
    for (int rotation = 0; rotation < 4; ++rotation) {
        for (int dx : {0, r.width / 3, r.width}) for (int dy : {0, pixelsH / 3, pixelsH}) {
            double px = 0, py = 0;
            int x = 0, y = 0;
            require(FPDF_DeviceToPage(page.get(), 0, 0, r.width, pixelsH, rotation, dx, dy, &px, &py), "device to page failed");
            require(FPDF_PageToDevice(page.get(), 0, 0, r.width, pixelsH, rotation, px, py, &x, &y), "page to device failed");
            maxPixelError = std::max(maxPixelError, double(std::max(std::abs(x-dx), std::abs(y-dy))));
        }
    }
    require(maxPixelError <= 1, "coordinate roundtrip exceeds one pixel");
    meta["coordinate_max_pixel_error"] = maxPixelError;
    const auto extracted = textOf(page.get());
    meta["text_characters"] = extracted.size(); // Do not log private document text.
    if (!r.exportPath.isEmpty()) {
        require(!QFileInfo::exists(r.exportPath), "export refuses existing target, including aliases");
        QFile fontFile(r.font);
        require(fontFile.open(QIODevice::ReadOnly), "font file missing");
        const auto data = fontFile.readAll();
        ScopedFPDFFont font(FPDFText_LoadFont(document.get(), reinterpret_cast<const uint8_t*>(data.constData()),
                                             static_cast<uint32_t>(data.size()), FPDF_FONT_TRUETYPE, 1));
        require(bool(font), "font loading failed");
        ScopedFPDFPageObject text(FPDFPageObj_CreateTextObj(document.get(), font.get(), 18));
        require(bool(text), "text object creation failed");
        const QString phrase = QString::fromUtf8("Bonjour 中文练习");
        require(FPDFText_SetText(text.get(), reinterpret_cast<FPDF_WIDESTRING>(phrase.utf16())), "set UTF16 text failed");
        FPDFPageObj_Transform(text.get(), 1, 0, 0, 1, left + 36, bottom + 80);
        require(FPDFPageObj_SetFillColor(text.get(), 0, 40, 180, 255), "text color failed");
        FPDFPage_InsertObject(page.get(), text.release());
        ScopedFPDFPageObject path(FPDFPageObj_CreateNewPath(left + 36, bottom + 50));
        require(bool(path), "path creation failed");
        require(FPDFPath_LineTo(path.get(), left + 200, bottom + 65), "path line failed");
        require(FPDFPageObj_SetStrokeColor(path.get(), 220, 20, 60, 128), "stroke color failed");
        require(FPDFPageObj_SetStrokeWidth(path.get(), 3), "stroke width failed");
        require(FPDFPath_SetDrawMode(path.get(), FPDF_FILLMODE_NONE, 1), "path mode failed");
        FPDFPage_InsertObject(page.get(), path.release());
        require(FPDFPage_GenerateContent(page.get()), "generate content failed");
        Writer writer(r.exportPath);
        require(FPDF_SaveAsCopy(document.get(), &writer, FPDF_NO_INCREMENTAL), "save copy failed");
        require(writer.file.commit(), "export atomic commit failed");
        // Font must outlive objects/page until that page has been closed.
        page.reset();
        font.reset();
        QFile verification(r.exportPath);
        require(verification.open(QIODevice::ReadOnly), "reopen export failed");
        FPDF_FILEACCESS va{};
        va.m_FileLen = static_cast<unsigned long>(verification.size());
        va.m_GetBlock = readBlock; va.m_Param = &verification;
        ScopedFPDFDocument reopened(FPDF_LoadCustomDocument(&va, nullptr));
        require(bool(reopened), "export PDF cannot reopen");
        ScopedFPDFPage exported(FPDF_LoadPage(reopened.get(), r.page));
        require(bool(exported), "exported page failed reopen");
        const auto exportedText = textOf(exported.get());
        // Noto maps space and NBSP to the same glyph; auto ToUnicode may choose NBSP.
        // Preserve this known limitation in evidence rather than silently changing persisted text.
        auto normalizedText = exportedText;
        normalizedText.replace(QChar(0x00a0), QChar(0x20));
        require(normalizedText.contains(phrase), "exported Chinese text failed roundtrip");
        meta["export_space_normalized"] = exportedText != normalizedText;
        meta["export_text_roundtrip"] = true;
    }
    meta["before_output_cleanup_ms"] = timer.nsecsElapsed() / 1e6;
    if (!r.output.isEmpty()) {
        QFile png(r.output + "/page.png");
        require(png.open(QIODevice::WriteOnly | QIODevice::NewOnly), "PNG target must be new");
        require(image.save(&png, "PNG"), "cannot save PNG");
    }
    return meta;
}
}
void PdfWorker::replace(Request request) {
    std::lock_guard lock(mutex_);
    latest_.store(request.generation);
    pending_ = std::move(request);
}
void PdfWorker::cancel() {
    std::lock_guard lock(mutex_);
    latest_.fetch_add(1);
    pending_.reset();
}
void PdfWorker::drain() {
    Q_ASSERT(QThread::currentThread() == thread());
    for (;;) {
        std::optional<Request> request;
        {
            std::lock_guard lock(mutex_);
            request.swap(pending_);
        }
        if (!request) { emit idle(); return; }
        QImage image;
        QJsonObject metadata;
        const auto started = monotonicNs();
        emit this->started(request->generation);
        try { metadata = run(*request, image); }
        catch (const std::exception& error) { metadata["error"] = QString::fromUtf8(error.what()); }
        // run() returned: every native RAII destructor and DestroyLibrary already ran here.
        metadata["native_scope_closed"] = true;
        metadata["generation"] = qint64(request->generation);
        metadata["started_ns"] = QString::number(started);
        const auto finished = monotonicNs();
        metadata["finished_ns"] = QString::number(finished);
        metadata["total_worker_ms"] = (finished - started) / 1e6;
        emit trace(metadata);
        if (current(request->generation)) emit result(request->generation, image, metadata);
        else emit discarded(request->generation);
    }
}
}
