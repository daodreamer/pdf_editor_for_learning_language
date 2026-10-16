#pragma once
#include "pagegeometry.h"
#include "inkstroke.h"
#include <QAbstractListModel>
#include <QColor>
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
public:
    enum Role { Id=Qt::UserRole+1, Page, Anchor, BoxWidth, BoxHeight, FontSize, Text, Family, Color, PageUnits, Order };
    explicit AnnotationModel(QObject* parent=nullptr) : QAbstractListModel(parent) { connect(this,&AnnotationModel::contentChanged,this,[this]{++version_;}); }
    quint64 version() const { return version_; }
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
    bool beginInk(int page, QPointF normalized, QColor color, double width);
    bool beginErase(int page, QPointF normalized, double radius);
    bool beginMoveInk(int page, QPointF normalized, double radius);
    void appendInk(QPointF normalized);
    void endInk();
    Q_INVOKABLE void cancelGesture();
    Q_INVOKABLE void removeInk();
    bool gestureActive() const { return gesture_ != Gesture::None; }
    Q_INVOKABLE QString create(int page,double x,double y);
    Q_INVOKABLE void select(const QString& id);
    Q_INVOKABLE void finishEditing();
    Q_INVOKABLE void setText(const QString& id,const QString& text,double height);
    Q_INVOKABLE void move(const QString& id,double x,double y);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void cycleMark(const QString& id);
    Q_INVOKABLE bool selectNext(const QString& id);
signals:
    void contentChanged();
    void selectionChanged();
    void historyChanged();
    void inkChanged(int page);
    void inkSelectionChanged();
    void gestureErrorChanged();
private:
    struct Command {
        TextAnnotation before, after;
        QList<InkStroke> inkBefore, inkAfter;
        TextAnnotation linkedBefore, linkedAfter;
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
    QString selectedInk_, gestureError_;
    quint64 nextInkOrder_ = 0;
    quint64 nextTextOrder_ = 0;
    quint64 version_ = 0;
};
