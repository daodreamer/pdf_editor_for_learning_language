#pragma once
#include <QImage>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

class PageItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QImage image READ image WRITE present NOTIFY imageChanged)
public:
    explicit PageItem(QQuickItem* parent = nullptr);
    void present(QImage image);
    QImage image() const { return image_; }
signals:
    void imageChanged();
protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
private:
    QImage image_;
    quint64 revision_ = 0;
};
