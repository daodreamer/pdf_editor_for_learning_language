#pragma once
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>

namespace phase0 {
inline qint64 monotonicNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct Config {
    static constexpr qint64 maxImageBytes = 64 * 1024 * 1024;
    static constexpr int maxDimension = 8192;
};
struct Request {
    quint64 generation = 0;
    QString path;
    int page = 0;
    int width = 1000;
    QString output;
    QString font;
    QString exportPath;
};
class PdfWorker final : public QObject {
    Q_OBJECT
public:
    // Thread-safe latest-only mailbox; exactly one worker may exist per process.
    void replace(Request request);
    void cancel();
    bool current(quint64 id) const { return latest_.load() == id; }
public slots:
    void drain();
signals:
    void started(quint64 id);
    void trace(QJsonObject metadata);
    void result(quint64 id, QImage image, QJsonObject metadata);
    void discarded(quint64 id);
    void idle();
private:
    std::mutex mutex_;
    std::optional<Request> pending_;
    std::atomic<quint64> latest_{0};
};
}
