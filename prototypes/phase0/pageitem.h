#pragma once
#include <QImage>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

class PageItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
public:
    explicit PageItem(QQuickItem* parent = nullptr);
    void present(QImage image);
protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
private:
    QImage image_;
    quint64 revision_ = 0;
};
