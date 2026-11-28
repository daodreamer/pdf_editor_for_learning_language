// Redo mode: hiding answers (display only) and archived practice rounds (schema 2).
#include "testsupport.h"
#include "shellstate.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

namespace {
PageGeometry pageGeometry() {
    PageGeometry g; g.displayToPdf=QTransform(600,0,0,-800,0,800);
    g.pdfToDisplay=g.displayToPdf.inverted(); g.units={600,800}; g.valid=true;
    return g;
}
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
// Text answer with a grading mark, and one stroke, on the page.
QString answer(AnnotationModel& model,int page,const QString& text,double y=.3) {
    const auto id=model.create(page,.2,y);
    model.setText(id,text,24); model.finishEditing();
    return id;
}
void stroke(AnnotationModel& model,int page,double y=.6) {
    QVERIFY(model.beginInk(page,{.1,y},Qt::red,3)); model.appendInk({.4,y+.02}); model.endInk();
}
QStringList texts(const QList<TextAnnotation>& notes) {
    QStringList result; for(const auto& n:notes) result.append(n.text); result.sort(); return result;
}
void put(const QString& path,const QByteArray& bytes) {
    QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes),bytes.size());
}
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray(); }
// Text PDFium extracts from one page of an exported file, without whitespace.
QString exportedText(ShellState& shell,int page) {
    shell.goToPage(page+1);
    if(!QTest::qWaitFor([&]{ return bool(shell.textSelection()->snapshot(page)); },30000)) return {};
    QString text;
    for(const auto& c:shell.textSelection()->snapshot(page)->characters) text+=QString::fromUcs4(&c.unicode,1);
    text.remove(' '); text.remove('\r'); text.remove('\n');
    return text;
}
}

class PracticeTests : public QObject {
    Q_OBJECT
    const QString fixture=LANGLERN_FIXTURE("text.pdf");
    std::unique_ptr<QTemporaryDir> caseWorkspace;
private slots:
    void init() { caseWorkspace=std::make_unique<QTemporaryDir>(); setTestEnvironment("LANGLERN_WORKSPACE_DIR",caseWorkspace->path()); }

    void hidingIsDisplayOnly() {
        AnnotationModel model; model.setGeometry(0,pageGeometry()); model.setGeometry(1,pageGeometry()); model.setActivePages({0,1});
        const auto id=answer(model,0,QStringLiteral("réponse"));
        model.cycleMark(id); // ✓ grading mark follows its answer
        stroke(model,0);
        const QRectF box(.7,.7,8./600,8./800);
        QVERIFY(model.toggleBoxMark(0,box,false,Qt::black));
        answer(model,1,QStringLiteral("other page"));
        const auto notes=model.snapshot(); const auto ink=model.inkSnapshot();
        const auto version=model.version(); const bool undo=model.canUndo();
        QSignalSpy content(&model,&AnnotationModel::contentChanged), history(&model,&AnnotationModel::historyChanged);
        QCOMPARE(model.rowCount(),3);
        model.setPageHidden(0,true);
        QVERIFY(model.pageHidden(0)); QVERIFY(!model.pageHidden(1));
        // Data, history and the save revision are untouched; only the view changes.
        QCOMPARE(model.snapshot(),notes); QCOMPARE(model.inkSnapshot(),ink);
        QCOMPARE(model.version(),version); QCOMPARE(model.canUndo(),undo);
        QCOMPARE(content.count(),0); QCOMPARE(history.count(),0);
        QCOMPARE(model.rowCount(),1); // page 1's answer only; the mark hides with its answer
        QVERIFY(model.displayStrokes(0).isEmpty());
        QCOMPARE(model.hiddenCount(0),3); QCOMPARE(model.pageStats(0)["hidden"].toInt(),3);
        // Hidden items are never hit: eraser, tick box, selection, Tab order.
        QVERIFY(model.beginErase(0,{.25,.61},.05)); model.endInk();
        QVERIFY(model.boxMark(0,box).isEmpty());
        model.select(id); QVERIFY(model.selectedId().isEmpty());
        QVERIFY(!model.selectNext(id));
        QCOMPARE(model.snapshot(),notes); QCOMPARE(model.inkSnapshot(),ink); QCOMPARE(content.count(),0);
        model.setPageHidden(0,false);
        QCOMPARE(model.rowCount(),3); QCOMPARE(model.displayStrokes(0).size(),2);
        // Whole book.
        model.setBookHidden(true); QVERIFY(model.bookHidden()); QCOMPARE(model.rowCount(),0); QCOMPARE(model.hiddenTotal(),4);
        model.setPageHidden(1,false); QCOMPARE(model.rowCount(),1); QVERIFY(model.bookHidden());
        model.setBookHidden(false); QVERIFY(!model.bookHidden()); QCOMPARE(model.rowCount(),3); QCOMPARE(model.hiddenTotal(),0);
        QCOMPARE(model.snapshot(),notes); QCOMPARE(model.inkSnapshot(),ink); QCOMPARE(content.count(),0); QCOMPARE(history.count(),0);
    }

