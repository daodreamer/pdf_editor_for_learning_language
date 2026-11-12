#include "textselection.h"
#include "readingconfig.h"
#include <QClipboard>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>
#include <limits>

qint64 TextSelection::bytes() const {
    qint64 n=0;
    for (const auto& page : pages_) n += page->bytes();
    return n;
}
void TextSelection::reset() { pages_.clear(); clear(); }
void TextSelection::clear() {
    ++interactionRevision_;
    anchorPage_=endPage_=anchorIndex_=endIndex_=-1;
    extended_=complete_=false;
    wordPending_=false;
    text_.clear(); status_.clear(); notify();
}
void TextSelection::put(int page, TextSnapshot snapshot) {
    if (bytes()-(pages_.contains(page)?pages_[page]->bytes():0)+snapshot->bytes() > ReadingConfig::textBytes) {
        clear();
        auto failure=std::make_shared<TextPage>();
        failure->error=QStringLiteral("文字选择超过 16 MiB 预算，请缩小选区。");
        status_=failure->error;
        while (!pages_.isEmpty() && bytes()+failure->bytes()>ReadingConfig::textBytes) pages_.erase(pages_.begin());
        pages_.insert(page,std::move(failure));
        notify();
        return;
    }
    pages_.insert(page,std::move(snapshot));
    rebuild();
    if(wordPending_ && page==anchorPage_ && !pages_[page]->pending)expandWord();
}
QList<int> TextSelection::selectedPages() const {
    QList<int> result;
    if(anchorPage_<0 || endPage_<0)return result;
    for(int p=std::min(anchorPage_,endPage_);p<=std::max(anchorPage_,endPage_);++p)result.append(p);
    return result;
}
void TextSelection::clearOcr() {
    for(auto it=pages_.begin();it!=pages_.end();) {
        if(it.value()->ocr)it=pages_.erase(it);else ++it;
    }
    clear();
}
int TextSelection::nextPage(const QList<int>& active) {
    const int first=std::min(anchorPage_,endPage_), last=std::max(anchorPage_,endPage_);
    for (auto it=pages_.begin(); it!=pages_.end();) {
        if (!active.contains(it.key()) && !(first>=0 && it.key()>=first && it.key()<=last)) it=pages_.erase(it);
        else ++it;
    }
    if (first>=0) for (int p=first; p<=last; ++p) if (!pages_.contains(p)) return p;
    for (int p : active) if (!pages_.contains(p)) return p;
    return -1;
}
int TextSelection::hit(int page, QPointF point, bool nearest) const {
    const auto data=pages_.value(page);
    if (!data) return -1;
    double best=std::numeric_limits<double>::max(),bestCenter=best; int index=-1;
    for (int i=0; i<data->characters.size(); ++i) {
        const auto& c=data->characters[i];
        if (!c.visible || c.unicode==0 || c.unicode==U'\r' || c.unicode==U'\n') continue;
        if (!nearest && !c.box.adjusted(-tolerance_.x(),-tolerance_.y(),tolerance_.x(),tolerance_.y()).contains(point)) continue;
        const double dx=std::max({c.box.left()-point.x(),0.0,point.x()-c.box.right()});
        const double dy=std::max({c.box.top()-point.y(),0.0,point.y()-c.box.bottom()});
        const double distance=dx*dx+dy*dy;
        const auto delta=point-c.box.center();
        const double centerDistance=delta.x()*delta.x()+delta.y()*delta.y();
        if (distance<best || (distance==best && centerDistance<bestCenter)) { best=distance;bestCenter=centerDistance;index=i; }
    }
    return index;
}
void TextSelection::begin(int page,double x,double y,double tx,double ty) {
    clear();
    if (page<0 || !std::isfinite(x) || !std::isfinite(y)) return;
    anchorPage_=endPage_=page; anchor_=end_=QPointF(x,y); tolerance_=QPointF(tx,ty);
    rebuild(); emit demandChanged();
}
void TextSelection::extend(int page,double x,double y) {
    if (anchorPage_<0 || page<0 || !std::isfinite(x) || !std::isfinite(y)) return;
    if (extended_ && endPage_==page && end_==QPointF(x,y)) return;
    if (std::abs(page-anchorPage_)>=ReadingConfig::selectionPages) {
        clear(); status_=QStringLiteral("一次最多选择 128 页，请分段复制。"); notify(); return;
    }
    endPage_=page; end_=QPointF(x,y); extended_=true;
    wordPending_=false;
    ++interactionRevision_;
    rebuild(); emit demandChanged();
}
QPair<int,int> TextSelection::range(int page) const {
    if (anchorIndex_<0 || endIndex_<0 || !extended_) return {-1,-1};
    int firstPage=anchorPage_, lastPage=endPage_, first=anchorIndex_, last=endIndex_;
    if (firstPage>lastPage || (firstPage==lastPage && first>last)) {
        std::swap(firstPage,lastPage); std::swap(first,last);
    }
    if (page<firstPage || page>lastPage || !pages_.contains(page)) return {-1,-1};
    const auto& chars=pages_[page]->characters;
    int start=page==firstPage ? first : 0, finish=page==lastPage ? last : int(chars.size())-1;
    if (chars.isEmpty()) return {-1,-1};
    // PDFium may expand one glyph into several stream characters (ligature, surrogate pair,
    // combining sequence). Never return only part of a glyph sharing the same bounds.
    while(start>0 && chars[start].box==chars[start-1].box && chars[start].visible) --start;
    while(finish>=0 && finish+1<chars.size() && chars[finish].box==chars[finish+1].box && chars[finish].visible) ++finish;
    return {start,finish};
}
void TextSelection::rebuild() {
    text_.clear(); status_.clear(); complete_=false;
    if (anchorPage_<0) { notify(); return; }
    anchorIndex_=hit(anchorPage_,anchor_,false);
    endIndex_=hit(endPage_,end_,true);
    if (endIndex_<0 && endPage_!=anchorPage_ && pages_.contains(endPage_)) endIndex_=0;
    if (!pages_.contains(anchorPage_) || !pages_.contains(endPage_)) {
        status_=QStringLiteral("正在读取文字层…"); notify(); return;
    }
    if (!pages_[anchorPage_]->error.isEmpty()) { status_=pages_[anchorPage_]->error; notify(); return; }
    if (pages_[anchorPage_]->pending || pages_[endPage_]->pending) { status_=QStringLiteral("正在离线识别选区文字…"); notify(); return; }
    if (anchorIndex_<0) {
        status_=pages_[anchorPage_]->characters.isEmpty() ? QStringLiteral("此页未识别到文字，可调整 OCR 语言后重试。") : QStringLiteral("请从文字上开始拖选。");
        notify(); return;
    }
    if (!extended_) { notify(); return; }
    const int first=std::min(anchorPage_,endPage_), last=std::max(anchorPage_,endPage_);
    for (int p=first; p<=last; ++p) {
        const auto data=pages_.value(p);
        if (!data) { text_.clear(); status_=QStringLiteral("正在读取跨页选区…"); notify(); return; }
        if (!data->error.isEmpty()) { text_.clear(); status_=data->error; notify(); return; }
        if (data->pending) { text_.clear(); status_=QStringLiteral("正在离线识别跨页选区…"); notify(); return; }
        if (p>first) text_+=u'\n';
        const auto [start,finish]=range(p);
        for (int i=start; i>=0 && i<=finish && i<data->characters.size(); ++i) {
            const auto& c=data->characters[i];
            // PDFium inserts CR/LF with degenerate bounds; preserve them within the selected stream.
            if (c.unicode && (c.visible || c.unicode==U' ' || c.unicode==U'\r' || c.unicode==U'\n') && c.unicode<=0x10ffff)
                text_+=c.unicode<=0xffff ? QString(QChar(ushort(c.unicode))) : QString::fromUcs4(&c.unicode,1);
        }
    }
    text_.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));
    complete_=true;
    notify();
}
void TextSelection::copy() {
    if (ready()) QGuiApplication::clipboard()->setText(text_);
}
void TextSelection::expandWord() {
    const auto data=pages_.value(anchorPage_);
    if(anchorPage_>=0 && (!data || data->pending)) {wordPending_=true;return;}
    wordPending_=false;
    if(!data || anchorIndex_<0 || anchorIndex_>=data->characters.size())return;
    const auto wordCharacter=[](const TextCharacter& c) {
        return c.visible && (QChar::isLetterOrNumber(c.unicode) || c.unicode==U'\'' || c.unicode==U'’');
    };
    int first=anchorIndex_,last=first;
    if(!wordCharacter(data->characters[first]))return;
    while(first>0 && wordCharacter(data->characters[first-1]))--first;
    while(last+1<data->characters.size() && wordCharacter(data->characters[last+1]))++last;
    anchor_=data->characters[first].box.center();end_=data->characters[last].box.center();endPage_=anchorPage_;extended_=true;rebuild();
}
QString TextSelection::context() const {
    const auto data=pages_.value(anchorPage_);if(!data||anchorIndex_<0||anchorIndex_>=data->characters.size())return text_;
    const auto boundary=[](char32_t c){return c==U'\r'||c==U'\n';};
    int first=anchorIndex_,last=anchorIndex_;
    while(first>0&&anchorIndex_-first<1000&&!boundary(data->characters[first-1].unicode))--first;
    while(last+1<data->characters.size()&&last-anchorIndex_<1000&&!boundary(data->characters[last+1].unicode))++last;
    QString result;for(int i=first;i<=last;++i){const auto& c=data->characters[i];if((c.visible||c.unicode==U' ')&&c.unicode&&c.unicode<=0x10ffff)result+=QString::fromUcs4(&c.unicode,1);}
    return result;
}
namespace {
bool sameLine(const QRectF& a, const QRectF& b) {
    const double overlap=std::min(a.bottom(),b.bottom())-std::max(a.top(),b.top());
    if (overlap<0.5*std::min(a.height(),b.height())) return false;
    const double lineHeight=std::max(a.height(),b.height());
    // Overlapping or nearby on the same baseline; a wide jump is a column gap.
    const double gap=std::max(a.left(),b.left())-std::min(a.right(),b.right());
    return gap<=3*lineHeight;
}
// Splits rectangles into a disjoint set covering the same area, so a translucent
// fill never paints a pixel twice (double-painted overlaps read as darker blotches).
QList<QRectF> disjoint(const QList<QRectF>& input) {
    QList<double> ys;
    for (const auto& r : input) { ys.append(r.top()); ys.append(r.bottom()); }
    std::sort(ys.begin(),ys.end());
    ys.erase(std::unique(ys.begin(),ys.end()),ys.end());
    QList<QRectF> output;
    QList<QPair<double,double>> spans;
    for (int i=0; i+1<ys.size(); ++i) {
        const double top=ys[i], bottom=ys[i+1];
        spans.clear();
        for (const auto& r : input) if (r.top()<=top && r.bottom()>=bottom) spans.append({r.left(),r.right()});
        std::sort(spans.begin(),spans.end());
        for (int j=0; j<spans.size();) {
            double left=spans[j].first, right=spans[j].second;
            for (++j; j<spans.size() && spans[j].first<=right; ++j) right=std::max(right,spans[j].second);
            // Extend a run from the slab directly above when it has identical edges.
            bool merged=false;
            for (auto& o : output) if (o.bottom()==top && o.left()==left && o.right()==right) { o.setBottom(bottom); merged=true; break; }
            if (!merged) output.append(QRectF(QPointF(left,top),QPointF(right,bottom)));
        }
    }
    return output;
}
}
QVariantList TextSelection::rectangles(int page) const {
    QVariantList result;
    const auto data=pages_.value(page);
    if (!data) return result;
    const auto [first,last]=range(page);
    // Paint one continuous band per text line, like a native text selection.
    // OCR glyph boxes are tight and separated (x-height letters, word spaces),
    // so per-glyph rectangles read as faint outlines rather than a selection.
    QList<QRectF> bands;
    QRectF band;
    auto flush=[&] { if (!band.isEmpty()) bands.append(band); band=QRectF(); };
    for (int i=first; i>=0 && i<=last && i<data->characters.size(); ++i) {
        const auto& c=data->characters[i];
        if (!c.visible) {
            if (c.unicode==U'\n' || c.unicode==U'\r') flush();
            continue;
        }
        if (band.isEmpty()) { band=c.box; continue; }
        const double lineHeight=std::max(band.height(),c.box.height());
        // Text running backwards or jumping a column gap starts a new band;
        // vertical/rotated text therefore keeps per-glyph boxes.
        if (sameLine(band,c.box) && c.box.left()>=band.right()-0.5*lineHeight) band=band.united(c.box);
        else { flush(); band=c.box; }
    }
    flush();
    // Fragments of one visual line (italic runs, font switches, reordered text
    // streams) become a single band with uniform height. Bands are ordered by
    // vertical center so each one is only compared with its vertical neighbours.
    const auto byCenter=[](const QRectF& a,const QRectF& b){return a.center().y()<b.center().y();};
    std::sort(bands.begin(),bands.end(),byCenter);
    QList<QRectF> lines;
    double tallest=0;
    for (const auto& b : bands) {
        tallest=std::max(tallest,b.height());
        bool merged=false;
        for (int k=int(lines.size())-1; k>=0 && lines[k].center().y()>=b.center().y()-tallest; --k)
            if (sameLine(lines[k],b)) { lines[k]=lines[k].united(b); merged=true; break; }
        if (!merged) lines.append(b);
    }
    tallest=0;
    for (auto& b : lines) {
        const double pad=b.height()*0.12; // ascender/descender breathing room
        b=b.adjusted(-pad*0.25,-pad,pad*0.25,pad).intersected(QRectF(0,0,1,1));
        tallest=std::max(tallest,b.height());
    }
    // Adjacent lines meet at the middle of their shared padding instead of overlapping;
    // the translucent fill would otherwise paint the overlap twice, visibly darker.
    std::sort(lines.begin(),lines.end(),byCenter);
    bool overlapping=false;
    for (int i=0; i<lines.size(); ++i)
        for (int j=i+1; j<lines.size() && lines[j].center().y()-lines[i].center().y()<=tallest; ++j) {
            auto& a=lines[i]; auto& b=lines[j];
            if (!a.intersects(b)) continue;
            const double mid=(b.top()+a.bottom())/2;
            if (a.center().y()<b.center().y() && mid>a.top() && mid<b.bottom()) { a.setBottom(mid); b.setTop(mid); }
            else overlapping=true; // same-height collision that a horizontal cut cannot separate
        }
    if (overlapping && lines.size()<=ReadingConfig::selectionDisjointBands) lines=disjoint(lines);
    bands=std::move(lines);
    for (const auto& b : bands) if (!b.isEmpty()) result.append(b);
    return result;
}
