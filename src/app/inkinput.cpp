#include "inkinput.h"
#include <QQuickWindow>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QStyleHints>
#include <QLineF>

InkInput::InkInput(QQuickItem* parent):QQuickItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton); setKeepMouseGrab(true);
    setCursor(QCursor(Qt::CrossCursor));
    connect(this,&QQuickItem::windowChanged,this,[this](QQuickWindow* window){
        cancel();
        if(filteredWindow_) filteredWindow_->removeEventFilter(this);
        filteredWindow_=window;
        if(window) window->installEventFilter(this);
    });
    connect(this,&QQuickItem::enabledChanged,this,[this]{ if(!isEnabled()) cancel(); });
    connect(this,&QQuickItem::visibleChanged,this,[this]{ if(!isVisible()) cancel(); });
}
void InkInput::setShell(ShellState* shell) {
    if(shell_==shell) return;
    cancel(); if(shell_) disconnect(shell_,nullptr,this,nullptr);
    shell_=shell;
    if(shell_) connect(shell_,&ShellState::layoutChanged,this,&InkInput::cancel);
    emit shellChanged();
}
void InkInput::setMode(int mode) { if(mode_!=mode) { cancel(); mode_=mode; emit modeChanged(); } }
void InkInput::setInkColor(QColor color) { if(color_!=color) { cancel(); color_=color; emit styleChanged(); } }
void InkInput::setInkWidth(double width) { if(width_!=width) { cancel(); width_=width; emit styleChanged(); } }
void InkInput::cancel() { if(shell_) shell_->annotations()->cancelGesture(); page_=-1; tablet_=false;autoPending_=false; }
QPointF InkInput::normalized(QPointF p) const {
    const double left=shell_->pageLeft(page_);
    return {(p.x()+contentX_-left)/shell_->pageWidth(),(p.y()+contentY_-shell_->pageTop(page_))/shell_->pageHeight(page_)};
}
bool InkInput::begin(QPointF p) {
    cancel();
    if(!shell_ || (mode_!=0&&mode_<3) || !shell_->containsPage(p.x()+contentX_,p.y()+contentY_)) return false;
    emit gestureStarting();
    shell_->finishTextEditing(); shell_->textSelection()->clear();
    page_=shell_->model().pageAtPosition(p.x()+contentX_,p.y()+contentY_);
    shell_->focusPage(page_);
    const auto n=normalized(p);
    const double radius=AnnotationConfig::eraserPixels*shell_->annotations()->geometry(page_).units.width()/shell_->pageWidth();
    bool started=false;
    if(mode_==0||mode_==3) started=shell_->annotations()->beginInk(page_,n,color_,width_);
    else if(mode_==4) started=shell_->annotations()->beginErase(page_,n,radius);
    else if(mode_==5) started=shell_->annotations()->beginMoveInk(page_,n,radius);
    forceActiveFocus();
    return started;
}
void InkInput::extend(QPointF p) {
    if(!shell_ || !shell_->annotations()->gestureActive()) return;
    if(!contains(p)) { cancel(); return; }
    shell_->annotations()->appendInk(normalized(p));
}
void InkInput::mousePressEvent(QMouseEvent* event) {
    if(mode_==0&&shell_) {
        const auto p=event->position();
        if(shell_->model().pageAtPosition(p.x()+contentX_,p.y()+contentY_)==passivePage_ && passivePage_>=0) {
            const QPointF normalized((p.x()+contentX_-shell_->pageLeft(passivePage_))/shell_->pageWidth(),
                (p.y()+contentY_-shell_->pageTop(passivePage_))/shell_->pageHeight(passivePage_));
            for(const auto& value:passiveRects_)if(value.toRectF().contains(normalized)){event->ignore();return;}
        }
        if(shell_->interactiveContentAt(p.x()+contentX_,p.y()+contentY_)){event->ignore();return;}
        cancel();autoOrigin_=p;autoPending_=true;forceActiveFocus();event->accept();return;
    }
    if(mode_==4&&shell_&&shell_->eraseTextAt(event->position().x()+contentX_,event->position().y()+contentY_)){event->accept();return;}
    begin(event->position()); event->accept();
}
void InkInput::mouseMoveEvent(QMouseEvent* event) {
    if(autoPending_){if(QLineF(autoOrigin_,event->position()).length()<QGuiApplication::styleHints()->startDragDistance()){event->accept();return;}const auto origin=autoOrigin_;begin(origin);}
    extend(event->position());event->accept();
}
void InkInput::mouseReleaseEvent(QMouseEvent* event) {
    if(autoPending_){autoPending_=false;if(shell_)shell_->createTextBox(autoOrigin_.x()+contentX_,autoOrigin_.y()+contentY_);event->accept();return;}
    extend(event->position()); if(shell_) shell_->annotations()->endInk(); page_=-1; event->accept();
}
void InkInput::mouseUngrabEvent() { cancel(); }
void InkInput::keyPressEvent(QKeyEvent* event) {
    if(shell_ && event->key()==Qt::Key_Escape) { cancel(); event->accept(); }
    else if(shell_ && (event->key()==Qt::Key_Delete || event->key()==Qt::Key_Backspace)) { shell_->annotations()->removeInk(); event->accept(); }
    else QQuickItem::keyPressEvent(event);
}
bool InkInput::eventFilter(QObject* watched,QEvent* event) {
    if(watched!=filteredWindow_) return false;
    if(event->type()==QEvent::WindowDeactivate || event->type()==QEvent::Hide) cancel();
    if(!isEnabled() || !isVisible() || (mode_!=0&&mode_<3)) return false;
    if(event->type()==QEvent::TabletPress || event->type()==QEvent::TabletMove || event->type()==QEvent::TabletRelease) {
        auto* tablet=static_cast<QTabletEvent*>(event);
        const auto p=mapFromScene(tablet->position());
        if(event->type()==QEvent::TabletPress && contains(p)) { begin(p); tablet_=true; }
        else if(!tablet_) return false;
        else extend(p);
        if(event->type()==QEvent::TabletRelease) { if(shell_) shell_->annotations()->endInk(); tablet_=false; page_=-1; }
        event->accept(); return true;
    }
    return false;
}
