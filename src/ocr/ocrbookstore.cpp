#include "ocrbookstore.h"
#include "readingconfig.h"
#include "runtimepaths.h"
#include <tesseract/baseapi.h>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace {
constexpr char magic[4] = {'L','P','O','C'};
constexpr int headerBytes = 4 + 2 + 2 + 4 + 4 + 4*8 + 6*8;
constexpr int characterBytes = 4 + 4*2 + 1;
constexpr int digestBytes = 32;
constexpr qint64 maxPageBytes = headerBytes + qint64(ReadingConfig::textCharacters) * characterBytes + digestBytes;
// Recognition pipeline parameters that change results; part of the profile hash.
const QByteArray pipeline = "w" + QByteArray::number(ReadingConfig::ocrWidth) + "|gray8|psm-auto|ppi250|lstm";

template<typename T> void put(QByteArray& out, T value) {
    char bytes[sizeof(T)]; qToLittleEndian(value, bytes); out.append(bytes, sizeof(T));
}
void putDouble(QByteArray& out, double value) { quint64 bits; std::memcpy(&bits,&value,8); put<quint64>(out,bits); }
template<typename T> T get(const char*& p) { const T v = qFromLittleEndian<T>(p); p += sizeof(T); return v; }
double getDouble(const char*& p) { const auto bits=get<quint64>(p); double v; std::memcpy(&v,&bits,8); return v; }
quint16 quantize(double v) { return quint16(std::lround(std::clamp(v,0.0,1.0) * 65535.0)); }
double dequantize(quint16 v) { return v / 65535.0; }

qint64 directoryBytes(const QString& path) {
    qint64 bytes = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); bytes += it.fileInfo().size(); }
    return bytes;
}
QString pageName(int page) { return QStringLiteral("%1.lpo").arg(page, 5, 10, QLatin1Char('0')); }
bool isHex(const QString& s, int length) {
    if (s.size() != length) return false;
    for (const auto c : s) if (!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return false;
    return true;
}
QJsonObject readManifest(const QString& directory) {
    QFile file(directory + "/manifest.json");
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64*1024) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
bool writeAtomic(const QString& path, const QByteArray& data) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
}

QString OcrBook::defaultRoot() {
    const auto overrideRoot = qEnvironmentVariable("LANGLERN_WORKSPACE_DIR");
    return (overrideRoot.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : overrideRoot)
        + "/ocr-books-v1";
}
QString OcrBook::modelDirectory() {
    const auto override = qEnvironmentVariable("LANGLERN_TESSDATA");
    return override.isEmpty() ? runtimeDataDir() + "/tessdata" : override;
}

QByteArray OcrBook::profile(const QString& language, const QString& directory, QString* error) {
    struct Model { qint64 size; qint64 modified; QByteArray hash; };
    static QMutex mutex;
    static std::map<QString,Model> models;
    QByteArray hashes;
    for (const auto& part : language.split('+')) {
        const QString path = directory + '/' + part + ".traineddata";
        const QFileInfo info(path);
        if (part.isEmpty() || !info.isFile() || info.size() > 32*1024*1024) {
            if (error) *error = QObject::tr("缺少离线 OCR 语言模型：%1").arg(part);
            return {};
        }
        const qint64 modified = info.lastModified().toMSecsSinceEpoch();
        {
            QMutexLocker lock(&mutex);
            const auto found = models.find(path);
            if (found != models.end() && found->second.size == info.size() && found->second.modified == modified) {
                hashes += part.toUtf8() + '=' + found->second.hash + ';'; continue;
            }
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = QObject::tr("缺少离线 OCR 语言模型：%1").arg(part);
            return {};
        }
        QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(&file);
        const auto hex = hash.result().toHex();
        { QMutexLocker lock(&mutex); models[path] = {info.size(), modified, hex}; }
        hashes += part.toUtf8() + '=' + hex + ';';
    }
    return QCryptographicHash::hash("lpoc" + QByteArray::number(formatVersion) + '|' + QByteArray(tesseract::TessBaseAPI::Version())
        + '|' + language.toUtf8() + '|' + hashes + '|' + pipeline, QCryptographicHash::Sha256).toHex();
}

