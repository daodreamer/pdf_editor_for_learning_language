#include "pageitem.h"
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
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
