#pragma once
#include <QImage>
#include <QQuickItem>
#include <QVariantList>
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

class SelectionItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList rectangles READ rectangles WRITE setRectangles NOTIFY rectanglesChanged)
public:
    explicit SelectionItem(QQuickItem* parent=nullptr) : QQuickItem(parent) { setFlag(ItemHasContents); }
    QVariantList rectangles() const { return rectangles_; }
    void setRectangles(QVariantList rectangles) {
        if (rectangles_==rectangles) return;
        rectangles_=std::move(rectangles); update(); emit rectanglesChanged();
    }
signals:
    void rectanglesChanged();
protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
private:
    QVariantList rectangles_;
};
