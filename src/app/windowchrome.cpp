#include "windowchrome.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

WindowChrome::WindowChrome(QObject* parent) : QObject(parent) {
    connect(this, &WindowChrome::changed, this, &WindowChrome::apply);
}

void WindowChrome::setWindow(QQuickWindow* window) {
    if (window_ == window) return;
    if (window_) disconnect(window_, nullptr, this, nullptr);
    window_ = window;
    // Apply once a native window exists; asking for winId() earlier would force creation.
    if (window_) connect(window_, &QWindow::visibleChanged, this, &WindowChrome::apply);
    emit changed();
}

void WindowChrome::apply() {
    bool tinted = false;
#ifdef Q_OS_WIN
    if (window_ && window_->isVisible() && caption_.isValid()) {
        const auto hwnd = reinterpret_cast<HWND>(window_->winId());
        // Attribute ids from dwmapi.h (Windows SDK 10.0.22000+); spelled out so older SDK
        // headers still build. Unsupported attributes return an error and are ignored.
        constexpr DWORD useImmersiveDarkMode = 20, cornerPreference = 33, borderColor = 34,
                        captionColor = 35, textColor = 36;
        const auto colorRef = [](const QColor& c) { return COLORREF(RGB(c.red(), c.green(), c.blue())); };
        const BOOL darkMode = dark_ ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, useImmersiveDarkMode, &darkMode, sizeof darkMode);
        const int rounded = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, cornerPreference, &rounded, sizeof rounded);
        const COLORREF captionValue = colorRef(caption_);
        tinted = SUCCEEDED(DwmSetWindowAttribute(hwnd, captionColor, &captionValue, sizeof captionValue));
        if (text_.isValid()) {
            const COLORREF value = colorRef(text_);
            DwmSetWindowAttribute(hwnd, textColor, &value, sizeof value);
        }
        if (border_.isValid()) {
            const COLORREF value = colorRef(border_);
            DwmSetWindowAttribute(hwnd, borderColor, &value, sizeof value);
        }
    }
#endif
    if (tinted_ != tinted) {
        tinted_ = tinted;
        emit appliedChanged();
    }
}