    void hidingDoesNotSaveOrRender() {
        ShellState shell; shell.updateViewport(0,700,1000,1);
        shell.selectFile(QUrl::fromLocalFile(fixture)); QTRY_VERIFY(shell.storageReady()); QTRY_VERIFY(shell.annotations()->geometry(0).valid);
        QTRY_VERIFY(!shell.busy());
        answer(*shell.annotations(),0,QStringLiteral("saved"));
        QTRY_VERIFY_WITH_TIMEOUT(shell.storageStatus().contains(QStringLiteral("已保存")),10000);
        const auto status=shell.storageStatus(); const auto key=shell.pageImage().cacheKey();
        QSignalSpy images(&shell,&ShellState::imageChanged), storage(&shell,&ShellState::storageChanged);
        for(int i=0;i<4;++i) { shell.annotations()->setPageHidden(0,i%2==0); shell.annotations()->setBookHidden(i%2==0); }
        QTest::qWait(1200); // past the 750 ms autosave debounce
        QCOMPARE(storage.count(),0); QCOMPARE(shell.storageStatus(),status);
        QCOMPARE(images.count(),0); QCOMPARE(shell.pageImage().cacheKey(),key);
        shell.beginShutdown(); QTRY_VERIFY(shell.shutdownComplete());
    }

    void answeringWhileHiddenStartsRound() {
        AnnotationModel model; model.setGeometry(0,pageGeometry()); model.setActivePages({0});
        QSignalSpy started(&model,&AnnotationModel::roundStarted);
        const auto old=answer(model,0,QStringLiteral("first try")); model.cycleMark(old); stroke(model,0);
        const auto before=model.snapshot(); const auto beforeInk=model.inkSnapshot();
        model.setPageHidden(0,true);
        QVERIFY(model.rounds().isEmpty()); // hiding alone archives nothing
        const auto fresh=answer(model,0,QStringLiteral("second try"),.5);
        QCOMPARE(started.count(),1); QVERIFY(started.last()[2].toBool());
        QCOMPARE(model.rounds().size(),1); QCOMPARE(model.currentRound(0),2);
        QCOMPARE(texts(model.rounds().first().notes),texts(before)); QCOMPARE(model.rounds().first().ink,beforeInk);
        QCOMPARE(texts(model.snapshot()),QStringList{QStringLiteral("second try")}); QVERIFY(model.inkSnapshot().isEmpty());
        QVERIFY(!model.pageHidden(0)); QCOMPARE(model.rowCount(),1);
        // Undo 1: the new answer; undo 2: the old answers return to the current round, visible.
        model.undo(); QVERIFY(model.snapshot().isEmpty()); QCOMPARE(model.rounds().size(),1);
        model.undo(); QCOMPARE(model.snapshot(),before); QCOMPARE(model.inkSnapshot(),beforeInk); QVERIFY(model.rounds().isEmpty());
        QCOMPARE(model.rowCount(),2);
        model.redo(); model.redo();
        QCOMPARE(model.rounds().size(),1); QCOMPARE(model.snapshot().first().id,fresh);
        // Ink on a hidden page starts a round the same way.
        model.setPageHidden(0,true); stroke(model,0,.8);
        QCOMPARE(model.rounds().size(),2); QCOMPARE(model.snapshot().size(),0); QCOMPARE(model.inkSnapshot().size(),1);
        // A step that brings hidden answers back shows their page instead of acting invisibly.
        model.undo(); model.undo(); // ink, then the automatic round: "second try" is current again
        answer(model,0,QStringLiteral("third"),.7);
        model.setPageHidden(0,true);
        QSignalSpy revealed(&model,&AnnotationModel::pageRevealed);
        model.undo(); // removes "third", which was hidden
        QCOMPARE(revealed.count(),1); QVERIFY(!model.pageHidden(0));
    }