QByteArray OcrBook::encode(const OcrBookPage& page) {
    QByteArray out;
    const auto count = page.text ? page.text->characters.size() : 0;
    out.reserve(headerBytes + count * characterBytes + digestBytes);
    out.append(magic, 4);
    put<quint16>(out, formatVersion);
    put<quint16>(out, page.native ? 1 : 0);
    put<quint32>(out, quint32(page.page));
    put<quint32>(out, quint32(count));
    const QRectF region = page.text ? page.text->region : QRectF(0,0,1,1);
    for (double v : {region.x(), region.y(), region.width(), region.height()}) putDouble(out, v);
    const auto& t = page.displayToPdf;
    for (double v : {t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()}) putDouble(out, v);
    if (page.text) for (const auto& c : page.text->characters) {
        put<quint32>(out, quint32(c.unicode));
        put<quint16>(out, quantize(c.box.x())); put<quint16>(out, quantize(c.box.y()));
        put<quint16>(out, quantize(c.box.width())); put<quint16>(out, quantize(c.box.height()));
        out.append(char(c.visible ? 1 : 0));
    }
    out.append(QCryptographicHash::hash(out, QCryptographicHash::Sha256));
    return out;
}

std::optional<OcrBookPage> OcrBook::decode(const QByteArray& bytes, int expectedPage) {
    if (bytes.size() < headerBytes + digestBytes || bytes.size() > maxPageBytes) return std::nullopt;
    const auto body = QByteArrayView(bytes).first(bytes.size() - digestBytes);
    if (QCryptographicHash::hash(body, QCryptographicHash::Sha256) != bytes.right(digestBytes)) return std::nullopt;
    const char* p = bytes.constData();
    if (std::memcmp(p, magic, 4) != 0) return std::nullopt;
    p += 4;
    if (get<quint16>(p) != formatVersion) return std::nullopt;
    const auto kind = get<quint16>(p);
    const auto pageNumber = get<quint32>(p);
    const auto count = get<quint32>(p);
    if (kind > 1 || int(pageNumber) != expectedPage || count > quint32(ReadingConfig::textCharacters)
        || bytes.size() != headerBytes + qint64(count) * characterBytes + digestBytes || (kind == 1 && count != 0)) return std::nullopt;
    double r[4], m[6];
    for (auto& v : r) v = getDouble(p);
    for (auto& v : m) v = getDouble(p);
    for (double v : r) if (!std::isfinite(v)) return std::nullopt;
    for (double v : m) if (!std::isfinite(v)) return std::nullopt;
    OcrBookPage result;
    result.page = expectedPage;
    result.native = kind == 1;
    result.displayToPdf = QTransform(m[0], m[1], m[2], m[3], m[4], m[5]);
    if (!result.native) {
        auto text = std::make_shared<TextPage>();
        text->ocr = true;
        text->region = QRectF(r[0], r[1], r[2], r[3]);
        if (text->region.isEmpty() || !QRectF(0,0,1,1).contains(text->region)) return std::nullopt;
        text->characters.reserve(count);
        for (quint32 i = 0; i < count; ++i) {
            const auto u = get<quint32>(p);
            const double x = dequantize(get<quint16>(p)), y = dequantize(get<quint16>(p));
            const double w = dequantize(get<quint16>(p)), h = dequantize(get<quint16>(p));
            const auto visible = quint8(*p++);
            if (u == 0 || u > 0x10ffff || (u >= 0xd800 && u <= 0xdfff) || visible > 1) return std::nullopt;
            text->characters.append({char32_t(u), QRectF(x, y, w, h), visible == 1});
        }
        result.text = std::move(text);
    }
    return result;
}

OcrBookReader::OcrBookReader(QString root, QByteArray identity, QByteArray profile)
    : directory_(root + '/' + QString::fromLatin1(identity) + '/' + QString::fromLatin1(profile)) {}
