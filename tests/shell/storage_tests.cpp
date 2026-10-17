#include "testapplication.h"
#include "shellstate.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlApplicationEngine>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QtQml/QQmlExtensionPlugin>
#include <cstdlib>
#include <filesystem>
#include <QElapsedTimer>
Q_IMPORT_QML_PLUGIN(LanglernShellPlugin)

class StorageTests : public QObject {
    Q_OBJECT
    const QString fixture=QStringLiteral(LANGLERN_FIXTURES)+"/text.pdf";
    static void put(const QString& path,const QByteArray& bytes) {
        QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes),bytes.size());
    }
    static AnnotationSnapshot sample() {
        TextAnnotation n; n.id=QUuid::createUuid().toString(QUuid::WithoutBraces); n.page=0;
        n.anchor={120,560}; n.right={1,0}; n.down={0,-1}; n.text=QStringLiteral("Bonjour 中文练习 e\u0301\n第二行 answer"); n.height=80;
        n.styles={{0,7,22,QColor("#e5484d"),true,true},{8,4,18,QColor("#30a46c"),false,false}};
        InkStroke i; i.id=QUuid::createUuid().toString(QUuid::WithoutBraces); i.page=0; i.points={{10,40},{100,60}}; i.color=QColor(220,20,60,128); i.width=5;
        return {{n},{i},12};
    }