    void roundsArchiveRestoreCompareDelete() {
        AnnotationModel model; model.setGeometry(0,pageGeometry()); model.setGeometry(1,pageGeometry()); model.setActivePages({0,1});
        QVERIFY(!model.startRound(0)); // nothing to archive
        const auto a=answer(model,0,QStringLiteral("round one")); model.cycleMark(a); model.cycleMark(a); // ×
        stroke(model,0);
        const auto r1Notes=model.snapshot(); const auto r1Ink=model.inkSnapshot();
        QVERIFY(model.startRound(0));
        QVERIFY(model.snapshot().isEmpty()); QVERIFY(model.inkSnapshot().isEmpty()); QCOMPARE(model.currentRound(0),2);
        auto rounds=model.pageRounds(0); QCOMPARE(rounds.size(),1);
        QCOMPARE(rounds[0].toMap()["number"].toInt(),1); QCOMPARE(rounds[0].toMap()["answered"].toInt(),1);
        QCOMPARE(rounds[0].toMap()["wrong"].toInt(),1); QCOMPARE(rounds[0].toMap()["ink"].toInt(),1);
        const auto firstRound=rounds[0].toMap()["id"].toString();
        answer(model,0,QStringLiteral("round two"));
        // Comparison overlay: display only, read-only.
        model.setOverlayRound(0,firstRound);
        QCOMPARE(model.overlayRound(0),firstRound); QCOMPARE(model.overlayStrokes(0),r1Ink);
        const auto overlay=model.overlayNotes(0); QCOMPARE(overlay.size(),2);
        model.setOverlayRound(1,firstRound); QVERIFY(model.overlayRound(1).isEmpty()); // wrong page
        // Restore: the current answers are archived first, then round one is copied back.
        QVERIFY(model.restoreRound(firstRound).isEmpty());
        QCOMPARE(model.currentRound(0),3);
        QCOMPARE(texts(model.snapshot()),texts(r1Notes));
        for(const auto& n:model.snapshot()) QVERIFY(!r1Notes.contains(n)); // copies with new ids
        const auto copies=model.snapshot(); QCOMPARE(copies.size(),2);
        const auto copiedAnswer=std::find_if(copies.cbegin(),copies.cend(),[](const auto& n){ return !n.id.endsWith("-review"); })->id;
        QVERIFY(std::any_of(copies.cbegin(),copies.cend(),[&](const auto& n){ return n.id==copiedAnswer+"-review"; }));
        QCOMPARE(model.inkSnapshot().size(),1); QCOMPARE(model.inkSnapshot().first().points,r1Ink.first().points);
        QCOMPARE(model.pageRounds(0).size(),2); // history unchanged plus the archived "round two"
        model.undo(); QCOMPARE(texts(model.snapshot()),QStringList{QStringLiteral("round two")}); QCOMPARE(model.pageRounds(0).size(),1);
        model.redo(); QCOMPARE(texts(model.snapshot()),texts(r1Notes));
        // Delete (undoable); deleting the compared round turns the overlay off.
        QVERIFY(model.deleteRound(firstRound)); QCOMPARE(model.pageRounds(0).size(),1); QVERIFY(model.overlayRound(0).isEmpty());
        model.undo(); QCOMPARE(model.pageRounds(0).size(),2);
        // Whole book: one batch, one round per answered page, one undo step.
        answer(model,1,QStringLiteral("page two"));
        QCOMPARE(model.answeredPages(),2);
        QCOMPARE(model.startBookRound(),2);
        QVERIFY(model.snapshot().isEmpty()); QVERIFY(model.inkSnapshot().isEmpty());
        const auto all=model.rounds(); QCOMPARE(all.size(),4);
        QCOMPARE(all[2].batch,all[3].batch); QVERIFY(!all[2].batch.isEmpty()); QCOMPARE(all[2].archived,all[3].archived);
        QVERIFY(model.pageRounds(1).first().toMap()["book"].toBool());
        model.undo(); QCOMPARE(model.rounds().size(),2); QCOMPARE(model.answeredPages(),2);
    }

    void restoreRespectsCapacity() {
        AnnotationModel model; model.setGeometry(0,pageGeometry());
        AnnotationSnapshot big;
        PracticeRound round; round.id=uuid(); round.page=0; round.archived=QDateTime::currentDateTimeUtc();
        for(int i=0;i<16;++i) {
            InkStroke s; s.id=uuid(); s.page=0; s.color=Qt::black; s.width=1; s.order=quint64(i);
            s.points.resize(65536); for(int k=0;k<65536;++k) s.points[k]=QPointF(10+k%500,10+(k/500)%700);
            round.ink.append(std::move(s));
        }
        big.rounds={round};
        model.restore(big);
        const auto error=model.restoreRound(round.id);
        QVERIFY2(error.contains(QStringLiteral("上限")),qPrintable(error));
        QVERIFY(model.inkSnapshot().isEmpty()); QCOMPARE(model.rounds().size(),1); QVERIFY(!model.canUndo());
    }

