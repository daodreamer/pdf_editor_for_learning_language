#pragma once
#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>

// Tints the native title bar and border to the shell theme. The system caption, buttons,
// snapping and resizing stay native; only colours, dark mode and corner rounding are set.
// Windows 11 (build 22000+) honours caption/text/border colours through DWM; older Windows
// and macOS ignore them and keep the system look (`tinted` reports which happened).
class WindowChrome : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow* window READ window WRITE setWindow NOTIFY changed)
    Q_PROPERTY(QColor caption MEMBER caption_ NOTIFY changed)
    Q_PROPERTY(QColor text MEMBER text_ NOTIFY changed)
    Q_PROPERTY(QColor border MEMBER border_ NOTIFY changed)
    Q_PROPERTY(bool dark MEMBER dark_ NOTIFY changed)
    Q_PROPERTY(bool tinted READ tinted NOTIFY appliedChanged)
public:
    explicit WindowChrome(QObject* parent = nullptr);
    QQuickWindow* window() const { return window_; }
    void setWindow(QQuickWindow* window);
    bool tinted() const { return tinted_; }
signals:
    void changed();
    void appliedChanged();
private:
    void apply();
    QPointer<QQuickWindow> window_;
    QColor caption_, text_, border_;
    bool dark_ = false;
    bool tinted_ = false;
};
