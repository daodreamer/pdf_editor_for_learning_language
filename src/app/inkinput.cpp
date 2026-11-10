#include "inkinput.h"
#include <QQuickWindow>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QStyleHints>
#include <QLineF>
#include <QPainter>
#include <QPixmap>
#include <cmath>

namespace {
bool overlayBlocksInk(QQuickItem* item,QPointF position) {
    if(!item->isVisible() || !item->isEnabled()) return false;
    const bool contains=item->contains(item->mapFromScene(position));
    if(item->property("blocksInkInput").toBool() && contains) return true;
    if(item->clip() && !contains) return false;
    // An empty Controls Overlay may be above the panel: search visible siblings too.
    for(auto* child:item->childItems()) if(overlayBlocksInk(child,position)) return true;
    return false;
}
}

InkInput::InkInput(QQuickItem* parent):QQuickItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton); setKeepMouseGrab(true);
    autoWordTimer_.setSingleShot(true);
    connect(&autoWordTimer_,&QTimer::timeout,this,[this]{emit originalWordClicked(autoWordPoint_);});
    // Hover events stop while the button is held, so an item/hover cursor cannot be
    // relied on during a double-click drag: hold an application override instead.
    connect(this,&InkInput::textGestureChanged,this,[this]{
        if(selectingText_ && !selectionCursor_) { QGuiApplication::setOverrideCursor(QCursor(Qt::IBeamCursor)); selectionCursor_=true; }
        else if(!selectingText_ && selectionCursor_) { QGuiApplication::restoreOverrideCursor(); selectionCursor_=false; }
    });

    connect(this,&QQuickItem::windowChanged,this,[this](QQuickWindow* window){
        cancel();
        if(filteredWindow_) filteredWindow_->removeEventFilter(this);
        filteredWindow_=window;
        if(window) window->installEventFilter(this);
        updateToolCursor();
    });
    connect(this,&QQuickItem::enabledChanged,this,[this]{ if(!isEnabled()) cancel(); });
    connect(this,&QQuickItem::visibleChanged,this,[this]{ if(!isVisible()) cancel(); });
}
void InkInput::setShell(ShellState* shell) {
    if(shell_==shell) return;
    cancel(); if(shell_) disconnect(shell_,nullptr,this,nullptr);
    shell_=shell;
    if(shell_) {
        connect(shell_,&ShellState::layoutChanged,this,&InkInput::cancel);
        connect(shell_,&ShellState::layoutChanged,this,&InkInput::updateToolCursor); // pen size follows zoom
    }
    emit shellChanged();
}
void InkInput::setMode(int mode) { if(mode_!=mode) { cancel(); breakRhythm(); mode_=mode; updateMouseInput(); emit modeChanged(); } }
void InkInput::setDrawingEnabled(bool enabled) {
    if(drawingEnabled_==enabled) return;
    cancel(); drawingEnabled_=enabled; updateMouseInput();
}
void InkInput::updateMouseInput() {
    const bool drawing=drawingEnabled_ && (mode_==0 || mode_>=3);
    setAcceptedMouseButtons(drawing?Qt::LeftButton:Qt::NoButton);
    updateToolCursor();
}
InkInput::~InkInput() { if(selectionCursor_) QGuiApplication::restoreOverrideCursor(); }
void InkInput::updateToolCursor() {
    // Automatic/select/type: no cursor of our own, so the arrow and the cursors of
    // text boxes and selection areas beneath this overlay show through.
    if(!drawingEnabled_ || mode_<3) { unsetCursor(); return; }
    if(mode_==5) { setCursor(QCursor(Qt::OpenHandCursor)); return; }
    // Pen: a dot of the ink colour at the stroke's on-screen size. Eraser: a ring of
    // the eraser's radius. Both show exactly where the tool acts.
    double diameter=2*AnnotationConfig::eraserPixels;
    if(mode_==3) {
        double scale=1;
        if(shell_ && shell_->pageCount()>0) {
            const auto g=shell_->annotations()->geometry(std::max(0,shell_->currentPage()-1));
            if(g.valid && g.units.width()>0) scale=shell_->pageWidth()/g.units.width();
        }
        diameter=std::clamp(width_*scale,4.0,32.0);
    }
    const double dpr=window()?window()->effectiveDevicePixelRatio():qApp->devicePixelRatio();
    const int box=int(std::ceil(diameter))+4;
    QPixmap pixmap(QSize(box,box)*dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF r((box-diameter)/2,(box-diameter)/2,diameter,diameter);
    if(mode_==3) {
        painter.setPen(QPen(QColor(255,255,255,235),1.5)); // halo keeps dark ink visible on dark pages
        painter.setBrush(color_); painter.drawEllipse(r);
    } else {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255,255,255,230),3)); painter.drawEllipse(r);
        painter.setPen(QPen(QColor(60,60,67,220),1.2)); painter.drawEllipse(r);
    }
    painter.end();
    setCursor(QCursor(pixmap,box/2,box/2));
}
void InkInput::setInkColor(QColor color) { if(color_!=color) { cancel(); color_=color; updateToolCursor(); emit styleChanged(); } }
void InkInput::setInkWidth(double width) { if(width_!=width) { cancel(); width_=width; updateToolCursor(); emit styleChanged(); } }
void InkInput::cancel() { autoWordTimer_.stop();if(shell_) shell_->annotations()->cancelGesture(); page_=-1; tablet_=false;autoPending_=false;autoTrail_.clear(); rightErasing_=false; if(selectingText_){selectingText_=false;emit textGestureChanged();} }
bool InkInput::eraseTextUnder(QPointF p) {
    if(!shell_) return false;
    const auto id=shell_->textBoxAt(p.x()+contentX_,p.y()+contentY_);
    if(id.isEmpty() || pendingTextErase_.contains(id)) return false;
    // The box may be the focused editor: delete it after this event has been
    // dispatched, never from inside the window's event filter.
    pendingTextErase_.insert(id);
    QMetaObject::invokeMethod(this,[this,id] {
        pendingTextErase_.remove(id);
        if(!shell_) return;
        // Selecting the box cancels an active stroke erase: commit it, then resume.
        const bool resume=rightErasing_ && shell_->annotations()->gestureActive();
        if(resume) shell_->annotations()->endInk();
        shell_->annotations()->select(id);
        if(shell_->annotations()->selectedId()==id) shell_->annotations()->removeSelected();
        if(resume) { begin(lastErasePoint_,true); rightErasing_=true; }
    },Qt::QueuedConnection);
    return true;
}
bool InkInput::inWritingRhythm(QPointF p) const {
    if(!shell_ || !lastStroke_.isValid() || lastStroke_.elapsed()>AnnotationConfig::writingRhythmMs) return false;
    const QPointF content(p.x()+contentX_,p.y()+contentY_);
    return shell_->model().pageAtPosition(content.x(),content.y())==lastStrokePage_
        && QLineF(content,lastStrokePoint_).length()<=AnnotationConfig::writingRhythmPixels;
}
void InkInput::noteStrokeEnded(QPointF p) {
    lastStrokePoint_=QPointF(p.x()+contentX_,p.y()+contentY_);
    lastStrokePage_=page_;
    lastStroke_.start();
}
QPointF InkInput::normalized(QPointF p) const {
    const double left=shell_->pageLeft(page_);
    return {(p.x()+contentX_-left)/shell_->pageWidth(),(p.y()+contentY_-shell_->pageTop(page_))/shell_->pageHeight(page_)};
}
bool InkInput::begin(QPointF p, bool erase) {
    cancel();
    if(!shell_ || (!erase && (!drawingEnabled_ || (mode_!=0&&mode_<3))) || !shell_->containsPage(p.x()+contentX_,p.y()+contentY_)) return false;
    emit gestureStarting();
    shell_->finishTextEditing(); shell_->textSelection()->clear();
    page_=shell_->model().pageAtPosition(p.x()+contentX_,p.y()+contentY_);
    shell_->focusPage(page_);
    const auto n=normalized(p);
    const double radius=AnnotationConfig::eraserPixels*shell_->annotations()->geometry(page_).units.width()/shell_->pageWidth();
    bool started=false;
    if(erase) started=shell_->annotations()->beginErase(page_,n,radius);
    else if(mode_==0||mode_==3) started=shell_->annotations()->beginInk(page_,n,color_,width_);
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
    if(!drawingEnabled_ || (mode_!=0&&mode_<3)) { event->ignore(); return; }
    if(mode_==0&&shell_) {
        const auto p=event->position();
        if(shell_->interactiveContentAt(p.x()+contentX_,p.y()+contentY_)){event->ignore();return;}
        cancel();autoOrigin_=p;autoPending_=true;autoTextHit_=false;autoTrail_={p};
        const int page=shell_->model().pageAtPosition(p.x()+contentX_,p.y()+contentY_);
        if(const auto text=shell_->textSelection()->snapshot(page);text) {
            const QPointF n((p.x()+contentX_-shell_->pageLeft(page))/shell_->pageWidth(),(p.y()+contentY_-shell_->pageTop(page))/shell_->pageHeight(page));
            for(const auto& c:text->characters)if(c.visible && c.box.contains(n)){autoTextHit_=true;break;}
        }
        forceActiveFocus();event->accept();return;
    }
    if(mode_==4&&shell_&&shell_->eraseTextAt(event->position().x()+contentX_,event->position().y()+contentY_)){event->accept();return;}
    begin(event->position()); event->accept();
}
void InkInput::mouseMoveEvent(QMouseEvent* event) {
    if(autoPending_){
        autoTrail_.append(event->position());
        if(QLineF(autoOrigin_,event->position()).length()<AnnotationConfig::autoInkStartPixels){event->accept();return;}
        // Intent is clear: replay the whole buffered trail, not a straight jump
        // from the press point, so the first millimetres of a letter are kept.
        const auto trail=std::move(autoTrail_);
        if(begin(trail.first())) for(qsizetype i=1;i<trail.size();++i) extend(trail[i]);
        event->accept();return;
    }
    extend(event->position());event->accept();
}
void InkInput::mouseReleaseEvent(QMouseEvent* event) {
    if(autoPending_){
        autoPending_=false;autoTrail_.clear();
        if(inWritingRhythm(autoOrigin_)) {
            // A tap inside the current writing rhythm is a dot of handwriting.
            if(begin(autoOrigin_) && shell_) { shell_->annotations()->endInk(); noteStrokeEnded(autoOrigin_); }
            page_=-1;
        }
        else if(autoTextHit_) {autoWordPoint_=autoOrigin_;autoWordTimer_.start(QGuiApplication::styleHints()->mouseDoubleClickInterval());}
        else if(shell_){autoNote_=shell_->createTextBox(autoOrigin_.x()+contentX_,autoOrigin_.y()+contentY_);autoNoteAge_.start();}
        event->accept();return;
    }
    extend(event->position());
    if(shell_ && shell_->annotations()->gestureActive()) {
        shell_->annotations()->endInk();
        if(mode_==0) noteStrokeEnded(event->position());
    }
    page_=-1; event->accept();
}
void InkInput::mouseUngrabEvent() { cancel(); }
void InkInput::keyPressEvent(QKeyEvent* event) {
    if(shell_ && event->key()==Qt::Key_Escape) { cancel();shell_->textSelection()->clear();event->accept(); }
    else if(shell_ && (event->key()==Qt::Key_Delete || event->key()==Qt::Key_Backspace)) { shell_->annotations()->removeInk(); event->accept(); }
    else QQuickItem::keyPressEvent(event);
}
bool InkInput::eventFilter(QObject* watched,QEvent* event) {
    if(watched!=filteredWindow_) return false;
    if(event->type()==QEvent::WindowDeactivate || event->type()==QEvent::Hide) { cancel(); breakRhythm(); }
    if(event->type()==QEvent::KeyPress) breakRhythm();
    if(event->type()==QEvent::MouseButtonPress) {
        // A press on a toolbar, panel or outside the page area ends the writing burst.
        const auto* press=static_cast<QMouseEvent*>(event);
        if(!contains(mapFromScene(press->position())) || overlayBlocksInk(filteredWindow_->contentItem(),press->position())) breakRhythm();
    }
    if(!isEnabled() || !isVisible()) return false;
    if(event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseButtonDblClick || event->type()==QEvent::MouseMove || event->type()==QEvent::MouseButtonRelease) {
        auto* mouse=static_cast<QMouseEvent*>(event);
        const auto p=mapFromScene(mouse->position());
        if(selectingText_) {
            if(event->type()==QEvent::MouseMove) {
                textDragging_|=QLineF(textOrigin_,p).length()>=QGuiApplication::styleHints()->startDragDistance();
                textPointer_=p;if(textDragging_)shell_->extendTextSelection(p.x()+contentX_,p.y()+contentY_);emit textGestureChanged();
            } else if(event->type()==QEvent::MouseButtonRelease && mouse->button()==Qt::LeftButton) {
                if(textDragging_)shell_->extendTextSelection(p.x()+contentX_,p.y()+contentY_);else shell_->textSelection()->expandWord();
                selectingText_=false;emit textGestureChanged();emit textSelectionFinished(p);
            } else if(event->type()!=QEvent::MouseButtonPress && event->type()!=QEvent::MouseButtonDblClick)return false;
            event->accept();return true;
        }
        if(event->type()==QEvent::MouseButtonDblClick && mouse->button()==Qt::LeftButton && mode_==0 && drawingEnabled_ && shell_ && contains(p)
            && !inWritingRhythm(p) && !overlayBlocksInk(filteredWindow_->contentItem(),mouse->position())) {
            bool transient=false;
            for(const auto& n:shell_->annotations()->snapshot())if(n.id==autoNote_ && n.text.isEmpty()
                && autoNoteAge_.isValid() && autoNoteAge_.elapsed()<=QGuiApplication::styleHints()->mouseDoubleClickInterval()
                && shell_->annotations()->selectedId()==autoNote_ && QLineF(p,autoOrigin_).length()<=QGuiApplication::styleHints()->mouseDoubleClickDistance())transient=true;
            if(transient || !shell_->interactiveContentAt(p.x()+contentX_,p.y()+contentY_)) {
                cancel();shell_->finishTextEditing();emit gestureStarting();forceActiveFocus();
                selectingText_=shell_->beginTextSelection(p.x()+contentX_,p.y()+contentY_);
                textPointer_=textOrigin_=p;textDragging_=false;autoNote_.clear();emit textGestureChanged();
                if(selectingText_) { shell_->textSelection()->expandWord();event->accept();return true; }
            }
        }
        if((event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseButtonDblClick) && mouse->button()==Qt::RightButton && contains(p) && !overlayBlocksInk(filteredWindow_->contentItem(),mouse->position())) {
            // The right button erases with every tool: strokes and typed answers alike.
            lastErasePoint_=p;
            const bool text=eraseTextUnder(p);
            rightErasing_=begin(p,true) || text;
            if(rightErasing_) { event->accept(); return true; }
        } else if(rightErasing_) {
            if(event->type()==QEvent::MouseButtonRelease && mouse->button()==Qt::RightButton) {
                extend(p); eraseTextUnder(p);
                if(shell_) shell_->annotations()->endInk();
                rightErasing_=false; page_=-1;
            } else if(event->type()==QEvent::MouseMove) {
                extend(p); lastErasePoint_=p;
                eraseTextUnder(p);
            }
            event->accept(); return true;
        }
    }
    if(!drawingEnabled_ || (mode_!=0&&mode_<3)) return false;
    if(event->type()==QEvent::TabletPress || event->type()==QEvent::TabletMove || event->type()==QEvent::TabletRelease) {
        auto* tablet=static_cast<QTabletEvent*>(event);
        const auto p=mapFromScene(tablet->position());
        if(event->type()==QEvent::TabletPress && contains(p) && !overlayBlocksInk(filteredWindow_->contentItem(),tablet->position())) { begin(p); tablet_=true; }
        else if(!tablet_) return false;
        else extend(p);
        if(event->type()==QEvent::TabletRelease) { if(shell_) shell_->annotations()->endInk(); tablet_=false; page_=-1; }
        event->accept(); return true;
    }
    return false;
}