    void undoRedoAcrossRounds() {
        AnnotationModel model; model.setGeometry(0,pageGeometry()); model.setActivePages({0});
        const auto id=answer(model,0,QStringLiteral("draft"));
        model.select(id); model.setText(id,QStringLiteral("draft edited"),24); model.finishEditing();
        stroke(model,0);
        QVERIFY(model.startRound(0));
        answer(model,0,QStringLiteral("new round"));
        QVERIFY(model.startRound(0));
        const auto endNotes=model.snapshot(); const auto endInk=model.inkSnapshot(); const auto endRounds=model.rounds();
        // Walk all the way back across both round boundaries, then forward again.
        while(model.canUndo()) model.undo();
        QVERIFY(model.snapshot().isEmpty()); QVERIFY(model.inkSnapshot().isEmpty()); QVERIFY(model.rounds().isEmpty());
        model.redo(); model.redo(); QCOMPARE(model.snapshot().first().text,QStringLiteral("draft edited"));
        model.redo(); QCOMPARE(model.inkSnapshot().size(),1);
        model.redo(); QCOMPARE(model.rounds().size(),1); QVERIFY(model.snapshot().isEmpty());
        while(model.canRedo()) model.redo();
        QCOMPARE(model.snapshot(),endNotes); QCOMPARE(model.inkSnapshot(),endInk); QCOMPARE(model.rounds(),endRounds);
        // A new edit after undoing a round start drops the redo of that round.
        model.undo(); QCOMPARE(model.rounds().size(),1);
        answer(model,0,QStringLiteral("branch")); QVERIFY(!model.canRedo()); QCOMPARE(model.rounds().size(),1);
    }

