#pragma once
#include <QImage>
#include <QObject>
#include <QSizeF>
#include <QList>
#include "readingconfig.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

struct PdfRequest {
    quint64 session = 0;
    quint64 revision = 0;
    QString path;
    int page = 0;
    int width = 1200;
    qint64 maxBytes = 64 * 1024 * 1024;
    int metadataStart = -1;
    bool skipPreview = false;
    bool previewOnly = false;
};
struct PdfFrame {
    quint64 session = 0;
    quint64 revision = 0;
    int page = 0;
    int pages = 0;
    QSizeF points;
    QImage image;
    QString errorCode;
    QString error;
    bool preview = false;
    bool limited = false;
    int metadataStart = -1;
    QList<QSizeF> sizes;
};
Q_DECLARE_METATYPE(PdfFrame)

class PdfWorker final : public QObject {
    Q_OBJECT
public:
    explicit PdfWorker(QObject* parent = nullptr);
    ~PdfWorker() override;
    void submit(PdfRequest request);
    void shutdown();
    void cancel(quint64 revision);
signals:
    void frameReady(PdfFrame frame);
    void nativeClosed(quint64 session, int handles);
    void requestStarted(quint64 revision);
    void requestFinished(quint64 revision);
    void discarded(quint64 revision);
    void stopped();
private:
    void drain();
    bool current(quint64 revision) const;
    void execute(const PdfRequest& request);
    struct Engine;
    std::unique_ptr<Engine> engine_;
    std::mutex mutex_;
    std::optional<PdfRequest> pending_;
    std::atomic<quint64> latest_{0};
    std::atomic<bool> stopping_{false};
    bool scheduled_ = false;
};
