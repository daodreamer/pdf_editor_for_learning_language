#include "shellstate.h"

#include <QFile>
#include <QCryptographicHash>
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
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

Q_IMPORT_QML_PLUGIN(LanglernShellPlugin)

class ShellTests : public QObject {
    Q_OBJECT
private slots:
    void shutdownOnPreview() {
        ShellState shell;
        QSignalSpy closed(&shell, &ShellState::documentClosed);
        QSignalSpy discarded(&shell, &ShellState::renderDiscarded);
        shell.setRenderWidth(8192);
        bool requested = false;
        connect(&shell, &ShellState::imageChanged, &shell, [&] {
            if (!requested && !shell.pageImage().isNull() && shell.pageImage().width() == 480) {
                requested = true;
                shell.beginShutdown();
            }
        });
        shell.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES) + "/scan.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(shell.shutdownComplete(), 10000);
        QVERIFY(requested);
        QTRY_VERIFY(!discarded.isEmpty());
        QVERIFY(!closed.isEmpty());
        QCOMPARE(closed.last().at(1).toInt(), 0);
    }
    void largeFileSmoke() {
        const auto path = qEnvironmentVariable("LANGLERN_LARGE_PDF");
        if (path.isEmpty()) QSKIP("Set LANGLERN_LARGE_PDF to run the local large scanned document check");
        ShellState shell;
        shell.selectFile(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(!shell.busy(), 60000);
        QVERIFY2(shell.errorCode().isEmpty(), qPrintable(shell.statusText()));
        QVERIFY(!shell.pageImage().isNull());
        QVERIFY(shell.pageCount() > 1);
        shell.goToPage(shell.pageCount());
        QTRY_VERIFY_WITH_TIMEOUT(!shell.busy(), 60000);
        QVERIFY(shell.errorCode().isEmpty());
        QVERIFY(!shell.pageImage().isNull());
        shell.beginShutdown();
        QTRY_VERIFY_WITH_TIMEOUT(shell.shutdownComplete(), 60000);
    }
    void documentLifecycle() {
        const QString fixtures = QStringLiteral(LANGLERN_FIXTURES);
        QFile original(fixtures + "/geometry.pdf");
        QVERIFY(original.open(QIODevice::ReadOnly));
        const auto hash = QCryptographicHash::hash(original.readAll(), QCryptographicHash::Sha256);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto unicode = dir.filePath(QStringLiteral("只读 中文教材.PDF"));
        QVERIFY(QFile::copy(original.fileName(), unicode));
        QVERIFY(QFile::setPermissions(unicode, QFile::ReadOwner | QFile::ReadUser | QFile::ReadGroup | QFile::ReadOther));
        ShellState shell;
        QSignalSpy closed(&shell, &ShellState::documentClosed);
        QSignalSpy started(&shell, &ShellState::renderStarted);
        QSignalSpy discarded(&shell, &ShellState::renderDiscarded);
        shell.selectFile(QUrl::fromLocalFile(unicode));
        QTRY_VERIFY_WITH_TIMEOUT(!shell.busy(), 10000);
        QVERIFY2(shell.errorCode().isEmpty(), qPrintable(shell.statusText()));
        QCOMPARE(shell.pageCount(), 8);
        QCOMPARE(shell.currentPage(), 1);
        QVERIFY(!shell.pageImage().isNull());
        QVERIFY(shell.pageRatio() > 1);
        const auto image = shell.pageImage();
        // Original fixture contains opaque primary colors: validate actual engine pixels.
        bool red = false, green = false, blue = false;
        for (int y = 0; y < image.height(); y += 5) for (int x = 0; x < image.width(); x += 5) {
            const auto c = image.pixelColor(x, y);
            red |= c.red() > 240 && c.green() < 15 && c.blue() < 15;
            green |= c.green() > 240 && c.red() < 15 && c.blue() < 15;
            blue |= c.blue() > 240 && c.red() < 15 && c.green() < 15;
        }
        QVERIFY(red && green && blue);
        shell.goToPage(2);
        QTRY_VERIFY(!shell.busy());
        QCOMPARE(shell.currentPage(), 2);
        QVERIFY(shell.pageRatio() < 1);
        shell.goToPage(0);
        shell.goToPage(9);
        QCOMPARE(shell.currentPage(), 2);
        shell.setRenderWidth(8192);
        QTRY_VERIFY(!shell.busy());
        QVERIFY2(shell.errorCode().isEmpty(), qPrintable(shell.statusText()));
        QVERIFY(!shell.pageImage().isNull());
        QVERIFY(shell.pageImage().sizeInBytes() <= 64 * 1024 * 1024);
        shell.setRenderWidth(800);
        QTRY_VERIFY(!shell.busy());
        for (const auto& name : {"scan.pdf", "password.pdf", "damaged.pdf"}) {
            shell.selectFile(QUrl::fromLocalFile(fixtures + '/' + name));
            QTRY_VERIFY(!shell.busy());
            if (QString::fromLatin1(name) == "scan.pdf") {
                QCOMPARE(shell.pageCount(), 1);
                QVERIFY(!shell.pageImage().isNull());
                shell.setRenderWidth(8192);
                QTRY_VERIFY(!shell.busy());
                QVERIFY2(shell.errorCode().isEmpty(), qPrintable(shell.statusText()));
                QVERIFY(!shell.pageImage().isNull());
                QVERIFY(shell.pageImage().sizeInBytes() <= 64 * 1024 * 1024);
                shell.setRenderWidth(800);
                QTRY_VERIFY(!shell.busy());
            } else {
                QCOMPARE(shell.errorCode(), QString::fromLatin1(name) == "password.pdf"
                    ? QStringLiteral("password") : QStringLiteral("format"));
                QVERIFY(shell.pageImage().isNull());
            }
        }
        // Replace on preview delivery: either the worker is rendering the target, or
        // its target is queued to the GUI. Both paths must reject obsolete results.
        shell.selectFile(QUrl::fromLocalFile(unicode));
        QTRY_VERIFY(!shell.busy());
        bool replaced = false;
        const auto interception = connect(&shell, &ShellState::imageChanged, &shell, [&] {
            if (!replaced && !shell.pageImage().isNull() && shell.pageImage().width() == 480) {
                replaced = true;
                shell.selectFile(QUrl::fromLocalFile(fixtures + "/scan.pdf"));
            }
        });
        shell.setRenderWidth(8100);
        QTRY_VERIFY(replaced);
        QTRY_VERIFY(!shell.busy());
        QCOMPARE(shell.pageCount(), 1);
        QVERIFY(!shell.pageImage().isNull());
        QTRY_VERIFY(!discarded.isEmpty());
        disconnect(interception);
        const auto beforeClose = closed.size();
        shell.clearSelection();
        QTRY_VERIFY(closed.size() > beforeClose);
        QTest::qWait(150);
        QVERIFY(shell.pageImage().isNull());
        QCOMPARE(shell.pageCount(), 0);
        shell.setRenderWidth(800);
        for (int i = 0; i < 50; ++i) {
            shell.selectFile(QUrl::fromLocalFile(unicode));
            QTRY_VERIFY(!shell.busy());
            QCOMPARE(shell.pageCount(), 8);
            const auto count = closed.size();
            shell.clearSelection();
            QTRY_VERIFY(closed.size() > count);
            QCOMPARE(closed.last().at(1).toInt(), 0);
#ifdef Q_OS_WIN
            if (i == 0 || i == 9 || i == 24 || i == 49) {
                PROCESS_MEMORY_COUNTERS_EX memory{};
                memory.cb = sizeof(memory);
                QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)));
                qInfo() << "closed_cycle" << i + 1 << "working_set_bytes" << memory.WorkingSetSize
                        << "private_bytes" << memory.PrivateUsage << "peak_working_set_bytes" << memory.PeakWorkingSetSize;
            }
