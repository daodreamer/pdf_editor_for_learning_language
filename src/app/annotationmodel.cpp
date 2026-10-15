#include "annotationmodel.h"
#include <QUuid>
#include <algorithm>
#include <cmath>

QHash<int,QByteArray> AnnotationModel::roleNames() const {
    return {{Id,"noteId"},{Page,"notePage"},{Anchor,"noteAnchor"},{BoxWidth,"noteWidth"},{BoxHeight,"noteHeight"},
        {FontSize,"noteFontSize"},{Text,"noteText"},{Family,"noteFamily"},{Color,"noteColor"},{PageUnits,"notePageUnits"}};
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
    beginResetModel(); notes_.clear(); visible_.clear(); geometry_.clear(); active_.clear(); selected_.clear();
    beforeEdit_={}; undo_.clear(); redo_.clear(); endResetModel(); emit selectionChanged(); emit historyChanged();
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
    undo_.append({std::move(before),std::move(after)});
    if(undo_.size()>256) undo_.removeFirst();
    redo_.clear(); emit historyChanged();
}
QString AnnotationModel::create(int page,double x,double y) {
    if(!geometry_.contains(page) || !std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=1 || y>=1) return {};
    finishEditing();
    // Leave room for a usable editor at the right/bottom edge.
    const auto g=geometry_[page];
    x=std::min(x,std::max(0.0,1-48/g.units.width()));
    y=std::min(y,std::max(0.0,1-24/g.units.height()));
    TextAnnotation n;
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
    finishEditing(); selected_=id; beforeEdit_=notes_[id]; emit selectionChanged();
}
void AnnotationModel::finishEditing() {
    if(selected_.isEmpty()) return;
    const auto id=selected_;
    auto after=notes_.value(id);
    if(after.text.isEmpty()) { notes_.remove(id); after={}; }
    selected_.clear(); record(beforeEdit_,after); beforeEdit_={};
    emit selectionChanged(); refreshRows();
}
void AnnotationModel::setText(const QString& id,const QString& text,double height) {
    if(id!=selected_ || !notes_.contains(id) || !std::isfinite(height)) return;
    auto& n=notes_[id]; n.text=text;
    const double h=std::max(24.0,height);
    if(n.height!=h) { n.height=h; notify(id,{BoxHeight}); }
    // No Text role notification: resetting the TextEdit would destroy its local undo/IME state.
}
void AnnotationModel::move(const QString& id,double x,double y) {
    if(!notes_.contains(id) || !std::isfinite(x) || !std::isfinite(y)) return;
    finishEditing();
    if(!notes_.contains(id)) return;
    auto& n=notes_[id]; const auto before=n; const auto g=geometry_[n.page];
    x=std::clamp(x,0.0,std::max(0.0,1-n.width/g.units.width()));
    y=std::clamp(y,0.0,std::max(0.0,1-std::min(n.height,g.units.height())/g.units.height()));
    n.anchor=g.displayToPdf.map(QPointF(x,y)); notify(id,{Anchor}); record(before,n);
}
void AnnotationModel::removeSelected() {
    if(selected_.isEmpty()) return;
    const auto id=selected_; finishEditing();
    if(!notes_.contains(id)) return;
    const auto before=notes_.take(id); record(before,{}); refreshRows();
}
void AnnotationModel::apply(const TextAnnotation& from,const TextAnnotation& to) {
    if(!from.id.isEmpty()) notes_.remove(from.id);
    if(!to.id.isEmpty()) notes_[to.id]=to;
    refreshRows(); if(!to.id.isEmpty()) notify(to.id,{});
}
void AnnotationModel::undo() {
    finishEditing(); if(undo_.isEmpty()) return;
    const auto c=undo_.takeLast(); apply(c.after,c.before); redo_.append(c); emit historyChanged();
}
void AnnotationModel::redo() {
    finishEditing(); if(redo_.isEmpty()) return;
    const auto c=redo_.takeLast(); apply(c.before,c.after); undo_.append(c); emit historyChanged();
}