bool OcrBookReader::exists() const { return QFileInfo(directory_ + "/manifest.json").isFile(); }
QString OcrBookReader::pagePath(int page) const { return directory_ + "/pages/" + pageName(page); }
QList<int> OcrBookReader::list(bool native) const {
    QList<int> result;
    if (!exists()) return result;
    // Native records carry no characters: header plus digest only.
    for (const auto& info : QDir(directory_ + "/pages").entryInfoList({"*.lpo"}, QDir::Files, QDir::Name)) {
        bool ok = false;
        const int page = info.completeBaseName().toInt(&ok);
        if (!ok || page < 0 || info.size() < headerBytes + digestBytes) continue;
        const bool isNative = info.size() == headerBytes + digestBytes && [&] {
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) return false;
            const auto head = file.read(8);
            return head.size() == 8 && qFromLittleEndian<quint16>(head.constData() + 6) == 1;
        }();
        if (isNative == native) result.append(page);
    }
    return result;
}
QList<int> OcrBookReader::pages() const { return list(false); }
QList<int> OcrBookReader::nativePages() const { return list(true); }
std::optional<OcrBookPage> OcrBookReader::page(int page) const {
    QFile file(pagePath(page));
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxPageBytes) return std::nullopt;
    return OcrBook::decode(file.readAll(), page);
}
QString OcrBookReader::text(int number) const {
    const auto record = page(number);
    if (!record || !record->text) return {};
    QString result;
    for (const auto& c : record->text->characters) result += QString::fromUcs4(&c.unicode, 1);
    return result;
}
bool OcrBookReader::forEachPage(const std::function<bool(const OcrBookPage&)>& visit) const {
    for (int number : pages()) if (const auto record = page(number); record && !visit(*record)) return false;
    return true;
}

