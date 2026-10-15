#include "inkitem.h"
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include <QLineF>
#include <QSet>
#include <cmath>
#include <numbers>

namespace {
class StrokeNode : public QSGNode {
public:
    QPointF first, last;
    qsizetype count=0;
    double width=0;
    QColor color;
    bool selected=false;
};
class InkRoot : public QSGNode {
public:
    QMap<QString,StrokeNode*> strokes; // Non-owning index; QSGNode owns all children.
    QSizeF size;
    QTransform projection;
};
QSGGeometryNode* chunk(const InkStroke& stroke,const QTransform& transform,double scale,qsizetype start,qsizetype end,bool selected) {
    QList<QPointF> vertices;
    vertices.reserve((end-start)*48);
    const double radius=stroke.width*scale/2;
    for(qsizetype i=start;i<end;++i) {
        const auto b=transform.map(stroke.points[i]);
        if(i>0) {
            const auto a=transform.map(stroke.points[i-1]);
            const auto d=b-a;
            const double length=std::hypot(d.x(),d.y());
            if(length>1e-9) {
                const QPointF normal(-d.y()*radius/length,d.x()*radius/length);
                vertices.append({a+normal,a-normal,b+normal,b+normal,a-normal,b-normal});
            }
        }
        for(int j=0;j<12;++j) {
            const double t=j*std::numbers::pi/6, u=(j+1)*std::numbers::pi/6;
            vertices.append({b,b+QPointF(std::cos(t)*radius,std::sin(t)*radius),b+QPointF(std::cos(u)*radius,std::sin(u)*radius)});
        }
    }
    auto* node=new QSGGeometryNode;
    auto* geometry=new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(),int(vertices.size()));
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    auto* data=geometry->vertexDataAsPoint2D();
    for(qsizetype i=0;i<vertices.size();++i) data[i].set(float(vertices[i].x()),float(vertices[i].y()));
    node->setGeometry(geometry); node->setFlag(QSGNode::OwnsGeometry);
    auto* material=new QSGFlatColorMaterial;
    material->setColor(selected?stroke.color.lighter(150):stroke.color);
    node->setMaterial(material); node->setFlag(QSGNode::OwnsMaterial);
    return node;
}
}
InkItem::InkItem(QQuickItem* parent):QQuickItem(parent) { setFlag(ItemHasContents); }
void InkItem::setModel(AnnotationModel* model) {
    if(model_==model) return;
    if(model_) disconnect(model_,nullptr,this,nullptr);
    model_=model;
    if(model_) connect(model_,&AnnotationModel::inkChanged,this,[this](int page){ if(page==page_) update(); });
    update(); emit modelChanged();
}
void InkItem::setPage(int page) { if(page_!=page) { page_=page; update(); emit pageChanged(); } }
QSGNode* InkItem::updatePaintNode(QSGNode* old,UpdatePaintNodeData*) {
    auto* root=static_cast<InkRoot*>(old);
    if(!model_ || !model_->geometry(page_).valid) { delete root; return nullptr; }
    const auto g=model_->geometry(page_);
    if(root && (root->size!=size() || root->projection!=g.pdfToDisplay)) { delete root; root=nullptr; }
    if(!root) { root=new InkRoot; root->size=size(); root->projection=g.pdfToDisplay; }
    // Called during scene graph synchronization with GUI blocked. No model access in render().
    const auto strokes=model_->displayStrokes(page_);
    QSet<QString> wanted;
    for(const auto& stroke:strokes) wanted.insert(stroke.id);
    for(auto it=root->strokes.begin();it!=root->strokes.end();) {
        if(!wanted.contains(it.key())) { delete it.value(); it=root->strokes.erase(it); } else ++it;
    }
    const QTransform transform=g.pdfToDisplay*QTransform::fromScale(width(),height());
    for(const auto& stroke:strokes) {
        if(stroke.points.isEmpty()) continue;
        auto* node=root->strokes.value(stroke.id,nullptr);
        const bool selected=stroke.id==model_->inkSelection();
        if(node && (node->first!=stroke.points.first() || node->width!=stroke.width || node->color!=stroke.color || node->selected!=selected || node->count>stroke.points.size())) {
            root->strokes.remove(stroke.id); delete node; node=nullptr;
        }
        if(!node) {
            node=new StrokeNode; root->appendChildNode(node); root->strokes[stroke.id]=node;
            node->first=stroke.points.first(); node->width=stroke.width; node->color=stroke.color; node->selected=selected;
        }
        if(node->count==stroke.points.size() && node->last==stroke.points.last()) continue;
        const qsizetype step=AnnotationConfig::segmentsPerChunk;
        const qsizetype start=node->count ? ((node->count-1)/step)*step : 0;
        if(node->count) delete node->lastChild(); // Only replace the incomplete tail chunk.
        for(qsizetype i=start;i<stroke.points.size();i+=step)
            node->appendChildNode(chunk(stroke,transform,width()/g.units.width(),i,std::min(i+step,stroke.points.size()),selected));
        node->count=stroke.points.size(); node->last=stroke.points.last();
    }
    // Keep creation order across incremental additions, selection and virtualization rebuilds.
    auto* expected=root->firstChild();
    for(const auto& stroke:strokes) {
        auto* node=root->strokes.value(stroke.id,nullptr);
        if(!node) continue;
        if(node!=expected) {
            root->removeChildNode(node);
            if(expected) root->insertChildNodeBefore(node,expected); else root->appendChildNode(node);
        }
        expected=node->nextSibling();
    }
    return root;
}
