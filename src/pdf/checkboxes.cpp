#include "checkboxes.h"
#include <algorithm>
#include <cmath>
#include <vector>

QList<QRectF> CheckBoxes::detect(const QImage& source,QSizeF units) {
    QList<QRectF> result;
    if(source.isNull() || !std::isfinite(units.width()) || !std::isfinite(units.height()) || units.width()<=0 || units.height()<=0) return result;
    // Judged at one fixed scale; PdfWorker renders this scale for detection (see header).
    const QSize target=QSize(int(std::lround(units.width()*pixelsPerPoint)),int(std::lround(units.height()*pixelsPerPoint)))
        .boundedTo(QSize(maxWidth,maxHeight)).expandedTo(QSize(1,1));
    const auto image=source.scaled(target,Qt::KeepAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
    const int w=image.width(),h=image.height();
    const double sx=w/units.width(),sy=h/units.height();
    const int minSide=std::max(5,int(std::floor(minSidePoints*std::min(sx,sy))));
    const int maxSideX=int(std::ceil(maxSidePoints*sx)),maxSideY=int(std::ceil(maxSidePoints*sy));
    // Ink: the darkest channel is dark, so coloured outlines count; tinted paper does not.
    std::vector<quint8> mask(size_t(w)*size_t(h));
    for(int y=0;y<h;++y) {
        const auto* row=reinterpret_cast<const QRgb*>(image.constScanLine(y));
        auto* out=mask.data()+size_t(y)*w;
        for(int x=0;x<w;++x) out[x]=std::min({qRed(row[x]),qGreen(row[x]),qBlue(row[x])})<inkLevel;
    }
    // mask: 0 paper, 1 ink, 2 ink already assigned to a component.
    auto ink=[&](int x,int y) { return mask[size_t(y)*w+x]!=0; };
    // 8-connected components (iterative flood fill); every ink pixel is visited once.
    std::vector<int> stack,pixels; stack.reserve(4096); pixels.reserve(4096);
    std::vector<quint8> local;
    for(int y0=0;y0<h && result.size()<maxBoxes;++y0) for(int x0=0;x0<w && result.size()<maxBoxes;++x0) {
        if(mask[size_t(y0)*w+x0]!=1) continue;
        int left=x0,right=x0,top=y0,bottom=y0;
        pixels.clear();
        mask[size_t(y0)*w+x0]=2; stack.push_back(y0*w+x0);
        while(!stack.empty()) {
            const int i=stack.back(); stack.pop_back();
            const int x=i%w,y=i/w;
            left=std::min(left,x); right=std::max(right,x); top=std::min(top,y); bottom=std::max(bottom,y);
            // Only small components are judged; large ones need no pixel list.
            if(right-left<=maxSideX && bottom-top<=maxSideY) pixels.push_back(i);
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                const int nx=x+dx,ny=y+dy;
                if(nx<0 || ny<0 || nx>=w || ny>=h) continue;
                const size_t j=size_t(ny)*w+nx;
                if(mask[j]==1) { mask[j]=2; stack.push_back(ny*w+nx); }
            }
        }
        const int bw=right-left+1,bh=bottom-top+1;
        if(bw<minSide || bh<minSide || bw>maxSideX || bh>maxSideY) continue;
        const double aspect=(bw/sx)/(bh/sy);
        if(aspect>maxAspect || aspect<1/maxAspect || qsizetype(pixels.size())>maxFill*bw*bh) continue;
        local.assign(size_t(bw)*size_t(bh),0);
        for(int i:pixels) local[size_t(i/w-top)*bw+(i%w-left)]=1;
        auto own=[&](int x,int y) { return local[size_t(y-top)*bw+(x-left)]!=0; };
        // Each side: the outline is present along (almost) its full length, near the edge.
        const int bandX=std::max(1,int(std::lround(bw*.2))),bandY=std::max(1,int(std::lround(bh*.2)));
        auto coveredRows=[&](int from,int to,int x0b,int x1b) {
            int covered=0;
            for(int y=from;y<=to;++y) { bool hit=false; for(int x=x0b;x<=x1b && !hit;++x) hit=own(x,y); covered+=hit; }
            return covered>=sideCoverage*(to-from+1);
        };
        auto coveredColumns=[&](int from,int to,int y0b,int y1b) {
            int covered=0;
            for(int x=from;x<=to;++x) { bool hit=false; for(int y=y0b;y<=y1b && !hit;++y) hit=own(x,y); covered+=hit; }
            return covered>=sideCoverage*(to-from+1);
        };
        if(!coveredColumns(left,right,top,top+bandY-1) || !coveredColumns(left,right,bottom-bandY+1,bottom)
            || !coveredRows(top,bottom,left,left+bandX-1) || !coveredRows(top,bottom,right-bandX+1,right)) continue;
        // Hollow: the inside (inset past the outline band) is paper.
        const int ix0=left+bandX,ix1=right-bandX,iy0=top+bandY,iy1=bottom-bandY;
        if(ix1-ix0<1 || iy1-iy0<1) continue;
        int inside=0;
        for(int y=iy0;y<=iy1;++y) for(int x=ix0;x<=ix1;++x) inside+=ink(x,y);
        if(inside>maxInsideInk*(ix1-ix0+1)*(iy1-iy0+1)) continue;
        // Distance from each side to the nearest ink on the box's own rows.
        auto gapTo=[&](int dir) {
            const int edge=dir<0?left:right;
            for(int k=1;k<=bw;++k) {
                const int x=edge+dir*k;
                if(x<0 || x>=w) return bw+1;
                for(int y=top;y<=bottom;++y) if(ink(x,y)) return k;
            }
            return bw+1;
        };
        const int gapLeft=gapTo(-1),gapRight=gapTo(1);
        if(gapLeft<=sideClearance*bw || gapRight<=sideClearance*bw) continue;
        auto anyInk=[&](int x0,int x1,int y0,int y1) {
            int found=0;
            for(int y=std::max(0,y0);y<=std::min(h-1,y1);++y)
                for(int x=std::max(0,std::min(x0,x1));x<=std::min(w-1,std::max(x0,x1));++x) found+=ink(x,y);
            return found>=2;
        };
        // A CJK glyph next to 口 is taller and dense: its strokes rise above and drop
        // below the square and fill much of its own square. Text after a real box starts
        // at x-height or cap height, ends at the baseline, or (a slash) is thin.
        const int margin=std::max(1,int(std::lround(nearMargin*bh))),reach=int(std::lround(nearReach*bh));
        auto cjkNeighbour=[&](int dir,int gap) {
            const int edge=dir<0?left:right;
            const int x0=edge+dir*gap,x1=edge+dir*(gap+int(std::lround(neighbourWidth*bw)));
            if(!anyInk(x0,x1,top-reach,top-margin) || !anyInk(x0,x1,bottom+margin,bottom+reach)) return false;
            int dark=0,total=0;
            for(int y=top;y<=bottom;++y) for(int x=std::max(0,std::min(x0,x1));x<=std::min(w-1,std::max(x0,x1));++x) { ++total; dark+=ink(x,y); }
            return dark>=neighbourDensity*total;
        };
        if((gapLeft<closeGap*bw && cjkNeighbour(-1,gapLeft)) || (gapRight<closeGap*bw && cjkNeighbour(1,gapRight))) continue;
        // Separate strokes right under the square (the dots of 只 and 识). Another box
        // stacked below shows a full-width edge instead, which is allowed.
        {
            const int y1=std::min(h-1,bottom+std::max(2,int(std::lround(.25*bh))));
            int covered=0;
            for(int x=left;x<=right;++x) { bool hit=false; for(int y=bottom+1;y<=y1 && !hit;++y) hit=ink(x,y); covered+=hit; }
            const double fraction=double(covered)/bw;
            if(fraction>.08 && fraction<.7) continue;
        }
        result.append(QRectF(double(left)/w,double(top)/h,double(bw)/w,double(bh)/h));
    }
    std::sort(result.begin(),result.end(),[](const QRectF& a,const QRectF& b) { return a.top()<b.top() || (a.top()==b.top() && a.left()<b.left()); });
    return result;
}
