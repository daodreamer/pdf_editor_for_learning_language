#include "annotationmodel.h"
#include <QUuid>
#include <QLineF>
#include <QSet>
#include <algorithm>
#include <cmath>

QHash<int,QByteArray> AnnotationModel::roleNames() const {
    return {{Id,"noteId"},{Page,"notePage"},{Anchor,"noteAnchor"},{BoxWidth,"noteWidth"},{BoxHeight,"noteHeight"},
        {FontSize,"noteFontSize"},{Text,"noteText"},{Family,"noteFamily"},{Color,"noteColor"},{PageUnits,"notePageUnits"},{Order,"noteOrder"}};
}
QVariantMap AnnotationModel::pageStats(int page) const {
    int answered=0,right=0,wrong=0,ink=0;
    for(const auto& n:notes_)if(n.page==page&&!n.text.isEmpty()){
        if(n.id.endsWith("-review")){right+=n.text==QStringLiteral("✓");wrong+=n.text==QStringLiteral("×");}else ++answered;
    }
    for(const auto& stroke:strokes_)if(stroke.page==page)++ink;
    return {{"answered",answered},{"right",right},{"wrong",wrong},{"ink",ink}};
}
QVariant AnnotationModel::data(const QModelIndex& index,int role) const {
    if (!index.isValid() || index.row()<0 || index.row()>=visible_.size()) return {};
    const auto& n=notes_[visible_[index.row()]];
    const auto g=geometry_.value(n.page);
    switch(role) {
    case Id: return n.id;
    case Page: return n.page;
    case Anchor: return g.pdfToDisplay.map(n.anchor);
    case BoxWidth: return n.width;
    case BoxHeight: return n.height;
    case FontSize: return n.fontSize;
    case Text: return n.text;
    case Family: return n.family;
    case Color: return n.color;
    case PageUnits: return g.units;
    case Order: return double(n.order);
    default: return {};
    }
}
void AnnotationModel::notify(const QString& id,const QList<int>& roles) {
    const auto row=visible_.indexOf(id);
    if(row>=0) emit dataChanged(index(int(row)),index(int(row)),roles);
}
void AnnotationModel::refreshRows() {
    QStringList wanted;
    for(const auto& n:notes_) if(active_.contains(n.page) && geometry_.contains(n.page)) wanted.append(n.id);
    for(int i=int(visible_.size())-1;i>=0;--i) if(!wanted.contains(visible_[i])) {
        beginRemoveRows({},i,i); visible_.removeAt(i); endRemoveRows();
    }
    for(const auto& id:wanted) if(!visible_.contains(id)) {
        const int row=int(visible_.size()); beginInsertRows({},row,row); visible_.append(id); endInsertRows();
    }
}
void AnnotationModel::reset() {
    cancelGesture();
    const auto oldPages=geometry_.keys();
    strokes_.clear(); nextInkOrder_=0; nextTextOrder_=0; clearInkSelection();
    beginResetModel(); notes_.clear(); visible_.clear(); geometry_.clear(); active_.clear(); selected_.clear();
    beforeEdit_={}; undo_.clear(); redo_.clear(); endResetModel(); emit selectionChanged(); emit historyChanged(); emit contentChanged();
    for(int page:oldPages) emit inkChanged(page);
}
void AnnotationModel::setGeometry(int page,const PageGeometry& geometry) {
    if(!geometry.valid) return;
    geometry_[page]=geometry;
    refreshRows();
}
void AnnotationModel::setActivePages(const QList<int>& pages) {
    if(!selected_.isEmpty() && !pages.contains(notes_.value(selected_).page)) finishEditing();
    active_=pages; refreshRows();
}
void AnnotationModel::record(TextAnnotation before,TextAnnotation after) {
    if(before==after) return;
    record(Command{std::move(before),std::move(after),{}, {}});
}
QString AnnotationModel::create(int page,double x,double y) {
    if(!geometry_.contains(page) || !std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=1 || y>=1) return {};
    cancelGesture(); clearInkSelection(); finishEditing();
    // Leave room for a usable editor at the right/bottom edge.
    const auto g=geometry_[page];
    x=std::min(x,std::max(0.0,1-48/g.units.width()));
    y=std::min(y,std::max(0.0,1-24/g.units.height()));
    TextAnnotation n;
    n.order=nextTextOrder_++;
    n.id=QUuid::createUuid().toString(QUuid::WithoutBraces); n.page=page;
    n.anchor=g.displayToPdf.map(QPointF(x,y)); n.right=g.right(); n.down=g.down();
    n.width=std::min(200.0,(1-x)*g.units.width());
    notes_.insert(n.id,n); refreshRows();
    // Creation and its first text session form one history transaction.
    beforeEdit_={}; selected_=n.id; emit selectionChanged();
    return n.id;
}
void AnnotationModel::select(const QString& id) {
    if(id==selected_ || !notes_.contains(id)) return;
    cancelGesture(); clearInkSelection(); finishEditing(); selected_=id; beforeEdit_=notes_[id]; emit selectionChanged();
}
void AnnotationModel::finishEditing() {
    if(selected_.isEmpty()) return;
    const auto id=selected_;
    auto after=notes_.value(id);
    TextAnnotation linked;
    if(after.text.isEmpty()) { notes_.remove(id);linked=notes_.take(id+"-review");after={};emit contentChanged(); }
    selected_.clear();
    if(linked.id.isEmpty())record(beforeEdit_,after);else record(Command{beforeEdit_,after,{},{},linked,{}});
    beforeEdit_={};
    emit selectionChanged(); refreshRows();
}
void AnnotationModel::setText(const QString& id,const QString& text,double height) {
    if(id!=selected_ || !notes_.contains(id) || !std::isfinite(height)) return;
    auto& n=notes_[id]; n.text=text;
    const double h=std::max(24.0,height);
    if(n.height!=h) { n.height=h; notify(id,{BoxHeight}); }
    emit contentChanged();
    // No Text role notification: resetting the TextEdit would destroy its local undo/IME state.
}
void AnnotationModel::move(const QString& id,double x,double y) {
    if(!notes_.contains(id) || !std::isfinite(x) || !std::isfinite(y)) return;
    finishEditing();
    if(!notes_.contains(id)) return;
    auto& n=notes_[id]; const auto before=n; const auto g=geometry_[n.page];
    x=std::clamp(x,0.0,std::max(0.0,1-n.width/g.units.width()));
    y=std::clamp(y,0.0,std::max(0.0,1-std::min(n.height,g.units.height())/g.units.height()));
    n.anchor=g.displayToPdf.map(QPointF(x,y)); notify(id,{Anchor});
    const auto markBefore=notes_.value(id+"-review");auto markAfter=markBefore;
    if(!markBefore.id.isEmpty()){markAfter.anchor+=n.anchor-before.anchor;notes_[markAfter.id]=markAfter;notify(markAfter.id,{Anchor});}
    record(Command{before,n,{},{},markBefore,markAfter});
}
void AnnotationModel::removeSelected() {
    if(selected_.isEmpty()) return;
    const auto id=selected_; finishEditing();
    if(!notes_.contains(id)) return;
    const auto before=notes_.take(id);const auto mark=notes_.take(id+"-review");record(Command{before,{},{},{},mark,{}});refreshRows();
}
void AnnotationModel::cycleMark(const QString& id) {
    finishEditing();
    if(!notes_.contains(id) || id.endsWith("-review")) return;
    const auto source=notes_.value(id); const auto key=id+"-review";
    const auto before=notes_.value(key);
    if(before.text==QStringLiteral("×")) { notes_.remove(key);record(before,{});refreshRows();return; }
    auto after=source;after.id=key;after.order=nextTextOrder_++;after.width=24;after.height=24;after.fontSize=18;
    const auto g=geometry_.value(source.page);if(!g.valid)return;
    auto pos=g.pdfToDisplay.map(source.anchor);pos.setX(std::min(.96,pos.x()+source.width/g.units.width()));
    pos.setY(std::max(0.0,pos.y()-14/g.units.height()));after.anchor=g.displayToPdf.map(pos);
    after.text=before.id.isEmpty()?QStringLiteral("✓"):QStringLiteral("×");
    after.color=before.id.isEmpty()?QColor("#2fb457"):QColor("#ff3b30");
    notes_[key]=after;record(before,after);refreshRows();notify(key,{});
}
bool AnnotationModel::selectNext(const QString& id) {
    if(!notes_.contains(id))return false;
    const auto page=notes_.value(id).page;const auto g=geometry_.value(page);QList<TextAnnotation> answers;
    for(const auto& n:notes_)if(n.page==page&&!n.id.endsWith("-review"))answers.append(n);
    if(answers.size()<2)return false;
    std::sort(answers.begin(),answers.end(),[&g](const auto& a,const auto& b){const auto p=g.pdfToDisplay.map(a.anchor),q=g.pdfToDisplay.map(b.anchor);return p.y()!=q.y()?p.y()<q.y():p.x()<q.x();});
    for(int i=0;i<answers.size();++i)if(answers[i].id==id){select(answers[(i+1)%answers.size()].id);return true;}
    return false;
}
void AnnotationModel::apply(const TextAnnotation& from,const TextAnnotation& to) {
    if(!from.id.isEmpty()) notes_.remove(from.id);
    if(!to.id.isEmpty()) notes_[to.id]=to;
    refreshRows(); if(!to.id.isEmpty()) notify(to.id,{});
}
void AnnotationModel::undo() {
    cancelGesture(); clearInkSelection();
    finishEditing(); if(undo_.isEmpty()) return;
    const auto c=undo_.takeLast(); apply(c.after,c.before);apply(c.linkedAfter,c.linkedBefore);applyInk(c.inkAfter,c.inkBefore); redo_.append(c); trimHistory(); emit historyChanged(); emit contentChanged();
}
void AnnotationModel::redo() {
    cancelGesture(); clearInkSelection();
    finishEditing(); if(redo_.isEmpty()) return;
    const auto c=redo_.takeLast(); apply(c.before,c.after);apply(c.linkedBefore,c.linkedAfter);applyInk(c.inkBefore,c.inkAfter); undo_.append(c); trimHistory(); emit historyChanged(); emit contentChanged();
}


