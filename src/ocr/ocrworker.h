#pragma once
#include "textpage.h"
#include "ocrbookstore.h"
#include <QImage>
#include <QObject>
#include <QTransform>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

struct OcrRequest {
    quint64 session = 0, revision = 0;
    int page = 0;
    QByteArray identity;
    QString language;
    QImage image;
    QRectF region{0,0,1,1};
    // Whole-book store (OcrBookStore) token from openBook(); 0 keeps the request out of it.
    quint64 book = 0;
    bool background = false;  // whole-book queue step; reported back as such
    bool storeOnly = false;   // no image: load the stored page result (null text when missing)
    bool force = false;       // explicit re-recognition: ignore stored results, then replace them
    QTransform displayToPdf{};  // stored with the page for later search hit mapping
};

class OcrWorker final : public QObject {
    Q_OBJECT
public:
    explicit OcrWorker(QString cacheRoot, QString bookRoot = {});
    ~OcrWorker() override;
    void recognize(OcrRequest request);
    void submit(OcrRequest request);
    void invalidate(quint64 revision);
    // OCR thread (queued). Opens the document's store for `language`; reports the finished pages.
    void openBook(quint64 book, QByteArray identity, QString language, int pages);
    void closeBook();
    void recordNative(quint64 book, int page);
    void clearBook(quint64 book, QByteArray identity);
signals:
    void finished(quint64 session, quint64 revision, int page, TextSnapshot text, bool cached, double milliseconds, bool background);
    void bookOpened(quint64 book, QByteArray profile, QList<int> ocrPages, QList<int> nativePages, QString error);
    void bookCleared(quint64 book, QString error);
    void bookWriteFailed(quint64 book, int page, QString error);
private:
    bool bookMatches(const OcrRequest& request) const;
    QString cacheRoot_;
    std::unique_ptr<OcrBookStore> store_;
    quint64 book_ = 0;
    QString bookLanguage_;
    QByteArray bookIdentity_;
    void drain();
    std::mutex mutex_;
    std::optional<OcrRequest> pending_;
    bool scheduled_ = false;
    std::atomic<quint64> latest_{0};
};
Q_DECLARE_METATYPE(TextSnapshot)
