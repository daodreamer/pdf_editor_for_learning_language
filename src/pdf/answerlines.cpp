#include "answerlines.h"
#include <algorithm>
#include <cmath>

QList<QRectF> AnswerLines::detect(const QImage& source,QSizeF units) {
    QList<QRectF> result;
    if(source.isNull() || !std::isfinite(units.width()) || !std::isfinite(units.height()) || units.width()<=0 || units.height()<=0) return result;
    const auto image=source.scaled(source.size().boundedTo(QSize(maxWidth,maxHeight)),Qt::KeepAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
    const int w=image.width(),h=image.height();
    const double sx=w/units.width(),sy=h/units.height();
    const int minLength=std::max(12,int(std::ceil(24*sx)));
    const int above=std::max(6,int(std::ceil(16*sy))),thickness=std::max(2,int(std::ceil(3*sy)));
    int candidates=0;
    auto dark=[&](int x,int y) { return image.constScanLine(y)[x]<155; };
    for(int y=above+thickness;y<h-2 && result.size()<maxLines;++y) {
        const auto* row=image.constScanLine(y);
        for(int x=2;x<w-2;) {
            if(row[x]>=155) { ++x; continue; }
            const int left=x; int last=x,ink=0,gap=0;
            for(;x<w-2;++x) {
                if(row[x]<155) { last=x; ++ink; gap=0; }
                else if(++gap>2) break;
            }
            const int length=last-left+1;
            if(length<minLength || length>w*.8 || ink<length*.85) continue;
            if(++candidates>2048) return result;
            // Only the top edge of a thin rule; reject broad fills and table intersections.
            const int mid=(left+last)/2;
            if(dark(mid,y-1) || dark(mid,std::min(h-1,y+thickness))) continue;
            bool vertical=false;
            for(int edge:{left,last}) {
                int up=0,down=0;
                for(int dy=thickness+1;dy<above;++dy) {
                    if(dark(edge,y-dy)) ++up;
                    if(y+dy<h && dark(edge,y+dy)) ++down;
                }
                if(up>above/3 || down>above/3) vertical=true;
            }
            if(vertical) continue;
            int occupied=0,total=0;
            for(int yy=y-above;yy<y-thickness;++yy) {
                for(int xx=left+2;xx<last-1;xx+=2) { ++total; if(dark(xx,yy)) ++occupied; }
            }
            if(occupied>std::max(1,total/100)) continue;
            const QRectF area(double(left)/w,std::max(0.0,double(y)/h-18/units.height()),
                double(length)/w,24/units.height());
            bool duplicate=false;
            for(const auto& old:result) if(std::abs(old.y()-area.y())<4/units.height() && old.intersects(area)) { duplicate=true; break; }
            if(!duplicate) result.append(area.intersected(QRectF(0,0,1,1)));
        }
    }
    return result;
}
