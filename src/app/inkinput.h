#pragma once
#include "shellstate.h"
#include <QQuickItem>
#include <QPointer>

class InkInput : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(ShellState* shell READ shell WRITE setShell NOTIFY shellChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool drawingEnabled READ drawingEnabled WRITE setDrawingEnabled)
    Q_PROPERTY(QColor inkColor READ inkColor WRITE setInkColor NOTIFY styleChanged)
    Q_PROPERTY(double inkWidth READ inkWidth WRITE setInkWidth NOTIFY styleChanged)
    Q_PROPERTY(double contentX READ contentX WRITE setContentX)
    Q_PROPERTY(double contentY READ contentY WRITE setContentY)
    Q_PROPERTY(QVariantList passiveRects MEMBER passiveRects_)
    Q_PROPERTY(int passivePage MEMBER passivePage_)
public:
    explicit InkInput(QQuickItem* parent=nullptr);
    ShellState* shell() const { return shell_; }
    void setShell(ShellState* shell);
    int mode() const { return mode_; }
    void setMode(int mode);
    bool drawingEnabled() const { return drawingEnabled_; }
    void setDrawingEnabled(bool enabled);
    QColor inkColor() const { return color_; }
    void setInkColor(QColor color);
    double inkWidth() const { return width_; }
    void setInkWidth(double width);
    double contentX() const { return contentX_; }
    double contentY() const { return contentY_; }
    void setContentX(double value) { if(contentX_!=value) { cancel(); contentX_=value; } }
    void setContentY(double value) { if(contentY_!=value) { cancel(); contentY_=value; } }
signals:
    void gestureStarting();
    void shellChanged();
    void modeChanged();
    void styleChanged();
protected:
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseUngrabEvent() override;
    void keyPressEvent(QKeyEvent*) override;
    bool eventFilter(QObject*,QEvent*) override;
private:
    bool begin(QPointF position, bool erase = false);
    void extend(QPointF position);
    void cancel();
    void updateMouseInput();
    QPointF normalized(QPointF position) const;
    QPointer<ShellState> shell_;
    QPointer<QQuickWindow> filteredWindow_;
    int mode_=0, page_=-1;
    double contentX_=0,contentY_=0,width_=2;
    QColor color_=QColor("#235fbd");
    bool tablet_=false;
    bool rightErasing_=false;
    bool drawingEnabled_=true;
    bool autoPending_=false;
    QPointF autoOrigin_;
    QVariantList passiveRects_;
    int passivePage_=-1;
};
