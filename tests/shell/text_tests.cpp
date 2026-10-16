#include "testapplication.h"
#include <QTemporaryDir>
#include "shellstate.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QQmlApplicationEngine>
#include <QSignalSpy>
#include <QFile>
#include <QDir>
#include <QQuickItem>
#include <QElapsedTimer>
#include <QFont>
#include <QTest>
#include <QtQml/QQmlExtensionPlugin>
Q_IMPORT_QML_PLUGIN(LanglernShellPlugin)

class TextTests : public QObject {
    Q_OBJECT
    const QString fixture=QStringLiteral(LANGLERN_FIXTURES)+"/text.pdf";
    static QPointF center(const TextSnapshot& data,int index) { return data->characters[index].box.center(); }
    static int hello(const TextSnapshot& data) {
        for(int i=0;i+10<data->characters.size();++i)
            if(data->characters[i].unicode==U'H' && data->characters[i+1].unicode==U'e') return i;
        return -1;
    }
    std::unique_ptr<QTemporaryDir> caseWorkspace;
private slots:
    void init() { caseWorkspace=std::make_unique<QTemporaryDir>(); qputenv("LANGLERN_WORKSPACE_DIR",caseWorkspace->path().toUtf8()); }

    void mouseSelectionAndCopy() {
        ShellState shell;
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,&engine,[&](const QList<QQmlError>& errors) { for(const auto& error:errors) warnings.append(error.toString()); });
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}});
        engine.loadFromModule("Langlern.Shell","Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        shell.selectFile(QUrl::fromLocalFile(fixture));
        auto* s=shell.textSelection();
        QTRY_VERIFY(s->snapshot(0));
        auto* mouse=window->findChild<QQuickItem*>(QStringLiteral("textSelectionMouse")); QVERIFY(mouse);
        auto* viewport=window->findChild<QQuickItem*>(QStringLiteral("pdfViewport")); QVERIFY(viewport);
        QTRY_VERIFY(!shell.busy());
        QTest::qWait(250);
        const auto data=s->snapshot(0); const int h=hello(data); QVERIFY(h>=0);
        const auto screenPointF=[&](int page,QPointF p) {
            const double w=shell.model().pageWidth();
            return viewport->mapToScene(QPointF((shell.contentWidth()-w)/2+p.x()*w-viewport->property("contentX").toDouble(),
                shell.model().top(page)+p.y()*shell.model().height(page)-viewport->property("contentY").toDouble()));
        };
        const auto screenPoint=[&](int page,QPointF p) { return screenPointF(page,p).toPoint(); };
        auto start=screenPoint(0,center(data,h)), finish=screenPoint(0,center(data,h+10));
        const auto jobs=shell.model().renderJobs;
        const auto unselected=window->grabWindow();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,finish,30);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,finish);
        QTRY_COMPARE(s->text(),QStringLiteral("Hello World"));
        QTest::keyClick(window,Qt::Key_C,Qt::ControlModifier);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(),QStringLiteral("Hello World"));
        QCOMPARE(shell.model().renderJobs,jobs);
        QTest::qWait(100);
        const auto selected=window->grabWindow();
        QCOMPARE(selected.size(),unselected.size());
        const double dpr=window->devicePixelRatio();
        QList<QRectF> expected;
        for(const auto& value:s->rectangles(0)) {
            const auto r=value.toRectF();
            const auto a=screenPointF(0,r.topLeft()), b=screenPointF(0,r.bottomRight());
            expected.append(QRectF(a*dpr,b*dpr).normalized().adjusted(-1,-1,1,1));
        }
        int changedPixels=0;
        const int top=int(viewport->mapToScene(QPointF(0,0)).y()*dpr);
        for(int y=top;y<selected.height();++y) for(int x=0;x<selected.width();++x) {
            if(selected.pixel(x,y)==unselected.pixel(x,y)) continue;
            ++changedPixels;
            bool inside=false;
            for(const auto& r:expected) inside|=r.contains(QPointF(x,y));
            QVERIFY2(inside,"Selection changed a viewport pixel outside the projected glyph bounds (including logical/pixel rounding)");
        }
        QVERIFY(changedPixels>100);
        const auto output=qEnvironmentVariable("LANGLERN_SCREENSHOT_DIR");
        if(!output.isEmpty()) {
            QFile file(QDir(output).filePath("text-selection.png"));
            QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));
            QVERIFY(window->grabWindow().save(&file,"PNG"));
        }
        QTest::keyClick(window,Qt::Key_Escape); QVERIFY(!s->ready());
        // Cross-page pointer drag at reduced zoom, then hold at the bottom edge to autoscroll.
        shell.setZoom(.25); QTest::qWait(200); QTRY_VERIFY(s->snapshot(1));
        start=screenPoint(0,center(s->snapshot(0),hello(s->snapshot(0))));
        const auto next=s->snapshot(1);
        finish=screenPoint(1,center(next,hello(next)+10));
        QVERIFY(window->contentItem()->contains(finish));
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,finish,30);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,finish);
        QTRY_VERIFY(s->ready()); QVERIFY(s->text().count(QStringLiteral("Hello World"))>=2);
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        const auto edge=viewport->mapToScene(QPointF(viewport->width()/2,viewport->height()-3)).toPoint();
        QTest::mouseMove(window,edge,30);
        const double before=viewport->property("contentY").toDouble();
        QTest::qWait(250);
        QVERIFY(viewport->property("contentY").toDouble()>before);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,edge);
        QTRY_VERIFY(s->ready());
        QTest::keyClick(window,Qt::Key_Escape);
        const auto blank=viewport->mapToScene(QPointF(5,viewport->height()/2)).toPoint();
        const double blankBefore=viewport->property("contentY").toDouble();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,blank);
        for(int i=1;i<=5;++i) QTest::mouseMove(window,blank-QPoint(0,15*i),20);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,blank-QPoint(0,75));
        QTRY_VERIFY(viewport->property("contentY").toDouble()>blankBefore);
        viewport->metaObject()->invokeMethod(viewport,"cancelFlick");
        shell.setZoom(1);
        shell.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/chinese.pdf"));
        QTRY_VERIFY(s->snapshot(0)); QTRY_VERIFY(!shell.busy());
        const auto chinese=s->snapshot(0);
        start=screenPoint(0,center(chinese,0));
        finish=screenPoint(0,center(chinese,int(chinese->characters.size())-1));
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,finish,30);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,finish);
        QTRY_VERIFY(s->ready());
        QCOMPARE(s->text().replace(QChar(0xa0),QChar(' ')),QStringLiteral("中文练习 Bonjour"));
        QTest::keyClick(window,Qt::Key_C,Qt::ControlModifier);
        QCOMPARE(QGuiApplication::clipboard()->text(),s->text());
        QTest::qWait(100);
        if(!output.isEmpty()) {
            QFile file(QDir(output).filePath("chinese-selection.png"));
            QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));
            QVERIFY(window->grabWindow().save(&file,"PNG"));
        }
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close(); QTRY_VERIFY(shell.shutdownComplete());
    }
    void extractionAndGeometry() {
        ShellState shell;
        shell.updateViewport(0,700,1000,1.5);
        shell.selectFile(QUrl::fromLocalFile(fixture));
        auto* selection=shell.textSelection();
        for (int p=0; p<8; ++p) {
            shell.goToPage(p+1);
            QTRY_VERIFY_WITH_TIMEOUT(selection->snapshot(p),10000);
            const auto data=selection->snapshot(p);
            QVERIFY(data->error.isEmpty());
            QVERIFY(data->characters.size()>24);
            const int h=hello(data); QVERIFY(h>=0);
            const auto start=center(data,h), end=center(data,h+10);
            selection->begin(p,start.x(),start.y(),0,0);
            selection->extend(p,end.x(),end.y());
            QCOMPARE(selection->text(),QStringLiteral("Hello World"));
            selection->copy();
            QCOMPARE(QGuiApplication::clipboard()->text(),QStringLiteral("Hello World"));
            const auto rects=selection->rectangles(p);
            QVERIFY(!rects.isEmpty());
            for (const auto& rect : rects) QVERIFY(QRectF(0,0,1,1).contains(rect.toRectF()));
            // First H box in unrotated PDF: x ~40, y ~740; fixture crop [10,20,580,800].
            const auto box=data->characters[h].box;
            if (p%4==0) { QVERIFY(box.left()>0.05 && box.left()<0.06); QVERIFY(box.top()>0.05 && box.top()<0.07); }
            if (p%4==1) { QVERIFY(box.left()>0.92); QVERIFY(box.top()>0.05 && box.top()<0.06); }
            if (p%4==2) { QVERIFY(box.left()>0.90); QVERIFY(box.top()>0.92); }
            if (p%4==3) { QVERIFY(box.left()<0.08); QVERIFY(box.top()>0.90); }
            int firstVisible=-1,lastVisible=-1;
            for(int i=0;i<data->characters.size();++i) if(data->characters[i].visible) { if(firstVisible<0) firstVisible=i; lastVisible=i; }
            QVERIFY(lastVisible>22);
            const auto finish=center(data,lastVisible);
            const auto first=center(data,firstVisible);
            selection->begin(p,first.x(),first.y(),0,0);
            selection->extend(p,finish.x(),finish.y());
            QVERIFY(!selection->text().contains(QStringLiteral("HIDDEN")));
            QVERIFY(selection->text().contains(QStringLiteral("Second line")));
            QVERIFY(selection->text().contains(QStringLiteral("中文")));
            QVERIFY(selection->text().contains(QString::fromUtf8("😀")));
            QVERIFY(selection->text().contains(QString::fromUtf8("é")));
            QVERIFY(selection->text().contains(QStringLiteral("fi")));
            const auto reference=selection->text();
            selection->begin(p,finish.x(),finish.y(),0,0);
            selection->extend(p,first.x(),first.y());
            QCOMPARE(selection->text(),reference);
            const auto jobs=shell.model().renderJobs;
            selection->copy(); selection->clear();
            QCOMPARE(shell.model().renderJobs,jobs);
            shell.setZoom(0.25); shell.setZoom(4); shell.setZoom(1);
        }
        QSignalSpy closed(&shell,&ShellState::documentClosed);
        shell.beginShutdown(); QTRY_VERIFY(shell.shutdownComplete());
        QVERIFY(!closed.isEmpty()); QCOMPARE(closed.last()[1].toInt(),0);
    }
    void crossPageAndInvalidation() {
        ShellState shell;
        shell.selectFile(QUrl::fromLocalFile(fixture));
        auto* s=shell.textSelection();
        QTRY_VERIFY(s->snapshot(0));
        auto a=center(s->snapshot(0),0);
        s->begin(0,a.x(),a.y(),0,0);
        // Request a distant endpoint which is outside the visible neighborhood.
        s->extend(4,0.25,0.07);
        QTRY_VERIFY_WITH_TIMEOUT(s->snapshot(4),10000);
        auto b=center(s->snapshot(4),10);
        s->extend(4,b.x(),b.y());
        QTRY_VERIFY_WITH_TIMEOUT(s->ready(),10000);
        QVERIFY(s->text().startsWith(QStringLiteral("Hello World\nSecond line")));
        QVERIFY(s->text().endsWith(QStringLiteral("Hello World")));
        const auto forward=s->text();
        s->begin(4,b.x(),b.y(),0,0); s->extend(0,a.x(),a.y());
        QCOMPARE(s->text(),forward);
        QVERIFY(s->bytes()<=ReadingConfig::textBytes);
        s->begin(0,0.9,0.8,0,0); s->extend(0,a.x(),a.y());
        QVERIFY(!s->ready());
        s->begin(0,a.x(),a.y(),0,0); s->extend(0,b.x(),b.y());
        QVERIFY(s->ready());
        QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));
        shell.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/scan.pdf"));
        QVERIFY(!s->ready()); s->copy();
        QCOMPARE(QGuiApplication::clipboard()->text(),QStringLiteral("sentinel"));
        QTRY_VERIFY(s->snapshot(0));
        QVERIFY(s->snapshot(0)->characters.isEmpty());
        s->begin(0,0.5,0.5,0,0); s->extend(0,0.6,0.5);
        QVERIFY(!s->ready()); QVERIFY(s->status().contains(QStringLiteral("OCR")));
        for(int i=0;i<25;++i) {
            shell.selectFile(QUrl::fromLocalFile(fixture));
            s->begin(0,0.1,0.1,0,0); s->extend(7,0.5,0.5);
            shell.clearSelection();
        }
        QTest::qWait(100); QVERIFY(!s->ready()); QVERIFY(!s->snapshot(0));
        shell.beginShutdown(); QTRY_VERIFY(shell.shutdownComplete());
    }
    void budgetAndUnicodeMapping() {
        TextSelection s;
        auto page=std::make_shared<TextPage>();
        page->characters={{U'A',QRectF(.1,.1,.1,.1),true},{U'😀',QRectF(.2,.1,.1,.1),true},
            {U'e',QRectF(.3,.1,.1,.1),true},{U'\u0301',QRectF(.3,.1,.1,.1),true},{U'\ufb01',QRectF(.4,.1,.1,.1),true}};
        s.put(0,page); s.begin(0,.15,.15,0,0); s.extend(0,.45,.15);
        QCOMPARE(s.text(),QString::fromUtf8("A😀éﬁ"));
        s.extend(128,0,0); QVERIFY(!s.ready()); QVERIFY(!s.status().isEmpty());
        auto huge=std::make_shared<TextPage>();
        huge->characters.resize(ReadingConfig::textCharacters);
        for(int i=0;i<20;++i) { s.put(i,huge); QVERIFY(s.bytes()<=ReadingConfig::textBytes); }
        QVERIFY(!s.ready());
        s.reset(); QCOMPARE(s.bytes(),0);
        auto a=std::make_shared<TextPage>(); a->characters={{U'A',QRectF(.1,.1,.1,.1),true}};
        auto hidden=std::make_shared<TextPage>(); hidden->characters={{U'H',QRectF(),false}};
        s.put(0,a); s.put(1,hidden); s.begin(0,.15,.15,0,0); s.extend(1,.5,.5);
        QVERIFY(s.ready()); QCOMPARE(s.text(),QStringLiteral("A\n"));
    }
    void boundedTextCost() {
        TextSelection s;
        auto page=std::make_shared<TextPage>();
        for(int i=0;i<ReadingConfig::textCharacters;++i)
            page->characters.append({U'A',QRectF((i%256)/256.0,(i/256)/256.0,1.0/256,1.0/256),true});
        s.put(0,page); s.begin(0,.001,.001,0,0);
        QElapsedTimer timer; timer.start();
        for(int i=0;i<100;++i) s.extend(0,.99,(i+1)/100.0);
        qInfo()<<"100 GUI selection updates, 65536 native characters, elapsed_ms"<<timer.nsecsElapsed()/1e6<<"snapshot_bytes"<<s.bytes();
        QVERIFY(s.ready()); QVERIFY(s.bytes()<=ReadingConfig::textBytes);
    }
    void failedTextRequestFinishes() {
        QThread thread;
        auto* worker=new PdfWorker;
        worker->moveToThread(&thread);
        connect(worker,&PdfWorker::stopped,&thread,&QThread::quit,Qt::DirectConnection);
        connect(&thread,&QThread::finished,worker,&QObject::deleteLater);
        QSignalSpy frames(worker,&PdfWorker::frameReady);
        QSignalSpy closed(worker,&PdfWorker::nativeClosed);
        thread.start();
        PdfRequest r; r.session=1; r.revision=1; r.path=fixture; r.page=999; r.textOnly=true;
        worker->submit(r);
        // Always stop before asserting, so test failure cannot destroy a running thread.
        QElapsedTimer timeout; timeout.start();
        while(frames.isEmpty() && timeout.elapsed()<10000) QTest::qWait(10);
        worker->shutdown();
        while(thread.isRunning() && timeout.elapsed()<20000) QTest::qWait(10);
        QVERIFY(thread.wait(1000));
        QCOMPARE(frames.size(),1);
        const auto result=qvariant_cast<PdfFrame>(frames.first()[0]);
        QVERIFY(result.textOnly); QCOMPARE(result.errorCode,QStringLiteral("page"));
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!closed.isEmpty()); QCOMPARE(closed.last()[1].toInt(),0);
        TextSelection s;
        auto failure=std::make_shared<TextPage>(); failure->error=result.error;
        s.put(0,failure); QCOMPARE(s.nextPage({0}),-1);
        s.begin(0,.5,.5,0,0); QVERIFY(!s.ready()); QVERIFY(!s.status().isEmpty());
    }
    void realPageCharacterLimit() {
        ShellState shell;
        shell.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/text-limit.pdf"));
        auto* s=shell.textSelection();
        QTRY_VERIFY_WITH_TIMEOUT(s->snapshot(0),15000);
        QVERIFY(s->snapshot(0)->characters.isEmpty());
        QVERIFY(s->snapshot(0)->error.contains(QStringLiteral("65536")));
        s->begin(0,.1,.1,0,0); QVERIFY(!s->ready()); QVERIFY(!s->status().isEmpty());
        const auto snapshot=s->snapshot(0);
        QTest::qWait(150); QCOMPARE(s->snapshot(0),snapshot);
        shell.beginShutdown(); QTRY_VERIFY(shell.shutdownComplete());
    }
};
int main(int argc,char** argv) {
    QTemporaryDir workspace; qputenv("LANGLERN_WORKSPACE_DIR",workspace.path().toUtf8());

    QGuiApplication app(argc,argv);
#ifdef Q_OS_WIN
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI")));
#endif
    configureTestApplication(app);
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#endif
    TextTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "text_tests.moc"
