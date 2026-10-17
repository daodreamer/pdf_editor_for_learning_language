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
    // A separate pass for thin, regularly repeated marks. Do not treat arbitrary
    // gaps in a sentence as a line: marks must be flat, periodic, and blank above.
    struct Mark { int left,right; bool thin; };
    QList<Mark> marks; marks.reserve(256);
    const int maxGap=std::max(2,int(std::ceil(maxDashGapPoints*sx)));
    const int maxMark=std::max(2,int(std::ceil(maxDashMarkPoints*sx)));
    const int blankAbove=std::max(4,int(std::ceil(dottedBlankAbove*sy)));
    const int clearBelow=std::max(thickness+1,int(std::ceil(5*sy)));
    const int flatDistance=std::max(2,int(std::ceil(1.5*sy)));
    auto dottedDark=[&](int x,int y) { return image.constScanLine(y)[x]<dottedThreshold; };
    for(int y=blankAbove+thickness;y<h-thickness && result.size()<maxLines;++y) {
        marks.clear();
        const auto* row=image.constScanLine(y);
        for(int x=2;x<w-2;) {
            if(row[x]>=dottedThreshold) { ++x; continue; }
            const int left=x;
            while(x<w-2 && row[x]<dottedThreshold) ++x;
            bool thin=x-left<=maxMark;
            for(int xx=std::max(0,left-1);thin && xx<std::min(w,x+1);++xx)
                if(dottedDark(xx,y-flatDistance) || dottedDark(xx,y+flatDistance)) thin=false;
            marks.append({left,x-1,thin});
        }
        for(qsizetype first=0;first<marks.size();) {
            if(!marks[first].thin) { ++first; continue; }
            auto end=first+1;
            while(end<marks.size() && marks[end].thin && marks[end].left-marks[end-1].right-1<=maxGap) ++end;
            const auto next=end;
            if(end-first>=3) {
                QList<double> pitches;
                QList<int> widths;
                for(auto i=first;i<end;++i) widths.append(marks[i].right-marks[i].left+1);
                std::sort(widths.begin(),widths.end());
                const int required=widths[widths.size()/2]>=2*sx?3:minMarks;
                for(auto i=first+1;i<end;++i) pitches.append((marks[i].left+marks[i].right-marks[i-1].left-marks[i-1].right)/2.0);
                std::sort(pitches.begin(),pitches.end());
                const double pitch=pitches[pitches.size()/2];
                auto spacing=[&](qsizetype i) { return (marks[i].left+marks[i].right-marks[i-1].left-marks[i-1].right)/2.0; };
                while(end-first>=required && spacing(first+1)>pitch*1.6) ++first;
                while(end-first>=required && spacing(end-1)>pitch*1.6) --end;
                int irregular=0,ink=0;
                for(auto i=first;i<end;++i) {
                    ink+=marks[i].right-marks[i].left+1;
                    if(i>first && (spacing(i)<pitch*.5 || spacing(i)>pitch*1.8)) ++irregular;
                }
                const int left=marks[first].left,last=marks[end-1].right,length=last-left+1;
                if(end-first>=required && length>=minLength && length<w*.8 && ink>=length*.18 && ink<=length*.8 && irregular<=std::max(qsizetype(1),(end-first)/8)) {
                    int occupied=0,total=0;
                    for(int yy=y-blankAbove;yy<y-thickness;++yy)
                        for(int xx=left+1;xx<last;xx+=2) { ++total; if(dottedDark(xx,yy)) ++occupied; }
                    for(int yy=y+thickness;yy<std::min(h,y+clearBelow);++yy)
                        for(int xx=left+1;xx<last;xx+=2) { ++total; if(dottedDark(xx,yy)) ++occupied; }
                    bool vertical=false;
                    QList<int> edges{left,last};
                    // Corner marks may have been excluded by the flatness test.
                    // Keep their edges in the table-intersection check.
                    if(first>0 && !marks[first-1].thin && left-marks[first-1].right-1<=maxGap) {
                        edges.append(marks[first-1].left); edges.append(marks[first-1].right);
                    }
                    if(end<marks.size() && !marks[end].thin && marks[end].left-last-1<=maxGap) {
                        edges.append(marks[end].left); edges.append(marks[end].right);
                    }
                    for(int edge:edges) {
                        int up=0,down=0;
                        for(int dy=thickness+1;dy<blankAbove;++dy) {
                            bool a=false,b=false;
                            for(int dx=-1;dx<=1;++dx) {
                                const int xx=std::clamp(edge+dx,0,w-1);
                                a=a||dottedDark(xx,y-dy); b=b||(y+dy<h && dottedDark(xx,y+dy));
                            }
                            if(a) ++up; if(b) ++down;
                        }
                        if(up>blankAbove/3 || down>blankAbove/3) vertical=true;
                    }
                    if(!vertical && occupied<=std::max(1,total/100)) {
                        const double height=dottedFieldHeight/units.height();
                        const QRectF area(double(left)/w,std::max(0.0,double(y)/h-(dottedFieldHeight-3)/units.height()),double(length)/w,height);
                        bool duplicate=false;
                        for(const auto& old:result) if(std::abs(old.bottom()-area.bottom())<8/units.height() && old.intersects(area)) { duplicate=true; break; }
                        if(!duplicate) result.append(area.intersected(QRectF(0,0,1,1)));
                    }
                }
            }
            first=next;
        }
    }
    return result;
}