#endif
        }
        original.seek(0);
        QCOMPARE(QCryptographicHash::hash(original.readAll(), QCryptographicHash::Sha256), hash);
        QFile copied(unicode);
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(copied.readAll(), QCryptographicHash::Sha256), hash);
        copied.close();
        QFile::setPermissions(unicode, QFile::ReadOwner | QFile::WriteOwner);
        shell.selectFile(QUrl::fromLocalFile(unicode));
        shell.beginShutdown();
        QTRY_VERIFY_WITH_TIMEOUT(shell.shutdownComplete(), 10000);
        qInfo() << "50 open/close cycles; closed events" << closed.size() << "discarded" << discarded.size();
    }
    void selectionBoundary() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath(QStringLiteral("中文教材.PDF"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray content("not a PDF; invalid input");
        QCOMPARE(file.write(content), content.size());
        file.close();
        ShellState shell;
        shell.selectFile(QUrl::fromLocalFile(path));
        QCOMPARE(shell.selectedName(), QStringLiteral("中文教材.PDF"));
        QTRY_VERIFY(!shell.busy());
        QCOMPARE(shell.errorCode(), QStringLiteral("format"));
        shell.selectFile({}); // cancelled selection preserves state
        QCOMPARE(shell.selectedName(), QStringLiteral("中文教材.PDF"));
        shell.selectFile(QUrl(QStringLiteral("https://example.com/a.pdf")));
        QVERIFY(shell.statusText().contains(QStringLiteral("请选择")));
        shell.selectFile(QUrl::fromLocalFile(dir.path()));
        QTRY_VERIFY(!shell.busy());
        QVERIFY(!shell.errorCode().isEmpty());
        shell.selectFile(QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.pdf"))));
        QTRY_VERIFY(!shell.busy());
        QCOMPARE(shell.errorCode(), QStringLiteral("file"));
        shell.clearSelection();
        QVERIFY(shell.selectedName().isEmpty());
        QVERIFY(shell.statusText().isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), content);
        shell.beginShutdown();
        QTRY_VERIFY(shell.shutdownComplete());
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
        QTRY_VERIFY(!shell.busy());
        QVERIFY(!shell.errorCode().isEmpty());
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        window->contentItem()->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_W, modifier);
        QTRY_VERIFY(shell.selectedName().isEmpty());
        window->resize(640, 480);
        QTest::qWait(100);
        QCOMPARE(window->size(), QSize(640, 480));
        shell.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES) + "/geometry.pdf"));
        QTRY_VERIFY(!shell.busy());
        QCOMPARE(shell.pageCount(), 8);
        QTest::qWait(150);
        auto* page = window->findChild<QQuickItem*>(QStringLiteral("pdfPage"));
        QVERIFY(page);
        QVERIFY(page->isVisible());
        QVERIFY(!window->grabWindow().isNull());
        if (!screenshotDir.isEmpty()) {
            QFile output(screenshotDir + "/pdf.png");
            QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::NewOnly));
            QVERIFY(window->grabWindow().save(&output, "PNG"));
        }
        // Force destruction and reconstruction of the scene graph, then show the retained image.
        window->setPersistentSceneGraph(false);
        window->setPersistentGraphics(false);
        window->hide();
        window->releaseResources();
        QTest::qWait(150);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(150);
        const auto restored = window->grabWindow();
        QVERIFY(!restored.isNull());
        bool restoredRed = false, restoredBlue = false;
        for (int y = 0; y < restored.height(); y += 5) for (int x = 0; x < restored.width(); x += 5) {
            const auto c = restored.pixelColor(x, y);
            restoredRed |= c.red() > 240 && c.green() < 15 && c.blue() < 15;
            restoredBlue |= c.blue() > 240 && c.red() < 15 && c.green() < 15;
        }
        QVERIFY(restoredRed && restoredBlue);
        shell.goToPage(2);
        QTRY_VERIFY(!shell.busy());
        shell.cycleTheme();
        shell.cycleTheme();
        QTest::qWait(150);
        if (!screenshotDir.isEmpty()) {
            QFile output(screenshotDir + "/pdf-rotated-dark.png");
            QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::NewOnly));
            QVERIFY(window->grabWindow().save(&output, "PNG"));
        }
        qInfo() << "Window logical size" << window->size() << "DPR" << window->devicePixelRatio()
                << "graphics API" << window->rendererInterface()->graphicsApi();
        QCOMPARE(warnings.size(), 0);
        window->close();
        QTRY_VERIFY(shell.shutdownComplete());
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
