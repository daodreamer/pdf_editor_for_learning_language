#include "ocrworker.h"
#include "readingconfig.h"
#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>
#include <tesseract/ocrclass.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <cmath>
#include <algorithm>

namespace {
constexpr qint64 maxCacheFile = 8 * 1024 * 1024;
constexpr qint64 diskBudget = 128 * 1024 * 1024;
constexpr int diskEntries = 256;
QString modelDirectory() {
    const auto override = qEnvironmentVariable("LANGLERN_TESSDATA");
    return override.isEmpty() ? QCoreApplication::applicationDirPath() + "/tessdata" : override;
}
bool readModel(const char* path,std::vector<char>* output) {
    QFile file(QString::fromUtf8(path));
    if(!file.open(QIODevice::ReadOnly) || file.size()>32*1024*1024)return false;
    const auto bytes=file.readAll();
    if(file.error()!=QFileDevice::NoError)return false;
    output->assign(bytes.constData(),bytes.constData()+bytes.size());return true;
}
TextSnapshot loadCache(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxCacheFile) return {};
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.value("version").toInt() != 1 || !object.value("characters").isArray()) return {};
    const auto values = object.value("characters").toArray();
    if (values.size() > ReadingConfig::textCharacters) return {};
    auto result = std::make_shared<TextPage>(); result->ocr = true;
    const auto region=object.value("region").toArray();
    if(region.size()!=4)return {};
    result->region={region[0].toDouble(-1),region[1].toDouble(-1),region[2].toDouble(-1),region[3].toDouble(-1)};
    if(result->region.isEmpty() || !QRectF(0,0,1,1).contains(result->region))return {};
    for (const auto& value : values) {
        const auto a = value.toArray();
        if (a.size() != 6) return {};
        const int u = a[0].toInt(-1);
        const QRectF box(a[1].toDouble(-1), a[2].toDouble(-1), a[3].toDouble(-1), a[4].toDouble(-1));
        if (u <= 0 || u > 0x10ffff || (u >= 0xd800 && u <= 0xdfff)
            || !std::isfinite(box.x()+box.y()+box.width()+box.height())
            || box.x()<0 || box.y()<0 || box.width()<0 || box.height()<0 || box.right()>1.001 || box.bottom()>1.001) return {};
        result->characters.append({char32_t(u), box, a[5].toBool()});
    }
    return result;
}
void saveCache(const QString& root, const QString& path, const TextPage& page) {
    if (!QDir().mkpath(root)) return;
    QJsonArray chars;
    for (const auto& c : page.characters)
        chars.append(QJsonArray{int(c.unicode),c.box.x(),c.box.y(),c.box.width(),c.box.height(),c.visible});
    const auto& r=page.region;
    const auto data = QJsonDocument(QJsonObject{{"version",1},{"characters",chars},{"region",QJsonArray{r.x(),r.y(),r.width(),r.height()}}}).toJson(QJsonDocument::Compact);
    if (data.size() > maxCacheFile) return;
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size()) file.commit();
    const auto files = QDir(root).entryInfoList({"*.json"},QDir::Files,QDir::Time);
    qint64 bytes = 0; int count = 0;
    for (const auto& entry : files) {
        bytes += entry.size(); ++count;
        if (bytes > diskBudget || count > diskEntries) QFile::remove(entry.absoluteFilePath());
    }
}
}

