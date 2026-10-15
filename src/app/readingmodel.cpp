#include "readingmodel.h"
#include <algorithm>
#include <cmath>

QHash<int, QByteArray> ReadingModel::roleNames() const {
    return {{PageNumber,"pageNumber"},{Top,"pageTop"},{PageWidth,"pageWidth"},{PageHeight,"pageHeight"},{Pixels,"pixels"}};
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
    if (tops_.isEmpty()) return 0;
    return std::clamp(int(std::upper_bound(tops_.begin(), tops_.end(), y) - tops_.begin()) - 1, 0, count() - 1);
}
void ReadingModel::rebuild(int anchor, double fraction) {
    double top = ReadingConfig::gap;
    tops_.resize(count());
    for (int p = 0; p < count(); ++p) { tops_[p] = top; top += height(p) + ReadingConfig::gap; }
    total_ = count() ? top : 0;
    y_ = std::clamp(this->top(anchor) + fraction * height(anchor), 0.0, std::max(0.0,total_ - viewHeight_));
    activate(true);
    emit geometryChanged();
}
void ReadingModel::setSizes(int first, const QList<QSizeF>& sizes) {
    const int anchor = pageAt(y_);
    const double fraction = (y_ - top(anchor)) / std::max(1.0,height(anchor));
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
    const double fraction = (y_ - top(anchor)) / std::max(1.0,height(anchor));
    viewHeight_ = std::max(1.0,h);
    const bool resized = viewWidth_ != w || zoom_ != zoom;
    viewWidth_ = std::max(1.0,w); zoom_ = std::clamp(zoom,0.25,4.0);
    pageWidth_ = std::max(1.0,viewWidth_ - 2*ReadingConfig::margin)*zoom_;
    if (resized) rebuild(anchor,fraction);
    else { y_ = std::clamp(y,0.0,std::max(0.0,total_-viewHeight_)); activate(); }
}
void ReadingModel::activate(bool geometryChanged) {
    QList<int> active;
    if (count()) {
        const int first = std::max(0,pageAt(y_) - ReadingConfig::neighbors);
        const int last = std::min(count()-1,pageAt(y_+viewHeight_) + ReadingConfig::neighbors);
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
        emit dataChanged(index(0),index(rowCount()-1),{Top,PageWidth,PageHeight});
    for (int p : active_) if (cache_.contains(p)) cache_[p].use = ++tick_;
    evict();
}
QList<int> ReadingModel::priority() const {
    auto pages = active_;
    const int first = pageAt(y_), last = pageAt(y_+viewHeight_);
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
void ReadingModel::put(int p,QImage image,int width,bool final) {
    if (image.isNull() || image.sizeInBytes() > pageBudget()) return;
    cache_[p] = {std::move(image),width,final,++tick_};
    evict();
    const int row = int(active_.indexOf(p));
    if (row >= 0) emit dataChanged(index(row),index(row),{Pixels});
}
qint64 ReadingModel::cpuBytes() const { qint64 n=0; for (const auto& e:cache_) n+=e.pixels.sizeInBytes(); return n; }
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
