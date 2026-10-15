#include <QtTest>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTransform>
#include <QProcess>
#include <cstdio>

class Contracts final : public QObject {
    Q_OBJECT
private slots:
    void processDeathBeforeCommit() {
        QTemporaryDir dir;
        const auto path = dir.filePath("snapshot.json");
        QFile baseline(path);
        QVERIFY(baseline.open(QIODevice::WriteOnly));
        QCOMPARE(baseline.write("revision-1"), qint64(10));
        baseline.close();
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--snapshot-child", path});
        QVERIFY(child.waitForReadyRead(10000));
        QCOMPARE(child.readAllStandardOutput().trimmed(), QByteArray("uncommitted"));
        child.kill();
        QVERIFY(child.waitForFinished(10000));
        QVERIFY(baseline.open(QIODevice::ReadOnly));
        QCOMPARE(baseline.readAll(), QByteArray("revision-1"));
    }
    void coordinateTransforms() {
        for (int angle : {0, 90, 180, 270}) for (double dpr : {1., 1.25, 1.5, 2.}) {
            QTransform transform;
            transform.translate(31, -45);
            transform.scale(1.7*dpr, -1.7*dpr);
            transform.rotate(angle);
            transform.translate(-19, 23);
            bool ok = false;
            const auto inverse = transform.inverted(&ok);
            QVERIFY(ok);
            for (QPointF p : {QPointF(-31, 23), QPointF(0, 0), QPointF(595, 842)}) {
                const auto back = inverse.map(transform.map(p));
                QVERIFY(QLineF(p, back).length() <= 1e-6);
            }
        }
    }
    void atomicSnapshot() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("snapshot.json");
        auto commit = [&](int revision) {
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly)) return false;
            const auto bytes = QJsonDocument(QJsonObject{{"schema",1},{"revision",revision}}).toJson();
            return file.write(bytes) == bytes.size() && file.commit();
        };
        QVERIFY(commit(1));
        {
            QSaveFile interrupted(path);
            QVERIFY(interrupted.open(QIODevice::WriteOnly));
            QVERIFY(interrupted.write("truncated") > 0);
            interrupted.cancelWriting();
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object()["revision"].toInt(),1);
        file.close();
        QVERIFY(commit(2));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object()["revision"].toInt(),2);
        QSaveFile invalid(dir.filePath("missing/target"));
        QVERIFY(!invalid.open(QIODevice::WriteOnly));
    }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().value(1) == "--snapshot-child") {
        QSaveFile file(app.arguments().value(2));
        if (!file.open(QIODevice::WriteOnly) || file.write("revision-2") != 10) return 1;
        std::puts("uncommitted");
        std::fflush(stdout);
        return app.exec(); // Parent kills us with an uncommitted temporary file.
    }
    Contracts tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "contracts.moc"
