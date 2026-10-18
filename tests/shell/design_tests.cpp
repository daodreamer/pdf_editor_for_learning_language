#include "testapplication.h"
#include "shellstate.h"
#include "workspacestate.h"
#include "offlineservices.h"
#include "scancleanup.h"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QPainter>
#include <QDataStream>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QTest>
#include <QtQml/QQmlExtensionPlugin>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <mmsystem.h>
#endif
Q_IMPORT_QML_PLUGIN(LanglernShellPlugin)

class DesignTests : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> root_;
private slots:
    void init() { root_=std::make_unique<QTemporaryDir>();qputenv("LANGLERN_WORKSPACE_DIR",root_->path().toUtf8()); }
    void libraryRecoveryAndReview() {
        const auto url=QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/text.pdf");
        {
            WorkspaceState state;state.remember(url,"练习册",8,3);state.setDocument(url.toString());state.toggleBookmark(3);state.toggleBookmark(1);
            state.addWord("weekend","周末","What did you do last weekend?","练习册",3);
            state.addWord("WEEKEND","ignored","","",1);QCOMPARE(state.words().size(),1);QCOMPARE(state.dueCount(),1);
            state.grade(0,3);QCOMPARE(state.dueCount(),0);state.setLayout(2);state.setToolbar(1);state.setPaper(true);
        }
        WorkspaceState restored;restored.setDocument(url.toString());
        QCOMPARE(restored.books().size(),1);QCOMPARE(restored.books()[0].toMap()["page"].toInt(),3);
        QCOMPARE(restored.bookmarks(),QVariantList({1,3}));QCOMPARE(restored.words()[0].toMap()["meaning"].toString(),QStringLiteral("周末"));
        QCOMPARE(restored.dueCount(),0);QCOMPARE(restored.layout(),2);QCOMPARE(restored.toolbar(),1);QVERIFY(restored.paper());
        restored.toggleBookmark(3);QVERIFY(!restored.bookmarked(3));
        restored.forget(url);restored.updateProgress(url,8,4);QVERIFY(restored.books().isEmpty());
    }
    void corruptLibraryIsPreserved() {
        QFile file(root_->filePath("library-v1.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("broken");file.close();
        { WorkspaceState state;QVERIFY(!state.error().isEmpty());state.setPaper(true); }
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("broken"));
    }
    void capacityRejectionPreservesRecoverableLibrary() {
        QVariantMap word{{"word","existing"},{"meaning",QString(8*1024*1024-300,'x')},{"context",""},{"due",0}};
        const auto bytes=QJsonDocument::fromVariant(QVariantMap{{"version",1},{"words",QVariantList{word}}}).toJson(QJsonDocument::Compact);
        QVERIFY(bytes.size()<8*1024*1024);
        QFile file(root_->filePath("library-v1.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(bytes);file.close();
        {WorkspaceState state;QVERIFY(state.error().isEmpty());state.addWord("new",QString(4000,'y'),QString(4000,'z'),"source",1);QVERIFY(!state.error().isEmpty());QCOMPARE(state.words().size(),1);}
        WorkspaceState restored;QVERIFY(restored.error().isEmpty());QCOMPARE(restored.words().size(),1);
    }
    void realDictionaryAndStaleLookup() {
        if(qEnvironmentVariable("LANGLERN_DICTIONARY").isEmpty()&&!QFileInfo::exists(QDir(QCoreApplication::applicationDirPath()).filePath("dictionary/ecdict.sqlite")))QSKIP("Run tools/bootstrap-dictionary.py and reconfigure to test the optional dictionary");
        OfflineServices service;service.lookup("not-a-word-zzzzz");service.lookup("weekend");
        QTRY_VERIFY_WITH_TIMEOUT(!service.meaning().isEmpty(),10000);
        QCOMPARE(service.lookupWord(),QString("weekend"));
        QVERIFY2(service.meaning().contains(QStringLiteral("周末")),qPrintable(service.meaning()));
        QVERIFY(!service.phonetic().isEmpty());
    }
    void realLocalAudioPlaybackAndSeeking() {
#ifndef Q_OS_WIN
        QSKIP("Native audio currently Windows only");
#else
        if(waveOutGetNumDevs()==0) QSKIP("No WinMM audio output device; real playback requires an audio-capable runner");
#endif
        QFile file(root_->filePath("silent.wav"));QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream out(&file);out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF",4);out<<quint32(36+32000*3);out.writeRawData("WAVEfmt ",8);
        out<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(16000)<<quint32(32000)<<quint16(2)<<quint16(16);
        out.writeRawData("data",4);out<<quint32(32000*3);file.write(QByteArray(32000*3,'\0'));file.close();
        OfflineServices service;service.openAudio(QUrl::fromLocalFile(file.fileName()));
        QTRY_VERIFY2_WITH_TIMEOUT(service.duration()>2.9,qPrintable(service.status()),10000);service.toggleAudio();QTRY_VERIFY(service.playing());
        QTRY_VERIFY(service.position()>.05);service.toggleAudio();QTRY_VERIFY(!service.playing());
        service.seek(1.5);QTRY_VERIFY(service.position()>1.4);QVERIFY(service.position()<1.7);
        service.setLoop(.5,1);service.seek(.9);service.toggleAudio();QTRY_VERIFY(service.playing());
        QTest::qWait(650);QVERIFY(service.position()<1.4);service.toggleAudio();QTRY_VERIFY(!service.playing());
    }
    void realOfflineOcrAndInvalidation() {
#ifndef Q_OS_WIN
        QSKIP("Native OCR currently Windows only");
#endif
        OfflineServices service;QSignalSpy ready(&service,&OfflineServices::changed);QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(),10000);
        if(service.languages().isEmpty())QSKIP("No installed system OCR language pack or OCR runtime is available");
        QString language;for(const auto& l:service.languages())if(l.startsWith("en")){language=l;break;}
        if(language.isEmpty())QSKIP("English OCR language pack is not installed");
        QImage image(1400,450,QImage::Format_ARGB32);image.fill(Qt::white);
        QPainter painter(&image);painter.setPen(Qt::black);painter.setFont(QFont("Arial",42));
        painter.drawText(QRect(60,40,1280,300),Qt::AlignLeft,QStringLiteral("Hello language learning\nPractice every day"));painter.end();
        service.recognize(image,"test-document",1,language);
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(),30000);
        QVERIFY2(service.text().contains("Hello",Qt::CaseInsensitive),qPrintable(service.status()));QVERIFY(service.words().size()>3);
        for(const auto& entry:service.words()){const auto w=entry.toMap();QVERIFY(w["x"].toDouble()>=0);QVERIFY(w["y"].toDouble()>=0);QVERIFY(w["x"].toDouble()+w["w"].toDouble()<=1.001);}
        service.recognize(image,"old-document",2,language);service.invalidate();QTest::qWait(1000);
        QVERIFY(service.words().isEmpty());QVERIFY(service.document().isEmpty());
    }
    void manualMarksHaveHistoryAndSurviveSnapshot() {
        ShellState shell;shell.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/text.pdf"));
        QTRY_VERIFY(shell.storageReady());QTRY_VERIFY(shell.annotations()->geometry(0).valid);
        auto* model=shell.annotations();const auto id=model->create(0,.2,.3);model->setText(id,"answer",24);model->finishEditing();
        model->cycleMark(id);QCOMPARE(model->snapshot().size(),2);
        model->cycleMark(id);bool wrong=false;for(const auto& n:model->snapshot())wrong|=n.text==QStringLiteral("×");QVERIFY(wrong);
        model->undo();bool right=false;for(const auto& n:model->snapshot())right|=n.text==QStringLiteral("✓");QVERIFY(right);
        model->redo();model->cycleMark(id);QCOMPARE(model->snapshot().size(),1);
        model->undo();QCOMPARE(model->snapshot().size(),2);
        const auto anchored=model->snapshot();model->move(id,.4,.5);const auto moved=model->snapshot();
        QCOMPARE(moved[0].anchor-anchored[0].anchor,moved[1].anchor-anchored[1].anchor);
        model->undo();QCOMPARE(model->snapshot(),anchored);model->select(id);model->removeSelected();QVERIFY(model->snapshot().isEmpty());model->undo();QCOMPARE(model->snapshot(),anchored);
        model->select(id);model->setText(id,"",24);model->finishEditing();QVERIFY(model->snapshot().isEmpty());model->undo();QCOMPARE(model->snapshot(),anchored);
        shell.saveNow();QTRY_VERIFY(shell.storageStatus().contains(QStringLiteral("已保存")));
        shell.clearSelection();QTRY_VERIFY(shell.selectedName().isEmpty());shell.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/text.pdf"));QTRY_VERIFY(shell.storageReady());QCOMPARE(model->snapshot().size(),2);
        const auto exported=QUrl::fromLocalFile(root_->filePath("marked.pdf"));shell.exportPdf(exported);QTRY_VERIFY_WITH_TIMEOUT(!shell.exporting(),30000);
        QVERIFY2(shell.exportStatus().contains(QStringLiteral("已导出")),qPrintable(shell.exportStatus()));
        shell.selectFile(exported);QTRY_COMPARE(shell.selectedUrl(),exported);QTRY_VERIFY(shell.textSelection()->snapshot(0));
        bool exportedMark=false;for(const auto& c:shell.textSelection()->snapshot(0)->characters)exportedMark|=c.unicode==U'×';QVERIFY(exportedMark);
        shell.beginShutdown();QTRY_VERIFY(shell.shutdownComplete());
    }
    void cleanupWritesNewRealPdfAndProtectsSource() {
        ShellState shell;const auto source=QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/scan.pdf");
        shell.selectFile(source);QTRY_VERIFY(shell.storageReady());QTRY_VERIFY(!shell.busy());
        const auto original=shell.pageImage();
        shell.optimizeScan(source,{{"clarity",true}});QVERIFY(!shell.scanning());QVERIFY(shell.exportStatus().contains(QStringLiteral("新 PDF")));
        const auto target=QUrl::fromLocalFile(root_->filePath("optimized.pdf"));
        shell.optimizeScan(target,{{"deshadow",true},{"straighten",true},{"clarity",true},{"crop",true}});
        QTRY_VERIFY_WITH_TIMEOUT(!shell.scanning(),30000);
        QTRY_COMPARE(shell.selectedUrl(),target);QTRY_VERIFY(shell.storageReady());QTRY_VERIFY(!shell.busy());
        QCOMPARE(shell.pageCount(),1);QVERIFY(!shell.pageImage().isNull());QVERIFY(QFileInfo(target.toLocalFile()).size()>1000);
        QCOMPARE(shell.pageImage().width(),original.width());
        shell.beginShutdown();QTRY_VERIFY(shell.shutdownComplete());
    }
    void originalPdfOpensBeforeOptionalOptimization_data() {
        QTest::addColumn<QString>("path");
        QTest::newRow("scan") << QStringLiteral(LANGLERN_FIXTURES)+"/scan.pdf";
        QTest::newRow("real-textbook") << qEnvironmentVariable("LANGLERN_REAL_PDF",QStringLiteral("C:/Users/jiong/Desktop/法语/你好法语3学生用书第二版.pdf"));
    }
    void originalPdfOpensBeforeOptionalOptimization() {
        QFETCH(QString,path);
        if(!QFileInfo::exists(path)) QSKIP("Configured PDF unavailable; real textbook acceptance was not performed.");
        QFile input(path);QVERIFY(input.open(QIODevice::ReadOnly));
        QCryptographicHash before(QCryptographicHash::Sha256);QVERIFY(before.addData(&input));input.close();
        const auto url=QUrl::fromLocalFile(path);
        ShellState shell;QQmlApplicationEngine engine;
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}});engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));QVERIFY(window);
        window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* frame=window->findChild<QQuickItem*>("workspaceFrame");QVERIFY(frame);
        auto* library=window->findChild<QQuickItem*>("libraryPage");QVERIFY(library);
        auto* services=window->findChild<OfflineServices*>();QVERIFY(services);
        QSignalSpy choose(frame,SIGNAL(openRequested()));
        QVERIFY(QMetaObject::invokeMethod(library,"importRequested"));QCOMPARE(choose.size(),1);
        QCOMPARE(frame->property("screen").toString(),QStringLiteral("library"));
        shell.selectFile(url);
        QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),30000);QTRY_VERIFY_WITH_TIMEOUT(!shell.busy(),30000);
        QTRY_COMPARE(frame->property("screen").toString(),QStringLiteral("reader"));
        QCOMPARE(shell.selectedUrl(),url);QVERIFY(!shell.pageImage().isNull());QVERIFY(!shell.scanning());
        QVERIFY(!window->findChild<QQuickItem*>("importPage"));QVERIFY(services->preview().isNull());
        auto* optimize=window->findChild<QObject*>("optimizeScanAction");QVERIFY(optimize);
        QVERIFY(QMetaObject::invokeMethod(optimize,"triggered"));
        QTRY_COMPARE(frame->property("screen").toString(),QStringLiteral("import"));
        auto* page=window->findChild<QQuickItem*>("importPage");QVERIFY(page);
        QTRY_VERIFY_WITH_TIMEOUT(!services->preview().isNull(),10000);
        QCOMPARE(shell.selectedUrl(),url);QVERIFY(!shell.scanning());
        QVERIFY(QMetaObject::invokeMethod(page,"cancelRequested"));
        QTRY_COMPARE(frame->property("screen").toString(),QStringLiteral("reader"));
        shell.clearSelection();QTRY_VERIFY(shell.selectedName().isEmpty());
        QMimeData mime;mime.setUrls({url});
        const auto position=QPoint(window->width()/2,window->height()/2);
        QDragEnterEvent enter(position,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(window,&enter);QVERIFY(enter.isAccepted());
        QDropEvent drop(position,Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(window,&drop);QVERIFY(drop.isAccepted());
        QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),30000);
        QTRY_COMPARE(frame->property("screen").toString(),QStringLiteral("reader"));
        QCOMPARE(shell.selectedUrl(),url);QVERIFY(!shell.scanning());QVERIFY(!window->findChild<QQuickItem*>("importPage"));
        QVERIFY(input.open(QIODevice::ReadOnly));QCryptographicHash after(QCryptographicHash::Sha256);QVERIFY(after.addData(&input));
        QCOMPARE(after.result(),before.result());
        window->close();QTRY_VERIFY(shell.shutdownComplete());
    }
    void documentMenuAnchorsToButton_data() { originalPdfOpensBeforeOptionalOptimization_data(); }
    void documentMenuAnchorsToButton() {
        QFETCH(QString,path);
        if(!QFileInfo::exists(path)) QSKIP("Configured PDF unavailable; real textbook acceptance was not performed.");
        ShellState shell;QQmlApplicationEngine engine;QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors)warnings.append(e.toString());});
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}});engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));QVERIFY(window);
        window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        shell.selectFile(QUrl::fromLocalFile(path));QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),30000);
        auto* button=window->findChild<QQuickItem*>("documentMenuButton");QVERIFY(button);
        auto* menu=window->findChild<QObject*>("documentMenu");QVERIFY(menu);
        const auto click=[&](QQuickItem* item) { QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint()); };
        const QList<QSize> sizes{{1440,900},{900,700},{700,580},{1440,900}};
        for(int i=0;i<sizes.size();++i) {
            if(i==3) { shell.cycleTheme();shell.cycleTheme();QVERIFY(shell.dark()); }
            window->resize(sizes[i]);QTest::qWait(150);
            click(button);QTRY_VERIFY(menu->property("opened").toBool());
            auto* content=menu->property("contentItem").value<QQuickItem*>();QVERIFY(content);
            const auto rect=content->mapRectToScene(content->boundingRect()).adjusted(-6,-6,6,6);
            const auto anchor=button->mapRectToScene(button->boundingRect());
            QVERIFY2(qAbs(rect.right()-anchor.right())<=2,qPrintable(QString("menu right %1 != button right %2").arg(rect.right()).arg(anchor.right())));
            QVERIFY2(qAbs(rect.top()-anchor.bottom()-8)<=2,qPrintable(QString("menu top %1 != button bottom %2 + 8").arg(rect.top()).arg(anchor.bottom())));
            QVERIFY(rect.left()>=7);QVERIFY(rect.right()<=window->width()-7);QVERIFY(rect.bottom()<=window->height()-7);
            if(i==1) {
                auto* exportItem=window->findChild<QQuickItem*>("exportButton");QVERIFY(exportItem);
                QTest::mouseMove(window,exportItem->mapToScene(QPointF(exportItem->width()/2,exportItem->height()/2)).toPoint());
                QTRY_VERIFY(exportItem->property("highlighted").toBool());
            }
            const auto output=qEnvironmentVariable("LANGLERN_MENU_ARTIFACT_DIR");
            if(!output.isEmpty()) {
                QTest::qWait(100);
                const auto shot=window->grabWindow();
                const auto crop=rect.united(anchor).adjusted(-12,-12,12,12).intersected(QRectF(0,0,window->width(),window->height()));
                const auto pixels=QRectF(crop.topLeft()*window->devicePixelRatio(),crop.size()*window->devicePixelRatio()).toAlignedRect();
                QVERIFY(shot.copy(pixels).save(QDir(output).filePath(QString("menu-%1-%2.png").arg(QString::fromLatin1(QTest::currentDataTag())).arg(i))));
            }
            QTest::keyClick(window,Qt::Key_Down);QTRY_VERIFY(menu->property("currentIndex").toInt()>=0);
            QTest::keyClick(window,Qt::Key_Escape);QTRY_VERIFY(!menu->property("visible").toBool());
        }
        click(button);QTRY_VERIFY(menu->property("opened").toBool());
        auto* doublePage=window->findChild<QQuickItem*>("menuDoublePage");QVERIFY(doublePage);
        const bool previous=shell.doublePage();click(doublePage);QTRY_COMPARE(shell.doublePage(),!previous);
        QTRY_VERIFY(!menu->property("visible").toBool());
        click(button);QTRY_VERIFY(menu->property("opened").toBool());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,QPoint(200,26));QTRY_VERIFY(!menu->property("visible").toBool());
        click(button);QTRY_VERIFY(menu->property("opened").toBool());
        click(button);QTRY_VERIFY(!menu->property("visible").toBool());
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close();QTRY_VERIFY(shell.shutdownComplete());
    }
    void cleanupPixelChangesAreReal() {
        QImage source(500,700,QImage::Format_RGB32);source.fill(QColor(200,200,200));
        QPainter p(&source);p.fillRect(0,0,500,20,Qt::black);p.fillRect(0,0,20,700,Qt::black);p.fillRect(0,0,20,20,Qt::black);p.setPen(Qt::black);p.setFont(QFont("Arial",20));p.drawText(100,200,"Practice");p.end();
        const auto cleaned=cleanScan(source,{{"crop",true},{"deshadow",true},{"clarity",true}});
        QVERIFY(cleaned.width()<source.width());QVERIFY(cleaned.height()<source.height());QVERIFY(cleaned.pixelColor(cleaned.width()-2,cleaned.height()-2).red()>230);
        QCOMPARE(source.pixelColor(2,2),QColor(Qt::black));
    }
    void cleanupCancellationDoesNotPublishFile() {
        ShellState shell;shell.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/geometry.pdf"));QTRY_VERIFY(shell.storageReady());
        const auto target=root_->filePath("cancelled.pdf");shell.optimizeScan(QUrl::fromLocalFile(target),{{"deshadow",true}});shell.cancelScan();
        QTRY_VERIFY_WITH_TIMEOUT(!shell.scanning(),30000);QVERIFY(!QFileInfo::exists(target));QVERIFY(shell.exportStatus().contains(QStringLiteral("取消")));
        shell.beginShutdown();QTRY_VERIFY(shell.shutdownComplete());
    }
    void screenLayoutsAndScreenshots() {
        ShellState shell;QQmlApplicationEngine engine;QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors)warnings.append(e.toString());});
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}});engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));QVERIFY(window);
        window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* frame=window->findChild<QQuickItem*>("workspaceFrame");QVERIFY(frame);
        auto* workspace=window->findChild<WorkspaceState*>();QVERIFY(workspace);
        const auto output=qEnvironmentVariable("LANGLERN_SCREENSHOT_DIR");
        const auto snap=[&](const QString& name){QTest::qWait(180);if(!output.isEmpty())QVERIFY(window->grabWindow().save(QDir(output).filePath(name+".png")));};
        snap("library-empty");frame->setProperty("screen","import");snap("import");
        shell.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/chinese.pdf"));QTRY_VERIFY(!shell.busy());QTRY_VERIFY(shell.storageReady());
        snap("reader");workspace->toggleBookmark(1);
        workspace->addWord("Bonjour","你好","中文练习 Bonjour","chinese.pdf",1);
        frame->setProperty("screen","library");snap("library");
        frame->setProperty("screen","words");snap("words");
        workspace->addWord("practice","练习","Practice every day","chinese.pdf",1);
        auto* cards=window->findChild<QQuickItem*>("wordBookPage");QVERIFY(cards);cards->setProperty("revealed",true);
        auto* meaning=window->findChild<QQuickItem*>("wordMeaningEditor");QVERIFY(meaning);
        meaning->forceActiveFocus();meaning->setProperty("text",QStringLiteral("edited meaning"));
        QVERIFY(QMetaObject::invokeMethod(cards,"selectCard",Q_ARG(int,1)));
        QCOMPARE(workspace->words()[0].toMap()["meaning"].toString(),QStringLiteral("edited meaning"));
        frame->setProperty("screen","reader");workspace->setLayout(1);workspace->setToolbar(1);snap("focus-float");
        workspace->setLayout(2);workspace->setToolbar(2);snap("panel-rail");
        workspace->setPaper(true);snap("paper");workspace->setPaper(false);shell.cycleTheme();shell.cycleTheme();snap("dark");
        auto* services=window->findChild<OfflineServices*>();QVERIFY(services);services->setEnglish(true);
        QCOMPARE(QCoreApplication::translate("LibraryPage","练习册"),QStringLiteral("Workbooks"));snap("english");services->setEnglish(false);
        window->resize(700,580);snap("compact");
        frame->setProperty("screen","words");QTRY_VERIFY(window->findChild<QQuickItem*>("wordBookPage"));
        frame->setProperty("screen","import");QTRY_VERIFY(window->findChild<QQuickItem*>("importPage"));
        frame->setProperty("screen","reader");
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close();QTRY_VERIFY(shell.shutdownComplete());
    }
};
int main(int argc,char** argv){QGuiApplication app(argc,argv);configureTestApplication(app);DesignTests tests;return QTest::qExec(&tests,argc,argv);}
#include "design_tests.moc"