    void schemaCodecAndValidation() {
        const QByteArray identity="identity";
        TextAnnotation n; n.id=uuid(); n.page=2; n.anchor={100,500}; n.right={1,0}; n.down={0,-1}; n.text=QStringLiteral("archivée 中文"); n.order=4;
        TextAnnotation mark=n; mark.id=n.id+"-review"; mark.text=QStringLiteral("✓"); mark.order=5;
        InkStroke s; s.id=uuid(); s.page=2; s.points={{10,40},{100,60}}; s.color=Qt::blue; s.width=2; s.order=1; s.pressure={.5f,1.f};
        AnnotationSnapshot current; current.notes={n}; current.revision=7;
        // Without rounds the file stays schema 1, readable by older builds.
        auto bytes=StorageWorker::encode(current,identity);
        QCOMPARE(StorageWorker::schemaOf(bytes),1);
        AnnotationSnapshot decoded; QString error;
        QVERIFY2(StorageWorker::decode(bytes,identity,decoded,error),qPrintable(error)); QVERIFY(decoded.rounds.isEmpty());
        // With rounds: schema 2, exact round trip of items, batch and time.
        PracticeRound r; r.id=uuid(); r.page=2; r.archived=QDateTime::fromString("2026-11-28T09:30:00.125Z",Qt::ISODateWithMs).toUTC();
        r.notes={n,mark}; r.ink={s};
        PracticeRound r2=r; r2.id=uuid(); r2.batch=uuid(); r2.archived=r.archived.addSecs(60);
        r2.notes[0].id=uuid(); r2.notes[1].id=r2.notes[0].id+"-review"; r2.ink[0].id=uuid();
        AnnotationSnapshot withRounds=current; withRounds.notes[0].id=uuid(); withRounds.rounds={r,r2};
        bytes=StorageWorker::encode(withRounds,identity);
        QCOMPARE(StorageWorker::schemaOf(bytes),2);
        QVERIFY2(StorageWorker::decode(bytes,identity,decoded,error),qPrintable(error));
        QCOMPARE(decoded.notes,withRounds.notes); QCOMPARE(decoded.rounds,withRounds.rounds); QCOMPARE(decoded.revision,7ULL);
        // Every invalid variant is refused as a whole.
        auto refuse=[&](const QByteArray& b,const char* what) {
            AnnotationSnapshot out; QString message;
            QVERIFY2(!StorageWorker::decode(b,identity,out,message),what);
        };
        auto mutate=[&](const std::function<void(QJsonObject&)>& change) {
            auto outer=QJsonDocument::fromJson(bytes).object();
            auto payload=QJsonDocument::fromJson(outer["payload"].toString().toUtf8()).object();
            change(payload);
            const auto text=QJsonDocument(payload).toJson(QJsonDocument::Compact);
            outer["payload"]=QString::fromUtf8(text);
            outer["sha256"]=QString::fromLatin1(QCryptographicHash::hash(text,QCryptographicHash::Sha256).toHex());
            return QJsonDocument(outer).toJson(QJsonDocument::Compact);
        };
        auto roundField=[&](int index,const char* key,const QJsonValue& value) {
            return mutate([&](QJsonObject& p){ auto rs=p["rounds"].toArray(); auto o=rs[index].toObject(); o[key]=value; rs[index]=o; p["rounds"]=rs; });
        };
        refuse(roundField(0,"page",3),"round items on another page");
        refuse(mutate([](QJsonObject& p){ auto rs=p["rounds"].toArray(); auto o=rs[0].toObject(); o["notes"]=QJsonArray{}; o["ink"]=QJsonArray{}; rs[0]=o; p["rounds"]=rs; }),"empty round");
        refuse(roundField(0,"archived","2026-11-28 09:30"),"local or malformed time");
        refuse(roundField(1,"archived","2026-11-28T09:00:00.000Z"),"rounds of a page out of order");
        refuse(roundField(1,"batch","not-a-uuid"),"bad batch");
        refuse(roundField(1,"id",r.id),"duplicate round id");
        refuse(mutate([&](QJsonObject& p){ auto rs=p["rounds"].toArray(); auto o=rs[1].toObject(); auto ns=o["notes"].toArray(); auto x=ns[0].toObject(); x["id"]=withRounds.notes[0].id; ns[0]=x; o["notes"]=ns; rs[1]=o; p["rounds"]=rs; }),"id shared with the current round");
        refuse(mutate([](QJsonObject& p){ p["rounds"]=QJsonArray{}; }),"schema 2 without rounds");
        auto v1=StorageWorker::encode(current,identity);
        auto outer=QJsonDocument::fromJson(v1).object(); auto payload=QJsonDocument::fromJson(outer["payload"].toString().toUtf8()).object();
        payload["rounds"]=QJsonArray{}; const auto text=QJsonDocument(payload).toJson(QJsonDocument::Compact);
        outer["payload"]=QString::fromUtf8(text); outer["sha256"]=QString::fromLatin1(QCryptographicHash::hash(text,QCryptographicHash::Sha256).toHex());
        refuse(QJsonDocument(outer).toJson(),"schema 1 with rounds");
        outer=QJsonDocument::fromJson(bytes).object(); outer["schema"]=3;
        AnnotationSnapshot out; QString message;
        QVERIFY(!StorageWorker::decode(QJsonDocument(outer).toJson(),identity,out,message)); QVERIFY(message.contains(QStringLiteral("版本")));
        // Limits count archived rounds too: 2000000 points across the file.
        AnnotationSnapshot heavy; PracticeRound h; h.page=0; h.archived=QDateTime::currentDateTimeUtc();
        for(int i=0;i<30;++i) { // 1966080 points: fits alone
            InkStroke big; big.id=uuid(); big.page=0; big.color=Qt::black; big.order=quint64(i);
            big.points.resize(65536); for(auto& p:big.points) p={1,1};
            h.ink.append(big);
        }
        h.id=uuid(); heavy.rounds={h};
        QVERIFY2(StorageWorker::decode(StorageWorker::encode(heavy,identity),identity,out,message),qPrintable(message));
        auto extra=h.ink.first(); extra.id=uuid(); extra.order=0; extra.points.resize(40000); heavy.ink={extra};
        refuse(StorageWorker::encode(heavy,identity),"current plus archived points beyond the whole-file limit");
    }

