#pragma once
#include "textpage.h"
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QTransform>
#include <functional>
#include <optional>

// Per-document store of whole-page OCR results (docs/OCR_SELECTION.md, "全书后台识别").
//
//   <root>/<pdf sha256 hex>/<profile hex>/manifest.json
//   <root>/<pdf sha256 hex>/<profile hex>/pages/<page, 5 digits>.lpo
//
// The profile hash covers the record format, Tesseract version, language string, model file
// hashes and the recognition pipeline, so a language or model change never reads old pages.
// Every file is written atomically (QSaveFile); a page file that fails its SHA-256 is deleted
// and reported as missing. Learning aid only: never written into a PDF, answer snapshot or export.

struct OcrBookPage {
    int page = -1;
    bool native = false;      // page has a PDF text layer; background OCR skipped it
    TextSnapshot text;        // OCR characters in normalized display coordinates (null for native)
    QTransform displayToPdf;  // normalized display -> PDF page points, as PageGeometry
};

namespace OcrBook {
constexpr int formatVersion = 1;
// Default store root: LANGLERN_WORKSPACE_DIR/ocr-books-v1 for isolated runs, otherwise the
// application's local data directory (not the purgeable cache directory).
QString defaultRoot();
// Profile of the current models for `language` (e.g. "fra+eng"); empty with *error set when a
// model is missing. Model hashes are memoised by path, size and modification time.
QByteArray profile(const QString& language, const QString& modelDirectory, QString* error = nullptr);
QString modelDirectory();
QByteArray encode(const OcrBookPage& page);
std::optional<OcrBookPage> decode(const QByteArray& bytes, int expectedPage);
}

// Read-only view of one document profile. Any thread may own an instance; writers replace files
// atomically, so a reader sees either the previous or the new file of a page. Intended for
// whole-book text search (not implemented yet).
class OcrBookReader {
public:
    OcrBookReader(QString root, QByteArray identity, QByteArray profile);
    bool exists() const;
    QList<int> pages() const;        // OCR pages with a record, ascending (native pages excluded)
    QList<int> nativePages() const;  // pages recorded as having a text layer
    std::optional<OcrBookPage> page(int page) const; // validated; nullopt when missing or damaged
    QString text(int page) const;    // plain text of an OCR page, '\n' between lines
    // Visits every readable OCR page in page order; stop by returning false. False if stopped.
    bool forEachPage(const std::function<bool(const OcrBookPage&)>& visit) const;
    QString directory() const { return directory_; }
    QString pagePath(int page) const;
private:
    QList<int> list(bool native) const;
    QString directory_;
};

// Writer side; used only on the OCR thread.
class OcrBookStore {
public:
    struct Limits { qint64 bookBytes; qint64 totalBytes; int profilesPerBook; };
    struct Index { QList<int> ocr, native; QString error; };
    OcrBookStore(QString root, Limits limits);
    // Lists the finished pages of (identity, profile) and applies the store budgets, never
    // evicting `identity` itself. Damaged manifests remove that profile.
    Index open(const QByteArray& identity, const QByteArray& profile, const QString& language, int pageCount);
    void close();
    bool isOpen(const QByteArray& identity, const QByteArray& profile) const;
    bool write(const OcrBookPage& page, QString* error);
    // A native page is buffered until the profile directory exists (first OCR page): a document
    // that is all text layer leaves nothing on disk.
    void recordNative(int page);
    std::optional<OcrBookPage> read(int page); // removes a damaged file
    bool clearBook(const QByteArray& identity, QString* error);
    qint64 bookBytes() const { return bookBytes_; }
    QString root() const { return root_; }
private:
    bool ensureProfile(QString* error);
    void enforceTotal();
    QString root_;
    Limits limits_;
    QByteArray identity_, profile_;
    QString language_, directory_;
    int pageCount_ = 0;
    bool created_ = false;
    qint64 bookBytes_ = 0;
    QHash<QString,qint64> otherBooks_; // book directory -> bytes, from open()
    QSet<int> bufferedNative_;
};