namespace {
bool validPoint(QPointF p) {
    return std::isfinite(p.x()) && std::isfinite(p.y()) && p.x()>=0 && p.y()>=0 && p.x()<=1 && p.y()<=1;
}
double pointDistance(QPointF p,QPointF a,QPointF b) {
    const auto d=b-a;
    const double length=QPointF::dotProduct(d,d);
    const double t=length>0 ? std::clamp(QPointF::dotProduct(p-a,d)/length,0.0,1.0) : 0;
    return QLineF(p,a+t*d).length();
}
double cross(QPointF a,QPointF b) { return a.x()*b.y()-a.y()*b.x(); }
double segmentDistance(QPointF a,QPointF b,QPointF c,QPointF d) {
    const double denominator=cross(b-a,d-c);
    if(std::abs(denominator)>1e-12) {
        const double t=cross(c-a,d-c)/denominator, u=cross(c-a,b-a)/denominator;
        if(t>=0 && t<=1 && u>=0 && u<=1) return 0;
    }
    return std::min({pointDistance(a,c,d),pointDistance(b,c,d),pointDistance(c,a,b),pointDistance(d,a,b)});
}
bool hits(const InkStroke& stroke,QPointF from,QPointF to,double radius) {
    for(qsizetype i=0;i<stroke.points.size();++i)
        if(segmentDistance(from,to,stroke.points[i],stroke.points[std::max(qsizetype(0),i-1)])<=radius+stroke.width/2) return true;
    return false;
}
qsizetype inkBytes(const QList<InkStroke>& strokes) {
    qsizetype bytes=strokes.capacity()*sizeof(InkStroke);
    for(const auto& s:strokes) bytes+=s.id.capacity()*2+s.points.capacity()*sizeof(QPointF);
    return bytes;
}
}
qsizetype AnnotationModel::Command::bytes() const {
    return (before.id.capacity()+after.id.capacity()+before.text.capacity()+after.text.capacity()
        +before.family.capacity()+after.family.capacity()+linkedBefore.id.capacity()+linkedAfter.id.capacity()
        +linkedBefore.text.capacity()+linkedAfter.text.capacity()+linkedBefore.family.capacity()+linkedAfter.family.capacity())*2+inkBytes(inkBefore)+inkBytes(inkAfter);
}
qsizetype AnnotationModel::historyBytes() const {
    qsizetype bytes=(undo_.capacity()+redo_.capacity())*sizeof(Command);
    for(const auto& c:undo_) bytes+=c.bytes();
    for(const auto& c:redo_) bytes+=c.bytes();
    return bytes;
}
void AnnotationModel::record(Command command) {
    redo_.clear(); undo_.append(std::move(command));
    trimHistory();
    emit historyChanged(); emit contentChanged();
}
void AnnotationModel::trimHistory() {
    undo_.squeeze(); redo_.squeeze();
    while(undo_.size()+redo_.size()>AnnotationConfig::maxHistoryCommands || historyBytes()>AnnotationConfig::historyBytes) {
        if(!undo_.isEmpty()) { undo_.removeFirst(); undo_.squeeze(); }
        else if(!redo_.isEmpty()) { redo_.removeFirst(); redo_.squeeze(); }
        else break;
    }
}
void AnnotationModel::clearInkSelection() {
    if(selectedInk_.isEmpty()) return;
    const int page=strokes_.value(selectedInk_).page;
    selectedInk_.clear(); emit inkSelectionChanged(); emit inkChanged(page);
}
QList<InkStroke> AnnotationModel::inkSnapshot() const {
    auto result=strokes_.values(); result.append(erased_);
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.order<b.order;});
    return result;
}
QList<InkStroke> AnnotationModel::displayStrokes(int page) const {
    QList<InkStroke> result;
    for(const auto& s:strokes_) if(s.page==page && !(gesture_==Gesture::Move && s.id==preview_.id)) result.append(s);
    if(!preview_.id.isEmpty() && preview_.page==page) result.append(preview_);
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.order<b.order;});
    return result;
}
void AnnotationModel::applyInk(const QList<InkStroke>& from,const QList<InkStroke>& to) {
    QSet<int> pages;
    for(const auto& s:from) { strokes_.remove(s.id); pages.insert(s.page); }
    for(const auto& s:to) { strokes_[s.id]=s; pages.insert(s.page); }
    for(int page:pages) emit inkChanged(page);
}
bool AnnotationModel::beginInk(int page,QPointF p,QColor color,double width) {
    cancelGesture(); finishEditing(); clearInkSelection();
    if(!geometry_.contains(page) || !validPoint(p) || !color.isValid() || !std::isfinite(width) || width<.25 || width>32) return false;
    gestureError_.clear(); emit gestureErrorChanged();
    color.setAlpha(255); // The MVP toolbar offers opaque ink, avoiding overlap alpha artifacts.
    preview_={QUuid::createUuid().toString(QUuid::WithoutBraces),page,{geometry_[page].displayToPdf.map(p)},color,width};
    preview_.order=nextInkOrder_++;
    gesture_=Gesture::Draw; gesturePage_=page;
    emit inkChanged(page); return true;
}
bool AnnotationModel::beginErase(int page,QPointF p,double radius) {
    cancelGesture(); finishEditing(); clearInkSelection();
    if(!geometry_.contains(page) || !validPoint(p) || !std::isfinite(radius) || radius<0) return false;
    gesture_=Gesture::Erase; gesturePage_=page; eraserRadius_=radius;
    lastPoint_=geometry_[page].displayToPdf.map(p);
    eraseSegment(lastPoint_,lastPoint_); return true;
}
bool AnnotationModel::beginMoveInk(int page,QPointF p,double radius) {
    cancelGesture(); finishEditing(); clearInkSelection();
    if(!geometry_.contains(page) || !validPoint(p) || !std::isfinite(radius) || radius<0) return false;
    const auto raw=geometry_[page].displayToPdf.map(p);
    const auto ordered=displayStrokes(page);
    for(auto it=ordered.crbegin();it!=ordered.crend();++it) {
        if(!hits(*it,raw,raw,radius)) continue;
        preview_=moveBefore_=*it; moveOrigin_=p;
        selectedInk_=it->id; gesture_=Gesture::Move; gesturePage_=page;
        emit inkSelectionChanged(); emit inkChanged(page); return true;
    }
    return false;
}
void AnnotationModel::eraseSegment(QPointF from,QPointF to) {
    bool changed=false;
    for(auto it=strokes_.begin();it!=strokes_.end();) {
        if(it->page==gesturePage_ && hits(*it,from,to,eraserRadius_)) {
            erased_.append(it.value()); it=strokes_.erase(it); changed=true;
        } else ++it;
    }
    if(changed) emit inkChanged(gesturePage_);
}
void AnnotationModel::appendInk(QPointF p) {
    if(gesture_==Gesture::None) return;
    if(!validPoint(p)) { cancelGesture(); return; }
    const auto g=geometry_[gesturePage_];
    const auto raw=g.displayToPdf.map(p);
    if(gesture_==Gesture::Draw) {
        if(QLineF(raw,preview_.points.last()).length()<.1) return;
        if(preview_.points.size()>=AnnotationConfig::maxStrokePoints) {
            cancelGesture(); gestureError_=QStringLiteral("笔迹超过 65536 点，已取消本次手势；请分段书写。");
            emit gestureErrorChanged(); return;
        }
        preview_.points.append(raw); emit inkChanged(gesturePage_);
    } else if(gesture_==Gesture::Erase) {
        eraseSegment(lastPoint_,raw); lastPoint_=raw;
    } else {
        // Clamp path centers to the page, keeping relative shape; the ink layer clips the edge.
        const auto normalized=g.pdfToDisplay.map(moveBefore_.points.first());
        double minX=normalized.x(),maxX=minX,minY=normalized.y(),maxY=minY;
        for(const auto& point:moveBefore_.points) {
            const auto n=g.pdfToDisplay.map(point);
            minX=std::min(minX,n.x()); maxX=std::max(maxX,n.x());
            minY=std::min(minY,n.y()); maxY=std::max(maxY,n.y());
        }
        const double dx=std::clamp(p.x()-moveOrigin_.x(),-minX,1-maxX);
        const double dy=std::clamp(p.y()-moveOrigin_.y(),-minY,1-maxY);
        const auto delta=g.displayToPdf.map(QPointF(dx,dy))-g.displayToPdf.map(QPointF());
        preview_.points=moveBefore_.points;
        for(auto& point:preview_.points) point+=delta;
        emit inkChanged(gesturePage_);
    }
}
void AnnotationModel::endInk() {
    if(gesture_==Gesture::None) return;
    const int page=gesturePage_;
    Command c;
    if(gesture_==Gesture::Draw) { strokes_[preview_.id]=preview_; c.inkAfter={preview_}; }
    else if(gesture_==Gesture::Erase) c.inkBefore=erased_;
    else if(preview_!=moveBefore_) { strokes_[preview_.id]=preview_; c.inkBefore={moveBefore_}; c.inkAfter={preview_}; }
    gesture_=Gesture::None; preview_={}; moveBefore_={}; erased_.clear(); gesturePage_=-1;
    if(!c.inkBefore.isEmpty() || !c.inkAfter.isEmpty()) record(std::move(c));
    emit inkChanged(page);
}
void AnnotationModel::cancelGesture() {
    if(gesture_==Gesture::None) return;
    const int page=gesturePage_;
    for(const auto& s:erased_) strokes_[s.id]=s;
    gesture_=Gesture::None; preview_={}; moveBefore_={}; erased_.clear(); gesturePage_=-1;
    emit inkChanged(page);
}
void AnnotationModel::removeInk() {
    cancelGesture();
    if(!strokes_.contains(selectedInk_)) return;
    const auto stroke=strokes_.value(selectedInk_); clearInkSelection(); strokes_.remove(stroke.id);
    Command c; c.inkBefore={stroke}; record(std::move(c)); emit inkChanged(stroke.page);
}

void AnnotationModel::restore(const AnnotationSnapshot& snapshot) {
    cancelGesture();
    beginResetModel(); notes_.clear(); strokes_.clear(); visible_.clear();
    selected_.clear(); beforeEdit_={}; undo_.clear(); redo_.clear(); nextInkOrder_=0;
    nextTextOrder_=0;
    for(const auto& n:snapshot.notes) { notes_[n.id]=n; nextTextOrder_=std::max(nextTextOrder_,n.order+1); }
    for(const auto& s:snapshot.ink) { strokes_[s.id]=s; nextInkOrder_=std::max(nextInkOrder_,s.order+1); }
    endResetModel(); refreshRows(); clearInkSelection();
    emit selectionChanged(); emit historyChanged();
    for(int page:geometry_.keys()) emit inkChanged(page);
}
