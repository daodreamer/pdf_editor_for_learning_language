#pragma once
#include "readingconfig.h"
#include <QAbstractListModel>
#include <QImage>
#include <QMap>
#include <QSet>
#include <QSizeF>
#include <QRectF>

class ReadingModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(double contentHeight READ contentHeight NOTIFY geometryChanged)
    Q_PROPERTY(double contentWidth READ contentWidth NOTIFY geometryChanged)
public:
    enum Role { PageNumber = Qt::UserRole + 1, Top, PageWidth, PageHeight, Pixels, Left };
    explicit ReadingModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(active_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void reset(int count);
    void setSizes(int first, const QList<QSizeF>& sizes);
    void setViewport(double y, double height, double width, double zoom);
    void setDoublePage(bool enabled);
    bool doublePage() const { return doublePage_; }
    void put(int page, QImage image, int requestedWidth, bool final, QList<QRectF> answerLines={}, bool linesReady=false);
    QList<QRectF> answerLines(int page) const;
    void setBudgets(qint64 cpu, qint64 gpu);
    QImage image(int page) const;
    bool ready(int page, int width) const;
    bool known(int page) const { return page >= 0 && page < known_.size() && known_[page]; }
    int nextUnknown() const;
    bool visible(int page) const { return page >= pageAt(y_) && page < std::min(count(),pageAt(y_+viewHeight_)+columns()); }
    int pageAt(double y) const;
    int pageAtPosition(double x,double y,bool nearest=false) const;
    double left(int page) const;
    double top(int page) const { return tops_.value(page, ReadingConfig::gap); }
    double height(int page) const { return pageWidth_ * ratio(page); }
    double ratio(int page) const;
    double contentHeight() const { return total_; }
    double pageWidth() const { return pageWidth_; }
    double contentWidth() const { return std::max(viewWidth_, columns()*pageWidth_ + (columns()-1)*ReadingConfig::gap + 2 * ReadingConfig::margin); }
    double offset() const { return y_; }
    int count() const { return int(sizes_.size()); }
    QList<int> priority() const;
    int targetWidth(int page, double dpr) const;
    qint64 cpuBytes() const;
    qint64 gpuBytes() const;
    qint64 pageBudget() const;
    void recordDemand(int page, int width);
    quint64 lookups = 0, exactHits = 0, degradedHits = 0, misses = 0, renderJobs = 0, evictions = 0;
    ReadingConfig config;
signals:
    void geometryChanged();
private:
    void rebuild(int anchor, double fraction);
    void activate(bool geometryChanged = false);
    void evict();
    int columns() const { return doublePage_ ? 2 : 1; }
    void updateWidth();
    double rowHeight(int page) const;
    struct Entry { QImage pixels; int requestedWidth = 0; bool final = false; quint64 use = 0; QList<QRectF> answerLines; };
    QList<QSizeF> sizes_;
    QList<bool> known_;
    QList<double> tops_;
    QList<double> rowTops_;
    QList<int> active_;
    QMap<int, Entry> cache_;
    quint64 tick_ = 0;
    bool doublePage_=false;
    double y_ = 0, viewHeight_ = 700, viewWidth_ = 1000, pageWidth_ = 960, zoom_ = 1, total_ = 0;
};
