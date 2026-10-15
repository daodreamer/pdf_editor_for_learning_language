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
    anchorPage_=endPage_=anchorIndex_=endIndex_=-1;
    extended_=complete_=false;
    text_.clear(); status_.clear(); notify();
}
void TextSelection::put(int page, TextSnapshot snapshot) {
    if (bytes()+snapshot->bytes() > ReadingConfig::textBytes) {
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
    double best=std::numeric_limits<double>::max(); int index=-1;
    for (int i=0; i<data->characters.size(); ++i) {
        const auto& c=data->characters[i];
        if (!c.visible || c.unicode==0 || c.unicode==U'\r' || c.unicode==U'\n') continue;
        if (!nearest && !c.box.adjusted(-tolerance_.x(),-tolerance_.y(),tolerance_.x(),tolerance_.y()).contains(point)) continue;
        const double dx=std::max({c.box.left()-point.x(),0.0,point.x()-c.box.right()});
        const double dy=std::max({c.box.top()-point.y(),0.0,point.y()-c.box.bottom()});
        const double distance=dx*dx+dy*dy;
        if (distance<best) { best=distance; index=i; }
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
    if (anchorIndex_<0) {
        status_=pages_[anchorPage_]->characters.isEmpty() ? QStringLiteral("此页没有可选择的原生文字（未提供 OCR）。") : QStringLiteral("请从原生文字上开始拖选。");
        notify(); return;
    }
    if (!extended_) { notify(); return; }
    const int first=std::min(anchorPage_,endPage_), last=std::max(anchorPage_,endPage_);
    for (int p=first; p<=last; ++p) {
        const auto data=pages_.value(p);
        if (!data) { text_.clear(); status_=QStringLiteral("正在读取跨页选区…"); notify(); return; }
        if (!data->error.isEmpty()) { text_.clear(); status_=data->error; notify(); return; }
        if (p>first) text_+=u'\n';
        const auto [start,finish]=range(p);
        for (int i=start; i>=0 && i<=finish && i<data->characters.size(); ++i) {
            const auto& c=data->characters[i];
            // PDFium inserts CR/LF with degenerate bounds; preserve them within the selected stream.
            if (c.unicode && (c.visible || c.unicode==U'\r' || c.unicode==U'\n') && c.unicode<=0x10ffff)
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
QVariantList TextSelection::rectangles(int page) const {
    QVariantList result;
    const auto data=pages_.value(page);
    if (!data) return result;
    const auto [first,last]=range(page);
    QRectF pending;
    for (int i=first; i>=0 && i<=last && i<data->characters.size(); ++i) {
        const auto& c=data->characters[i];
        if (!c.visible) continue;
        // Merge touching glyph bounds only; preserve line/column gaps and intrinsic rotation.
        if (!pending.isEmpty() && pending.adjusted(-0.001,-0.001,0.001,0.001).intersects(c.box)) pending=pending.united(c.box);
        else { if (!pending.isEmpty()) result.append(pending); pending=c.box; }
    }
    if (!pending.isEmpty()) result.append(pending);
    return result;
}
