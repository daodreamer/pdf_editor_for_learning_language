#include "legacydata.h"
#include "shellstate.h"

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStandardPaths>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(LanglernShellPlugin)

int main(int argc, char* argv[]) {
    QElapsedTimer clock;
    clock.start();
    QGuiApplication app(argc, argv);
    // Data lives in <AppLocalData>/LinguaPDF (no organization level). Versions before 1.0
    // used "Langlern/Langlern"; move it once so existing practice data stays visible.
    app.setApplicationName(QStringLiteral("LinguaPDF"));
    app.setApplicationVersion(QStringLiteral(LINGUAPDF_VERSION));
    if(!qEnvironmentVariableIsSet("LANGLERN_WORKSPACE_DIR")) {
        const auto legacy=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+QStringLiteral("/Langlern/Langlern");
        const auto moved=migrateLegacyData(legacy,QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
        if(!moved.detail.isEmpty()) qInfo().noquote() << "legacy_data:" << moved.detail;
        if(moved.useLegacy) {
            app.setOrganizationName(QStringLiteral("Langlern"));
            app.setApplicationName(QStringLiteral("Langlern"));
        }
    }
    // Title bar, taskbar and Alt+Tab use this window icon; Explorer uses the .rc icon.
    // The startup log reports how many sizes really loaded so packaging can verify it.
    int loadedIconSizes = 0;
    {
        QIcon icon;
        for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
            const auto path = QStringLiteral(":/icons/linguapdf-%1.png").arg(size);
            if (QImage(path).isNull()) continue;
            icon.addFile(path, QSize(size, size));
            ++loadedIconSizes;
        }
        app.setWindowIcon(icon);
    }
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
    parser.setApplicationDescription(QStringLiteral("LinguaPDF — offline PDF reader for language practice"));
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
    log(QStringLiteral("application_ready"), {{QStringLiteral("qt"), QString::fromLatin1(qVersion())},
                                              {QStringLiteral("window_icon_sizes"), loadedIconSizes}});
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
