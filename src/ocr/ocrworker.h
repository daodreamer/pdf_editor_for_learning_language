#pragma once
#include "textpage.h"
#include <QImage>
#include <QObject>
#include <atomic>
#include <mutex>
#include <optional>

struct OcrRequest {
    quint64 session = 0, revision = 0;
    int page = 0;
    QByteArray identity;
    QString language;
    QImage image;
    QRectF region{0,0,1,1};
};

class OcrWorker final : public QObject {
    Q_OBJECT
public:
    explicit OcrWorker(QString cacheRoot);
    void recognize(OcrRequest request);
    void submit(OcrRequest request);
    void invalidate(quint64 revision);
signals:
    void finished(quint64 session, quint64 revision, int page, TextSnapshot text, bool cached, double milliseconds);
private:
    QString cacheRoot_;
    void drain();
    std::mutex mutex_;
    std::optional<OcrRequest> pending_;
    bool scheduled_ = false;
    std::atomic<quint64> latest_{0};
};
Q_DECLARE_METATYPE(TextSnapshot)
