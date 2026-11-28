#pragma once

#include <QObject>
#include <QUrl>
#include <QThread>
#include "pdfworker.h"
#include "readingmodel.h"
#include "textselection.h"
#include "annotationmodel.h"
#include "storageworker.h"
#include "ocrworker.h"
#include "ocrbookqueue.h"
#include <QElapsedTimer>
#include "languagedetect.h"
#include <functional>
#include <QTimer>
#include <QCache>
#include <QPointer>
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
    Q_PROPERTY(bool ocrSelectionEnabled READ ocrSelectionEnabled WRITE setOcrSelectionEnabled NOTIFY ocrChanged)
    Q_PROPERTY(QString ocrLanguage READ ocrLanguage WRITE setOcrLanguage NOTIFY ocrChanged)
    Q_PROPERTY(QString ocrStatus READ ocrStatus NOTIFY ocrChanged)
    Q_PROPERTY(bool ocrBusy READ ocrBusy NOTIFY ocrChanged)
    // Whole-book background OCR of scanned pages, persisted per document (docs/OCR_SELECTION.md).
    Q_PROPERTY(bool ocrBookEnabled READ ocrBookEnabled WRITE setOcrBookEnabled NOTIFY ocrBookChanged)
    Q_PROPERTY(bool ocrBookPaused READ ocrBookPaused WRITE setOcrBookPaused NOTIFY ocrBookChanged)
    Q_PROPERTY(int ocrBookDone READ ocrBookDone NOTIFY ocrBookChanged)
    Q_PROPERTY(int ocrBookNative READ ocrBookNative NOTIFY ocrBookChanged)
    Q_PROPERTY(int ocrBookPages READ ocrBookPages NOTIFY ocrBookChanged)
    Q_PROPERTY(bool ocrBookReady READ ocrBookReady NOTIFY ocrBookChanged)
    Q_PROPERTY(bool ocrBookRunning READ ocrBookRunning NOTIFY ocrBookChanged)
    Q_PROPERTY(QString ocrBookStatus READ ocrBookStatus NOTIFY ocrBookChanged)
    // Mark drawn into printed tick boxes: 0 follows the page language, 1 tick, 2 cross.
    Q_PROPERTY(int checkMarkStyle READ checkMarkStyle WRITE setCheckMarkStyle NOTIFY checkMarkStyleChanged)
    // Main language of the open document (ISO 639-1, e.g. "fr"), from the text pages seen so far;
    // the file name or OCR language until they say enough. Empty without a document.
    Q_PROPERTY(QString documentLanguage READ documentLanguage NOTIFY documentLanguageChanged)
