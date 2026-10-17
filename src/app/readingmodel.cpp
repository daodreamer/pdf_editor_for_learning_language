#include "readingmodel.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <QRectF>

QHash<int, QByteArray> ReadingModel::roleNames() const {
    return {{PageNumber,"pageNumber"},{Top,"pageTop"},{PageWidth,"pageWidth"},{PageHeight,"pageHeight"},{Pixels,"pixels"},{Left,"pageLeft"}};
}
QVariant ReadingModel::data(const QModelIndex& i, int role) const {
    if (!i.isValid() || i.row() < 0 || i.row() >= active_.size()) return {};
    const int p = active_[i.row()];
    switch (role) {
    case PageNumber: return p + 1;
    case Top: return top(p);
    case PageWidth: return pageWidth_;
    case PageHeight: return height(p);
    case Pixels: return image(p);
    case Left: return left(p);
    default: return {};
    }
}
double ReadingModel::ratio(int p) const {
    const auto size = sizes_.value(p, QSizeF(612,792));
    return size.height() / size.width();
}
void ReadingModel::reset(int count) {
    beginResetModel();
    sizes_.fill(QSizeF(612,792), count);
    known_.fill(false, count);
    tops_.clear(); active_.clear(); cache_.clear(); y_ = 0;
    lookups = exactHits = degradedHits = misses = renderJobs = evictions = 0;
    endResetModel();
    rebuild(0, 0);
}
int ReadingModel::pageAt(double y) const {
    if (rowTops_.isEmpty()) return 0;
    return std::clamp(int(std::upper_bound(rowTops_.begin(), rowTops_.end(), y) - rowTops_.begin()) - 1,0,int(rowTops_.size())-1)*columns();
}
double ReadingModel::left(int page) const {
    if(!doublePage_ || (page==count()-1 && page%2==0))return (contentWidth()-pageWidth_)/2;
    return (contentWidth()-2*pageWidth_-ReadingConfig::gap)/2+(page%2)*(pageWidth_+ReadingConfig::gap);
}
int ReadingModel::pageAtPosition(double x,double y,bool nearest) const {
    if(!count()||!std::isfinite(x)||!std::isfinite(y))return -1;
    const int first=pageAt(y),last=std::min(count()-1,first+columns()-1);
    int best=first;double distance=std::numeric_limits<double>::max();
    for(int p=first;p<=last;++p){
        const QRectF rect(left(p),top(p),pageWidth_,height(p));
        if(rect.contains(QPointF(x,y)))return p;
        const double dx=std::max({rect.left()-x,0.0,x-rect.right()}),dy=std::max({rect.top()-y,0.0,y-rect.bottom()});
        if(dx*dx+dy*dy<distance){distance=dx*dx+dy*dy;best=p;}
    }
    return nearest?best:-1;
}
void ReadingModel::updateWidth() { pageWidth_=std::max(1.0,(viewWidth_-2*ReadingConfig::margin-(columns()-1)*ReadingConfig::gap)/columns())*zoom_; }
double ReadingModel::rowHeight(int page) const {
    const int first=page/columns()*columns();double result=height(first);
    for(int p=first+1;p<std::min(count(),first+columns());++p)result=std::max(result,height(p));
    return result;
}
void ReadingModel::setDoublePage(bool enabled) {
    if(doublePage_==enabled)return;
    const int anchor=pageAt(y_);doublePage_=enabled;updateWidth();rebuild(anchor,0);
}
void ReadingModel::rebuild(int anchor, double fraction) {
    double top = ReadingConfig::gap;
    tops_.resize(count());
    rowTops_.clear();
    for(int p=0;p<count();p+=columns()) {
        rowTops_.append(top);double rowHeight=0;
        for(int i=p;i<std::min(count(),p+columns());++i){tops_[i]=top;rowHeight=std::max(rowHeight,height(i));}
        top+=rowHeight+ReadingConfig::gap;
    }
    total_ = count() ? top : 0;
    y_ = std::clamp(this->top(anchor) + fraction * rowHeight(anchor), 0.0, std::max(0.0,total_ - viewHeight_));
    activate(true);
    emit geometryChanged();
}
void ReadingModel::setSizes(int first, const QList<QSizeF>& sizes) {
    const int anchor = pageAt(y_);
    const double fraction = (y_ - top(anchor)) / std::max(1.0,rowHeight(anchor));
    bool changed = false;
    for (int i = 0; i < sizes.size() && first+i < count(); ++i) {
        const auto s = sizes[i];
        if (first+i < 0 || !std::isfinite(s.width()) || !std::isfinite(s.height()) || s.width() <= 0 || s.height() <= 0) continue;
        changed |= sizes_[first+i] != s;
        sizes_[first+i] = s; known_[first+i] = true;
    }
    if (changed) rebuild(anchor, fraction);
}
void ReadingModel::setViewport(double y, double h, double w, double zoom) {
    if (!std::isfinite(y) || !std::isfinite(h) || !std::isfinite(w) || !std::isfinite(zoom)) return;
    const int anchor = pageAt(y_);
    const double fraction = (y_ - top(anchor)) / std::max(1.0,rowHeight(anchor));
    viewHeight_ = std::max(1.0,h);
    const bool resized = viewWidth_ != w || zoom_ != zoom;
    viewWidth_ = std::max(1.0,w); zoom_ = std::clamp(zoom,0.25,4.0);
    updateWidth();
    if (resized) rebuild(anchor,fraction);
    else { y_ = std::clamp(y,0.0,std::max(0.0,total_-viewHeight_)); activate(); }
}
void ReadingModel::activate(bool geometryChanged) {
    QList<int> active;
    if (count()) {
        const int first = std::max(0,pageAt(y_) - columns()*ReadingConfig::neighbors);
        const int last = std::min(count()-1,pageAt(y_+viewHeight_) + columns()-1 + columns()*ReadingConfig::neighbors);
        for (int p=first;p<=last;++p) active.append(p);
    }
    // Preserve overlapping delegates and their textures during ordinary scrolling.
    while (!active_.isEmpty() && !active.contains(active_.first())) {
        beginRemoveRows({},0,0); active_.removeFirst(); endRemoveRows();
    }
    while (!active_.isEmpty() && !active.contains(active_.last())) {
        const int last = rowCount()-1;
        beginRemoveRows({},last,last); active_.removeLast(); endRemoveRows();
    }
    for (int p : active) if (!active_.contains(p)) {
        const int row = int(std::lower_bound(active_.begin(),active_.end(),p)-active_.begin());
        beginInsertRows({},row,row); active_.insert(row,p); endInsertRows();
    }
    for (int p : active_) {
        auto it=cache_.find(p);
        if (it!=cache_.end() && it->pixels.sizeInBytes()>pageBudget()) {
            cache_.erase(it);
            const int row=int(active_.indexOf(p));
            emit dataChanged(index(row),index(row),{Pixels});
        }
    }
    if (geometryChanged && !active_.isEmpty())
        emit dataChanged(index(0),index(rowCount()-1),{Top,PageWidth,PageHeight,Left});
    for (int p : active_) if (cache_.contains(p)) cache_[p].use = ++tick_;
    evict();
}
QList<int> ReadingModel::priority() const {
    auto pages = active_;
    const int first = pageAt(y_), last = std::min(count()-1,pageAt(y_+viewHeight_)+columns()-1);
    std::stable_sort(pages.begin(),pages.end(),[=](int a,int b) {
        const auto distance = [=](int p) { return p < first ? first-p : p > last ? p-last : 0; };
        return distance(a) < distance(b);
    });
    return pages;
}
qint64 ReadingModel::pageBudget() const { return std::max(qint64(4), config.gpuBytes / std::max(qint64(1),qint64(active_.size()))); }
int ReadingModel::targetWidth(int page, double dpr) const {
    const double r = ratio(page);
    const double cap = std::min({double(ReadingConfig::maxDimension), ReadingConfig::maxDimension/r,
        std::sqrt(double(pageBudget())/(4*r))});
    int width = std::max(1,int(std::min(std::ceil(pageWidth_*dpr),std::floor(cap))));
    while (width > 1 && qint64(width)*qint64(std::ceil(width*r))*4 > pageBudget()) --width;
    return width;
}
QImage ReadingModel::image(int p) const {
    const auto it = cache_.constFind(p);
    if (it == cache_.cend() || it->pixels.sizeInBytes() > pageBudget()) return {};
    return it->pixels;
}
bool ReadingModel::ready(int p,int width) const {
    const auto it = cache_.constFind(p);
    return it != cache_.cend() && it->final && it->requestedWidth == width && !image(p).isNull();
}
QList<QRectF> ReadingModel::answerLines(int p) const {
    const auto it=cache_.constFind(p);
    return it==cache_.cend()?QList<QRectF>{}:it->answerLines;
}
void ReadingModel::put(int p,QImage image,int width,bool final,QList<QRectF> lines,bool linesReady) {
    if (image.isNull() || image.sizeInBytes() > pageBudget()) return;
    if(!linesReady) lines=answerLines(p);
    const auto cap=config.cpuBytes/(2*std::max(qsizetype(1),active_.size())*qint64(sizeof(QRectF)));
    if(lines.size()>cap) lines.resize(qsizetype(cap));
    lines.squeeze();
    cache_[p] = {std::move(image),width,final,++tick_,std::move(lines)};
    evict();
    const int row = int(active_.indexOf(p));
    if (row >= 0) emit dataChanged(index(row),index(row),{Pixels});
}
qint64 ReadingModel::cpuBytes() const { qint64 n=0; for (const auto& e:cache_) n+=e.pixels.sizeInBytes()+e.answerLines.capacity()*qint64(sizeof(QRectF)); return n; }
qint64 ReadingModel::gpuBytes() const { qint64 n=0; for(int p:active_) n+=image(p).sizeInBytes(); return n; }
void ReadingModel::evict() {
    while (cpuBytes() > config.cpuBytes && !cache_.isEmpty()) {
        auto victim = cache_.end();
        for (auto it=cache_.begin();it!=cache_.end();++it) {
            if (active_.contains(it.key())) continue;
            if (victim==cache_.end() || it->use < victim->use) victim=it;
        }
        if (victim==cache_.end()) break; // pin bytes <= GPU budget <= CPU budget / 2
        cache_.erase(victim); ++evictions;
    }
}
void ReadingModel::setBudgets(qint64 cpu,qint64 gpu) {
    beginResetModel(); cache_.clear(); endResetModel();
    config.cpuBytes=std::max(qint64(64*1024),cpu);
    config.gpuBytes=std::clamp(gpu,qint64(32*1024),config.cpuBytes/2);
}
int ReadingModel::nextUnknown() const {
    for(int p=0;p<count();++p) if(!known_[p]) return p;
    return -1;
}

void ReadingModel::recordDemand(int page,int width) {
    ++lookups;
    if (ready(page,width)) ++exactHits;
    else if (!image(page).isNull()) ++degradedHits;
    else ++misses;
}
