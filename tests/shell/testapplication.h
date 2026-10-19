#pragma once
#include <QFont>
#include <QGuiApplication>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <cstdlib>

// Qt reads Windows environment paths through the wide CRT environment. Narrow
// qputenv uses the active code page and cannot represent arbitrary Unicode paths.
inline bool setTestEnvironment(const char* name,const QString& value) {
#ifdef Q_OS_WIN
    const auto wideName=QString::fromLatin1(name).toStdWString();
    const auto wideValue=value.toStdWString();
    return _wputenv_s(wideName.c_str(),wideValue.c_str())==0;
#else
    return qputenv(name,value.toUtf8());
#endif
}
class TestEnvironmentRestore {
    QByteArray name_;
    bool wasSet_;
    QString previous_;
public:
    explicit TestEnvironmentRestore(const char* name):name_(name),wasSet_(qEnvironmentVariableIsSet(name)),previous_(qEnvironmentVariable(name)) {}
    ~TestEnvironmentRestore() {
        if(wasSet_)setTestEnvironment(name_.constData(),previous_);
        else {
#ifdef Q_OS_WIN
            setTestEnvironment(name_.constData(),QString{});
#else
            qunsetenv(name_.constData());
#endif
        }
    }
    TestEnvironmentRestore(const TestEnvironmentRestore&)=delete;
    TestEnvironmentRestore& operator=(const TestEnvironmentRestore&)=delete;
};

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
