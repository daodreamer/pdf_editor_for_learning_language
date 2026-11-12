#include "testsupport.h"
#include "shellstate.h"
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>
#include <cstdio>
Q_IMPORT_QML_PLUGIN(LanglernShellPlugin)

namespace {
// Child-process roles for storage_tests: a writer killed before QSaveFile commits,
// and a session killed after autosave (then reopened by a second process).
int interruptedWrite(QGuiApplication& app,const QString& path) {
    QSaveFile f(path);
    if(!f.open(QIODevice::WriteOnly) || f.write("partial")!=7) return 9;
    QFile ready(path+".ready"); if(!ready.open(QIODevice::WriteOnly)) return 10; ready.write("ready"); ready.close();
    return app.exec(); // Parent kills the process before commit.
}
int crashSession(QGuiApplication& app,bool writing,const QString& root,const QString& pdf) {
    setTestEnvironment("LANGLERN_WORKSPACE_DIR",root);
    ShellState shell; shell.selectFile(QUrl::fromLocalFile(pdf));
    QTimer timer; timer.setInterval(20); bool created=false;
    QObject::connect(&timer,&QTimer::timeout,&app,[&] {
        if(!shell.storageReady() || !shell.annotations()->geometry(0).valid) return;
        if(!writing) {
            if(shell.annotations()->snapshot().size()!=1 || shell.annotations()->snapshot().first().text!=QStringLiteral("crash 恢复")) app.exit(4);
            else { shell.beginShutdown(); QObject::connect(&shell,&ShellState::shutdownChanged,&app,[&] { if(shell.shutdownComplete()) app.quit(); }); }
            timer.stop();
        } else if(!created) {
            const auto id=shell.annotations()->create(0,.2,.3); shell.annotations()->setText(id,QStringLiteral("crash 恢复"),24); shell.finishTextEditing(); created=true;
        } else if(shell.storageStatus().contains(QStringLiteral("已保存"))) {
            QFile marker(root+"/saved.ready"); if(!marker.open(QIODevice::WriteOnly)) app.exit(5); marker.write("ready"); marker.close(); timer.stop();
        }
    });
    timer.start(); QTimer::singleShot(12000,&app,[&] { app.exit(6); }); return app.exec();
}
void usage() {
    std::fprintf(stderr,"usage: langlern_tests <suite|all> [QtTest options and test functions]\nsuites:");
    for(const auto& [name,factory]:TestSuites::registry()) std::fprintf(stderr," %s",name.constData());
    std::fprintf(stderr,"\n");
}
}

int main(int argc,char** argv) {
    // Deterministic Qt dialog automation; the product keeps the system-native default.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QGuiApplication app(argc,argv); configureTestApplication(app);
    if(argc==3 && QByteArray(argv[1])=="--interrupted-write") return interruptedWrite(app,QString::fromLocal8Bit(argv[2]));
    if(argc==4 && (QByteArray(argv[1])=="--crash-session" || QByteArray(argv[1])=="--read-session"))
        return crashSession(app,QByteArray(argv[1])=="--crash-session",QString::fromLocal8Bit(argv[2]),QString::fromLocal8Bit(argv[3]));
    if(argc<2) { usage(); return 2; }
    const QByteArray suite(argv[1]);
    QStringList rest; rest<<QString::fromLocal8Bit(argv[0]);
    for(int i=2;i<argc;++i) rest<<QString::fromLocal8Bit(argv[i]);
    QTemporaryDir workspace; setTestEnvironment("LANGLERN_WORKSPACE_DIR",workspace.path());
    qRegisterMetaType<AnnotationSnapshot>();
    int failures=0;
    for(const auto& [name,factory]:TestSuites::registry()) {
        if(suite!="all" && suite!=name) continue;
        std::unique_ptr<QObject> tests(factory());
        failures+=QTest::qExec(tests.get(),rest);
        if(suite!="all") return failures;
    }
    if(suite!="all") { usage(); return 2; }
    return failures;
}
