#pragma once
#include <QByteArray>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QString>
#include <cstdlib>
#include <functional>
#include <map>

// All GUI suites live in one executable (langlern_tests); each suite registers a
// factory under a short name and main.cpp runs the suite named on the command line.
namespace TestSuites {
using Factory = std::function<QObject*()>;
inline std::map<QByteArray,Factory>& registry() { static std::map<QByteArray,Factory> suites; return suites; }
struct Registrar { Registrar(const char* name,Factory factory) { registry().emplace(name,std::move(factory)); } };
}
#define LANGLERN_TEST_SUITE(Class,name) \
    static const TestSuites::Registrar Class##Registrar(name,[]() -> QObject* { return new Class; });

#define LANGLERN_FIXTURE(name) (QStringLiteral(LANGLERN_FIXTURES) + QStringLiteral("/" name))

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

// Depth-first search of the visual tree by objectName (popups hang off the overlay,
// so QObject::findChild does not reach every item).
inline QQuickItem* findItem(QQuickItem* item,const QString& name) {
    if(item->objectName()==name) return item;
    for(auto* child:item->childItems()) if(auto* found=findItem(child,name)) return found;
    return nullptr;
}
inline QPoint itemCenter(QQuickItem* item) { return item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint(); }

// The fixed real acceptance samples (AGENTS.md); an empty result means "not on this machine".
inline QString realTextbookPath() {
    const auto path=qEnvironmentVariable("LANGLERN_REAL_PDF",QStringLiteral("C:/Users/jiong/Desktop/法语/你好法语3学生用书第二版.pdf"));
    return QFileInfo::exists(path)?path:QString();
}
inline QString realWorkbookPath() {
    const auto path=qEnvironmentVariable("LANGLERN_REAL_WORKBOOK",QStringLiteral("C:/Users/jiong/Desktop/法语/你好法语3练习册第二版.pdf"));
    return QFileInfo::exists(path)?path:QString();
}
