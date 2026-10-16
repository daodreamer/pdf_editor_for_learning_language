#pragma once

#include <QObject>
#include <QUrl>
#include <QThread>
#include "pdfworker.h"
#include "readingmodel.h"
#include "textselection.h"
#include "annotationmodel.h"
#include "storageworker.h"
#include <functional>
#include <QTimer>
#include <QCache>
#include <QtQml/qqmlregistration.h>

class ShellState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged)
    Q_PROPERTY(QUrl selectedUrl READ selectedUrl NOTIFY selectionChanged)
    Q_PROPERTY(quint64 sessionId READ sessionId NOTIFY selectionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY selectionChanged)
    Q_PROPERTY(int themeMode READ themeMode NOTIFY themeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY themeChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY selectionChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY selectionChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY selectionChanged)
    Q_PROPERTY(bool shutdownComplete READ shutdownComplete NOTIFY shutdownChanged)
    Q_PROPERTY(QImage pageImage READ pageImage NOTIFY imageChanged)
    Q_PROPERTY(QAbstractItemModel* readingModel READ readingModel CONSTANT)
    Q_PROPERTY(double displayPageWidth READ pageWidth NOTIFY layoutChanged)
    Q_PROPERTY(double contentHeight READ contentHeight NOTIFY layoutChanged)
    Q_PROPERTY(double contentWidth READ contentWidth NOTIFY layoutChanged)
    Q_PROPERTY(double scrollOffset READ scrollOffset NOTIFY layoutChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY layoutChanged)
    Q_PROPERTY(bool doublePage READ doublePage WRITE setDoublePage NOTIFY layoutChanged)
    Q_PROPERTY(double pageRatio READ pageRatio NOTIFY selectionChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY selectionChanged)
    Q_PROPERTY(TextSelection* textSelection READ textSelection CONSTANT)
    Q_PROPERTY(AnnotationModel* annotations READ annotations CONSTANT)
    Q_PROPERTY(QString storageStatus READ storageStatus NOTIFY storageChanged)
    Q_PROPERTY(QString exportStatus READ exportStatus NOTIFY storageChanged)
    Q_PROPERTY(bool storageReady READ storageReady NOTIFY storageChanged)
    Q_PROPERTY(bool transitioning READ transitioning NOTIFY storageChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY storageChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY storageChanged)
