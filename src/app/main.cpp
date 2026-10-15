#include "shellstate.h"

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(LanglernShellPlugin)

int main(int argc, char* argv[]) {
    QElapsedTimer clock;
    clock.start();
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Langlern"));
    app.setOrganizationName(QStringLiteral("Langlern"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
#ifdef Q_OS_WIN
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI")));
#endif
    QQuickStyle::setStyle(QStringLiteral("Basic"));
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#elif defined(Q_OS_MACOS)
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
#endif
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Langlern PDF reader"));
    parser.addPositionalArgument(QStringLiteral("pdf"), QStringLiteral("Local PDF to open after showing the window"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("startup-probe"), QStringLiteral("Measure a synthetic Qt input roundtrip after the first frame")});
    parser.addOption({QStringLiteral("quit-after-ready"), QStringLiteral("Exit after startup probe (requires --startup-probe)")});
    parser.process(app);
    const bool probe = parser.isSet(QStringLiteral("startup-probe"));
    if (parser.isSet(QStringLiteral("quit-after-ready")) && !probe) return 2;
    const auto log = [&clock](const QString& event, QJsonObject fields = {}) {
        fields.insert(QStringLiteral("event"), event);
        fields.insert(QStringLiteral("main_ms"), static_cast<double>(clock.nsecsElapsed()) / 1e6);
        fields.insert(QStringLiteral("build_type"), QStringLiteral(LANGLERN_BUILD_TYPE));
        qInfo().noquote() << QJsonDocument(fields).toJson(QJsonDocument::Compact);
    };
    log(QStringLiteral("application_ready"), {{QStringLiteral("qt"), QString::fromLatin1(qVersion())}});
    ShellState shell;
    QQmlApplicationEngine engine;
    log(QStringLiteral("engine_ready"));
    engine.setInitialProperties({{QStringLiteral("shell"), QVariant::fromValue(&shell)},
                                 {QStringLiteral("probeEnabled"), probe}});
    engine.loadFromModule(QStringLiteral("Langlern.Shell"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return 1;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) return 1;
    log(QStringLiteral("qml_ready"));
    bool firstFrame = true;
    bool acknowledged = false;
    QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&] {
        if (!firstFrame) return;
        firstFrame = false;
        log(QStringLiteral("first_frame_submitted"), {
            {QStringLiteral("graphics_api"), static_cast<int>(window->rendererInterface()->graphicsApi())},
            {QStringLiteral("dpr"), window->devicePixelRatio()}});
        if (probe) {
            QCoreApplication::postEvent(window, new QKeyEvent(QEvent::KeyPress, Qt::Key_F6, Qt::NoModifier));
            QCoreApplication::postEvent(window, new QKeyEvent(QEvent::KeyRelease, Qt::Key_F6, Qt::NoModifier));
        }
    }, Qt::QueuedConnection);
    QObject::connect(&shell, &ShellState::startupAcknowledged, &app, [&] {
        acknowledged = true;
        log(QStringLiteral("interactive_proxy"));
        if (parser.isSet(QStringLiteral("quit-after-ready"))) app.quit();
    });
    if (probe) {
        QTimer::singleShot(10000, &app, [&] {
            if (!acknowledged) { log(QStringLiteral("probe_timeout")); app.exit(3); }
        });
    }
    log(QStringLiteral("window_show_requested"));
    window->show();
    if (!parser.positionalArguments().isEmpty()) {
        const auto path = parser.positionalArguments().first();
        QTimer::singleShot(0, &shell, [&shell, path] { shell.selectFile(QUrl::fromLocalFile(path)); });
    }
    log(QStringLiteral("window_show_returned"));
    return app.exec();
}
