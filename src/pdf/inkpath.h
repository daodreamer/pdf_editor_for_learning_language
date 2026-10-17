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
