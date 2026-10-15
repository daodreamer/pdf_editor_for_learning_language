#include "worker.h"
#include "pageitem.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QJsonDocument>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <QDir>
#include <QInputMethodEvent>
#include <QFile>

int main(int argc, char** argv) {
    QElapsedTimer clock;
    clock.start();
    QGuiApplication app(argc, argv);
    if (app.arguments().size() < 2) { qCritical("Usage: phase0_quick_probe PDF [screenshot.png]"); return 2; }
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#elif defined(Q_OS_MACOS)
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
#endif
    QQmlApplicationEngine engine;
    engine.loadFromModule("PhaseZero", "Main");
    if (engine.rootObjects().isEmpty()) return 2;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    auto* item = window->findChild<PageItem*>("pageItem");
    if (!item) return 2;
    QThread thread;
    auto* worker = new phase0::PdfWorker;
    worker->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    bool closing = false;
    int status = 0;
    auto stop = [&] {
        if (closing) return;
        closing = true;
        worker->cancel();
        QMetaObject::invokeMethod(worker, [&thread] { thread.quit(); }, Qt::QueuedConnection);
    };
    app.setQuitOnLastWindowClosed(false);
    QObject::connect(window, &QQuickWindow::closing, &app, stop);
    QObject::connect(&thread, &QThread::finished, &app, [&] { app.exit(status); });
    QObject::connect(window, &QQuickWindow::sceneGraphInitialized, window, [window] {
        qInfo() << "actual_graphics_api" << window->rendererInterface()->graphicsApi()
                << "dpr" << window->devicePixelRatio();
    }, Qt::DirectConnection);
    QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&] {
        static bool first = true;
        if (first) { first = false; qInfo() << "first_frame_submitted_ms" << clock.nsecsElapsed()/1e6; }
    });
    QObject::connect(worker, &phase0::PdfWorker::discarded, &app, [](quint64 id) { qInfo() << "discarded" << id; });
    QObject::connect(worker, &phase0::PdfWorker::trace, &app, [](const QJsonObject& meta) {
        qInfo().noquote() << "worker_trace" << QJsonDocument(meta).toJson(QJsonDocument::Compact);
    });
    int received = 0;
    auto request = [&](quint64 id, int width) {
        qInfo() << "request_ns" << phase0::monotonicNs() << "generation" << id;
        phase0::Request r;
        r.generation = id; r.path = app.arguments().at(1); r.width = width;
        worker->replace(std::move(r));
        QMetaObject::invokeMethod(worker, &phase0::PdfWorker::drain, Qt::QueuedConnection);
    };
    QObject::connect(worker, &phase0::PdfWorker::started, &app, [&](quint64 id) {
        if (id == 2 && !closing) request(3, 1400);
    });
    QObject::connect(worker, &phase0::PdfWorker::result, &app,
                     [&](quint64 id, QImage image, const QJsonObject& meta) {
        if (closing || !worker->current(id)) { qInfo() << "ui_rejected" << id; return; }
        qInfo().noquote() << QJsonDocument(meta).toJson(QJsonDocument::Compact);
        if (meta.contains("error")) { status = 1; stop(); return; }
        item->present(std::move(image));
        qInfo() << "image_delivered_ms" << clock.nsecsElapsed()/1e6 << "generation" << id;
        ++received;
        if (received == 1) {
            // Real in-flight replacement: new target after a queued render starts.
            request(2, 2600);
        } else if (id == 3) {
            if (qEnvironmentVariableIsSet("PHASE0_SYNTHETIC_IME")) {
                auto* editor = window->findChild<QQuickItem*>("imeInput");
                if (!editor) { status = 1; stop(); return; }
                editor->forceActiveFocus();
                const auto phrase = QString::fromUtf8("\xE4\xB8\xAD\xE6\x96\x87");
                QInputMethodEvent preedit(phrase, {});
                QCoreApplication::sendEvent(editor, &preedit);
                const bool composing = editor->property("inputMethodComposing").toBool()
                    && editor->property("text").toString().isEmpty();
                QInputMethodEvent commit;
                commit.setCommitString(phrase);
                QCoreApplication::sendEvent(editor, &commit);
                const bool committed = editor->property("text").toString() == phrase
                    && !editor->property("inputMethodComposing").toBool();
                qInfo() << "synthetic_ime_preedit_ok" << composing << "commit_ok" << committed;
                if (!composing || !committed) status = 1;
            }
            window->setPersistentSceneGraph(false);
            window->setPersistentGraphics(false);
            if (!qEnvironmentVariableIsSet("PHASE0_MANUAL")) {
                QTimer::singleShot(200, &app, [&] { window->hide(); window->releaseResources(); });
                QTimer::singleShot(450, &app, [&] { window->show(); item->update(); });
            }
            if (app.arguments().size() > 2) {
                QTimer::singleShot(1000, &app, [&] {
                    const auto shot = window->grabWindow();
                    QFile screenshot(app.arguments().at(2));
                    const bool saved = screenshot.open(QIODevice::WriteOnly | QIODevice::NewOnly)
                        && shot.save(&screenshot, "PNG");
                    if (!saved) status = 1;
                    qInfo() << "screenshot_saved" << saved;
                    stop();
                });
            }
        }
    });
    thread.start();
    QTimer::singleShot(0, &app, [&] { request(1, 700); });
    // A manual acceptance session must remain open for real IME/viewer checks.
    if (!qEnvironmentVariableIsSet("PHASE0_MANUAL")) {
        QTimer::singleShot(60000, &app, [&] { status = 3; stop(); });
    }
    return app.exec();
}
