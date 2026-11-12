#pragma once
#include <QPainterPath>
#include <QTransform>
#include <QList>
#include <algorithm>
#include <cmath>

// Midpoint quadratics stay inside the samples' convex hull and share tangents.
// Raw PDF-space samples remain the storage contract; all consumers use this curve.
namespace InkPath {
struct Curve {
    QPointF a, control, b;
    QPointF at(double t) const { return (1-t)*(1-t)*a+2*t*(1-t)*control+t*t*b; }
    QPointF tangent(double t) const { return (1-t)*(control-a)+t*(b-control); }
    int steps(double tolerance) const {
        const auto bend=a-2*control+b;
        return std::clamp(int(std::ceil(std::sqrt(std::hypot(bend.x(),bend.y())/(4*tolerance)))),1,64);
    }
};
inline Curve segment(const QList<QPointF>& points,qsizetype i,const QTransform& transform={}) {
    const auto p=transform.map(points[i]);
    return {i ? (transform.map(points[i-1])+p)/2 : p,p,
        i+1<points.size() ? (p+transform.map(points[i+1]))/2 : p};
}
// Width factor along segment i, interpolated with the same quadratic as the centre
// line so width and position stay in step. Strokes without factors are uniform.
inline double factor(const QList<float>& f,qsizetype count,qsizetype i,double t) {
    if(f.size()!=count || count<2) return 1;
    const double p=f[i],a=i ? (f[i-1]+p)/2 : p,b=i+1<count ? (p+f[i+1])/2 : p;
    return (1-t)*(1-t)*a+2*t*(1-t)*p+t*t*b;
}
// Pen landing and lifting: the first and last few widths of a hand-drawn stroke
// ramp from these fractions of the full width (ease-out, so most of the line is full).
inline constexpr double startTaper=.55, endTaper=.3;
inline double startTaperLength(double width) { return std::clamp(width*1.6,1.2,8.0); }
inline double ramp(double from,double s,double length) {
    const double t=std::clamp(length>0 ? s/length : 1.0,0.0,1.0);
    return from+(1-from)*t*(2-t);
}
inline void taperTail(const QList<QPointF>& points,QList<float>& f,double width,double total) {
    if(f.size()!=points.size() || points.size()<2) return;
    const double length=std::min(std::clamp(width*2.2,1.5,10.0),total*.4);
    double d=0;
    for(qsizetype i=points.size()-1;i>=0 && d<length;--i) {
        f[i]=float(f[i]*ramp(endTaper,d,length));
        if(i) d+=std::hypot(points[i].x()-points[i-1].x(),points[i].y()-points[i-1].y());
    }
}
inline QPainterPath path(const QList<QPointF>& points,const QTransform& transform={}) {
    QPainterPath result;
    if(points.isEmpty()) return result;
    result.moveTo(transform.map(points.first()));
    for(qsizetype i=0;i<points.size();++i) {
        const auto c=segment(points,i,transform);
        result.quadTo(c.control,c.b);
    }
    return result;
}
}
