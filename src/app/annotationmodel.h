#pragma once
#include "pagegeometry.h"
#include "inkstroke.h"
#include <QAbstractListModel>
#include <QColor>
#include <QFont>
#include <QMap>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class AnnotationModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ShellState")
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString inkSelection READ inkSelection NOTIFY inkSelectionChanged)
    Q_PROPERTY(QString gestureError READ gestureError NOTIFY gestureErrorChanged)
    Q_PROPERTY(quint64 version READ version NOTIFY contentChanged)
    Q_PROPERTY(double defaultFontSize READ defaultFontSize WRITE setDefaultFontSize NOTIFY defaultFontSizeChanged)
    // Style of new text answers (set in the typing tool's settings, kept across launches).
    Q_PROPERTY(QColor defaultTextColor READ defaultTextColor WRITE setDefaultTextColor NOTIFY defaultTextStyleChanged)
    Q_PROPERTY(bool defaultBold READ defaultBold WRITE setDefaultBold NOTIFY defaultTextStyleChanged)
    Q_PROPERTY(bool defaultItalic READ defaultItalic WRITE setDefaultItalic NOTIFY defaultTextStyleChanged)
    // Redo mode (docs/STORAGE_FORMAT.md): display-only hiding and archived practice rounds.
    Q_PROPERTY(quint64 practiceVersion READ practiceVersion NOTIFY practiceChanged)
    Q_PROPERTY(bool bookHidden READ bookHidden NOTIFY practiceChanged)
public:
    enum Role { Id=Qt::UserRole+1, Page, Anchor, BoxWidth, BoxHeight, FontSize, Text, Family, Color, PageUnits, Order, WidthLocked, Styles, LineSpacing };
    explicit AnnotationModel(QObject* parent=nullptr) : QAbstractListModel(parent) { connect(this,&AnnotationModel::contentChanged,this,[this]{++version_;}); }
    quint64 version() const { return version_; }
    double defaultFontSize() const { return defaultFontSize_; }
    void setDefaultFontSize(double size);
    QColor defaultTextColor() const { return defaultTextColor_; }
    void setDefaultTextColor(QColor color);
    bool defaultBold() const { return defaultBold_; }
    void setDefaultBold(bool bold) { if(bold!=defaultBold_) { defaultBold_=bold; emit defaultTextStyleChanged(); } }
    bool defaultItalic() const { return defaultItalic_; }
    void setDefaultItalic(bool italic) { if(italic!=defaultItalic_) { defaultItalic_=italic; emit defaultTextStyleChanged(); } }
    // Height of one line of a new plain answer at the default size, in PDF units.
    double defaultTextHeight() const;
    // The font an answer of this family/size is laid out and drawn with (see AnswerFonts).
    Q_INVOKABLE QFont answerFont(const QString& family,double size) const;
    Q_INVOKABLE QVariantMap pageStats(int page) const;
    int rowCount(const QModelIndex& parent={}) const override { return parent.isValid()?0:int(visible_.size()); }
    QVariant data(const QModelIndex&,int role) const override;
    QHash<int,QByteArray> roleNames() const override;
    int selectedPage() const { return selected_.isEmpty() ? -1 : notes_.value(selected_).page; }
    QString selectedId() const { return selected_; }
    bool canUndo() const { return !undo_.isEmpty(); }
    bool canRedo() const { return !redo_.isEmpty(); }
    void reset();
    void restore(const AnnotationSnapshot& snapshot);
    void setGeometry(int page,const PageGeometry& geometry);
    PageGeometry geometry(int page) const { return geometry_.value(page); }
    void setActivePages(const QList<int>& pages);
    QList<TextAnnotation> snapshot() const { return notes_.values(); }
    QList<InkStroke> inkSnapshot() const;
    QList<InkStroke> displayStrokes(int page) const;
    QString inkSelection() const { return selectedInk_; }
    QString gestureError() const { return gestureError_; }
    qsizetype historyBytes() const;
    // pressure >= 0 gives the stroke per-point width factors (pen pressure or the
    // speed-derived factor of mouse ink) with tapered ends; < 0 keeps it uniform.
    bool beginInk(int page, QPointF normalized, QColor color, double width, double pressure = -1);
    bool beginErase(int page, QPointF normalized, double radius);
    bool beginMoveInk(int page, QPointF normalized, double radius);
    void appendInk(QPointF normalized, double pressure = -1);
    void endInk();
    Q_INVOKABLE void cancelGesture();
    Q_INVOKABLE void removeInk();
    // A printed tick box (normalized page rect): removes the mark already drawn in it,
    // otherwise draws a tick or a cross as ink. One history step; true when drawn.
    bool toggleBoxMark(int page, QRectF box, bool cross, QColor color);
    // Ink lying entirely inside the (slightly enlarged) box: an existing mark.
    QList<InkStroke> boxMark(int page, QRectF box) const;
    Q_INVOKABLE bool gestureActive() const { return gesture_ != Gesture::None; }
    Q_INVOKABLE QString create(int page,double x,double y,double maximumWidth=0,double fieldHeight=0,double lineSpacing=0,int lines=1);
    Q_INVOKABLE void select(const QString& id);
    Q_INVOKABLE void finishEditing();
    Q_INVOKABLE void setText(const QString& id,const QString& text,double height,const QVariantList& styles = {});
    Q_INVOKABLE void setEditorText(const QString& id,const QString& text,double height,const QVariantList& styles);
    Q_INVOKABLE void move(const QString& id,double x,double y);
    Q_INVOKABLE void resize(const QString& id,double x,double y,double width,double height);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void cycleMark(const QString& id);
    Q_INVOKABLE bool selectNext(const QString& id);
    Q_INVOKABLE double textWidthLimit(const QString& id) const;

    // ---- Hiding (display only: no data change, no history, no save) ----
    // A page veil hides what existed when it was set (creation order below the mark);
    // answers written afterwards show. Hidden items are not hit by any tool.
    quint64 practiceVersion() const { return practiceVersion_; }
    bool bookHidden() const { return bookHidden_; }
    Q_INVOKABLE bool pageHidden(int page) const { return veils_.contains(page); }
    Q_INVOKABLE void setPageHidden(int page, bool hidden);
    Q_INVOKABLE void setBookHidden(bool hidden);
    Q_INVOKABLE int hiddenCount(int page) const;  // hidden answers + strokes on the page
    Q_INVOKABLE int hiddenTotal() const;
    bool noteHidden(const TextAnnotation& note) const;
    bool inkHidden(const InkStroke& stroke) const;

    // ---- Practice rounds (each one undo step) ----
    QList<PracticeRound> rounds() const { return rounds_; }
    Q_INVOKABLE int currentRound(int page) const;  // 1 + archived rounds of the page
    Q_INVOKABLE QVariantList pageRounds(int page) const;
    Q_INVOKABLE bool startRound(int page);         // false when the page has nothing to archive
    Q_INVOKABLE int startBookRound();              // pages archived
    Q_INVOKABLE int answeredPages() const;         // pages with current answers
    // Archives the current answers (if any) and copies the round back as current.
    // Returns an error message, empty on success.
    Q_INVOKABLE QString restoreRound(const QString& id);
    Q_INVOKABLE bool deleteRound(const QString& id);
    // Faint read-only overlay of one archived round for comparison (display only).
    Q_INVOKABLE QString overlayRound(int page) const { return overlay_.value(page); }
    Q_INVOKABLE void setOverlayRound(int page, const QString& id);
    Q_INVOKABLE QVariantList overlayNotes(int page) const;
    QList<InkStroke> overlayStrokes(int page) const;
