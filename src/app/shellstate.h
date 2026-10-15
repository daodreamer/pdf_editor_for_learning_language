#pragma once

#include <QObject>
#include <QUrl>
#include <QThread>
#include "pdfworker.h"
#include <QtQml/qqmlregistration.h>

class ShellState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY selectionChanged)
    Q_PROPERTY(int themeMode READ themeMode NOTIFY themeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY themeChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY selectionChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY selectionChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY selectionChanged)
    Q_PROPERTY(bool shutdownComplete READ shutdownComplete NOTIFY shutdownChanged)
    Q_PROPERTY(QImage pageImage READ pageImage NOTIFY imageChanged)
    Q_PROPERTY(double pageRatio READ pageRatio NOTIFY selectionChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY selectionChanged)
public:
    explicit ShellState(QObject* parent = nullptr);
    ~ShellState() override;
    QString selectedName() const { return selectedName_; }
    QString statusText() const { return statusText_; }
    int themeMode() const { return themeMode_; }
    bool dark() const;
    int pageCount() const { return pageCount_; }
    int currentPage() const { return page_ + 1; }
    bool busy() const { return busy_; }
    bool shutdownComplete() const { return shutdownComplete_; }
    QImage pageImage() const { return image_; }
    double pageRatio() const { return ratio_; }
    QString errorCode() const { return errorCode_; }
    Q_INVOKABLE void selectFile(const QUrl& url);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void cycleTheme();
    Q_INVOKABLE void goToPage(int page);
    Q_INVOKABLE void setRenderWidth(int pixels);
    Q_INVOKABLE void beginShutdown();
    Q_INVOKABLE void acknowledgeProbe() { emit startupAcknowledged(); }
signals:
    void startupAcknowledged();
    void selectionChanged();
    void themeChanged();
    void imageChanged();
    void shutdownChanged();
    void documentClosed(quint64 session, int handles);
    void renderStarted(quint64 revision);
    void renderDiscarded(quint64 revision);
private:
    void request();
    void acceptFrame(const PdfFrame& frame);
    QString selectedName_;
    QString statusText_;
    int themeMode_ = 0; // system, light, dark
    QThread thread_;
    PdfWorker* worker_ = nullptr; // deleted by QThread::finished
    QString path_;
    QString errorCode_;
    QImage image_;
    quint64 session_ = 0;
    quint64 revision_ = 0;
    int pageCount_ = 0;
    int page_ = 0;
    int renderWidth_ = 1200;
    double ratio_ = 1;
    bool busy_ = false;
    bool shuttingDown_ = false;
    bool shutdownComplete_ = false;
};