public:
    explicit ShellState(QObject* parent = nullptr);
    ~ShellState() override;
    bool ocrSelectionEnabled() const { return ocrSelectionEnabled_; }
    void setOcrSelectionEnabled(bool enabled);
    QString ocrLanguage() const { return ocrLanguage_; }
    void setOcrLanguage(QString language);
    QString ocrStatus() const { return ocrStatus_; }
    bool ocrBusy() const { return ocrBusyPage_>=0 || !ocrDemand_.isEmpty(); }
    Q_INVOKABLE void recognizeCurrentPage();
    bool ocrBookEnabled() const { return bookQueue_.enabled(); }
    void setOcrBookEnabled(bool enabled);
    bool ocrBookPaused() const { return bookQueue_.paused(); }
    void setOcrBookPaused(bool paused);
    int ocrBookDone() const { return bookQueue_.done(); }
    int ocrBookNative() const { return bookQueue_.native(); }
    int ocrBookPages() const { return bookQueue_.pages(); }
    bool ocrBookReady() const { return bookQueue_.ready(); }
    bool ocrBookRunning() const { return backgroundPage_>=0; }
    // The background page is in Tesseract (not rendering on PDFium); for tests and diagnostics.
    bool ocrBookRecognizing() const { return backgroundStage_==BackgroundStage::Ocr; }
    QString ocrBookStatus() const { return bookStatus_; }
    // Deletes this document's stored OCR results (every language) and starts over.
    Q_INVOKABLE void clearBookOcr();
    const OcrBookQueue& ocrBookQueue() const { return bookQueue_; }
    // Read-only view of this document's stored OCR for the current language (search later).
    OcrBookReader ocrBookReader() const;
    Q_INVOKABLE void recognizeRegion(double x1,double y1,double x2,double y2);
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
    Q_INVOKABLE bool interactiveContentAt(double x,double y) const;
    bool eraseTextAt(double x,double y);
    // Topmost text answer under a content point, or within tolerance (logical px).
    QString textBoxAt(double x,double y,double tolerance=0) const;
    Q_INVOKABLE QString createTextBox(double x,double y);
    Q_INVOKABLE QRectF answerBlankAt(double x,double y) const;
    Q_INVOKABLE QString createAnswerAt(double x,double y);
    // Printed tick box under a content point (padded for easier hits), in content coordinates.
    Q_INVOKABLE QRectF checkBoxAt(double x,double y) const;
    // Ticks/crosses the box under the point, or clears its mark. False when there is no box.
    Q_INVOKABLE bool toggleCheckBox(double x,double y);
    // Whether a box on this page gets a cross (French, German, Spanish, Italian habits)
    // rather than a tick (English, Chinese): the page's text layer or OCR text decides,
    // otherwise the document's OCR language.
    Q_INVOKABLE bool crossMarks(int page) const;
    QString documentLanguage() const { return documentLanguage_; }
    int checkMarkStyle() const { return checkMarkStyle_; }
    void setCheckMarkStyle(int style) { style=std::clamp(style,0,2); if(style!=checkMarkStyle_) { checkMarkStyle_=style; emit checkMarkStyleChanged(); } }
    // Consecutive ruled lines answered as one text: normalized page area (all rules),
    // rule pitch (normalized height) and rule count. lines==0 when nothing is there.
    struct AnswerBlock { QRectF area; QRectF first; double pitch = 0; int lines = 0; };
    AnswerBlock answerBlockAt(int page,QPointF normalized) const;
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
    // Zoom keeping the document point under (x,y) (content coordinates) at viewport
    // position (viewX,viewY); returns the new content origin {contentX, contentY}.
    Q_INVOKABLE QPointF zoomAt(double zoom,double x,double y,double viewX,double viewY);
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
    void ocrChanged();
    void ocrBookChanged();
    void answerLinesChanged();
    void checkMarkStyleChanged();
    void documentLanguageChanged();
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
    void ensureOcr(int page, bool force=false, QRectF region=QRectF(0,0,1,1));
    void resetOcr();
    void updateOcrDemand();
    QThread ocrThread_;
    QPointer<OcrWorker> ocr_;
    quint64 ocrRevision_ = 0;
    QSet<int> ocrDemand_;
    QHash<int,quint64> ocrRegionSelections_;
    int ocrBusyPage_ = -1;
    bool ocrSelectionEnabled_ = false;
    QString ocrLanguage_ = QStringLiteral("fra"), ocrStatus_;
    QSet<int> ocrForced_;
    // Whole-book queue: at most one background page, either rendering on PDFium or in OCR.
    void ensureOcrWorker();
    void openBook();
    void cancelBackground();
    void noteInteraction();
    int backgroundCandidate();
    void submitOcr(OcrRequest request);
    void updateBookStatus();
    OcrBookQueue bookQueue_;
    quint64 bookToken_ = 0;
    QByteArray bookProfile_;
    bool bookOpening_ = false;
    void closeBookSession();
    enum class BackgroundStage { None, Pdf, Ocr } backgroundStage_ = BackgroundStage::None;
    int backgroundPage_ = -1;
    quint64 backgroundPdfRevision_ = 0;
    QTimer backgroundTimer_;
    QElapsedTimer clock_;
    QString bookStatus_, bookError_;
    bool bookAllowed_ = true;
    int bookFailures_ = 0; // consecutive background failures
    int bookIdleMs_ = OcrBookConfig::idleMs;
    int checkMarkStyle_ = 0;
    void noteLanguage(int page,const TextSnapshot& text);
    void updateDocumentLanguage();
    void resetDocumentLanguage();
    struct PageLanguage { LanguageEvidence evidence; bool ocr = false; };
    QHash<int,PageLanguage> pageLanguages_;
    QString documentLanguage_;
    struct PageBox { int page = -1; QRectF box; };
    PageBox checkBoxNear(double x,double y) const;
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
