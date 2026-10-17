#include "pdfworker.h"
#include "inkpath.h"
#include "answerlines.h"
#include "exporttext.h"
#include "scancleanup.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <cpp/fpdf_scopers.h>
#include <fpdfview.h>
#include <fpdf_text.h>
#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fpdf_ppo.h>
#include <QBuffer>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <QTemporaryFile>
#include <QSet>
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
void PdfWorker::optimizeScan(quint64 session,QString target,QByteArray identity,QVariantMap options) {
    scanCancelled_.store(false);
    QMetaObject::invokeMethod(this,[this,session,target,identity,options] {
        QString error;
        try {
            require(engine_ && engine_->session==session && engine_->document,"原文档已关闭。");
            require(!QFileInfo::exists(target),"请使用尚未存在的新 PDF 文件名，不能覆盖原文件。");
            QFile input(engine_->file.fileName());require(input.open(QIODevice::ReadOnly),"无法校验原文件。");
            QCryptographicHash hash(QCryptographicHash::Sha256);require(hash.addData(&input)&&hash.result().toHex()==identity,"原文件已改变，未执行扫描优化。");
            const int pages=FPDF_GetPageCount(engine_->document.get());require(pages>0,"文档没有可处理的页面。");
            QTemporaryFile output(QFileInfo(target).absolutePath()+"/.linguapdf-scan-XXXXXX");require(output.open(),"无法创建扫描优化临时文件。");
            {
                QPdfWriter writer(&output);writer.setResolution(72);writer.setTitle(QStringLiteral("LinguaPDF optimized scan"));
                writer.setPageMargins(QMarginsF(0,0,0,0),QPageLayout::Point);
                QPainter painter;
                for(int i=0;i<pages;++i) {
                    require(!stopping_&&!scanCancelled_,"扫描优化已取消，未提交输出。");
                    ScopedFPDFPage page(FPDF_LoadPage(engine_->document.get(),i));require(bool(page),"无法加载扫描页面。");
                    const double width=FPDF_GetPageWidthF(page.get()),height=FPDF_GetPageHeightF(page.get());
                    require(width>0&&height>0&&std::isfinite(width)&&std::isfinite(height),"扫描页面尺寸无效。");
                    const double scale=std::min({150./72.,4096./width,4096./height,std::sqrt(double(maxImageBytes)/(4*width*height))});
                    const int pw=std::max(1,int(std::floor(width*scale))),ph=std::max(1,int(std::floor(height*scale)));
                    QImage image(pw,ph,QImage::Format_ARGB32);require(!image.isNull(),"扫描图像内存分配失败。");image.fill(Qt::white);
                    ScopedFPDFBitmap bitmap(FPDFBitmap_CreateEx(pw,ph,FPDFBitmap_BGRA,image.bits(),int(image.bytesPerLine())));require(bool(bitmap),"无法创建扫描位图。");
                    FPDF_RenderPageBitmap(bitmap.get(),page.get(),0,0,pw,ph,0,FPDF_ANNOT);bitmap.reset();page.reset();
                    image=cleanScan(std::move(image),options);
                    writer.setPageSize(QPageSize(QSizeF(width,height),QPageSize::Point));
                    writer.setPageMargins(QMarginsF(0,0,0,0),QPageLayout::Point);
                    if(i==0)require(painter.begin(&writer),"无法初始化优化 PDF 写入器。");else require(writer.newPage(),"无法写入优化 PDF 页面。");
                    painter.drawImage(QRectF(0,0,width,height),image);
                    emit scanProgress(session,i+1,pages);
                }
                require(painter.end(),"扫描优化 PDF 写入失败。");
            }
            require(output.flush(),"无法刷新扫描优化文件。");
            require(quint64(output.size())<=std::numeric_limits<unsigned long>::max(),"结果超过当前校验接口上限，未提交。");
            FPDF_FILEACCESS access{};access.m_FileLen=static_cast<unsigned long>(output.size());access.m_GetBlock=readBlock;access.m_Param=&output;
            {ScopedFPDFDocument check(FPDF_LoadCustomDocument(&access,nullptr));require(bool(check)&&FPDF_GetPageCount(check.get())==pages,"优化 PDF 回读失败。");}
            hash.reset();require(input.seek(0)&&hash.addData(&input)&&hash.result().toHex()==identity,"处理期间原文件改变，未提交。");
            require(!stopping_&&!scanCancelled_,"扫描优化已取消，未提交输出。");
            require(output.rename(target),"无法提交优化文件；目标可能已存在。");output.setAutoRemove(false);
        }catch(const std::exception& e){error=QString::fromUtf8(e.what());}
        emit scanFinished(session,target,error);
    },Qt::QueuedConnection);
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
    result.textOnly = r.textOnly;
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
        const auto pagePoint = [&](int x, int y) {
            double px=0, py=0;
            require(FPDF_DeviceToPage(page.get(),0,0,1000000,1000000,0,x,y,&px,&py), "页面坐标转换失败。");
            require(std::isfinite(px) && std::isfinite(py), "页面坐标无效。");
            return QPointF(px,py);
        };
        const auto origin=pagePoint(0,0), right=pagePoint(1000000,0)-origin, down=pagePoint(0,1000000)-origin;
        result.geometry.displayToPdf=QTransform(right.x(),right.y(),down.x(),down.y(),origin.x(),origin.y());
        result.geometry.pdfToDisplay=result.geometry.displayToPdf.inverted(&result.geometry.valid);
        result.geometry.units=QSizeF(std::hypot(right.x(),right.y()),std::hypot(down.x(),down.y()));
        require(result.geometry.valid && result.geometry.units.width()>0 && result.geometry.units.height()>0, "页面变换不可逆。");

        if (r.textOnly) {
            result.errorCode = QStringLiteral("text");
            auto snapshot = std::make_shared<TextPage>();
            TrackedHandle<ScopedFPDFTextPage> text(FPDFText_LoadPage(page.get()));
            require(bool(text), "无法读取页面文字层。");
            const int count = FPDFText_CountChars(text.get());
            require(count >= 0, "读取文字字符数失败。");
            if (count > ReadingConfig::textCharacters) {
                snapshot->error = QStringLiteral("此页文字超过选择上限（65536 字符）。");
            } else {
                snapshot->characters.reserve(count);
                constexpr int device = ReadingConfig::textDeviceSize;
                const auto map = [&](double x, double y) {
                    int dx = 0, dy = 0;
                    require(FPDF_PageToDevice(page.get(),0,0,device,device,0,x,y,&dx,&dy), "文字坐标转换失败。");
                    return QPointF(double(dx)/device,double(dy)/device);
                };
                for (int i=0; i<count; ++i) {
                    if (!current(r.revision)) return;
                    TextCharacter c;
                    c.unicode = char32_t(FPDFText_GetUnicode(text.get(),i));
                    double left=0,charRight=0,bottom=0,top=0;
                    if (FPDFText_GetCharBox(text.get(),i,&left,&charRight,&bottom,&top)
                        && std::isfinite(left) && std::isfinite(charRight) && std::isfinite(bottom) && std::isfinite(top)) {
                        c.box = QRectF(map(left,top),map(charRight,bottom)).normalized().intersected(QRectF(0,0,1,1));
                        c.visible = !c.box.isEmpty();
                    }
                    snapshot->characters.append(c);
                }
            }
            result.text = std::move(snapshot);
            result.errorCode.clear();
            qInfo("pdf_text session=%llu page=%d chars=%lld worker_ms=%.3f",r.session,r.page,
                  qint64(result.text->characters.size()),timer.nsecsElapsed()/1e6);
            if (current(r.revision)) emit frameReady(std::move(result));
            return;
        }
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
            if(!preview || r.previewOnly) {
                frame.answerLines=AnswerLines::detect(frame.image,frame.geometry.units);
                frame.answerLinesReady=true;
            }
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

void PdfWorker::exportPdf(quint64 session,QString source,QString target,QByteArray identity,AnnotationSnapshot snapshot) {
    Q_ASSERT(QThread::currentThread()==thread());
    QString error;
    try {
        require(!stopping_, "正在关闭，未导出。");
        require(!target.isEmpty() && !QFileInfo::exists(target) && !QFileInfo(target).isSymLink(), "导出只接受新文件名，禁止覆盖原文件、别名或已有文件。");
        QFile input(source); require(input.open(QIODevice::ReadOnly), "无法读取原 PDF。");
        QCryptographicHash hash(QCryptographicHash::Sha256);
        require(hash.addData(&input) && hash.result().toHex()==identity, "原 PDF 已改变，已取消导出。");
        require(input.size()>0 && quint64(input.size())<=std::numeric_limits<unsigned long>::max(), "原 PDF 大小不受支持。");
        if(!engine_) engine_=std::make_unique<Engine>();
        FPDF_FILEACCESS access{}; access.m_FileLen=static_cast<unsigned long>(input.size()); access.m_GetBlock=readBlock; access.m_Param=&input;
        ScopedFPDFDocument document(FPDF_LoadCustomDocument(&access,nullptr));
        require(bool(document), "导出无法打开独立 PDF 会话。");
        const int pages=FPDF_GetPageCount(document.get());
        QSet<int> changed;
        for(const auto& n:snapshot.notes) changed.insert(n.page);
        for(const auto& i:snapshot.ink) changed.insert(i.page);
        for(int p:changed) require(p>=0 && p<pages, "标注页码超出文档范围。");
        auto ordered=changed.values(); std::sort(ordered.begin(),ordered.end());
        const auto prepared=prepareExportText(snapshot.notes);
        ScopedFPDFFont font;
        if(!prepared.notes.isEmpty()) {
            font.reset(FPDFText_LoadCidType2Font(document.get(),reinterpret_cast<const uint8_t*>(prepared.font.constData()),
                uint32_t(prepared.font.size()),prepared.cmap.constData(),reinterpret_cast<const uint8_t*>(prepared.cidToGid.constData()),uint32_t(prepared.cidToGid.size())));
            require(bool(font),"嵌入字体和 Unicode 映射失败。");
        }
        int completed=0;
        for(int index:ordered) {
            require(!stopping_, "关闭时取消了导出。");
            ScopedFPDFPage page(FPDF_LoadPage(document.get(),index)); require(bool(page), "无法打开导出页面。");
            auto map=[&](int x,int y) { double px=0,py=0; require(FPDF_DeviceToPage(page.get(),0,0,1000000,1000000,0,x,y,&px,&py), "导出坐标转换失败。"); return QPointF(px,py); };
            const auto origin=map(0,0), right=map(1000000,0)-origin, down=map(0,1000000)-origin;
            const double w=std::hypot(right.x(),right.y()),h=std::hypot(down.x(),down.y());
            require(w>0 && h>0 && std::isfinite(w) && std::isfinite(h), "导出页面尺寸无效。");
            QTransform display(right.x()/w,right.y()/w,down.x()/h,down.y()/h,origin.x(),origin.y());
            bool invertible=false; const auto local=display.inverted(&invertible); require(invertible,"页面变换不可逆。");
            QByteArray overlay; QBuffer buffer(&overlay); require(buffer.open(QIODevice::WriteOnly), "无法创建矢量标注缓冲区。");
            {
                QPdfWriter writer(&buffer);
                writer.setResolution(720);
                writer.setPageSize(QPageSize(QSizeF(w,h),QPageSize::Point,QString(),QPageSize::ExactMatch));
                writer.setPageMargins(QMarginsF(0,0,0,0),QPageLayout::Point);
                writer.setTitle(QStringLiteral("Langlern annotations"));
                QPainter painter(&writer); require(painter.isActive(),"无法启动 PDF 矢量绘制。");
                painter.scale(writer.width()/w,writer.height()/h);
                auto ink=snapshot.ink;
                std::sort(ink.begin(),ink.end(),[](const auto& a,const auto& b){return a.order<b.order;});
                for(const auto& i:ink) if(i.page==index && !i.points.isEmpty()) {
                    QPen pen(i.color,i.width,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin); painter.setPen(pen); painter.setBrush(Qt::NoBrush);
                    if(i.points.size()==1) { painter.setPen(Qt::NoPen); painter.setBrush(i.color); painter.drawEllipse(local.map(i.points.first()),i.width/2,i.width/2); }
                    else painter.drawPath(InkPath::path(i.points,local));
                }
                require(painter.end(),"矢量 PDF 写入失败。");
            }
            ScopedFPDFDocument overlayDoc(FPDF_LoadMemDocument64(overlay.constData(),size_t(overlay.size()),nullptr));
            require(bool(overlayDoc),"无法读取矢量标注 PDF。");
            ScopedFPDFPage overlayPage(FPDF_LoadPage(overlayDoc.get(),0)); require(bool(overlayPage),"矢量页面无效。");
            const double ow=FPDF_GetPageWidthF(overlayPage.get()),oh=FPDF_GetPageHeightF(overlayPage.get());
            std::unique_ptr<std::remove_pointer_t<FPDF_XOBJECT>,decltype(&FPDF_CloseXObject)> form(FPDF_NewXObjectFromPage(document.get(),overlayDoc.get(),0),&FPDF_CloseXObject); require(bool(form),"无法导入标注矢量对象。");
            ScopedFPDFPageObject object(FPDF_NewFormObjectFromXObject(form.get())); require(bool(object),"无法创建标注对象。");
            const auto bottom=origin+down;
            FPDFPageObj_Transform(object.get(),right.x()/ow,right.y()/ow,-down.x()/oh,-down.y()/oh,bottom.x(),bottom.y());
            FPDFPage_InsertObject(page.get(),object.release());
            for(const auto& note:prepared.notes) if(note.note.page==index) insertExportText(document.get(),page.get(),font.get(),note);
            require(FPDFPage_GenerateContent(page.get()),"生成 PDF 页面内容失败。");
            emit exportProgress(session,++completed,int(ordered.size()));
        }
        struct Writer : FPDF_FILEWRITE {
            QTemporaryFile file;
            explicit Writer(const QString& target) : file(QFileInfo(target).absolutePath()+"/.langlern-export-XXXXXX") {
                version=1; WriteBlock=[](FPDF_FILEWRITE* base,const void* data,unsigned long size)->int {
                    return static_cast<Writer*>(base)->file.write(static_cast<const char*>(data),size)==size;
                };
            }
        } writer(target);
        require(writer.file.open(),"无法在目标目录创建导出临时文件。");
        require(FPDF_SaveAsCopy(document.get(),&writer,FPDF_NO_INCREMENTAL),"PDF 写入失败，请检查磁盘空间。");
        require(writer.file.flush(),"PDF 临时文件刷新失败。");
        require(quint64(writer.file.size())<=std::numeric_limits<unsigned long>::max(),"导出文件超过当前回读接口的大小限制，未提交。");
        FPDF_FILEACCESS verifyAccess{}; verifyAccess.m_FileLen=static_cast<unsigned long>(writer.file.size()); verifyAccess.m_GetBlock=readBlock; verifyAccess.m_Param=&writer.file;
        { ScopedFPDFDocument check(FPDF_LoadCustomDocument(&verifyAccess,nullptr)); require(bool(check) && FPDF_GetPageCount(check.get())==pages,"导出回读校验失败。"); }
        require(!stopping_,"关闭时取消了导出。");
        hash.reset();
        require(input.seek(0) && hash.addData(&input) && hash.result().toHex()==identity,"原 PDF 在导出期间改变，未提交输出。");
        // QFile::rename never replaces an existing target; racing creators and hardlink aliases are safe.
        require(writer.file.rename(target),"无法提交导出文件；目标可能已存在或目录不可写。");
        writer.file.setAutoRemove(false);
    } catch(const std::exception& e) { error=QString::fromUtf8(e.what()); }
    emit exportFinished(session,snapshot.revision,error);
}