    void schemaMigrationBackupsAndRecovery() {
        QTemporaryDir dir; const auto identity=StorageWorker::digest(fixture);
        const auto main=dir.filePath(QString::fromLatin1(identity)+".json"), legacy=dir.filePath(QString::fromLatin1(identity)+".v1.json");
        TextAnnotation n; n.id=uuid(); n.page=0; n.anchor={100,500}; n.right={1,0}; n.down={0,-1}; n.text=QStringLiteral("ancienne réponse");
        AnnotationSnapshot old; old.notes={n}; old.revision=3;
        const auto schema1=StorageWorker::encode(old,identity);
        put(main,schema1);
        StorageWorker store(dir.path()); QSignalSpy loaded(&store,&StorageWorker::loaded), saved(&store,&StorageWorker::saved);
        // An existing schema 1 workspace opens writable and unchanged.
        store.open(1,fixture); QVERIFY(loaded.last()[4].toBool());
        QCOMPARE(qvariant_cast<AnnotationSnapshot>(loaded.last()[1]).notes,old.notes);
        // Saving without rounds keeps schema 1 and makes no legacy copy.
        auto same=old; same.revision=4; store.save(1,same); QVERIFY(saved.last()[2].toString().isEmpty());
        QCOMPARE(StorageWorker::schemaOf(read(main)),1); QVERIFY(!QFileInfo::exists(legacy));
        const auto lastSchema1=read(main);
        // First schema 2 write: the schema 1 file is kept once as .v1.json and as .bak.
        AnnotationSnapshot upgraded; upgraded.revision=5;
        PracticeRound r; r.id=uuid(); r.page=0; r.archived=QDateTime::currentDateTimeUtc(); r.notes={n};
        upgraded.rounds={r};
        store.save(1,upgraded); QVERIFY2(saved.last()[2].toString().isEmpty(),qPrintable(saved.last()[2].toString()));
        QCOMPARE(StorageWorker::schemaOf(read(main)),2);
        QCOMPARE(read(legacy),lastSchema1); QCOMPARE(read(main+".bak"),lastSchema1);
        // Later writes rotate .bak but never touch the legacy copy.
        auto later=upgraded; later.revision=6; store.save(1,later); QVERIFY(saved.last()[2].toString().isEmpty());
        QCOMPARE(read(legacy),lastSchema1); QCOMPARE(StorageWorker::schemaOf(read(main+".bak")),2);
        store.close();
        store.open(2,fixture); QVERIFY(loaded.last()[4].toBool());
        auto reopened=qvariant_cast<AnnotationSnapshot>(loaded.last()[1]);
        QCOMPARE(reopened.rounds,later.rounds); QCOMPARE(reopened.revision,6ULL);
        store.close();
        // Corrupt schema 2 main: recovery from the backup, evidence kept.
        put(main,"{broken"); store.open(3,fixture); QVERIFY(loaded.last()[4].toBool());
        QCOMPARE(qvariant_cast<AnnotationSnapshot>(loaded.last()[1]).rounds,upgraded.rounds);
        QVERIFY(!QDir(dir.path()).entryList({"*.corrupt-*"},QDir::Files).isEmpty());
        store.close();
        // Corrupt main with a schema 1 backup (the state right after an upgrade).
        put(main,"{broken"); put(main+".bak",schema1); store.open(4,fixture); QVERIFY(loaded.last()[4].toBool());
        QCOMPARE(qvariant_cast<AnnotationSnapshot>(loaded.last()[1]).notes,old.notes);
        store.close();
        // A future version is never replaced, even with a readable backup.
        QFile::remove(main); auto future=QJsonDocument::fromJson(StorageWorker::encode(upgraded,identity)).object(); future["schema"]=3;
        const auto futureBytes=QJsonDocument(future).toJson(QJsonDocument::Compact); put(main,futureBytes);
        store.open(5,fixture); QVERIFY(!loaded.last()[4].toBool());
        store.save(5,later); QVERIFY(!saved.last()[2].toString().isEmpty()); QCOMPARE(read(main),futureBytes);
        store.close();
    }

    void exportContainsCurrentRoundOnly() {
        QTemporaryDir output; const auto hash=StorageWorker::digest(fixture);
        ShellState shell; shell.updateViewport(0,700,1000,1);
        shell.selectFile(QUrl::fromLocalFile(fixture)); QTRY_VERIFY(shell.storageReady()); QTRY_VERIFY(shell.annotations()->geometry(0).valid);
        auto& model=*shell.annotations();
        answer(model,0,QStringLiteral("ROUNDONE"));
        QVERIFY(model.startRound(0));
        answer(model,0,QStringLiteral("ROUNDTWO"));
        model.setPageHidden(0,true);
        auto exportTo=[&](const QString& name,bool includeHidden) {
            const auto target=output.filePath(name);
            shell.exportPdf(QUrl::fromLocalFile(target),includeHidden); QTRY_VERIFY_WITH_TIMEOUT(!shell.exporting(),30000);
            QVERIFY2(shell.exportStatus().contains(QStringLiteral("已导出")),qPrintable(shell.exportStatus()));
        };
        exportTo("with-hidden.pdf",true);
        exportTo("without-hidden.pdf",false);
        QVERIFY(model.pageHidden(0)); QCOMPARE(model.snapshot().size(),1); // exporting changes nothing
        QCOMPARE(StorageWorker::digest(fixture),hash);
        shell.selectFile(QUrl::fromLocalFile(output.filePath("with-hidden.pdf"))); QTRY_COMPARE(shell.selectedName(),QStringLiteral("with-hidden.pdf")); QTRY_VERIFY(shell.storageReady()); QTRY_COMPARE(shell.pageCount(),8);
        auto text=exportedText(shell,0);
        QVERIFY2(text.contains("ROUNDTWO") && !text.contains("ROUNDONE"),qPrintable(text));
        shell.selectFile(QUrl::fromLocalFile(output.filePath("without-hidden.pdf"))); QTRY_COMPARE(shell.selectedName(),QStringLiteral("without-hidden.pdf")); QTRY_VERIFY(shell.storageReady()); QTRY_COMPARE(shell.pageCount(),8);
        text=exportedText(shell,0);
        QVERIFY2(!text.isEmpty() && !text.contains("ROUNDTWO") && !text.contains("ROUNDONE"),qPrintable(text));
        shell.beginShutdown(); QTRY_VERIFY(shell.shutdownComplete());
    }