signals:
    void contentChanged();
    void selectionChanged();
    void historyChanged();
    void inkChanged(int page);
    void inkSelectionChanged();
    void gestureErrorChanged();
    void defaultFontSizeChanged();
    void defaultTextStyleChanged();
    void practiceChanged();
    // An undo/redo brought hidden answers back on this page, so the page was shown.
    void pageRevealed(int page);
    void roundStarted(int page, int round, bool automatic);
private:
    struct Command {
        TextAnnotation before, after;
        QList<InkStroke> inkBefore, inkAfter;
        TextAnnotation linkedBefore, linkedAfter;
        // Rounds archived (their items leave the current round) and rounds deleted.
        // PracticeRound lists are implicitly shared with rounds_, so they cost no copy.
        QList<PracticeRound> archived, deleted;
        QList<TextAnnotation> notesAdded; // restored copies, added after archiving
        qsizetype bytes() const;
    };
    void record(TextAnnotation before,TextAnnotation after);
    void record(Command command);
    void trimHistory();
    void applyInk(const QList<InkStroke>& from, const QList<InkStroke>& to);
    void eraseSegment(QPointF from, QPointF to);
    void clearInkSelection();
    void apply(const TextAnnotation& from,const TextAnnotation& to);
    void refreshRows();
    void notify(const QString& id,const QList<int>& roles);
    void runCommand(const Command& c, bool forward);
    void revealTouched(const Command& c);
    PracticeRound archivePage(int page, const QString& batch, bool hiddenOnly) const;
    void archiveHiddenBeforeEdit(int page);
    void insertRound(const PracticeRound& round);
    void practiceTouched() { ++practiceVersion_; emit practiceChanged(); }
    QDateTime nextArchiveTime();
    struct Veil { quint64 text = 0, ink = 0; };
    QHash<int, Veil> veils_;
    bool bookHidden_ = false;
    QList<PracticeRound> rounds_;
    QHash<int, QString> overlay_;
    QDateTime lastArchived_;
    quint64 practiceVersion_ = 0;
    QMap<QString,TextAnnotation> notes_;
    QMap<int,PageGeometry> geometry_;
    QList<int> active_;
    QStringList visible_;
    QString selected_;
    TextAnnotation beforeEdit_;
    QList<Command> undo_,redo_;
    enum class Gesture { None, Draw, Erase, Move };
    Gesture gesture_ = Gesture::None;
    QMap<QString,InkStroke> strokes_;
    InkStroke preview_, moveBefore_;
    QList<InkStroke> erased_;
    int gesturePage_ = -1;
    QPointF lastPoint_, moveOrigin_;
    double eraserRadius_ = 0;
    double inkLength_ = 0; // PDF-unit arc length of the stroke being drawn
    QString selectedInk_, gestureError_;
    quint64 nextInkOrder_ = 0;
    quint64 nextTextOrder_ = 0;
    quint64 version_ = 0;
    double defaultFontSize_ = AnnotationConfig::defaultTextSize;
    QColor defaultTextColor_ = TextAnnotation{}.color;
    bool defaultBold_ = false, defaultItalic_ = false;
};