public:
    explicit ShellState(QObject* parent = nullptr);
    ~ShellState() override;
    QString selectedName() const { return selectedName_; }
    QUrl selectedUrl() const { return path_.isEmpty()?QUrl{}:QUrl::fromLocalFile(path_); }
    quint64 sessionId() const { return session_; }
    Q_INVOKABLE QImage cachedPage(int page) const {
        const auto image=model_.image(page);if(image.isNull())return {};
        const auto key=image.cacheKey();if(const auto* old=thumbnails_.object(key))return *old;
        auto thumb=image.scaled(216,300,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        thumbnails_.insert(key,new QImage(thumb),int(thumb.sizeInBytes()/1024)+1);return thumb;
    }
    Q_INVOKABLE bool hasCachedPage(int page) const { return !model_.image(page).isNull(); }
    QString statusText() const { return statusText_; }
    int themeMode() const { return themeMode_; }
    bool dark() const;
    int pageCount() const { return pageCount_; }
    int currentPage() const { return page_ + 1; }
    bool busy() const { return busy_; }
    bool shutdownComplete() const { return shutdownComplete_; }
    QImage pageImage() const { return model_.image(page_); }
    QAbstractItemModel* readingModel() { return &model_; }
    ReadingModel& model() { return model_; }
    AnnotationModel* annotations() { return &annotations_; }
    Q_INVOKABLE bool containsPage(double x,double y) const;
    bool interactiveContentAt(double x,double y) const;
    bool eraseTextAt(double x,double y);
    Q_INVOKABLE QString createTextBox(double x,double y);
    Q_INVOKABLE void finishTextEditing();
    Q_INVOKABLE void commitInput();
    Q_INVOKABLE void resetInput();
    Q_INVOKABLE double pageTop(int page) const { return model_.top(page); }
    Q_INVOKABLE double pageLeft(int page) const { return model_.left(page); }
    Q_INVOKABLE void focusPage(int page);
    Q_INVOKABLE double pageHeight(int page) const { return model_.height(page); }
    Q_INVOKABLE double pageWidth() const { return model_.pageWidth(); }
    TextSelection* textSelection() { return &textSelection_; }
    Q_INVOKABLE bool beginTextSelection(double x, double y);
    Q_INVOKABLE void extendTextSelection(double x, double y);
    double contentHeight() const { return model_.contentHeight(); }
    double contentWidth() const { return model_.contentWidth(); }
    double scrollOffset() const { return model_.offset(); }
    double zoom() const { return zoom_; }
    bool doublePage() const { return model_.doublePage(); }
    void setDoublePage(bool enabled);
    Q_INVOKABLE void updateViewport(double y, double height, double width, double dpr);
    Q_INVOKABLE void setZoom(double zoom);
    double pageRatio() const { return model_.ratio(page_); }
    QString errorCode() const { return errorCode_; }
    Q_INVOKABLE void selectFile(const QUrl& url);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void cycleTheme();
    Q_INVOKABLE void goToPage(int page);
    Q_INVOKABLE void setRenderWidth(int pixels);
    Q_INVOKABLE void beginShutdown();
    QString storageStatus() const { return storageStatus_; }
    QString exportStatus() const { return exportStatus_; }
    bool storageReady() const { return storageReady_; }
    bool transitioning() const { return transitioning_; }
    bool exporting() const { return exporting_; }
    bool scanning() const { return scanning_; }
    Q_INVOKABLE void optimizeScan(const QUrl& target,const QVariantMap& options);
    Q_INVOKABLE void cancelScan() { worker_->cancelScan(); }
    Q_INVOKABLE void saveNow();
    Q_INVOKABLE void exportPdf(const QUrl& target);
    Q_INVOKABLE void acknowledgeProbe() { emit startupAcknowledged(); }
signals:
    void textCommitRequested();
    void storageChanged();
    void startupAcknowledged();
    void selectionChanged();
    void themeChanged();
    void imageChanged();
    void shutdownChanged();
    void layoutChanged();
    void documentClosed(quint64 session, int handles);
    void renderStarted(quint64 revision);
    void renderDiscarded(quint64 revision);
private:
    void request();
    void acceptFrame(const PdfFrame& frame);
    void schedule();
    void invalidate();
    void syncAnnotations();
    void flushBefore(std::function<void()> action);
    void initializeStorage();
    AnnotationSnapshot annotationSnapshot() const;
    QThread storageThread_;
    StorageWorker* storage_ = nullptr;
    QTimer saveTimer_, maxSaveTimer_;
    quint64 editRevision_ = 0, savedRevision_ = 0;
    QByteArray identity_;
    QString storageStatus_, exportStatus_;
    bool storageReady_ = false, loadingStorage_ = false, restoring_ = false;
    bool saving_ = false, transitioning_ = false, exporting_ = false;
    bool applyingTransition_ = false;
    bool scanning_ = false;
    std::function<void()> pendingTransition_;
    ReadingModel model_;
    mutable QCache<qint64,QImage> thumbnails_{8192}; // KiB, separate from full page cache
    TextSelection textSelection_;
    AnnotationModel annotations_;
    QTimer scheduleTimer_;
    QSet<int> failed_;
    bool inFlight_ = false;
    int requestedWidth_ = 1200;
    int metadataCursor_ = 0;
    double zoom_ = 1, dpr_ = 1, viewportWidth_ = 1000, viewportHeight_ = 700;
    double singlePageZoom_=1;
    bool viewportAttached_ = false;
    QString selectedName_;
    QString statusText_;
    int themeMode_ = 0; // system, light, dark
    QThread thread_;
    PdfWorker* worker_ = nullptr; // deleted by QThread::finished
    QString path_;
    QString errorCode_;
    quint64 session_ = 0;
    quint64 revision_ = 0;
    int pageCount_ = 0;
    int page_ = 0;
    int renderWidth_ = 1200;
    bool busy_ = false;
    bool shuttingDown_ = false;
    bool shutdownComplete_ = false;
};
