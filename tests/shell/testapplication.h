#pragma once
#include <QFont>
#include <QGuiApplication>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>

// Keep GUI fixtures on the same font, control style and public graphics API as main.cpp.
inline void configureTestApplication(QGuiApplication& app) {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
#ifdef Q_OS_WIN
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI")));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#else
    Q_UNUSED(app);
#endif
}