private slots:
    void bulkSnapshotDiskRoundtrip() {
        QTemporaryDir dir;
        AnnotationSnapshot bulk; bulk.revision=4096;
        const auto one=sample();
        for (int index=0;index<2048;++index) {
            auto note=one.notes.first(); note.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            note.order=index; note.page=index%8; bulk.notes.append(std::move(note));
        }
        for (int index=0;index<64;++index) {
            auto ink=one.ink.first(); ink.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            ink.order=index; ink.page=index%8; ink.points.clear();
            for (int point=0;point<4096;++point) ink.points.append({20.0+point%400,40.0+(point%50)*.25});
            bulk.ink.append(std::move(ink));
        }
        StorageWorker store(dir.path());
        QSignalSpy loaded(&store,&StorageWorker::loaded), saved(&store,&StorageWorker::saved);
        store.open(1,fixture); QVERIFY(loaded.last()[4].toBool());
        QElapsedTimer timer; timer.start(); store.save(1,bulk);
        const double saveMs=timer.nsecsElapsed()/1e6;
        QVERIFY2(saved.last()[2].toString().isEmpty(),qPrintable(saved.last()[2].toString()));
        store.close(); timer.restart(); store.open(2,fixture);
        const double restoreMs=timer.nsecsElapsed()/1e6;
        QVERIFY(loaded.last()[4].toBool());
        const auto restored=qvariant_cast<AnnotationSnapshot>(loaded.last()[1]);
        QCOMPARE(restored.notes,bulk.notes); QCOMPARE(restored.ink,bulk.ink); QCOMPARE(restored.revision,bulk.revision);
        const auto bytes=QFileInfo(dir.filePath(QString::fromLatin1(StorageWorker::digest(fixture))+".json")).size();
        qInfo()<<"bulk_snapshot notes"<<bulk.notes.size()<<"strokes"<<bulk.ink.size()<<"points"<<262144
               <<"file_bytes"<<bytes<<"disk_save_ms"<<saveMs<<"disk_restore_ms"<<restoreMs;
    }
    void codecAndValidation() {
        const auto original=sample(); const auto identity=StorageWorker::digest(fixture);
        const auto encoded=StorageWorker::encode(original,identity); AnnotationSnapshot restored; QString error;
        QVERIFY(StorageWorker::decode(encoded,identity,restored,error)); QCOMPARE(restored.notes,original.notes); QCOMPARE(restored.ink,original.ink); QCOMPARE(restored.revision,original.revision);
        QVERIFY(!StorageWorker::decode(encoded,"wrong",restored,error));
        auto growing=original; growing.notes[0].widthLocked=false; growing.notes[0].minimumHeight=60; growing.notes[0].maximumWidth=240;
        growing.notes[0].styles={{0,7,22,QColor("#e5484d"),true,true},{8,4,18,QColor("#30a46c"),false,false}};
        QVERIFY(StorageWorker::decode(StorageWorker::encode(growing,identity),identity,restored,error));
        QCOMPARE(restored.notes,growing.notes);
        auto invalidWidth=growing; invalidWidth.notes[0].maximumWidth=-1;
        QVERIFY(!StorageWorker::decode(StorageWorker::encode(invalidWidth,identity),identity,restored,error));
        auto object=QJsonDocument::fromJson(encoded).object(); object["schema"]=2;
        QVERIFY(!StorageWorker::decode(QJsonDocument(object).toJson(),identity,restored,error)); QVERIFY(error.contains(QStringLiteral("版本")));
        object=QJsonDocument::fromJson(encoded).object(); object["payload"]=QStringLiteral("{}");
        QVERIFY(!StorageWorker::decode(QJsonDocument(object).toJson(),identity,restored,error));
        auto invalid=original; invalid.notes[0].right={0,0};
        QVERIFY(!StorageWorker::decode(StorageWorker::encode(invalid,identity),identity,restored,error));
        invalid=original; invalid.ink.append(invalid.ink.first());
        QVERIFY(!StorageWorker::decode(StorageWorker::encode(invalid,identity),identity,restored,error));
        invalid=original; invalid.notes[0].styles={{0,2,14,Qt::red,true,false},{1,3,14,Qt::blue,false,true}};
        QVERIFY(!StorageWorker::decode(StorageWorker::encode(invalid,identity),identity,restored,error));
        invalid=original; invalid.notes[0].styles={{0,200,14,Qt::red,true,false}};
        QVERIFY(!StorageWorker::decode(StorageWorker::encode(invalid,identity),identity,restored,error));
    }
    void backupVersionsLocksAndFailures() {
        QTemporaryDir dir; const auto identity=StorageWorker::digest(fixture); const auto main=dir.filePath(QString::fromLatin1(identity)+".json");
        StorageWorker store(dir.path()); QSignalSpy loaded(&store,&StorageWorker::loaded),saved(&store,&StorageWorker::saved);
        store.open(1,fixture); QVERIFY(loaded.last()[4].toBool());
        const auto original=sample(); store.save(1,original); QVERIFY(saved.last()[2].toString().isEmpty());
        auto later=original; later.notes[0].text="later"; later.notes[0].styles.clear(); later.revision++;
        store.save(1,later); QVERIFY(saved.last()[2].toString().isEmpty());
        StorageWorker conflict(dir.path()); QSignalSpy locked(&conflict,&StorageWorker::loaded);
        conflict.open(2,fixture); QVERIFY(!locked.last()[4].toBool()); conflict.close();
        store.close(); put(main,"broken"); store.open(3,fixture);
        QVERIFY(loaded.last()[4].toBool()); QCOMPARE(qvariant_cast<AnnotationSnapshot>(loaded.last()[1]).notes,original.notes);
        QVERIFY(!QDir(dir.path()).entryList({"*.corrupt-*"},QDir::Files).isEmpty());
        store.save(3,later); QVERIFY(saved.last()[2].toString().isEmpty()); store.close();
        const QByteArray future="{\"schema\":99}"; put(main,future); store.open(4,fixture);
        QVERIFY(!loaded.last()[4].toBool()); store.save(4,later); QVERIFY(!saved.last()[2].toString().isEmpty());
        QFile preserved(main); QVERIFY(preserved.open(QIODevice::ReadOnly)); QCOMPARE(preserved.readAll(),future); store.close();
        // A directory at the backup path injects a real file-commit error without filling the host disk.
        QVERIFY(preserved.remove()); put(main,StorageWorker::encode(original,identity)); QFile::remove(main+".bak"); QVERIFY(QDir().mkdir(main+".bak"));
        store.open(5,fixture); store.save(5,later); QVERIFY(!saved.last()[2].toString().isEmpty());
        AnnotationSnapshot check; QString error; QFile intact(main); QVERIFY(intact.open(QIODevice::ReadOnly)); QVERIFY(StorageWorker::decode(intact.readAll(),identity,check,error)); QCOMPARE(check.notes,original.notes);
    }
    void killedAtomicWrite() {
        QTemporaryDir dir; const auto target=dir.filePath("snapshot.json"); const auto data=StorageWorker::encode(sample(),"identity"); put(target,data);
        QProcess child; child.start(QCoreApplication::applicationFilePath(),{"--interrupted-write",target});
        QVERIFY(child.waitForStarted()); QTRY_VERIFY(QFileInfo::exists(target+".ready")); child.kill(); QVERIFY(child.waitForFinished());
        QFile f(target); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(),data);
    }
    void actualProcessCrashRecovery() {
        QTemporaryDir dir;
        QProcess writer; writer.start(QCoreApplication::applicationFilePath(),{"--crash-session",dir.path(),fixture});
        QVERIFY(writer.waitForStarted()); QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir.filePath("saved.ready")),15000);
        writer.kill(); QVERIFY(writer.waitForFinished());
        QProcess reader; reader.start(QCoreApplication::applicationFilePath(),{"--read-session",dir.path(),fixture});
        QVERIFY(reader.waitForStarted()); QVERIFY(reader.waitForFinished(15000)); QCOMPARE(reader.exitCode(),0);
    }
    void emptyEditingDeletionIsSaved() {
        QTemporaryDir dir; qputenv("LANGLERN_WORKSPACE_DIR",dir.path().toUtf8());
        {
            ShellState shell; shell.selectFile(QUrl::fromLocalFile(fixture)); QTRY_VERIFY(shell.storageReady()); QTRY_VERIFY(shell.annotations()->geometry(0).valid);
            const auto id=shell.annotations()->create(0,.2,.3); shell.annotations()->setText(id,"a",24); shell.annotations()->setText(id,"",24);
            QTRY_VERIFY(shell.storageStatus().contains(QStringLiteral("已保存")));
            shell.finishTextEditing(); QVERIFY(shell.annotations()->snapshot().isEmpty()); shell.beginShutdown(); QTRY_VERIFY(shell.shutdownComplete());
        }
        ShellState shell; shell.selectFile(QUrl::fromLocalFile(fixture)); QTRY_VERIFY(shell.storageReady()); QVERIFY(shell.annotations()->snapshot().isEmpty());
        shell.beginShutdown(); QTRY_VERIFY(shell.shutdownComplete());
    }
    void largeDocumentStorage() {
        const auto path=qEnvironmentVariable("LANGLERN_LARGE_PDF");
        if(path.isEmpty()) QSKIP("Set LANGLERN_LARGE_PDF for the retained large-file run");
        QTemporaryDir root,output; qputenv("LANGLERN_WORKSPACE_DIR",root.path().toUtf8());
        ShellState shell; shell.updateViewport(0,700,1000,1);
        QElapsedTimer clock; clock.start(); double firstImage=-1; int ticks=0;
        QTimer heartbeat; heartbeat.setInterval(1); connect(&heartbeat,&QTimer::timeout,this,[&] { ++ticks; }); heartbeat.start();
        connect(&shell,&ShellState::imageChanged,this,[&] { if(firstImage<0 && !shell.pageImage().isNull()) firstImage=clock.nsecsElapsed()/1e6; });
        shell.selectFile(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),120000); QTRY_VERIFY_WITH_TIMEOUT(shell.annotations()->geometry(0).valid,120000);
        const double hashReady=clock.nsecsElapsed()/1e6;
        const auto id=shell.annotations()->create(0,.2,.3); QVERIFY(!id.isEmpty()); shell.annotations()->setText(id,QStringLiteral("大型教材 中文 answer"),48); shell.finishTextEditing();
        const auto notes=shell.annotations()->snapshot(); shell.saveNow(); clock.restart();
        QTRY_VERIFY_WITH_TIMEOUT(shell.storageStatus().contains(QStringLiteral("已保存")),30000); const double saveMs=clock.nsecsElapsed()/1e6;
        const auto artifactRoot=qEnvironmentVariable("LANGLERN_PHASE7_ARTIFACT_DIR");
        const auto target=(artifactRoot.isEmpty()?output.path():artifactRoot)+"/large.pdf";
        clock.restart(); shell.exportPdf(QUrl::fromLocalFile(target)); QTRY_VERIFY_WITH_TIMEOUT(!shell.exporting(),180000);
        QVERIFY2(shell.exportStatus().contains(QStringLiteral("已导出")),qPrintable(shell.exportStatus()));
        qInfo("phase7_large source_bytes=%lld pages=%d first_cpu_image_ms=%.3f identity_ready_ms=%.3f save_ms=%.3f export_ms=%.3f output_bytes=%lld gui_heartbeat_ticks=%d",
            QFileInfo(path).size(),shell.pageCount(),firstImage,hashReady,saveMs,clock.nsecsElapsed()/1e6,QFileInfo(target).size(),ticks);
        shell.clearSelection(); QTRY_VERIFY(shell.selectedName().isEmpty()); shell.selectFile(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),120000); QCOMPARE(shell.annotations()->snapshot(),notes);
        shell.beginShutdown(); QTRY_VERIFY_WITH_TIMEOUT(shell.shutdownComplete(),30000);
    }
    void autosaveReopenMoveChangeAndCloseFailure() {
        QTemporaryDir root,source; qputenv("LANGLERN_WORKSPACE_DIR",root.path().toUtf8());
        const auto path=source.filePath("教材.pdf"); QVERIFY(QFile::copy(fixture,path));
        QList<TextAnnotation> expected; QList<InkStroke> expectedInk;
        {
            ShellState shell; shell.selectFile(QUrl::fromLocalFile(path)); QTRY_VERIFY(shell.storageReady()); QTRY_VERIFY(shell.annotations()->geometry(0).valid);
            const auto id=shell.annotations()->create(0,.2,.3); QVERIFY(!id.isEmpty()); shell.annotations()->setText(id,QStringLiteral("恢复 中文 answer"),48); shell.finishTextEditing();
            QVERIFY(shell.annotations()->beginInk(0,{.1,.7},Qt::red,4)); shell.annotations()->appendInk({.4,.7}); shell.annotations()->endInk();
            expected=shell.annotations()->snapshot(); expectedInk=shell.annotations()->inkSnapshot();
            QTRY_VERIFY_WITH_TIMEOUT(shell.storageStatus().contains(QStringLiteral("已保存")),10000);
            shell.beginShutdown(); QTRY_VERIFY(shell.shutdownComplete());
        }
        const auto moved=source.filePath("移动.pdf"); QVERIFY(QFile::rename(path,moved));
        {
            ShellState shell; shell.selectFile(QUrl::fromLocalFile(moved)); QTRY_VERIFY(shell.storageReady()); QTRY_VERIFY(shell.annotations()->geometry(0).valid);
            QCOMPARE(shell.annotations()->snapshot(),expected); QCOMPARE(shell.annotations()->inkSnapshot(),expectedInk);
            QQmlApplicationEngine engine;
            engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}});
            engine.loadFromModule("Langlern.Shell","Main");
            auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(window);
            window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
            const auto exportTarget=source.filePath("before-save-failure.pdf");
            shell.exportPdf(QUrl::fromLocalFile(exportTarget));
            QTRY_VERIFY_WITH_TIMEOUT(!shell.exporting(),30000);
            QVERIFY(shell.exportStatus().contains(QStringLiteral("已导出")));
            const auto main=root.filePath(QString::fromLatin1(StorageWorker::digest(moved))+".json"); QVERIFY(QDir().mkdir(main+".bak"));
            shell.annotations()->select(expected.first().id); shell.annotations()->setText(expected.first().id,"new unsaved",48);
            shell.beginShutdown(); QTRY_VERIFY(shell.storageStatus().contains(QStringLiteral("失败"))); QVERIFY(!shell.shutdownComplete()); QCOMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("new unsaved"));
            auto* saveLabel=window->findChild<QQuickItem*>("storageStatusLabel"); QVERIFY(saveLabel);
            QVERIFY(saveLabel->isVisible());
            QCOMPARE(saveLabel->property("text").toString(),shell.storageStatus());
            QVERIFY(QDir().rmdir(main+".bak")); shell.beginShutdown(); QTRY_VERIFY(shell.shutdownComplete());
        }
        QFile modified(moved); QVERIFY(modified.open(QIODevice::Append)); modified.write("\n% changed\n"); modified.close();
        {
            ShellState shell; shell.selectFile(QUrl::fromLocalFile(moved)); QTRY_VERIFY(shell.storageReady()); QVERIFY(shell.annotations()->snapshot().isEmpty()); QVERIFY(shell.storageStatus().contains(QStringLiteral("改变")));
            shell.beginShutdown(); QTRY_VERIFY(shell.shutdownComplete());
        }
    }
    void exportAndSnapshotIsolation() {
        QTemporaryDir root,output; qputenv("LANGLERN_WORKSPACE_DIR",root.path().toUtf8());
        const auto artifactRoot=qEnvironmentVariable("LANGLERN_PHASE7_ARTIFACT_DIR");
        const auto target=(artifactRoot.isEmpty()?output.path():artifactRoot)+"/annotated.pdf";
        QVERIFY(!QFileInfo::exists(target));
        const auto originalHash=StorageWorker::digest(fixture);
        ShellState shell; shell.updateViewport(0,700,1000,1); shell.selectFile(QUrl::fromLocalFile(fixture)); QTRY_VERIFY(shell.storageReady()); QTRY_COMPARE(shell.pageCount(),8);
        AnnotationSnapshot exportSnapshot;
        for(int page=0;page<8;++page) {
            shell.goToPage(page+1); QTRY_VERIFY(shell.annotations()->geometry(page).valid);
            const auto id=shell.annotations()->create(page,.15,.3); QVERIFY(!id.isEmpty());
            shell.annotations()->setText(id,QStringLiteral("Bonjour 中文练习 e\u0301\n第二行 answer"),70); shell.finishTextEditing();
            QVERIFY(shell.annotations()->beginInk(page,{.15,.6},Qt::red,5));
            shell.annotations()->appendInk({.35,.74}); shell.annotations()->appendInk({.6,.65}); shell.annotations()->endInk();
            QVERIFY(shell.annotations()->beginInk(page,{.7,.7},Qt::blue,8)); shell.annotations()->endInk();
        }
        exportSnapshot={shell.annotations()->snapshot(),shell.annotations()->inkSnapshot(),0};
        // Exercise alpha in the data/export contract, though the MVP pen UI is opaque.
        exportSnapshot.ink[0].color.setAlpha(128); shell.annotations()->restore(exportSnapshot);
        for(auto& note:exportSnapshot.notes) if(note.page==0) note.styles={{0,7,22,QColor("#e5484d"),true,true},{8,4,18,QColor("#30a46c"),false,false}};
        shell.annotations()->restore(exportSnapshot);
        if(!artifactRoot.isEmpty()) {
            QQmlApplicationEngine engine; QStringList warnings;
            connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors) { for(const auto& e:errors) warnings.append(e.toString()); });
            engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}}); engine.loadFromModule("Langlern.Shell","Main");
            QVERIFY(!engine.rootObjects().isEmpty()); auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
            window->show(); QVERIFY(QTest::qWaitForWindowExposed(window)); shell.setZoom(.4); shell.goToPage(1); QTRY_VERIFY(!shell.busy()); QTest::qWait(200);
            QVERIFY(window->grabWindow().save(artifactRoot+"/reader-before-export.png"));
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n'))); window->hide();
        }
        QTemporaryDir linkDirectory(QFileInfo(fixture).absolutePath()+"/phase7-hardlink-XXXXXX"); QVERIFY(linkDirectory.isValid());
        const auto hardlink=linkDirectory.filePath("alias.pdf"); std::error_code linkError;
