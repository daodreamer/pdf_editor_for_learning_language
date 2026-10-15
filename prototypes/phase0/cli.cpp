#include "worker.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QThread>
#include <QTimer>
#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Stage 0 real PDF worker probe; no product UI");
    parser.addHelpOption();
    parser.addPositionalArgument("pdf", "Local input PDF");
    parser.addOptions({{{"o", "output"}, "Output directory", "dir"},
                       {"width", "Pixel width", "pixels", "1000"},
                       {"page", "Zero-based page", "index", "0"},
                       {"repeat", "Open/render/close iterations", "count", "1"},
                       {"font", "TTF font for export", "path"},
                       {"export", "New PDF target (must not exist)", "path"},
                       {"burst", "Replace demand 100 times before processing"}});
    parser.process(app);
    if (parser.positionalArguments().size() != 1) parser.showHelp(2);
    bool widthOk = false, pageOk = false, repeatOk = false;
    const int width = parser.value("width").toInt(&widthOk);
    const int page = parser.value("page").toInt(&pageOk);
    const int repeats = parser.value("repeat").toInt(&repeatOk);
    if (!widthOk || !pageOk || !repeatOk || repeats < 1 || repeats > 1000) return 2;
    const QString output = parser.value("output");
    if (!output.isEmpty() && !QDir().mkpath(output)) return 2;
    QFile events(output.isEmpty() ? QString() : output + "/events.jsonl");
    if (!output.isEmpty() && !events.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return 2;
    QThread thread;
    auto* worker = new phase0::PdfWorker;
    worker->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    int completed = 0;
    int exitCode = 0;
    quint64 generation = 0;
    auto submit = [&] {
        phase0::Request r;
        r.generation = ++generation;
        r.path = parser.positionalArguments().first();
        r.width = width; r.page = page;
        if (completed == 0) r.output = output;
        if (completed == 0) { r.font = parser.value("font"); r.exportPath = parser.value("export"); }
        worker->replace(std::move(r));
    };
    QObject::connect(worker, &phase0::PdfWorker::result, &app,
                     [&](quint64, const QImage&, const QJsonObject& meta) {
        const auto line = QJsonDocument(meta).toJson(QJsonDocument::Compact) + '\n';
        std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
        if (events.isOpen()) { events.write(line); events.flush(); }
        if (meta.contains("error")) exitCode = 1;
        if (++completed >= repeats || exitCode) {
            worker->cancel();
            QMetaObject::invokeMethod(worker, [&thread] { thread.quit(); }, Qt::QueuedConnection);
        } else {
            submit();
            QMetaObject::invokeMethod(worker, &phase0::PdfWorker::drain, Qt::QueuedConnection);
        }
    });
    QObject::connect(&thread, &QThread::finished, &app, [&] { app.exit(exitCode); });
    // Timeout reports failure; it cannot safely terminate a running native call.
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &app, [&] {
        exitCode = 3; worker->cancel();
        QMetaObject::invokeMethod(worker, [&thread] { thread.quit(); }, Qt::QueuedConnection);
    });
    deadline.start(120000);
    const int burst = parser.isSet("burst") ? 100 : 1;
    for (int i = 0; i < burst; ++i) submit();
    thread.start();
    QMetaObject::invokeMethod(worker, &phase0::PdfWorker::drain, Qt::QueuedConnection);
    return app.exec();
}
