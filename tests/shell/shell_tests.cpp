#include "shellstate.h"

#include <QFile>
#include <QGuiApplication>
#include <QFont>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(LanglernShellPlugin)

class ShellTests : public QObject {
    Q_OBJECT
private slots:
    void selectionBoundary() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath(QStringLiteral("中文教材.PDF"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray content("not a PDF; stage 1 must never decode it");
        QCOMPARE(file.write(content), content.size());
        file.close();
        ShellState shell;
        shell.selectFile(QUrl::fromLocalFile(path));
        QCOMPARE(shell.selectedName(), QStringLiteral("中文教材.PDF"));
        QVERIFY(shell.statusText().contains(QStringLiteral("尚未加载")));
        shell.selectFile({}); // cancelled selection preserves state
        QCOMPARE(shell.selectedName(), QStringLiteral("中文教材.PDF"));
        shell.selectFile(QUrl(QStringLiteral("https://example.com/a.pdf")));
        QVERIFY(shell.statusText().contains(QStringLiteral("请选择")));
        shell.selectFile(QUrl::fromLocalFile(dir.path()));
        QVERIFY(shell.statusText().contains(QStringLiteral("请选择")));
        shell.selectFile(QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.pdf"))));
        QVERIFY(shell.statusText().contains(QStringLiteral("请选择")));
        shell.clearSelection();
        QVERIFY(shell.selectedName().isEmpty());
        QVERIFY(shell.statusText().isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("not a PDF; stage 1 must never decode it"));
    }
    void windowInteractions() {
        ShellState shell;
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.setInitialProperties({{QStringLiteral("shell"), QVariant::fromValue(&shell)}});
        engine.loadFromModule(QStringLiteral("Langlern.Shell"), QStringLiteral("Main"));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        const auto screenshotDir = qEnvironmentVariable("LANGLERN_SCREENSHOT_DIR");
        if (!screenshotDir.isEmpty()) {
            const auto save = [&](const QString& name) {
                QFile output(screenshotDir + '/' + name);
                return output.open(QIODevice::WriteOnly | QIODevice::NewOnly)
                    && window->grabWindow().save(&output, "PNG");
            };
            shell.cycleTheme();
            QTest::qWait(150);
            QVERIFY(save(QStringLiteral("light.png")));
            shell.cycleTheme();
            QTest::qWait(150);
            QVERIFY(save(QStringLiteral("dark.png")));
            shell.cycleTheme();
        }
        const auto modifier = Qt::ControlModifier;
        QTest::keyClick(window, Qt::Key_L, modifier | Qt::ShiftModifier);
        QTRY_COMPARE(shell.themeMode(), 1);
        QVERIFY(!shell.dark());
        QTest::keyClick(window, Qt::Key_L, modifier | Qt::ShiftModifier);
        QTRY_COMPARE(shell.themeMode(), 2);
        QVERIFY(shell.dark());
        QTest::keyClick(window, Qt::Key_L, modifier | Qt::ShiftModifier);
        QTRY_COMPARE(shell.themeMode(), 0);
        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(window->activeFocusItem());
        const auto* initialFocus = window->activeFocusItem();
        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(window->activeFocusItem() != initialFocus);
        QTest::keyClick(window, Qt::Key_F1);
        auto* help = window->findChild<QObject*>(QStringLiteral("helpLoader"));
        QVERIFY(help);
        QTRY_VERIFY(help->property("active").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!help->property("active").toBool());
        QTest::keyClick(window, Qt::Key_O, modifier);
        auto* chooser = window->findChild<QObject*>(QStringLiteral("fileLoader"));
        QVERIFY(chooser);
        QTRY_VERIFY(chooser->property("active").toBool());
        auto* dialog = chooser->property("item").value<QObject*>();
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTest::qWait(300);
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTest::qWait(300);
        QVERIFY(shell.selectedName().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(window, "chooseFile"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTest::qWait(300);
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile selected(dir.filePath(QStringLiteral("选择结果.pdf")));
        QVERIFY(selected.open(QIODevice::WriteOnly));
        selected.close();
        QVERIFY(dialog->setProperty("selectedFile", QUrl::fromLocalFile(selected.fileName())));
        // Verify the QML result route separately from native OS selection.
        QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
        QTRY_COMPARE(shell.selectedName(), QStringLiteral("选择结果.pdf"));
        QVERIFY(shell.statusText().contains(QStringLiteral("尚未加载")));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        window->contentItem()->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_W, modifier);
        QTRY_VERIFY(shell.selectedName().isEmpty());
        window->resize(640, 480);
        QTest::qWait(100);
        QCOMPARE(window->size(), QSize(640, 480));
        qInfo() << "Window logical size" << window->size() << "DPR" << window->devicePixelRatio()
                << "graphics API" << window->rendererInterface()->graphicsApi();
        QCOMPARE(warnings.size(), 0);
        window->close();
    }
};

int main(int argc, char** argv) {
    // Deterministic Qt dialog automation; the product keeps the system-native default.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QGuiApplication app(argc, argv);
#ifdef Q_OS_WIN
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI")));
#endif
    QQuickStyle::setStyle(QStringLiteral("Basic"));
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#endif
    ShellTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "shell_tests.moc"
