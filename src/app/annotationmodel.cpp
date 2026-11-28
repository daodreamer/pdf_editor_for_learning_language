#include "annotationmodel.h"
#include "textstyles.h"
#include "inkpath.h"
#include "answerfonts.h"
#include <QUuid>
#include <QLineF>
#include <QSet>
#include <QFontMetricsF>
#include <QTextDocument>
#include <QTextOption>
#include <algorithm>
#include <cmath>

QHash<int,QByteArray> AnnotationModel::roleNames() const {
    return {{Id,"noteId"},{Page,"notePage"},{Anchor,"noteAnchor"},{BoxWidth,"noteWidth"},{BoxHeight,"noteHeight"},
        {FontSize,"noteFontSize"},{Text,"noteText"},{Family,"noteFamily"},{Color,"noteColor"},{PageUnits,"notePageUnits"},{Order,"noteOrder"},{WidthLocked,"noteWidthLocked"},{Styles,"noteStyles"},{LineSpacing,"noteLineSpacing"}};
}
namespace {
template<class Notes,class Strokes>
QVariantMap answerStats(const Notes& notes,const Strokes& strokes,int page) {
    int answered=0,right=0,wrong=0,ink=0;
    for(const auto& n:notes)if(n.page==page&&!n.text.isEmpty()){
        if(n.id.endsWith("-review")){right+=n.text==QStringLiteral("✓");wrong+=n.text==QStringLiteral("×");}else ++answered;
    }
    for(const auto& stroke:strokes)if(stroke.page==page)++ink;
    return {{"answered",answered},{"right",right},{"wrong",wrong},{"ink",ink}};
}
}
QVariantMap AnnotationModel::pageStats(int page) const {
    auto stats=answerStats(notes_,strokes_,page);
    stats.insert("hidden",hiddenCount(page));
    return stats;
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
    case WidthLocked: return n.widthLocked;
    case Styles: return encodeTextStyles(n.styles);
    case LineSpacing: return n.lineSpacing;
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
    for(const auto& n:notes_) if(active_.contains(n.page) && geometry_.contains(n.page) && !noteHidden(n)) wanted.append(n.id);
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
    veils_.clear(); bookHidden_=false; rounds_.clear(); overlay_.clear(); lastArchived_={};
    beginResetModel(); notes_.clear(); visible_.clear(); geometry_.clear(); active_.clear(); selected_.clear();
    beforeEdit_={}; undo_.clear(); redo_.clear(); endResetModel(); emit selectionChanged(); emit historyChanged(); emit contentChanged();
    practiceTouched();
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
static double lineHeight(const QString& family,double size) {
    const auto font=AnswerFonts::font(family,size);
    return std::ceil(QFontMetricsF(font).height());
}
void AnnotationModel::setDefaultFontSize(double size) {
    if(!std::isfinite(size)) return;
    size=std::clamp(std::round(size),AnnotationConfig::minTextSize,AnnotationConfig::maxTextSize);
    if(size==defaultFontSize_) return;
    defaultFontSize_=size; emit defaultFontSizeChanged();
}
void AnnotationModel::setDefaultTextColor(QColor color) {
    if(!color.isValid()) return;
    color.setAlpha(255);
    if(color==defaultTextColor_) return;
    defaultTextColor_=color; emit defaultTextStyleChanged();
}
double AnnotationModel::defaultTextHeight() const { return lineHeight(AnswerFonts::learning,defaultFontSize_); }
QFont AnnotationModel::answerFont(const QString& family,double size) const { return AnswerFonts::font(family,size); }
QString AnnotationModel::create(int page,double x,double y,double maximumWidth,double fieldHeight,double lineSpacing,int lines) {
    if(!geometry_.contains(page) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(maximumWidth) || !std::isfinite(fieldHeight) || fieldHeight<0 || maximumWidth<0 || x<0 || y<0 || x>=1 || y>=1) return {};
    if(!std::isfinite(lineSpacing) || lineSpacing<0 || lines<1 || lines>64 || (lines>1 && !(lineSpacing>0))) return {};
    cancelGesture(); clearInkSelection(); finishEditing();
    archiveHiddenBeforeEdit(page);
    // Leave room for a usable editor at the right/bottom edge.
    const auto g=geometry_[page];
    x=std::min(x,std::max(0.0,1-(maximumWidth>0?std::min(48.0,maximumWidth):48)/g.units.width()));
    TextAnnotation n;
    n.family=AnswerFonts::learning;
    n.fontSize=defaultFontSize_;
    n.color=defaultTextColor_;
    // 0 = a plain answer: exactly one line of the default font tall.
    const bool plain=fieldHeight==0;
    if(plain) fieldHeight=lineHeight(n.family,n.fontSize);
    y=std::min(y,std::max(0.0,1-fieldHeight/g.units.height()));
    n.order=nextTextOrder_++;
    n.id=QUuid::createUuid().toString(QUuid::WithoutBraces); n.page=page;
    n.anchor=g.displayToPdf.map(QPointF(x,y)); n.right=g.right(); n.down=g.down();
    if(plain) n.minimumHeight=n.height=fieldHeight;
    else if(fieldHeight<24) {
        n.fontSize=std::min(n.fontSize,std::clamp(double(qRound(fieldHeight*.62)),8.0,14.0));
        n.minimumHeight=n.height=fieldHeight;
    }
    if(lines>1) {
        // One answer covering every rule of the block; lines advance by the rule pitch.
        n.lineSpacing=lineSpacing;
        n.minimumHeight=n.height=fieldHeight+(lines-1)*lineSpacing;
    }
    const auto font=AnswerFonts::font(n.family,n.fontSize);
    n.width=std::min(std::ceil(QFontMetricsF(font).horizontalAdvance(QStringLiteral("this")))+2,(1-x)*g.units.width());
    n.widthLocked=false;
    n.maximumWidth=maximumWidth;
    if(maximumWidth>0) n.width=std::min(maximumWidth,(1-x)*g.units.width());
    notes_.insert(n.id,n); refreshRows();
    // Creation and its first text session form one history transaction.
    beforeEdit_={}; selected_=n.id; emit selectionChanged();
    return n.id;
}
double AnnotationModel::textWidthLimit(const QString& id) const {
    const auto n=notes_.value(id); const auto g=geometry_.value(n.page);
    if(!g.valid) return 0;
    const double available=(1-g.pdfToDisplay.map(n.anchor).x())*g.units.width();
    return n.maximumWidth>0?std::min(available,n.maximumWidth):available;
}
void AnnotationModel::select(const QString& id) {
    if(id==selected_ || !notes_.contains(id) || noteHidden(notes_.value(id))) return;
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
void AnnotationModel::setText(const QString& id,const QString& text,double height,const QVariantList& styles) {
    if(id!=selected_ || !notes_.contains(id)) return;
    setEditorText(id,text,height,styles);
    notify(id,{Text,Styles});
}
void AnnotationModel::setEditorText(const QString& id,const QString& text,double height,const QVariantList& styles) {
    if(id!=selected_ || !notes_.contains(id) || !std::isfinite(height)) return;
    auto& n=notes_[id];
    QList<TextStyle> decoded; if(!decodeTextStyles(styles,int(text.size()),decoded)) return;
    n.styles=std::move(decoded);
    QList<int> changedRoles;
    // A deletion must also release boxes locked by older automatic sizing or resizing.
    // Keep the former width as a wrap limit, while fitting the visible frame to the text.
    if(text.size()<n.text.size()) {
        if(n.widthLocked) {
            if(n.maximumWidth==0) n.maximumWidth=n.width;
            n.widthLocked=false; changedRoles.append(WidthLocked);
        }
        if(!(n.lineSpacing>0)) n.minimumHeight=lineHeight(n.family,n.fontSize);
    }
    const auto font=AnswerFonts::font(n.family,n.fontSize);
    QTextDocument layout; layout.setDocumentMargin(0); layout.setDefaultFont(font);
    QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setDefaultTextOption(option); layout.setPlainText(text); applyTextStyles(layout,n.styles); applyLineSpacing(layout,n.lineSpacing);
    if(!n.widthLocked) {
        const auto g=geometry_.value(n.page);
        double available=(1-g.pdfToDisplay.map(n.anchor).x())*g.units.width();
        if(n.maximumWidth>0) available=std::min(available,n.maximumWidth);
        layout.setTextWidth(-1);
        const double natural=text.isEmpty()?(n.text.isEmpty()?n.width:24.0):std::max(4.0,std::ceil(layout.idealWidth())+2);
        const double width=std::min(natural,available);
        if(width!=n.width) { n.width=width; changedRoles.append(BoxWidth); }
    }
    n.text=text;
    layout.setTextWidth(n.width);
    const double h=std::max(n.minimumHeight,layout.size().height());
    if(n.height!=h) { n.height=h; changedRoles.append(BoxHeight); }
    // Publish after all fields are updated: relayout can synchronously call setText again.
    if(!changedRoles.isEmpty()) notify(id,changedRoles);
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
void AnnotationModel::resize(const QString& id,double x,double y,double width,double height) {
    if(!notes_.contains(id) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height)) return;
    finishEditing();
    if(!notes_.contains(id)) return;
    const auto g=geometry_.value(notes_[id].page);
    if(!g.valid) return;
    cancelGesture();
    auto& n=notes_[id]; const auto before=n;
    n.width=std::clamp(width,std::min(24.0,g.units.width()),g.units.width());
    n.widthLocked=true;
    n.maximumWidth=0;
    n.minimumHeight=std::clamp(height,std::min(24.0,g.units.height()),g.units.height());
    const auto font=AnswerFonts::font(n.family,n.fontSize);
    QTextDocument layout; layout.setDocumentMargin(0); layout.setDefaultFont(font);
    QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setDefaultTextOption(option); layout.setPlainText(n.text); layout.setTextWidth(n.width);
    applyTextStyles(layout,n.styles); applyLineSpacing(layout,n.lineSpacing);
    n.height=std::min(g.units.height(),std::max(n.minimumHeight,layout.size().height()));
    x=std::clamp(x,0.0,std::max(0.0,1-n.width/g.units.width()));
    y=std::clamp(y,0.0,std::max(0.0,1-n.height/g.units.height()));
    n.anchor=g.displayToPdf.map(QPointF(x,y));
    const auto markBefore=notes_.value(id+"-review"); auto markAfter=markBefore;
    if(!markBefore.id.isEmpty()) {
        markAfter.anchor+=n.anchor-before.anchor; notes_[markAfter.id]=markAfter; notify(markAfter.id,{Anchor});
    }
    notify(id,{Anchor,BoxWidth,BoxHeight,WidthLocked});
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
    auto after=source;after.id=key;after.order=nextTextOrder_++;after.width=24;after.height=24;after.fontSize=18;after.styles.clear();
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
    for(const auto& n:notes_)if(n.page==page&&!n.id.endsWith("-review")&&!noteHidden(n))answers.append(n);
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
    const auto c=undo_.takeLast(); runCommand(c,false); revealTouched(c); redo_.append(c); trimHistory(); emit historyChanged(); emit contentChanged();
}
void AnnotationModel::redo() {
    cancelGesture(); clearInkSelection();
    finishEditing(); if(redo_.isEmpty()) return;
    const auto c=redo_.takeLast(); runCommand(c,true); revealTouched(c); undo_.append(c); trimHistory(); emit historyChanged(); emit contentChanged();
}
// Rounds move whole item sets between the current round and the archive. Forward order:
// archive, delete, single-item edits, then restored copies; backward is the exact reverse.
void AnnotationModel::runCommand(const Command& c,bool forward) {
    QSet<int> pages;
    auto take=[&](const PracticeRound& r) {
        for(const auto& n:r.notes) notes_.remove(n.id);
        for(const auto& i:r.ink) strokes_.remove(i.id);
        pages.insert(r.page);
    };
    auto putBack=[&](const PracticeRound& r) {
        for(const auto& n:r.notes) notes_[n.id]=n;
        for(const auto& i:r.ink) strokes_[i.id]=i;
        pages.insert(r.page);
    };
    auto dropRound=[&](const QString& id) {
        for(qsizetype k=0;k<rounds_.size();++k) if(rounds_[k].id==id) { rounds_.removeAt(k); break; }
        for(auto it=overlay_.begin();it!=overlay_.end();) { if(it.value()==id) { pages.insert(it.key()); it=overlay_.erase(it); } else ++it; }
    };
    if(forward) {
        for(const auto& r:c.archived) { take(r); insertRound(r); }
        for(const auto& r:c.deleted) dropRound(r.id);
        apply(c.before,c.after); apply(c.linkedBefore,c.linkedAfter); applyInk(c.inkBefore,c.inkAfter);
        for(const auto& n:c.notesAdded) { notes_[n.id]=n; pages.insert(n.page); }
    } else {
        for(const auto& n:c.notesAdded) { notes_.remove(n.id); pages.insert(n.page); }
        applyInk(c.inkAfter,c.inkBefore); apply(c.linkedAfter,c.linkedBefore); apply(c.after,c.before);
        for(const auto& r:c.deleted) insertRound(r);
        for(const auto& r:c.archived) { dropRound(r.id); putBack(r); }
    }
    if(!c.archived.isEmpty() || !c.deleted.isEmpty() || !c.notesAdded.isEmpty()) {
        refreshRows();
        for(int page:pages) emit inkChanged(page);
        practiceTouched();
    }
}
// Undo never acts on something invisible: a step that adds or removes hidden answers shows their page.
void AnnotationModel::revealTouched(const Command& c) {
    if(veils_.isEmpty()) return;
    QSet<int> pages;
    // Any item of the step that falls under a page's veil, whether the step added or removed it.
    auto note=[&](const TextAnnotation& n) { if(!n.id.isEmpty() && noteHidden(n)) pages.insert(n.page); };
    auto ink=[&](const InkStroke& s) { if(inkHidden(s)) pages.insert(s.page); };
    note(c.before); note(c.after); note(c.linkedBefore); note(c.linkedAfter);
    for(const auto& s:c.inkBefore) ink(s);
    for(const auto& s:c.inkAfter) ink(s);
    for(const auto& r:c.archived) { for(const auto& n:r.notes) note(n); for(const auto& s:r.ink) ink(s); }
    for(int page:pages) { veils_.remove(page); emit inkChanged(page); emit pageRevealed(page); }
    if(veils_.isEmpty()) bookHidden_=false;
    if(!pages.isEmpty()) { refreshRows(); practiceTouched(); }
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
    for(qsizetype i=0;i<stroke.points.size();++i) {
        const auto c=InkPath::segment(stroke.points,i);
        const int steps=c.steps(.05);
        auto a=c.a;
        for(int j=1;j<=steps;++j) {
            const auto b=c.at(double(j)/steps);
            if(segmentDistance(from,to,a,b)<=radius+stroke.width/2+.05) return true;
            a=b;
        }
    }
    return false;
}
qsizetype inkBytes(const QList<InkStroke>& strokes) {
    qsizetype bytes=strokes.capacity()*sizeof(InkStroke);
    for(const auto& s:strokes) bytes+=s.id.capacity()*2+s.points.capacity()*sizeof(QPointF)+s.pressure.capacity()*sizeof(float);
    return bytes;
}
}
qsizetype AnnotationModel::Command::bytes() const {
    return (before.id.capacity()+after.id.capacity()+before.text.capacity()+after.text.capacity()
        +before.family.capacity()+after.family.capacity()+linkedBefore.id.capacity()+linkedAfter.id.capacity()
        +linkedBefore.text.capacity()+linkedAfter.text.capacity()+linkedBefore.family.capacity()+linkedAfter.family.capacity())*2+inkBytes(inkBefore)+inkBytes(inkAfter)
        +(before.styles.capacity()+after.styles.capacity()+linkedBefore.styles.capacity()+linkedAfter.styles.capacity())*sizeof(TextStyle)
        // Round item lists are shared with the archive; only the references cost memory here.
        +(archived.size()+deleted.size())*qsizetype(sizeof(PracticeRound)+80)
        +[this]{ qsizetype b=notesAdded.capacity()*qsizetype(sizeof(TextAnnotation)); for(const auto& n:notesAdded) b+=(n.id.capacity()+n.text.capacity()+n.family.capacity())*2+n.styles.capacity()*qsizetype(sizeof(TextStyle)); return b; }();
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
    for(const auto& s:strokes_) if(s.page==page && !inkHidden(s) && !(gesture_==Gesture::Move && s.id==preview_.id)) result.append(s);
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
bool AnnotationModel::beginInk(int page,QPointF p,QColor color,double width,double pressure) {
    cancelGesture(); finishEditing(); clearInkSelection();
    if(!geometry_.contains(page) || !validPoint(p) || !color.isValid() || !std::isfinite(width) || width<.25 || width>32) return false;
    archiveHiddenBeforeEdit(page);
    gestureError_.clear(); emit gestureErrorChanged();
    color.setAlpha(255); // The MVP toolbar offers opaque ink, avoiding overlap alpha artifacts.
    preview_={QUuid::createUuid().toString(QUuid::WithoutBraces),page,{geometry_[page].displayToPdf.map(p)},color,width};
    preview_.order=nextInkOrder_++;
    if(std::isfinite(pressure) && pressure>=0) preview_.pressure={float(std::min(pressure,2.0)*InkPath::startTaper)};
    inkLength_=0;
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
        if(it->page==gesturePage_ && !inkHidden(*it) && hits(*it,from,to,eraserRadius_)) {
            erased_.append(it.value()); it=strokes_.erase(it); changed=true;
        } else ++it;
    }
    if(changed) emit inkChanged(gesturePage_);
}
void AnnotationModel::appendInk(QPointF p,double pressure) {
    if(gesture_==Gesture::None) return;
    if(!validPoint(p)) { cancelGesture(); return; }
    const auto g=geometry_[gesturePage_];
    const auto raw=g.displayToPdf.map(p);
    if(gesture_==Gesture::Draw) {
        const double step=QLineF(raw,preview_.points.last()).length();
        if(step<.1) return;
        if(preview_.points.size()>=AnnotationConfig::maxStrokePoints) {
            cancelGesture(); gestureError_=QStringLiteral("笔迹超过 65536 点，已取消本次手势；请分段书写。");
            emit gestureErrorChanged(); return;
        }
        if(!preview_.pressure.isEmpty()) {
            inkLength_+=step;
            const double f=std::isfinite(pressure) && pressure>=0 ? std::min(pressure,2.0) : 1.0;
            preview_.pressure.append(float(f*InkPath::ramp(InkPath::startTaper,inkLength_,InkPath::startTaperLength(preview_.width))));
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
    if(gesture_==Gesture::Draw) {
        // A tap is a round dot of full width; a line lifts off with a tapered tail.
        if(preview_.points.size()<2) preview_.pressure.clear();
        else InkPath::taperTail(preview_.points,preview_.pressure,preview_.width,inkLength_);
        strokes_[preview_.id]=preview_; c.inkAfter={preview_};
    }
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

QList<InkStroke> AnnotationModel::boxMark(int page,QRectF box) const {
    QList<InkStroke> result;
    const auto g=geometry_.value(page);
    if(!g.valid || !box.isValid()) return result;
    const double grow=.35*std::max(box.width(),box.height());
    const auto area=box.adjusted(-grow,-grow,grow,grow);
    for(const auto& s:strokes_) {
        if(s.page!=page || s.points.isEmpty() || inkHidden(s)) continue;
        bool inside=true;
        for(const auto& p:s.points) if(!area.contains(g.pdfToDisplay.map(p))) { inside=false; break; }
        if(inside) result.append(s);
    }
    return result;
}
bool AnnotationModel::toggleBoxMark(int page,QRectF box,bool cross,QColor color) {
    cancelGesture(); finishEditing(); clearInkSelection();
    const auto g=geometry_.value(page);
    if(!g.valid || !box.isValid() || !color.isValid() || !QRectF(0,0,1,1).contains(box.center())) return false;
    Command c;
    c.inkBefore=boxMark(page,box);
    if(!c.inkBefore.isEmpty()) {
        for(const auto& s:c.inkBefore) strokes_.remove(s.id);
        record(std::move(c)); emit inkChanged(page);
        return false;
    }
    archiveHiddenBeforeEdit(page);
    color.setAlpha(255);
    // Width follows the printed box (in PDF units): about a pen stroke for a 7-8 pt box.
    const double side=std::min(box.width()*g.units.width(),box.height()*g.units.height());
    const double width=std::clamp(side*.14,.8,2.5);
    auto at=[&](double x,double y) { return g.displayToPdf.map(QPointF(box.x()+x*box.width(),box.y()+y*box.height())); };
    auto stroke=[&](QList<QPointF> points) {
        InkStroke s;
        s.id=QUuid::createUuid().toString(QUuid::WithoutBraces); s.page=page; s.points=std::move(points);
        s.color=color; s.width=width; s.order=nextInkOrder_++;
        return s;
    };
    if(cross) {
        c.inkAfter={stroke({at(.2,.2),at(.8,.8)}),stroke({at(.8,.2),at(.2,.8)})};
    } else {
        // Tick: short down stroke, sharp turn, long rise. Samples right beside the turn
        // keep the midpoint-quadratic ink path from rounding it off.
        const QPointF a(.16,.5),v(.42,.8),b(.9,.12);
        auto mix=[](QPointF p,QPointF q,double t) { return p+(q-p)*t; };
        const QList<QPointF> shape{a,mix(a,v,.9),v,mix(v,b,.06),b};
        QList<QPointF> points; for(const auto& p:shape) points.append(at(p.x(),p.y()));
        c.inkAfter={stroke(points)};
    }
    for(const auto& s:c.inkAfter) strokes_[s.id]=s;
    record(std::move(c)); emit inkChanged(page);
    return true;
}
void AnnotationModel::restore(const AnnotationSnapshot& snapshot) {
    cancelGesture();
    beginResetModel(); notes_.clear(); strokes_.clear(); visible_.clear();
    selected_.clear(); beforeEdit_={}; undo_.clear(); redo_.clear(); nextInkOrder_=0;
    nextTextOrder_=0; veils_.clear(); bookHidden_=false; overlay_.clear(); lastArchived_={};
    for(const auto& n:snapshot.notes) { notes_[n.id]=n; nextTextOrder_=std::max(nextTextOrder_,n.order+1); }
    for(const auto& s:snapshot.ink) { strokes_[s.id]=s; nextInkOrder_=std::max(nextInkOrder_,s.order+1); }
    rounds_.clear();
    for(const auto& r:snapshot.rounds) {
        insertRound(r);
        if(!lastArchived_.isValid() || r.archived>lastArchived_) lastArchived_=r.archived;
        for(const auto& n:r.notes) nextTextOrder_=std::max(nextTextOrder_,n.order+1);
        for(const auto& s:r.ink) nextInkOrder_=std::max(nextInkOrder_,s.order+1);
    }
    endResetModel(); refreshRows(); clearInkSelection();
    emit selectionChanged(); emit historyChanged(); practiceTouched();
    for(int page:geometry_.keys()) emit inkChanged(page);
}


// ---- Hiding ----
bool AnnotationModel::noteHidden(const TextAnnotation& n) const {
    const auto veil=veils_.constFind(n.page);
    if(veil==veils_.cend()) return false;
    // A grading mark follows its answer.
    if(n.id.endsWith(QStringLiteral("-review"))) {
        const auto source=notes_.constFind(n.id.chopped(7));
        if(source!=notes_.cend()) return source->order<veil->text;
    }
    return n.order<veil->text;
}
bool AnnotationModel::inkHidden(const InkStroke& s) const {
    const auto veil=veils_.constFind(s.page);
    return veil!=veils_.cend() && s.order<veil->ink;
}
void AnnotationModel::setPageHidden(int page,bool hidden) {
    if(page<0 || hidden==veils_.contains(page)) return;
    cancelGesture(); finishEditing(); clearInkSelection();
    if(hidden) veils_[page]={nextTextOrder_,nextInkOrder_};
    else { veils_.remove(page); if(veils_.isEmpty()) bookHidden_=false; }
    refreshRows(); emit inkChanged(page); practiceTouched();
}
void AnnotationModel::setBookHidden(bool hidden) {
    cancelGesture(); finishEditing(); clearInkSelection();
    QSet<int> pages(veils_.keyBegin(),veils_.keyEnd());
    if(hidden) {
        for(const auto& n:notes_) pages.insert(n.page);
        for(const auto& s:strokes_) pages.insert(s.page);
        for(int page:pages) veils_[page]={nextTextOrder_,nextInkOrder_};
    } else veils_.clear();
    bookHidden_=hidden;
    refreshRows(); for(int page:pages) emit inkChanged(page);
    practiceTouched();
}
int AnnotationModel::hiddenCount(int page) const {
    if(!veils_.contains(page)) return 0;
    int count=0;
    for(const auto& n:notes_) count+=n.page==page && !n.id.endsWith(QStringLiteral("-review")) && noteHidden(n);
    for(const auto& s:strokes_) count+=s.page==page && inkHidden(s);
    return count;
}
int AnnotationModel::hiddenTotal() const {
    int count=0;
    for(auto it=veils_.cbegin();it!=veils_.cend();++it) count+=hiddenCount(it.key());
    return count;
}

// ---- Practice rounds ----
void AnnotationModel::insertRound(const PracticeRound& round) {
    auto at=std::upper_bound(rounds_.begin(),rounds_.end(),round,[](const auto& a,const auto& b) {
        return a.archived!=b.archived ? a.archived<b.archived : a.id<b.id;
    });
    rounds_.insert(at,round);
}
QDateTime AnnotationModel::nextArchiveTime() {
    // Strictly increasing per workspace, so rounds of a page keep their order on disk.
    auto now=QDateTime::currentDateTimeUtc();
    if(lastArchived_.isValid() && now<=lastArchived_) now=lastArchived_.addMSecs(1);
    lastArchived_=now;
    return now;
}
PracticeRound AnnotationModel::archivePage(int page,const QString& batch,bool hiddenOnly) const {
    PracticeRound r;
    r.id=QUuid::createUuid().toString(QUuid::WithoutBraces); r.page=page; r.batch=batch;
    for(const auto& n:notes_) if(n.page==page && (!hiddenOnly || noteHidden(n))) r.notes.append(n);
    for(const auto& s:strokes_) if(s.page==page && (!hiddenOnly || inkHidden(s))) r.ink.append(s);
    return r;
}
// The first answer written on a hidden page starts a new round: the hidden answers are
// archived (one undo step of its own) instead of mixing with the new attempt.
void AnnotationModel::archiveHiddenBeforeEdit(int page) {
    if(!veils_.contains(page)) return;
    auto r=archivePage(page,{},true);
    if(r.notes.isEmpty() && r.ink.isEmpty()) return;
    r.archived=nextArchiveTime();
    veils_.remove(page); if(veils_.isEmpty()) bookHidden_=false;
    Command c; c.archived={r};
    runCommand(c,true); record(std::move(c));
    emit roundStarted(page,currentRound(page),true);
}
int AnnotationModel::currentRound(int page) const {
    int count=1;
    for(const auto& r:rounds_) count+=r.page==page;
    return count;
}
QVariantList AnnotationModel::pageRounds(int page) const {
    QVariantList result; int number=0;
    for(const auto& r:rounds_) {
        if(r.page!=page) continue;
        auto entry=answerStats(r.notes,r.ink,page);
        entry.insert("id",r.id); entry.insert("number",++number); entry.insert("book",!r.batch.isEmpty());
        entry.insert("archived",r.archived.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        result.append(entry);
    }
    return result;
}
int AnnotationModel::answeredPages() const {
    QSet<int> pages;
    for(const auto& n:notes_) pages.insert(n.page);
    for(const auto& s:strokes_) pages.insert(s.page);
    return int(pages.size());
}
bool AnnotationModel::startRound(int page) {
    cancelGesture(); finishEditing(); clearInkSelection();
    auto r=archivePage(page,{},false);
    if(r.notes.isEmpty() && r.ink.isEmpty()) return false;
    r.archived=nextArchiveTime();
    veils_.remove(page); if(veils_.isEmpty()) bookHidden_=false;
    Command c; c.archived={r};
    runCommand(c,true); record(std::move(c));
    emit roundStarted(page,currentRound(page),false);
    return true;
}
int AnnotationModel::startBookRound() {
    cancelGesture(); finishEditing(); clearInkSelection();
    QSet<int> pageSet;
    for(const auto& n:notes_) pageSet.insert(n.page);
    for(const auto& s:strokes_) pageSet.insert(s.page);
    if(pageSet.isEmpty()) return 0;
    QList<int> pages(pageSet.cbegin(),pageSet.cend()); std::sort(pages.begin(),pages.end());
    const auto batch=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto time=nextArchiveTime();
    Command c;
    for(int page:pages) { auto r=archivePage(page,batch,false); r.archived=time; c.archived.append(std::move(r)); }
    veils_.clear(); bookHidden_=false;
    runCommand(c,true); record(std::move(c));
    emit roundStarted(-1,0,false);
    return int(pages.size());
}
QString AnnotationModel::restoreRound(const QString& id) {
    cancelGesture(); finishEditing(); clearInkSelection();
    const auto found=std::find_if(rounds_.cbegin(),rounds_.cend(),[&](const auto& r){ return r.id==id; });
    if(found==rounds_.cend()) return tr("找不到这一轮练习。");
    const auto source=*found;
    // Restoring copies the round, so it must fit the workspace limits (current + archive).
    qsizetype count=notes_.size()+strokes_.size(), points=0;
    for(const auto& s:strokes_) points+=s.points.size();
    for(const auto& r:rounds_) { count+=r.notes.size()+r.ink.size(); for(const auto& s:r.ink) points+=s.points.size(); }
    count+=source.notes.size()+source.ink.size();
    for(const auto& s:source.ink) points+=s.points.size();
    if(count>SnapshotLimits::maxAnnotations || points>SnapshotLimits::maxTotalPoints)
        return tr("恢复后会超出练习容量上限（%1 个标注、%2 个笔迹点）；请先删除较早的轮次。").arg(SnapshotLimits::maxAnnotations).arg(SnapshotLimits::maxTotalPoints);
    Command c;
    auto current=archivePage(source.page,{},false);
    if(!current.notes.isEmpty() || !current.ink.isEmpty()) { current.archived=nextArchiveTime(); c.archived={current}; }
    // Copies get new ids and creation orders; a grading mark keeps following its answer.
    auto notes=source.notes; std::sort(notes.begin(),notes.end(),[](const auto& a,const auto& b){ return a.order<b.order; });
    QHash<QString,QString> ids;
    for(const auto& n:notes) if(!n.id.endsWith(QStringLiteral("-review"))) ids[n.id]=QUuid::createUuid().toString(QUuid::WithoutBraces);
    for(auto n:notes) {
        if(n.id.endsWith(QStringLiteral("-review"))) {
            const auto answer=ids.value(n.id.chopped(7));
            if(answer.isEmpty()) continue;
            n.id=answer+QStringLiteral("-review");
        } else n.id=ids.value(n.id);
        n.order=nextTextOrder_++;
        c.notesAdded.append(std::move(n));
    }
    auto ink=source.ink; std::sort(ink.begin(),ink.end(),[](const auto& a,const auto& b){ return a.order<b.order; });
    for(auto s:ink) { s.id=QUuid::createUuid().toString(QUuid::WithoutBraces); s.order=nextInkOrder_++; c.inkAfter.append(std::move(s)); }
    veils_.remove(source.page); if(veils_.isEmpty()) bookHidden_=false;
    runCommand(c,true); record(std::move(c));
    return {};
}
bool AnnotationModel::deleteRound(const QString& id) {
    cancelGesture(); finishEditing(); clearInkSelection();
    const auto found=std::find_if(rounds_.cbegin(),rounds_.cend(),[&](const auto& r){ return r.id==id; });
    if(found==rounds_.cend()) return false;
    Command c; c.deleted={*found};
    runCommand(c,true); record(std::move(c));
    return true;
}
void AnnotationModel::setOverlayRound(int page,const QString& id) {
    if(id.isEmpty()) { if(overlay_.remove(page)) { emit inkChanged(page); practiceTouched(); } return; }
    const bool known=std::any_of(rounds_.cbegin(),rounds_.cend(),[&](const auto& r){ return r.id==id && r.page==page; });
    if(!known || overlay_.value(page)==id) return;
    overlay_[page]=id; emit inkChanged(page); practiceTouched();
}
QList<InkStroke> AnnotationModel::overlayStrokes(int page) const {
    const auto id=overlay_.value(page);
    if(id.isEmpty()) return {};
    for(const auto& r:rounds_) if(r.id==id) {
        auto ink=r.ink; std::sort(ink.begin(),ink.end(),[](const auto& a,const auto& b){ return a.order<b.order; });
        return ink;
    }
    return {};
}
QVariantList AnnotationModel::overlayNotes(int page) const {
    const auto id=overlay_.value(page);
    const auto g=geometry_.value(page);
    QVariantList result;
    if(id.isEmpty() || !g.valid) return result;
    for(const auto& r:rounds_) if(r.id==id) {
        for(const auto& n:r.notes) {
            if(n.text.isEmpty()) continue;
            const auto p=g.pdfToDisplay.map(n.anchor);
            result.append(QVariantMap{{"x",p.x()},{"y",p.y()},{"width",n.width/g.units.width()},{"height",n.height/g.units.height()},
                {"pdfWidth",n.width},{"unitsWidth",g.units.width()},{"text",n.text},{"size",n.fontSize},{"color",n.color},{"family",n.family},{"lineSpacing",n.lineSpacing}});
        }
    }
    return result;
}