OcrWorker::OcrWorker(QString root) : cacheRoot_(std::move(root)) { qRegisterMetaType<TextSnapshot>(); }
void OcrWorker::submit(OcrRequest request) {
    std::lock_guard lock(mutex_);
    latest_.store(request.revision);pending_=std::move(request);
    if(!scheduled_) {scheduled_=true;QMetaObject::invokeMethod(this,&OcrWorker::drain,Qt::QueuedConnection);}
}
void OcrWorker::invalidate(quint64 revision) {
    std::lock_guard lock(mutex_);latest_.store(revision);pending_.reset();
}
void OcrWorker::drain() {
    for(;;) {
        std::optional<OcrRequest> request;
        {std::lock_guard lock(mutex_);request.swap(pending_);if(!request){scheduled_=false;return;}}
        recognize(std::move(*request));
    }
}
void OcrWorker::recognize(OcrRequest request) {
    Q_ASSERT(QThread::currentThread() == thread() && QThread::currentThread() != qApp->thread());
    if (request.revision != latest_.load()) return;
    QElapsedTimer timer; timer.start();
    auto result = std::make_shared<TextPage>(); result->ocr = true;
    const auto fullSize=request.image.size();
    const auto crop=QRectF(request.region.x()*fullSize.width(),request.region.y()*fullSize.height(),
        request.region.width()*fullSize.width(),request.region.height()*fullSize.height()).toAlignedRect().intersected(request.image.rect());
    const auto image = (crop==request.image.rect()?request.image:request.image.copy(crop)).convertToFormat(QImage::Format_Grayscale8);
    result->region={double(crop.x())/std::max(1,fullSize.width()),double(crop.y())/std::max(1,fullSize.height()),
        double(crop.width())/std::max(1,fullSize.width()),double(crop.height())/std::max(1,fullSize.height())};
    const auto language = request.language.toUtf8();
    const auto directory = modelDirectory();
    QByteArray modelHash;
    for (const auto& part : request.language.split('+')) {
        QFile file(directory + '/' + part + ".traineddata");
        if (!file.open(QIODevice::ReadOnly) || file.size() > 32*1024*1024) {
            result->error = tr("缺少离线 OCR 语言模型：%1").arg(part); break;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(&file); modelHash += hash.result();
    }
    const auto key = QCryptographicHash::hash(request.identity + QByteArray::number(request.page) + ':' + language + modelHash
        + ':' + QByteArray(tesseract::TessBaseAPI::Version()) + ":v2:" + QByteArray::number(fullSize.width()) + 'x' + QByteArray::number(fullSize.height())
        + ':' + QByteArray::number(crop.x())+','+QByteArray::number(crop.y())+','+QByteArray::number(crop.width())+','+QByteArray::number(crop.height()), QCryptographicHash::Sha256).toHex();
    const auto path = cacheRoot_ + '/' + QString::fromLatin1(key) + ".json";
    if (!request.identity.isEmpty() && result->error.isEmpty()) {
        if (auto cached = loadCache(path)) {
            emit finished(request.session,request.revision,request.page,cached,true,timer.nsecsElapsed()/1e6); return;
        }
    }
    if (result->error.isEmpty() && (image.isNull() || request.image.sizeInBytes()>ReadingConfig::ocrImageBytes))
        result->error = tr("OCR 图像为空或超出内存预算。");
    if (result->error.isEmpty()) {
        tesseract::TessBaseAPI api;
        if (api.Init(directory.toUtf8().constData(),0,language.constData(),tesseract::OEM_LSTM_ONLY,
            nullptr,0,nullptr,nullptr,false,readModel) != 0)
            result->error = tr("无法初始化离线 OCR，请检查本地语言模型。");
        else {
            api.SetVariable("debug_file",
#ifdef Q_OS_WIN
                "NUL"
#else
                "/dev/null"
#endif
            );
            api.SetPageSegMode(request.region==QRectF(0,0,1,1)?tesseract::PSM_AUTO:tesseract::PSM_SINGLE_BLOCK);
            api.SetImage(image.constBits(),image.width(),image.height(),1,int(image.bytesPerLine()));
            api.SetSourceResolution(250);
            struct Cancellation {std::atomic<quint64>* latest;quint64 revision;} cancellation{&latest_,request.revision};
            tesseract::ETEXT_DESC monitor;
            monitor.cancel_this=&cancellation;
            monitor.cancel=[](void* context,int) {const auto& c=*static_cast<Cancellation*>(context);return c.latest->load()!=c.revision;};
            monitor.set_deadline_msecs(30000);
            if (api.Recognize(&monitor) != 0) result->error = tr("OCR 识别失败或超时，请重试或选择其他语言。");
            else if (std::unique_ptr<tesseract::ResultIterator> it{api.GetIterator()}; it) {
                do {
                    if (request.revision != latest_.load()) return;
                    std::unique_ptr<char[]> symbol(it->GetUTF8Text(tesseract::RIL_SYMBOL));
                    if (!symbol) continue;
                    int left=0,top=0,right=0,bottom=0;
                    if (!it->BoundingBox(tesseract::RIL_SYMBOL,&left,&top,&right,&bottom)) continue;
                    if (!result->characters.isEmpty()) {
                        if (it->IsAtBeginningOf(tesseract::RIL_TEXTLINE)) result->characters.append({U'\n',{},false});
                        else if (it->IsAtBeginningOf(tesseract::RIL_WORD) && it->BlanksBeforeWord()>0) result->characters.append({U' ',{},false});
                    }
                    const auto box = QRectF(double(left+crop.x())/fullSize.width(),double(top+crop.y())/fullSize.height(),
                        double(right-left)/fullSize.width(),double(bottom-top)/fullSize.height()).intersected(QRectF(0,0,1,1));
                    for (auto c : QString::fromUtf8(symbol.get()).toUcs4()) result->characters.append({c,box,!box.isEmpty()});
                    if (result->characters.size()>ReadingConfig::textCharacters) {
                        result->characters.clear(); result->error=tr("OCR 文字量超过本页上限，请缩小识别区域。"); break;
                    }
                } while (it->Next(tesseract::RIL_SYMBOL));
            }
        }
    }
    if (request.revision != latest_.load()) return;
    if (!request.identity.isEmpty() && result->error.isEmpty()) saveCache(cacheRoot_,path,*result);
    emit finished(request.session,request.revision,request.page,result,false,timer.nsecsElapsed()/1e6);
}
