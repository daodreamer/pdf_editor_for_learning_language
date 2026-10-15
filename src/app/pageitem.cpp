#include "pageitem.h"
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include <memory>

namespace {
class ImageNode final : public QSGSimpleTextureNode {
public:
    quint64 revision = 0;
};
}
PageItem::PageItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents);
}
void PageItem::present(QImage image) {
    if (image.cacheKey() == image_.cacheKey()) return;
    image_ = std::move(image);
    ++revision_;
    update();
    emit imageChanged();
}
QSGNode* PageItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* node = static_cast<ImageNode*>(old);
    if (image_.isNull()) { delete node; return nullptr; }
    if (!node || node->revision != revision_) {
        delete node; // Node owns its texture, destroyed in the render execution domain.
        auto replacement = std::make_unique<ImageNode>();
        auto* texture = window()->createTextureFromImage(image_);
        if (!texture) return nullptr;
        replacement->setTexture(texture);
        replacement->setOwnsTexture(true);
        replacement->setFiltering(QSGTexture::Linear);
        replacement->revision = revision_;
        node = replacement.release(); // Qt owns the returned subtree.
        qInfo("texture_created bytes_estimate=%lld", qint64(image_.sizeInBytes()));
    }
    node->setRect(boundingRect());
    return node;
}

QSGNode* SelectionItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    delete old;
    if (rectangles_.isEmpty()) return nullptr;
    auto node=std::make_unique<QSGGeometryNode>();
    auto geometry=std::make_unique<QSGGeometry>(QSGGeometry::defaultAttributes_Point2D(),int(rectangles_.size())*6);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto* vertices=geometry->vertexDataAsPoint2D();
    for (const auto& value : rectangles_) {
        const auto r=value.toRectF();
        const float l=float(r.left()*width()), t=float(r.top()*height());
        const float right=float(r.right()*width()), b=float(r.bottom()*height());
        vertices[0].set(l,t); vertices[1].set(right,t); vertices[2].set(l,b);
        vertices[3].set(l,b); vertices[4].set(right,t); vertices[5].set(right,b);
        vertices+=6;
    }
    auto material=std::make_unique<QSGFlatColorMaterial>();
    material->setColor(QColor(40,125,240,90));
    node->setGeometry(geometry.release()); node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(material.release()); node->setFlag(QSGNode::OwnsMaterial);
    return node.release();
}