#ifdef Q_OS_WIN
        std::filesystem::create_hard_link(std::filesystem::path(fixture.toStdWString()),std::filesystem::path(hardlink.toStdWString()),linkError);
#else
        std::filesystem::create_hard_link(std::filesystem::path(fixture.toStdString()),std::filesystem::path(hardlink.toStdString()),linkError);
#endif
        QVERIFY2(!linkError,qPrintable(QString::fromStdString(linkError.message())));
        shell.exportPdf(QUrl::fromLocalFile(hardlink)); QTRY_VERIFY(!shell.exporting()); QVERIFY(shell.exportStatus().contains(QStringLiteral("禁止覆盖")));
        shell.exportPdf(QUrl::fromLocalFile(fixture)); QTRY_VERIFY(!shell.exporting()); QVERIFY(shell.exportStatus().contains(QStringLiteral("禁止覆盖")));
        QElapsedTimer exportClock; exportClock.start();
        shell.exportPdf(QUrl::fromLocalFile(target)); QVERIFY(shell.exporting());
        const auto id=exportSnapshot.notes.first().id; shell.annotations()->select(id); shell.annotations()->setText(id,"EDIT AFTER EXPORT",50); shell.finishTextEditing();
        int ticks=0; QTimer heartbeat; heartbeat.setInterval(1); connect(&heartbeat,&QTimer::timeout,this,[&]{++ticks;}); heartbeat.start();
        QTRY_VERIFY_WITH_TIMEOUT(!shell.exporting(),30000); QVERIFY2(shell.exportStatus().contains(QStringLiteral("已导出")),qPrintable(shell.exportStatus())); QVERIFY(ticks>0);
        qInfo("phase7_export pages=8 total_ms=%.3f gui_heartbeat_ticks=%d",exportClock.nsecsElapsed()/1e6,ticks);
        QCOMPARE(StorageWorker::digest(fixture),originalHash); QVERIFY(QFileInfo(target).size()>0);
        shell.exportPdf(QUrl::fromLocalFile(target)); QTRY_VERIFY(!shell.exporting()); QVERIFY(shell.exportStatus().contains(QStringLiteral("禁止覆盖")));
        auto sparse=exportSnapshot;
        sparse.notes.erase(std::remove_if(sparse.notes.begin(),sparse.notes.end(),[](const auto& n){return n.page!=0;}),sparse.notes.end());
        sparse.ink.erase(std::remove_if(sparse.ink.begin(),sparse.ink.end(),[](const auto& i){return i.page!=0;}),sparse.ink.end());
        shell.annotations()->restore(sparse);
        const auto sparseTarget=(artifactRoot.isEmpty()?output.path():artifactRoot)+"/sparse.pdf";
        shell.exportPdf(QUrl::fromLocalFile(sparseTarget)); QTRY_VERIFY_WITH_TIMEOUT(!shell.exporting(),30000);
        QVERIFY2(shell.exportStatus().contains(QStringLiteral("已导出")),qPrintable(shell.exportStatus()));
        // Unsupported glyphs fail explicitly and do not leave an output file.
        auto unsupported=sparse; unsupported.notes[0].text=QString::fromUcs4(U"😀"); unsupported.notes[0].styles.clear(); shell.annotations()->restore(unsupported);
        const auto invalidTarget=output.filePath("unsupported.pdf"); shell.exportPdf(QUrl::fromLocalFile(invalidTarget)); QTRY_VERIFY(!shell.exporting());
        QVERIFY(shell.exportStatus().contains(QStringLiteral("不支持"))); QVERIFY(!QFileInfo::exists(invalidTarget));
        shell.selectFile(QUrl::fromLocalFile(target)); QTRY_COMPARE(shell.selectedName(),QStringLiteral("annotated.pdf")); QTRY_VERIFY(shell.storageReady()); QTRY_COMPARE(shell.pageCount(),8);
        for(int page=0;page<8;++page) {
            shell.goToPage(page+1); QTRY_VERIFY(shell.textSelection()->snapshot(page));
            const auto text=shell.textSelection()->snapshot(page); QString extracted;
            for(const auto& c:text->characters) extracted+=QString::fromUcs4(&c.unicode,1);
            auto joined=extracted; joined.remove(' '); joined.remove('\r'); joined.remove('\n');
            QVERIFY2(joined.contains(QStringLiteral("中文练习")),qPrintable(extracted)); QVERIFY(!extracted.contains("EDIT AFTER EXPORT"));
            if(!artifactRoot.isEmpty() && (page==0 || page==1 || page==4 || page==7)) {
                QTRY_VERIFY(!shell.busy()); QVERIFY(!shell.pageImage().isNull());
                QVERIFY(shell.pageImage().save(artifactRoot+QStringLiteral("/pdfium-page-%1.png").arg(page+1)));
            }
        }
        if(!artifactRoot.isEmpty()) {
            put(artifactRoot+"/snapshot.json",StorageWorker::encode(exportSnapshot,originalHash));
        }
        shell.beginShutdown(); QTRY_VERIFY(shell.shutdownComplete());
    }
};
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv); configureTestApplication(app);
    if(argc==3 && QByteArray(argv[1])=="--interrupted-write") {
        const auto path=QString::fromLocal8Bit(argv[2]); QSaveFile f(path);
        if(!f.open(QIODevice::WriteOnly) || f.write("partial")!=7) return 9;
        QFile ready(path+".ready"); if(!ready.open(QIODevice::WriteOnly)) return 10; ready.write("ready"); ready.close();
        return app.exec(); // Parent kills the process before commit.
    }
    if(argc==4 && (QByteArray(argv[1])=="--crash-session" || QByteArray(argv[1])=="--read-session")) {
        const bool writing=QByteArray(argv[1])=="--crash-session";
        const auto root=QString::fromLocal8Bit(argv[2]); qputenv("LANGLERN_WORKSPACE_DIR",root.toUtf8());
        ShellState shell; shell.selectFile(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[3])));
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
    QTemporaryDir workspace; qputenv("LANGLERN_WORKSPACE_DIR",workspace.path().toUtf8());
    qRegisterMetaType<AnnotationSnapshot>(); StorageTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "storage_tests.moc"
