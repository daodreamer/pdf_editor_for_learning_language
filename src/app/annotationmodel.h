#pragma once
#include "pagegeometry.h"
#include <QAbstractListModel>
#include <QColor>
#include <QMap>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

struct TextAnnotation {
    QString id;
    int page = 0;
    QPointF anchor, right, down;
    double width = 200, height = 24, fontSize = 14;
    QString text;
    QString family = QStringLiteral("Segoe UI");
    QColor color = QColor(30,70,140);
    bool operator==(const TextAnnotation&) const = default;
};

class AnnotationModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ShellState")
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
public:
    enum Role { Id=Qt::UserRole+1, Page, Anchor, BoxWidth, BoxHeight, FontSize, Text, Family, Color, PageUnits };
    explicit AnnotationModel(QObject* parent=nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent={}) const override { return parent.isValid()?0:int(visible_.size()); }
    QVariant data(const QModelIndex&,int role) const override;
    QHash<int,QByteArray> roleNames() const override;
    int selectedPage() const { return selected_.isEmpty() ? -1 : notes_.value(selected_).page; }
    QString selectedId() const { return selected_; }
    bool canUndo() const { return !undo_.isEmpty(); }
    bool canRedo() const { return !redo_.isEmpty(); }
    void reset();
    void setGeometry(int page,const PageGeometry& geometry);
    PageGeometry geometry(int page) const { return geometry_.value(page); }
    void setActivePages(const QList<int>& pages);
    QList<TextAnnotation> snapshot() const { return notes_.values(); }
    Q_INVOKABLE QString create(int page,double x,double y);
    Q_INVOKABLE void select(const QString& id);
    Q_INVOKABLE void finishEditing();
    Q_INVOKABLE void setText(const QString& id,const QString& text,double height);
    Q_INVOKABLE void move(const QString& id,double x,double y);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
signals:
    void selectionChanged();
    void historyChanged();
private:
    struct Command { TextAnnotation before, after; };
    void record(TextAnnotation before,TextAnnotation after);
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
};