OcrBookStore::OcrBookStore(QString root, Limits limits) : root_(std::move(root)), limits_(limits) {}
bool OcrBookStore::isOpen(const QByteArray& identity, const QByteArray& profile) const {
    return !identity_.isEmpty() && identity_ == identity && profile_ == profile;
}
void OcrBookStore::close() {
    identity_.clear(); profile_.clear(); language_.clear(); directory_.clear();
    created_ = false; bookBytes_ = 0; otherBooks_.clear(); bufferedNative_.clear(); pageCount_ = 0;
}
OcrBookStore::Index OcrBookStore::open(const QByteArray& identity, const QByteArray& profile, const QString& language, int pageCount) {
    close();
    Index index;
    if (!isHex(QString::fromLatin1(identity), 64) || !isHex(QString::fromLatin1(profile), 64) || pageCount <= 0) {
        index.error = QObject::tr("OCR 识别存储参数无效。");
        return index;
    }
    identity_ = identity; profile_ = profile; language_ = language; pageCount_ = pageCount;
    const QString book = root_ + '/' + QString::fromLatin1(identity);
    directory_ = book + '/' + QString::fromLatin1(profile);
    const auto manifest = readManifest(directory_);
    if (QFileInfo::exists(directory_)) {
        const bool valid = manifest.value("format").toInt() == OcrBook::formatVersion
            && manifest.value("identity").toString() == QString::fromLatin1(identity)
            && manifest.value("profile").toString() == QString::fromLatin1(profile)
            && manifest.value("pageCount").toInt() == pageCount;
        if (!valid) {
            qInfo("ocr_book_rebuild reason=manifest identity=%s", identity.left(12).constData());
            QDir(directory_).removeRecursively();
        } else created_ = true;
    }
    if (created_) {
        // Touch the manifest: its modification time is the book's last use for eviction.
        auto touched = manifest; touched["lastUsed"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        writeAtomic(directory_ + "/manifest.json", QJsonDocument(touched).toJson(QJsonDocument::Compact));
        const OcrBookReader reader(root_, identity, profile);
        for (int p : reader.pages()) if (p < pageCount) index.ocr.append(p);
        for (int p : reader.nativePages()) if (p < pageCount) index.native.append(p);
    }
    // Keep the newest profiles of this book only (by manifest time); invalid ones age out here.
    auto profiles = QDir(book).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(profiles.begin(), profiles.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return QFileInfo(a.absoluteFilePath() + "/manifest.json").lastModified() > QFileInfo(b.absoluteFilePath() + "/manifest.json").lastModified();
    });
    int others = 0; // the current profile always counts as one
    for (const auto& info : profiles) {
        if (info.fileName() == QString::fromLatin1(profile)) continue;
        if (!isHex(info.fileName(), 64) || ++others > limits_.profilesPerBook - 1) QDir(info.absoluteFilePath()).removeRecursively();
    }
    bookBytes_ = QFileInfo::exists(book) ? directoryBytes(book) : 0;
    for (const auto& info : QDir(root_).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (info.fileName() != QString::fromLatin1(identity)) otherBooks_.insert(info.absoluteFilePath(), directoryBytes(info.absoluteFilePath()));
    enforceTotal();
    return index;
}
void OcrBookStore::enforceTotal() {
    qint64 total = bookBytes_;
    for (auto bytes : otherBooks_) total += bytes;
    if (total <= limits_.totalBytes) return;
    // Least recently used first: newest manifest time among a book's profiles.
    QList<QPair<QDateTime,QString>> books;
    for (auto it = otherBooks_.cbegin(); it != otherBooks_.cend(); ++it) {
        QDateTime newest;
        for (const auto& profile : QDir(it.key()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            newest = std::max(newest, QFileInfo(profile.absoluteFilePath() + "/manifest.json").lastModified());
        books.append({newest, it.key()});
    }
    std::sort(books.begin(), books.end());
    for (const auto& [time, path] : books) {
        if (total <= limits_.totalBytes) break;
        total -= otherBooks_.take(path);
        QDir(path).removeRecursively();
        qInfo("ocr_book_evicted bytes_total=%lld", total);
    }
}
bool OcrBookStore::ensureProfile(QString* error) {
    if (created_) return true;
    if (!QDir().mkpath(directory_ + "/pages")) { if (error) *error = QObject::tr("无法创建 OCR 识别存储目录。"); return false; }
    const QJsonObject manifest{{"format", OcrBook::formatVersion}, {"identity", QString::fromLatin1(identity_)},
        {"profile", QString::fromLatin1(profile_)}, {"language", language_}, {"pageCount", pageCount_},
        {"tesseract", QString::fromLatin1(tesseract::TessBaseAPI::Version())},
        {"pipeline", QString::fromLatin1(pipeline)},
        {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    const auto data = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    if (!writeAtomic(directory_ + "/manifest.json", data)) { if (error) *error = QObject::tr("无法写入 OCR 识别存储清单。"); return false; }
    created_ = true;
    bookBytes_ += data.size();
    const auto buffered = std::exchange(bufferedNative_, {});
    for (int page : buffered) {
        OcrBookPage native; native.page = page; native.native = true;
        write(native, nullptr);
    }
    return true;
}
bool OcrBookStore::write(const OcrBookPage& page, QString* error) {
    if (identity_.isEmpty() || page.page < 0 || page.page >= pageCount_) { if (error) *error = QObject::tr("OCR 识别存储未打开。"); return false; }
    if (page.native && !created_) { bufferedNative_.insert(page.page); return true; }
    if (!ensureProfile(error)) return false;
    const auto data = OcrBook::encode(page);
    const QString path = directory_ + "/pages/" + pageName(page.page);
    const qint64 previous = QFileInfo(path).size();
    if (bookBytes_ - previous + data.size() > limits_.bookBytes) {
        if (error) *error = QObject::tr("本书识别结果超过容量上限（%1 MiB），后台识别已停止。").arg(limits_.bookBytes / (1024*1024));
        return false;
    }
    if (!writeAtomic(path, data)) { if (error) *error = QObject::tr("无法保存第 %1 页识别结果。").arg(page.page + 1); return false; }
    bookBytes_ += data.size() - previous;
    enforceTotal();
    return true;
}
void OcrBookStore::recordNative(int page) {
    OcrBookPage native; native.page = page; native.native = true;
    write(native, nullptr);
}
std::optional<OcrBookPage> OcrBookStore::read(int page) {
    if (identity_.isEmpty() || !created_) return std::nullopt;
    const OcrBookReader reader(root_, identity_, profile_);
    auto record = reader.page(page);
    if (!record && QFileInfo::exists(reader.pagePath(page))) {
        bookBytes_ -= QFileInfo(reader.pagePath(page)).size();
        QFile::remove(reader.pagePath(page));
        qInfo("ocr_book_rebuild reason=page page=%d", page);
    }
    return record;
}
bool OcrBookStore::clearBook(const QByteArray& identity, QString* error) {
    if (!isHex(QString::fromLatin1(identity), 64)) { if (error) *error = QObject::tr("OCR 识别存储参数无效。"); return false; }
    const QString book = root_ + '/' + QString::fromLatin1(identity);
    const bool ok = !QFileInfo::exists(book) || QDir(book).removeRecursively();
    if (identity == identity_) { created_ = false; bookBytes_ = 0; bufferedNative_.clear(); }
    if (!ok && error) *error = QObject::tr("无法删除本书识别结果，请检查文件权限。");
    return ok;
}
