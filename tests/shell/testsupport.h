#pragma once
#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QTest>
#include <QDebug>
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
#elif defined(Q_OS_MACOS)
    Q_UNUSED(app);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
#else
    Q_UNUSED(app);
#endif
}

// The window system may resize a window some time after a show or resize request (macOS
// shrinks windows that do not fit the screen; hosted runners have 1024x768 screens). A late
// resize rebuilds the page layout and deliberately cancels in-flight gestures, so wait until
// the size has stayed the same for a while before driving input.
inline bool settleWindowSize(QWindow* window) {
    QElapsedTimer total; total.start();
    QSize last=window->size(); QElapsedTimer stable; stable.start();
    while(total.elapsed()<3000) {
        QTest::qWait(25);
        if(window->size()!=last) { last=window->size(); stable.restart(); }
        else if(stable.elapsed()>=250) return true;
    }
    qWarning()<<"window size still changing after 3 s:"<<window->size();
    return false;
}
// Show a GUI fixture the same way on every runner: sized to fit the screen's available area
// (so the system never shrinks it later), exposed, active (QML shortcuts only fire in the
// focus window, and a deactivation cancels ink), and at a settled size.
inline bool showTestWindow(QWindow* window) {
    if(auto* screen=window->screen()) {
        // Leave room for the title bar and window frame, which the size excludes.
        const QSize room=screen->availableGeometry().size()-QSize(16,48);
        if(room.isValid() && (window->width()>room.width() || window->height()>room.height()))
            window->resize(window->size().boundedTo(room).expandedTo(window->minimumSize()));
    }
    window->show();
    if(!QTest::qWaitForWindowExposed(window)) return false;
    // Activation and a settled size are best effort: a runner may refuse focus or keep adjusting.
    // Tests whose input needs the focus window re-check activation right before that input.
    window->requestActivate();
    if(!QTest::qWaitForWindowActive(window)) qWarning()<<"test window did not become active";
    settleWindowSize(window);
    qInfo()<<"test_window size"<<window->size()<<"active"<<window->isActive();
    return true;
}

// Depth-first search of the visual tree by objectName (popups hang off the overlay,
// so QObject::findChild does not reach every item).
inline QQuickItem* findItem(QQuickItem* item,const QString& name) {
    if(item->objectName()==name) return item;
    for(auto* child:item->childItems()) if(auto* found=findItem(child,name)) return found;
    return nullptr;
}
inline QPoint itemCenter(QQuickItem* item) { return item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint(); }

// Frames the window actually presents per second while it keeps asking for updates. Hosted CI
// virtual machines can present far fewer frames than a real display; frame-paced behaviour
// (FrameAnimation glides) is then not measurable there.
inline double presentedFps(QQuickWindow* window,int ms=600) {
    int frames=0;
    auto connection=QObject::connect(window,&QQuickWindow::frameSwapped,window,[&]{ ++frames; window->update(); });
    window->update();
    QElapsedTimer timer; timer.start();
    while(timer.elapsed()<ms) QCoreApplication::processEvents(QEventLoop::AllEvents,10);
    QObject::disconnect(connection);
    return frames*1000.0/std::max<qint64>(1,timer.elapsed());
}

// The fixed real acceptance samples (AGENTS.md); an empty result means "not on this machine".
inline QString realTextbookPath() {
    const auto path=qEnvironmentVariable("LANGLERN_REAL_PDF",QStringLiteral("C:/Users/jiong/Desktop/法语/你好法语3学生用书第二版.pdf"));
    return QFileInfo::exists(path)?path:QString();
}
inline QString realWorkbookPath() {
    const auto path=qEnvironmentVariable("LANGLERN_REAL_WORKBOOK",QStringLiteral("C:/Users/jiong/Desktop/法语/你好法语3练习册第二版.pdf"));
    return QFileInfo::exists(path)?path:QString();
}
