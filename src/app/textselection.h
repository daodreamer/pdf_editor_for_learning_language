#pragma once
#include "textpage.h"
#include <QObject>
#include <QMap>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class TextSelection : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ShellState")
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(int version READ version NOTIFY changed)
public:
    explicit TextSelection(QObject* parent=nullptr) : QObject(parent) {}
    QString text() const { return text_; }
    QString status() const { return status_; }
    bool ready() const { return !text_.isEmpty() && complete_; }
    bool canStart() const { return anchorIndex_>=0 || (anchorPage_>=0 && !pages_.contains(anchorPage_)); }
    int version() const { return version_; }
    qint64 bytes() const;
    TextSnapshot snapshot(int page) const { return pages_.value(page); }
    void reset();
    void put(int page, TextSnapshot snapshot);
    int nextPage(const QList<int>& active);
    Q_INVOKABLE void begin(int page, double x, double y, double toleranceX, double toleranceY);
    Q_INVOKABLE void extend(int page, double x, double y);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void copy();
    Q_INVOKABLE QVariantList rectangles(int page) const;
signals:
    void changed();
    void demandChanged();
private:
    int hit(int page, QPointF point, bool nearest) const;
    void rebuild();
    void notify() { ++version_; emit changed(); }
    QPair<int,int> range(int page) const;
    QMap<int,TextSnapshot> pages_;
    int anchorPage_=-1, endPage_=-1, anchorIndex_=-1, endIndex_=-1;
    QPointF anchor_, end_, tolerance_;
    bool extended_=false, complete_=false;
    int version_=0;
    QString text_, status_;
};