    void keysChipAndExportSheet() {
        ShellState shell;
        QQmlApplicationEngine engine; QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){ for(const auto& e:errors) warnings.append(e.toString()); });
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}});
        engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(window);
        window->resize(1200,860); QVERIFY(showTestWindow(window));
        shell.selectFile(QUrl::fromLocalFile(fixture)); QTRY_VERIFY(shell.storageReady()); QTRY_VERIFY(shell.annotations()->geometry(0).valid);
        auto& model=*shell.annotations();
        answer(model,0,QStringLiteral("visible answer"));
        QTRY_VERIFY(!shell.busy()); window->requestActivate(); QTest::qWait(200);
        auto* chip=findItem(window->contentItem(),"hiddenAnswersChip"); QVERIFY(chip); QVERIFY(!chip->isVisible());
        const auto artifacts=qEnvironmentVariable("LANGLERN_PRACTICE_ARTIFACT_DIR");
        auto shot=[&](const char* name) { if(!artifacts.isEmpty()) { QTest::qWait(250); QVERIFY(window->grabWindow().save(artifacts+"/"+name)); } };
        QTest::keyClick(window,Qt::Key_H);
        QTRY_VERIFY(model.pageHidden(0)); QTRY_VERIFY(chip->isVisible());
        shot("hidden-chip.png");
        QTest::keyClick(window,Qt::Key_H); QTRY_VERIFY(!model.pageHidden(0));
        QTest::keyClick(window,Qt::Key_H,Qt::ShiftModifier); QTRY_VERIFY(model.bookHidden());
        // Typing an answer is never taken as the shortcut.
        model.setBookHidden(false);
        const auto id=model.create(0,.3,.6); QVERIFY(!id.isEmpty());
        QTRY_VERIFY(window->activeFocusItem() && window->activeFocusItem()->objectName()=="annotationEditor");
        // H goes into the editor (the model sees the text once the editor publishes it).
        QTest::keyClick(window,Qt::Key_H); QTest::keyClick(window,Qt::Key_B);
        auto textOf=[&](const QString& noteId) { for(const auto& n:model.snapshot()) if(n.id==noteId) return n.text; return QString(); };
        QTRY_COMPARE(textOf(id),QStringLiteral("hb")); QVERIFY(!model.pageHidden(0));
        shell.finishTextEditing();
        // Export sheet: the hidden-answer switch appears only when something is hidden, default on.
        model.setPageHidden(0,true);
        auto* sheet=engine.rootObjects().first()->findChild<QObject*>("exportSheet"); QVERIFY(sheet);
        QMetaObject::invokeMethod(sheet,"open"); QTRY_VERIFY(sheet->property("opened").toBool());
        auto* toggle=findItem(window->contentItem(),"exportIncludeHidden");
        QVERIFY(toggle); QVERIFY(toggle->isVisible()); QVERIFY(toggle->property("checked").toBool());
        QVERIFY(sheet->property("includeHidden").toBool());
        shot("export-sheet.png");
        QMetaObject::invokeMethod(sheet,"close"); QTRY_VERIFY(!sheet->property("visible").toBool());
        // Rounds: answering on the hidden page archived round 1; compare it and show the panel.
        answer(model,0,QStringLiteral("second attempt"),.45);
        QCOMPARE(model.currentRound(0),2);
        model.setOverlayRound(0,model.pageRounds(0).first().toMap()["id"].toString());
        auto* overlayInk=findItem(window->contentItem(),"roundOverlayInk"); QVERIFY(overlayInk); QTRY_VERIFY(overlayInk->isVisible());
        QTRY_VERIFY(findItem(window->contentItem(),"roundOverlayNote"));
        shot("round-overlay.png");
        auto* panel=engine.rootObjects().first()->findChild<QObject*>("practiceRoundsPopup"); QVERIFY(panel);
        QMetaObject::invokeMethod(panel,"open"); QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(findItem(window->contentItem(),"restoreRound"));
        shot("rounds-panel.png");
        QMetaObject::invokeMethod(panel,"close");
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close(); QTRY_VERIFY(shell.shutdownComplete());
    }

    // Fixed real textbook (AGENTS.md): answer, new round, restart, export, read back; source unchanged.
    void realTextbookRoundsRestartAndExport() {
        const auto path=realTextbookPath();
        if(path.isEmpty()) QSKIP("Real textbook NOT executed: set LANGLERN_REAL_PDF to the fixed acceptance PDF");
        const auto before=StorageWorker::digest(path); QVERIFY(!before.isEmpty());
        const auto artifactRoot=qEnvironmentVariable("LANGLERN_PRACTICE_ARTIFACT_DIR");
        QTemporaryDir output; const auto target=(artifactRoot.isEmpty()?output.path():artifactRoot)+"/practice-rounds.pdf";
        QFile::remove(target);
        constexpr int page=33; // a page with printed exercises
        QList<TextAnnotation> currentNotes; QList<PracticeRound> rounds;
        {
            ShellState shell; shell.updateViewport(0,700,1000,1);
            shell.selectFile(QUrl::fromLocalFile(path)); QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),120000);
            shell.goToPage(page+1); QTRY_VERIFY_WITH_TIMEOUT(shell.annotations()->geometry(page).valid,60000);
            auto& model=*shell.annotations();
            const auto first=answer(model,page,QStringLiteral("PREMIERETENTATIVE Je m'appelle"));
            model.cycleMark(first); stroke(model,page);
            QTRY_VERIFY_WITH_TIMEOUT(shell.storageStatus().contains(QStringLiteral("已保存")),20000);
            // Hide, then answer again: the first attempt becomes round 1.
            model.setPageHidden(page,true);
            answer(model,page,QStringLiteral("DEUXIEMEESSAI Bonjour"),.45);
            QCOMPARE(model.currentRound(page),2);
            QTRY_VERIFY_WITH_TIMEOUT(shell.storageStatus().contains(QStringLiteral("已保存")),20000);
            currentNotes=model.snapshot(); rounds=model.rounds();
            const auto main=caseWorkspace->filePath(QString::fromLatin1(before)+".json");
            QCOMPARE(StorageWorker::schemaOf(read(main)),2);
            shell.beginShutdown(); QTRY_VERIFY_WITH_TIMEOUT(shell.shutdownComplete(),30000);
        }
        {
            ShellState shell; shell.updateViewport(0,700,1000,1);
            shell.selectFile(QUrl::fromLocalFile(path)); QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),120000);
            auto& model=*shell.annotations();
            QCOMPARE(model.snapshot(),currentNotes); QCOMPARE(model.rounds(),rounds);
            QCOMPARE(model.currentRound(page),2); QCOMPARE(model.pageRounds(page).first().toMap()["right"].toInt(),1);
            shell.goToPage(page+1); QTRY_VERIFY_WITH_TIMEOUT(model.geometry(page).valid,60000);
            shell.exportPdf(QUrl::fromLocalFile(target)); QTRY_VERIFY_WITH_TIMEOUT(!shell.exporting(),240000);
            QVERIFY2(shell.exportStatus().contains(QStringLiteral("已导出")),qPrintable(shell.exportStatus()));
            shell.beginShutdown(); QTRY_VERIFY_WITH_TIMEOUT(shell.shutdownComplete(),30000);
        }
        {
            ShellState shell; shell.updateViewport(0,700,1000,1);
            shell.selectFile(QUrl::fromLocalFile(target)); QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),120000);
            QCOMPARE(shell.pageCount()>page,true);
            const auto text=exportedText(shell,page);
            QVERIFY2(text.contains("DEUXIEMEESSAI") && !text.contains("PREMIERETENTATIVE"),qPrintable(text.left(400)));
            shell.beginShutdown(); QTRY_VERIFY_WITH_TIMEOUT(shell.shutdownComplete(),30000);
        }
        QCOMPARE(StorageWorker::digest(path),before);
        qInfo().noquote()<<"real_textbook_practice source_sha256="<<before<<"export_bytes="<<QFileInfo(target).size();
    }
};
LANGLERN_TEST_SUITE(PracticeTests,"practice")
#include "practice_tests.moc"
