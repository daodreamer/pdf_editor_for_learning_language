#include "inkitem.h"
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QQuickWindow>
#include "inkpath.h"
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
    float tail=-1; // width factor of the last built sample; the end taper changes it
};
class InkRoot : public QSGNode {
public:
    QMap<QString,StrokeNode*> strokes; // Non-owning index; QSGNode owns all children.
    QSizeF size;
    QTransform projection;
    double dpr=1;
};
QSGGeometryNode* chunk(const InkStroke& stroke,const QTransform& transform,double scale,double dpr,qsizetype start,qsizetype end,bool selected) {
    using Vertex=QSGGeometry::ColoredPoint2D;
    QList<Vertex> vertices;
    vertices.reserve((end-start)*54);
    const qsizetype count=stroke.points.size();
    const double base=stroke.width*scale/2;
    const double feather=1/dpr; // one device pixel of analytic edge coverage
    const auto color=selected?stroke.color.lighter(150):stroke.color;
    auto radius=[&](qsizetype i,double t) { return base*InkPath::factor(stroke.pressure,count,i,t); };
    auto inner=[&](double r) { return std::max(0.0,r-feather/2); };
    auto outer=[&](double r) { return r+feather/2; };
    auto coverage=[&](double r) { return std::min(1.0,2*r/feather)*color.alphaF(); };
    auto vertex=[&](QPointF p,double alpha) {
        Vertex v; v.set(float(p.x()),float(p.y()),uchar(std::lround(color.red()*alpha)),
            uchar(std::lround(color.green()*alpha)),uchar(std::lround(color.blue()*alpha)),uchar(std::lround(255*alpha)));
        return v;
    };
    // Two triangles a-b-c, c-b-d; a/c lie on one side of the strip, b/d on the other.
    auto quad=[&](QPointF a,QPointF b,QPointF c,QPointF d,double aa,double ab,double ac,double ad) {
        vertices.append({vertex(a,aa),vertex(b,ab),vertex(c,ac),vertex(c,ac),vertex(b,ab),vertex(d,ad)});
    };
    auto cap=[&](QPointF p,double r,double angle,double sweep) {
        const double in=inner(r),out=outer(r),cover=coverage(r);
        const int steps=std::clamp(int(std::ceil(std::abs(sweep)*std::sqrt(out*dpr/.4))),12,128);
        for(int j=0;j<steps;++j) {
            const double t=angle+sweep*j/steps,u=angle+sweep*(j+1)/steps;
            const QPointF n(std::cos(t),std::sin(t)),m(std::cos(u),std::sin(u));
            vertices.append({vertex(p,cover),vertex(p+n*in,cover),vertex(p+m*in,cover)});
            quad(p+n*in,p+n*out,p+m*in,p+m*out,cover,0,cover,0);
        }
    };
    if(count==1) cap(transform.map(stroke.points.first()),base,0,2*std::numbers::pi);
    for(qsizetype i=start;i<end;++i) {
        const auto curve=InkPath::segment(stroke.points,i,transform);
        // A true reversal has a zero derivative: cover the cusp with a round join.
        const auto velocity=curve.control-curve.a,delta=curve.a-2*curve.control+curve.b;
        const double acceleration=QPointF::dotProduct(delta,delta);
        if(acceleration>1e-12) {
            const double t=-QPointF::dotProduct(velocity,delta)/acceleration;
            if(t>0 && t<1 && QLineF(QPointF(),curve.tangent(t)).length()<1e-6)
                cap(curve.at(t),radius(i,t),0,2*std::numbers::pi);
        }
        // Enough slices for both the bend and the width change to stay sub-pixel smooth.
        const double r0=radius(i,0),r1=radius(i,1);
        const int steps=std::clamp(std::max(curve.steps(.15/dpr),int(std::ceil(std::abs(r1-r0)*dpr/.25))),1,64);
        auto normal=[&](double t) {
            auto d=curve.tangent(t);
            if(std::hypot(d.x(),d.y())<1e-9) d=curve.b-curve.a;
            const double length=std::hypot(d.x(),d.y());
            return length>1e-9 ? QPointF(-d.y()/length,d.x()/length) : QPointF(0,1);
        };
        auto a=curve.a,n=normal(0);
        double ra=r0;
        if(i==0 && count>1) cap(a,ra,std::atan2(n.y(),n.x()),std::numbers::pi);
        for(int j=1;j<=steps;++j) {
            const double t=double(j)/steps;
            const auto b=curve.at(t),m=normal(t);
            const double rb=radius(i,t);
            const double ia=inner(ra),ib=inner(rb),oa=outer(ra),ob=outer(rb),ca=coverage(ra),cb=coverage(rb);
            quad(a+n*ia,a-n*ia,b+m*ib,b-m*ib,ca,ca,cb,cb);
            quad(a+n*ia,a+n*oa,b+m*ib,b+m*ob,ca,0,cb,0);
            quad(a-n*ia,a-n*oa,b-m*ib,b-m*ob,ca,0,cb,0);
            a=b; n=m; ra=rb;
        }
        if(i+1==count && count>1) cap(a,ra,std::atan2(n.y(),n.x()),-std::numbers::pi);
    }
    auto* node=new QSGGeometryNode;
    auto* geometry=new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(),int(vertices.size()));
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    std::copy(vertices.cbegin(),vertices.cend(),geometry->vertexDataAsColoredPoint2D());
    node->setGeometry(geometry); node->setFlag(QSGNode::OwnsGeometry);
    auto* material=new QSGVertexColorMaterial;
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
    const double dpr=window()?window()->effectiveDevicePixelRatio():1;
    if(root && (root->size!=size() || root->projection!=g.pdfToDisplay || root->dpr!=dpr)) { delete root; root=nullptr; }
    if(!root) { root=new InkRoot; root->size=size(); root->projection=g.pdfToDisplay; root->dpr=dpr; }
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
        const auto tailFactor=[&](qsizetype n) { return n>0 && stroke.pressure.size()==stroke.points.size() ? stroke.pressure[n-1] : -1.0f; };
        if(node && (node->first!=stroke.points.first() || node->width!=stroke.width || node->color!=stroke.color || node->selected!=selected
            || node->count>stroke.points.size() || node->tail!=tailFactor(node->count))) {
            root->strokes.remove(stroke.id); delete node; node=nullptr;
        }
        if(!node) {
            node=new StrokeNode; root->appendChildNode(node); root->strokes[stroke.id]=node;
            node->first=stroke.points.first(); node->width=stroke.width; node->color=stroke.color; node->selected=selected;
        }
        if(node->count==stroke.points.size() && node->last==stroke.points.last()) continue;
        const qsizetype step=AnnotationConfig::segmentsPerChunk;
        const qsizetype start=node->count ? (std::max(qsizetype(0),node->count-1)/step)*step : 0;
        // The old final curve gains a next sample. Rebuild its chunk and any later tail.
        while(node->childCount()>start/step) delete node->lastChild();
        for(qsizetype i=start;i<stroke.points.size();i+=step)
            node->appendChildNode(chunk(stroke,transform,width()/g.units.width(),dpr,i,std::min(i+step,stroke.points.size()),selected));
        node->count=stroke.points.size(); node->last=stroke.points.last(); node->tail=tailFactor(node->count);
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
