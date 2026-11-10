#include "answerlines.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <vector>

namespace {
struct Component {
    int left=INT_MAX,right=-1,top=INT_MAX,bottom=-1;
    int width() const { return right-left+1; }
    int height() const { return bottom-top+1; }
};
struct Mark { int left,right,top,bottom; double center; int width() const { return right-left+1; } };
struct Run { int left,right; };
int findRoot(std::vector<int>& parent,int i) {
    while(parent[i]!=i) { parent[i]=parent[parent[i]]; i=parent[i]; }
    return i;
}
template<typename T> T median(std::vector<T> values) {
    std::nth_element(values.begin(),values.begin()+values.size()/2,values.end());
    return values[values.size()/2];
}

// Dotted, dashed and faint printed rules. Thin connected components are linked
// left-to-right into chains; each chain is judged as a whole, so a slight slope,
// scan jitter, merged dots or a lower render resolution cannot split one rule
// into raster-row fragments.
QList<QRectF> detectDotted(const QImage& image,QSizeF units) {
    using namespace AnswerLines;
    QList<QRectF> result;
    const int w=image.width(),h=image.height();
    const double sx=w/units.width(),sy=h/units.height();
    const int offsetY=std::max(3,int(std::ceil(backgroundOffsetPoints*sy)));
    const int offsetX=std::max(3,int(std::ceil(backgroundOffsetPoints*sx)));
    std::vector<quint8> mask(size_t(w)*size_t(h));
    for(int y=0;y<h;++y) {
        const auto* row=image.constScanLine(y);
        const auto* up=image.constScanLine(std::max(0,y-offsetY));
        const auto* down=image.constScanLine(std::min(h-1,y+offsetY));
        auto* out=mask.data()+size_t(y)*w;
        for(int x=0;x<w;++x) {
            const int paper=std::max({up[x],down[x],row[std::max(0,x-offsetX)],row[std::min(w-1,x+offsetX)]});
            out[x]=row[x]<std::min(inkCeiling,paper-inkContrast);
        }
    }
    auto ink=[&](int x,int y) { return mask[size_t(y)*w+x]!=0; };

    // 8-connected components over horizontal runs.
    std::vector<Run> runs; runs.reserve(65536);
    std::vector<int> rowStart(size_t(h)+1,0);
    for(int y=0;y<h;++y) {
        rowStart[y]=int(runs.size());
        const auto* m=mask.data()+size_t(y)*w;
        for(int x=0;x<w;) {
            if(!m[x]) { ++x; continue; }
            const int left=x;
            while(x<w && m[x]) ++x;
            runs.push_back({left,x-1});
        }
        // Dense halftone/noise pages: give up on dotted rules rather than grow without bound.
        if(runs.size()>size_t(maxRuns)) return result;
    }
    rowStart[h]=int(runs.size());
    std::vector<int> parent(runs.size());
    for(size_t i=0;i<parent.size();++i) parent[i]=int(i);
    for(int y=1;y<h;++y) {
        int p=rowStart[y-1];
        for(int i=rowStart[y];i<rowStart[y+1];++i) {
            while(p<rowStart[y] && runs[p].right<runs[i].left-1) ++p;
            for(int q=p;q<rowStart[y] && runs[q].left<=runs[i].right+1;++q) {
                const int a=findRoot(parent,i),b=findRoot(parent,q);
                if(a!=b) parent[std::max(a,b)]=std::min(a,b);
            }
        }
    }
    std::vector<int> label(runs.size());
    std::vector<Component> components;
    {
        std::vector<int> index(runs.size(),-1);
        for(int y=0;y<h;++y) for(int i=rowStart[y];i<rowStart[y+1];++i) {
            const int r=findRoot(parent,i);
            if(index[r]<0) { index[r]=int(components.size()); components.push_back({}); }
            auto& c=components[index[r]]; label[i]=index[r];
            c.left=std::min(c.left,runs[i].left); c.right=std::max(c.right,runs[i].right);
            c.top=std::min(c.top,y); c.bottom=std::max(c.bottom,y);
        }
    }
    parent.clear(); parent.shrink_to_fit();

    const int thin=std::max(2,int(std::ceil(thinMarkPoints*sy)));
    std::vector<Mark> marks;
    for(const auto& c:components)
        if(c.height()<=thin) marks.push_back({c.left,c.right,c.top,c.bottom,(c.top+c.bottom)/2.0});
    std::sort(marks.begin(),marks.end(),[](const Mark& a,const Mark& b) { return a.left<b.left; });

    // Link each mark to its nearest collinear right neighbour; keep the cheapest
    // incoming link so every mark belongs to at most one chain.
    const int maxGap=std::max(2,int(std::ceil(maxDashGapPoints*sx)));
    // Scan jitter plus a slope step can move adjacent marks by about two rows.
    const double maxRise=std::max(2.5,chainRisePoints*sy);
    std::vector<int> next(marks.size(),-1),previous(marks.size(),-1);
    std::vector<double> linkCost(marks.size(),1e9);
    for(size_t i=0;i<marks.size();++i) {
        const auto& a=marks[i];
        int best=-1; double bestCost=1e9;
        for(size_t j=i+1;j<marks.size() && marks[j].left<=a.right+maxGap+1;++j) {
            const auto& b=marks[j];
            const int gap=b.left-a.right-1;
            if(gap<-1) continue;
            const double rise=std::abs(b.center-a.center);
            if(rise>maxRise) continue;
            const int ha=a.bottom-a.top+1,hb=b.bottom-b.top+1;
            if(std::max(ha,hb)>2*std::min(ha,hb)+2) continue;
            const double cost=gap+3*rise;
            if(cost<bestCost) { bestCost=cost; best=int(j); }
        }
        if(best>=0 && bestCost<linkCost[best]) {
            if(previous[best]>=0) next[previous[best]]=-1;
            previous[best]=int(i); next[i]=best; linkCost[best]=bestCost;
        }
    }

    const int maxSmall=std::max(2,int(std::ceil(smallMarkPoints*sx)));
    const int minLength=std::max(12,int(std::ceil(24*sx)));
    const int above=std::max(4,int(std::ceil(dottedBlankAbove*sy)));
    const int below=std::max(2,int(std::ceil(dottedBlankBelow*sy)));
    const int stack=std::max(2,int(std::ceil(punctuationStackPoints*sy)));
    const int fieldPixels=int(std::ceil(dottedFieldHeight*sy));
    // A tall stroke within dash-gap distance of a chain end (table cell border).
    auto border=[&](int x0,int x1,double center) {
        x0=std::max(0,x0); x1=std::min(w-1,x1);
        const int row=int(std::lround(center));
        for(int y=std::max(0,row-1);y<=std::min(h-1,row+1);++y)
            for(int i=rowStart[y];i<rowStart[y+1];++i) {
                if(runs[i].right<x0 || runs[i].left>x1) continue;
                if(components[label[i]].height()>fieldPixels) return true;
            }
        return false;
    };
    auto gapAfter=[&](int a,int b) { return std::max(0,marks[b].left-marks[a].right-1); };
    struct Chain { int left,right; double top,leftCenter,rightCenter; };
    std::vector<Chain> chains;
    std::vector<int> chain;
    for(size_t start=0;start<marks.size();++start) {
        if(previous[start]>=0) continue;
        chain.clear();
        for(int i=int(start);i>=0;i=next[i]) chain.push_back(i);
        int inkWidth=0;
        for(int i:chain) inkWidth+=marks[i].width();
        const bool solid=inkWidth>=(marks[chain.back()].right-marks[chain.front()].left+1)*.9;
        if(solid) {
            // A continuous thin rule, e.g. fine dots blurred together at this resolution.
            if(marks[chain.back()].right-marks[chain.front()].left+1<minLength) continue;
        } else {
            if(chain.size()<3) continue;
            // Trim a colon/semicolon base or an end mark set apart by an irregular gap.
            std::vector<int> gaps;
            for(size_t k=1;k<chain.size();++k) gaps.push_back(gapAfter(chain[k-1],chain[k]));
            const int typical=median(gaps);
            auto capped=[&](const Mark& m) {
                for(int y=std::max(0,m.top-stack);y<m.top-1;++y)
                    for(int x=m.left;x<=m.right;++x) if(ink(x,y)) return true;
                return false;
            };
            auto apart=[&](int a,int b) { return gapAfter(a,b)>std::max(1.8*typical,typical+2.0); };
            while(chain.size()>=3 && (capped(marks[chain.front()]) || apart(chain[0],chain[1]))) chain.erase(chain.begin());
            while(chain.size()>=3 && (capped(marks[chain.back()]) || apart(chain[chain.size()-2],chain.back()))) chain.pop_back();
            const int length=marks[chain.back()].right-marks[chain.front()].left+1;
            std::vector<int> widths; std::vector<double> pitches;
            gaps.clear(); inkWidth=0;
            for(size_t k=0;k<chain.size();++k) {
                const auto& m=marks[chain[k]];
                inkWidth+=m.width();
                if(m.width()<=maxSmall) widths.push_back(m.width());
                if(!k) continue;
                const auto& p=marks[chain[k-1]];
                gaps.push_back(gapAfter(chain[k-1],chain[k]));
                if(m.width()<=maxSmall && p.width()<=maxSmall) pitches.push_back((m.left+m.right-p.left-p.right)/2.0);
            }
            if(widths.size()<3 || pitches.empty()) continue;
            const int markWidth=median(widths);
            const int required=markWidth>=2*sx?3:minMarks;
            if(int(widths.size())<required || length<minLength || inkWidth<length*.1) continue;
            // Printed rules repeat one mark at a steady pitch, or at least a steady
            // gap where scanned dots have merged unevenly; speckles do not.
            const double pitch=median(pitches);
            const int gap=median(gaps);
            int steadyPitch=0,steadyGap=0,uniform=0;
            for(double p:pitches) steadyPitch+=p>=std::min(pitch*.6,pitch-1.5) && p<=std::max(pitch*1.6,pitch+1.5);
            for(int g:gaps) steadyGap+=g>=std::min(gap*.5,gap-1.5) && g<=std::max(gap*1.8,gap+1.5);
            for(int v:widths) uniform+=v>=markWidth*.5-1 && v<=markWidth*4+1;
            const bool periodic=steadyPitch>=pitches.size()*.7 && uniform>=widths.size()*.7;
            if(!periodic && steadyGap<gaps.size()*.9) continue;
            int irregular=0;
            for(int g:gaps) irregular+=g>std::max(2.5*gap,gap+2.0);
            if(irregular>std::max<qsizetype>(1,qsizetype(chain.size())/6)) continue;
        }
        // Writing space above and clear paper below each mark's own baseline.
        int occupied=0,total=0,under=0,underTotal=0;
        for(size_t k=0;k<chain.size();++k) {
            const auto& m=marks[chain[k]];
            const int end=k+1<chain.size()?marks[chain[k+1]].left:m.right+1;
            for(int x=m.left;x<end;++x) {
                for(int y=m.top-above;y<m.top-1;++y) { ++total; if(y>=0 && ink(x,y)) ++occupied; }
                for(int y=m.bottom+2;y<=m.bottom+below;++y) { ++underTotal; if(y<h && ink(x,y)) ++under; }
            }
        }
        if(occupied>std::max(2,total*3/100) || under>std::max(2,underTotal*5/100)) continue;
        // Printed marks stand out from the paper next to them; textured tint at a
        // panel edge only crosses the ink threshold by a few levels.
        std::vector<int> contrast;
        const int step=std::max(1,int(std::ceil(sx)));
        for(int i:chain) {
            const auto& m=marks[i];
            for(int x=m.left;x<=m.right;x+=step) {
                int dark=255;
                for(int y=m.top;y<=m.bottom;++y) dark=std::min<int>(dark,image.constScanLine(y)[x]);
                const int paper=std::min(image.constScanLine(std::max(0,m.top-2))[x],image.constScanLine(std::min(h-1,m.bottom+2))[x]);
                contrast.push_back(paper-dark);
            }
        }
        if(contrast.empty() || median(contrast)<minimumMarkContrast) continue;
        const auto& first=marks[chain.front()];
        const auto& last=marks[chain.back()];
        if(border(first.left-maxGap-1,first.left-1,first.center) && border(last.right+1,last.right+maxGap+1,last.center)) continue;
        double top=1e9;
        for(int i:chain) top=std::min(top,marks[i].center);
        chains.push_back({first.left,last.right,top,first.center,last.center});
    }
    // Faint rules (see faintContrast): a band slightly darker than flat paper just
    // above and below it, continuous apart from dash-sized gaps. Strokes taller
    // than a mark (text, table borders, panel outlines) crossing the band split it.
    {
        const int off=std::max(2,int(std::ceil(faintOffsetPoints*sy)));
        const int fringe=std::max(1,int(std::ceil(0.8*sx)));
        // A colon's dots can sit ~4.5 pt apart; rules keep ~7 pt clear above anyway.
        const int faintStack=std::max(stack,int(std::ceil(6*sy)));
        std::vector<quint8> hit(static_cast<size_t>(w)),blocked(static_cast<size_t>(w));
        for(int y=off;y+1+off<h;++y) {
            const auto* r0=image.constScanLine(y);
            const auto* r1=image.constScanLine(y+1);
            const auto* up=image.constScanLine(y-off);
            const auto* down=image.constScanLine(y+1+off);
            bool any=false;
            for(int x=0;x<w;++x) {
                // A rule is a valley: darker than the paper both above and below it.
                // Steps and gradients (panel edges, shadows) are darker on one side only.
                const int paper=std::min(up[x],down[x]);
                const int v=std::min(r0[x],r1[x]);
                hit[x]=paper>=faintPaperFloor && std::abs(int(up[x])-int(down[x]))<=faintFlatness && paper-v>=faintContrast;
                any=any||hit[x];
            }
            if(!any) continue;
            std::fill(blocked.begin(),blocked.end(),quint8(0));
            for(int yy=y;yy<=y+1;++yy)
                for(int i=rowStart[yy];i<rowStart[yy+1];++i)
                    if(components[label[i]].height()>thin)
                        std::fill(blocked.begin()+runs[i].left,blocked.begin()+runs[i].right+1,quint8(1));
            // Punctuation (a colon's base, i's dot) has ink just above it; a rule does not.
            // Check every column with ink or a hit in the band: a colon's base is not a
            // faint hit itself (its upper dot darkens the reference row) but still bounds it.
            for(int x=0;x<w;++x)
                if(!blocked[x] && (hit[x] || ink(x,y) || ink(x,y+1)))
                    for(int yy=std::max(0,y-faintStack);yy<y-1 && !blocked[x];++yy) blocked[x]=ink(x,yy);
            // Anti-aliased fringes beside a glyph or colon are faint too: widen blocks.
            for(int x=0;x<w;++x) if(blocked[x]==1)
                for(int d=1;d<=fringe;++d) {
                    if(x-d>=0 && !blocked[x-d]) blocked[x-d]=2;
                    if(x+d<w && !blocked[x+d]) blocked[x+d]=2;
                }
            for(int x=0;x<w;) {
                if(!hit[x] || blocked[x]) { ++x; continue; }
                int left=x,last=x,count=0,gap=0;
                for(;x<w && !blocked[x];++x) {
                    if(hit[x]) { last=x; ++count; gap=0; }
                    else if(++gap>maxGap) break;
                }
                const int length=last-left+1;
                if(length<minLength || count<length*faintFill) continue;
                bool smooth=true;
                for(const auto* paperRow:{up,down}) {
                    double sum=0,squares=0;
                    for(int xx=left;xx<=last;++xx) { sum+=paperRow[xx]; squares+=double(paperRow[xx])*paperRow[xx]; }
                    const double mean=sum/length;
                    smooth=smooth && std::sqrt(std::max(0.0,squares/length-mean*mean))<=faintPaperNoise;
                }
                if(!smooth) continue;
                int occupied=0,total=0,under=0,underTotal=0;
                for(int xx=left;xx<=last;++xx) {
                    for(int yy=y-above;yy<y-1;++yy) { ++total; if(yy>=0 && ink(xx,yy)) ++occupied; }
                    for(int yy=y+3;yy<=y+1+below;++yy) { ++underTotal; if(yy<h && ink(xx,yy)) ++under; }
                }
                if(occupied>std::max(2,total*3/100) || under>std::max(2,underTotal*5/100)) continue;
                if(border(left-maxGap-1,left-1,y+.5) && border(last+1,last+maxGap+1,y+.5)) continue;
                chains.push_back({left,last,y+.5,y+.5,y+.5});
            }
        }
    }
    // Join accepted pieces of one rule: overlapping detections (dot chain and faint
    // band) and pieces separated by a short blank stretch (a few dots lost in the
    // scan). Ink in the gap means a printed obstacle: keep apart.
    std::sort(chains.begin(),chains.end(),[](const Chain& a,const Chain& b) { return a.left<b.left; });
    for(size_t i=0;i<chains.size();++i) for(size_t j=i+1;j<chains.size();) {
        auto& a=chains[i];
        const auto& b=chains[j];
        const int gap=b.left-a.right-1;
        // Pieces split by a slope/jitter step that exceeded the per-mark link limit.
        bool join=gap<=2*maxGap && std::abs(b.leftCenter-a.rightCenter)<=2*maxRise;
        for(int x=a.right+1;join && x<b.left;++x) {
            const int row=int(std::lround((a.rightCenter+b.leftCenter)/2));
            for(int y=std::max(0,row-thin);join && y<=std::min(h-1,row+thin);++y) join=!ink(x,y);
        }
        if(join) {
            if(b.right>a.right) { a.right=b.right; a.rightCenter=b.rightCenter; }
            a.top=std::min(a.top,b.top);
            chains.erase(chains.begin()+j);
            j=i+1;
        } else ++j;
    }
    std::sort(chains.begin(),chains.end(),[](const Chain& a,const Chain& b) { return a.top<b.top || (a.top==b.top && a.left<b.left); });
    for(const auto& c:chains) {
        auto area=QRectF(double(c.left)/w,std::max(0.0,c.top/h-(dottedFieldHeight-3)/units.height()),
            double(c.right-c.left+1)/w,dottedFieldHeight/units.height()).intersected(QRectF(0,0,1,1));
        // A sloped rule is found as row pieces at slightly different heights; fields
        // that overlap mostly vertically and horizontally are one rule. Separate
        // stacked rules are at least a field height apart and never overlap.
        for(qsizetype i=0;i<result.size();) {
            const auto other=result[i];
            const double overlap=std::min(area.bottom(),other.bottom())-std::max(area.top(),other.top());
            if(overlap>=0.5*std::min(area.height(),other.height()) && area.left()<other.right() && other.left()<area.right()) {
                // Keep the standard field height (anchored at the higher piece): the
                // editor created from it must stay one line tall.
                const double top=std::min(area.top(),other.top());
                area=area.united(other); area.setTop(top); area.setHeight(dottedFieldHeight/units.height());
                result.removeAt(i); i=0;
            } else ++i;
        }
        result.append(area);
    }
    std::sort(result.begin(),result.end(),[](const QRectF& a,const QRectF& b) { return a.top()<b.top() || (a.top()==b.top() && a.left()<b.left()); });
    return result;
}
}

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
    // Solid dark rules.
    for(int y=above+thickness;y<h-2 && result.size()<maxLines && candidates<=2048;++y) {
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
            if(++candidates>2048) break;
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
    // Dotted rules; a solid rule already reported at the same baseline wins.
    // Same rule only: a form border a few points below a dotted field is not it.
    const double baseline=1.5/units.height();
    const auto solidCount=result.size();
    for(const auto& area:detectDotted(image,units)) {
        if(result.size()>=maxLines) break;
        bool duplicate=false;
        for(qsizetype i=0;i<solidCount && !duplicate;++i) {
            const auto& old=result[i];
            duplicate=std::abs((old.bottom()-6/units.height())-(area.bottom()-3/units.height()))<baseline
                && old.left()<area.right() && area.left()<old.right();
        }
        if(!duplicate) result.append(area);
    }
    return result;
}
