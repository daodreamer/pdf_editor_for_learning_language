#pragma once
#include "textstyles.h"
#include <QQuickTextDocument>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

class AnnotationText : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QColor baseColor MEMBER baseColor_)
    Q_PROPERTY(double baseSize MEMBER baseSize_)
public:
    explicit AnnotationText(QObject* parent=nullptr):QObject(parent) {}
    QQuickTextDocument* document() const { return document_; }
    void setDocument(QQuickTextDocument* document);
    Q_INVOKABLE QString plainText() const;
    Q_INVOKABLE QVariantList styles() const;
    Q_INVOKABLE QVariantMap selectionStyle(int start,int end) const;
    Q_INVOKABLE void format(int start,int end,const QString& property,const QVariant& value);
    Q_INVOKABLE void restore(const QVariantList& styles);
signals:
    void documentChanged();
    void changed();
private:
    QPointer<QQuickTextDocument> document_;
    QColor baseColor_=QColor(30,70,140);
    double baseSize_=14;
    bool restoring_=false;
};
