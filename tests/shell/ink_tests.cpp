#include "testapplication.h"
#include <QTemporaryDir>
#include "shellstate.h"
#include "inkinput.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QInputDevice>
#include <QTabletEvent>
#include <QTest>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <chrono>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif
#include <QtQml/QQmlExtensionPlugin>
Q_IMPORT_QML_PLUGIN(LanglernShellPlugin)

class InkTests : public QObject {
    Q_OBJECT
    static PageGeometry geometry() {
        PageGeometry g; g.units={570,780}; g.valid=true;
        g.displayToPdf=QTransform(570,0,0,-780,10,800); g.pdfToDisplay=g.displayToPdf.inverted(); return g;
    }
    static void stroke(AnnotationModel& m,double y,QColor color=Qt::red,double width=2) {
        QVERIFY(m.beginInk(0,{.2,y},color,width)); m.appendInk({.6,y}); m.endInk();
    }
    static QQuickItem* find(QQuickItem* item,const QString& name) {
        if(item->objectName()==name) return item;
        for(auto* c:item->childItems()) if(auto* result=find(c,name)) return result;
        return nullptr;
    }
    static QPoint point(ShellState& s,QQuickItem* viewport,double x,double y,int page=0) {
        return viewport->mapToScene(QPointF((s.contentWidth()-s.pageWidth())/2+x*s.pageWidth()-viewport->property("contentX").toDouble(),
            s.pageTop(page)+y*s.pageHeight(page)-viewport->property("contentY").toDouble())).toPoint();
    }
    std::unique_ptr<QTemporaryDir> caseWorkspace;
private slots:
    void init() { caseWorkspace=std::make_unique<QTemporaryDir>(); qputenv("LANGLERN_WORKSPACE_DIR",caseWorkspace->path().toUtf8()); }

    void gesturesAndSweptErase() {
        AnnotationModel m; m.setGeometry(0,geometry());
        stroke(m,.3); stroke(m,.5,Qt::blue,8);
        const auto original=m.inkSnapshot(); QCOMPARE(original.size(),2);
        // Crossing between event samples hits both paths, even though endpoints miss.
        QVERIFY(m.beginErase(0,{.4,.1},1)); m.appendInk({.4,.8});
        QVERIFY(m.displayStrokes(0).isEmpty()); QCOMPARE(m.inkSnapshot().size(),2);
        m.cancelGesture(); QCOMPARE(m.inkSnapshot(),original);
        QVERIFY(m.beginErase(0,{.4,.1},1)); m.appendInk({.4,.8}); m.endInk();
        QVERIFY(m.inkSnapshot().isEmpty()); m.undo(); QCOMPARE(m.inkSnapshot(),original);
        m.redo(); QVERIFY(m.inkSnapshot().isEmpty()); m.undo();
        QVERIFY(m.beginMoveInk(0,{.3,.3},1)); m.appendInk({.5,.4}); m.cancelGesture();
        QCOMPARE(m.inkSnapshot(),original);
        QVERIFY(m.beginMoveInk(0,{.3,.3},1)); m.appendInk({.5,.4}); m.endInk();
        const auto moved=m.inkSnapshot(); QVERIFY(moved!=original);
        m.undo(); QCOMPARE(m.inkSnapshot(),original); m.redo(); QCOMPARE(m.inkSnapshot(),moved);
        // Moving a whole stroke cannot change its length or line style.
        for(const auto& s:moved) {
            const auto it=std::find_if(original.begin(),original.end(),[&](const auto& o){return o.id==s.id;});
            QCOMPARE(QLineF(s.points.first(),s.points.last()).length(),QLineF(it->points.first(),it->points.last()).length());
            QCOMPARE(s.color,it->color); QCOMPARE(s.width,it->width);
        }
        m.removeInk(); // undo/redo clears selection; no unexpected deletion.
        QCOMPARE(m.inkSnapshot(),moved);
        QVERIFY(m.beginInk(0,{.1,.1},Qt::green,3)); m.appendInk({.2,.2});
        QCOMPARE(m.inkSnapshot(),moved); m.appendInk({1.1,.2}); QVERIFY(!m.gestureActive());
        QCOMPARE(m.inkSnapshot(),moved);
        QVERIFY(!m.beginInk(0,{NAN,.2},Qt::red,2));
        QVERIFY(!m.beginErase(0,{.2,.2},NAN));
        QVERIFY(!m.beginMoveInk(0,{.2,.2},-1));
        // A tap is a valid dot, hit and erased by the round pen radius.
        QVERIFY(m.beginInk(0,{.8,.8},Qt::black,10)); m.endInk();
        QVERIFY(m.beginErase(0,{.8+4.0/570,.8},0)); m.endInk();
        QCOMPARE(m.inkSnapshot(),moved);
    }
    void mixedHistoryAndBudget() {
        AnnotationModel m; m.setGeometry(0,geometry()); m.setActivePages({0});
        const auto text=m.create(0,.1,.1); m.setText(text,QStringLiteral("中文 answer"),24); m.finishEditing();
        stroke(m,.4); stroke(m,.6);
        m.move(text,.2,.2);
        const auto notes=m.snapshot(); const auto ink=m.inkSnapshot();
        for(int i=0;i<50;++i) {
            for(int j=0;j<4;++j) m.undo();
            QVERIFY(m.snapshot().isEmpty()); QVERIFY(m.inkSnapshot().isEmpty()); QVERIFY(!m.canUndo());
            for(int j=0;j<4;++j) m.redo();
            QCOMPARE(m.snapshot(),notes); QCOMPARE(m.inkSnapshot(),ink); QVERIFY(!m.canRedo());
        }
        m.undo(); QVERIFY(m.canRedo()); stroke(m,.7); QVERIFY(!m.canRedo());
        // Charges retained point capacities, not just logical point counts.
        qsizetype pointStorage=0;
        for(const auto& value:ink) pointStorage+=value.points.capacity()*sizeof(QPointF);
        QVERIFY(m.historyBytes()>=pointStorage);
        // Quantity eviction preserves all current user data.
        for(int i=0;i<270;++i) stroke(m,.8);
        QCOMPARE(m.inkSnapshot().size(),273);
        QVERIFY(m.historyBytes()<=AnnotationConfig::historyBytes);
        int undos=0; while(m.canUndo()) {m.undo(); ++undos;}
        QCOMPARE(undos,256); QCOMPARE(m.inkSnapshot().size(),17);
        // Byte eviction is independent of command count, including large text payloads.
        m.reset(); m.setGeometry(0,geometry()); m.setActivePages({0});
        const auto big=m.create(0,.1,.1);
        m.setText(big,QString(9*1024*1024,QChar('x')),24); m.finishEditing();
        QVERIFY(m.historyBytes()<=AnnotationConfig::historyBytes); QVERIFY(!m.canUndo());
        QCOMPARE(m.snapshot().first().text.size(),9*1024*1024);
        // Live input has an explicit cap and cancellation never commits a partial path.
        QVERIFY(m.beginInk(0,{0,.2},Qt::red,2));
        for(int i=1;i<=AnnotationConfig::maxStrokePoints;++i) m.appendInk({i%2?.1:.9,.2});
        QVERIFY(!m.gestureActive()); QVERIFY(m.inkSnapshot().isEmpty()); QVERIFY(!m.gestureError().isEmpty());
    }
    void fiftyCompleteMixedRoundTrips() {
        AnnotationModel m; m.setGeometry(0,geometry()); m.setActivePages({0});
        struct State { QList<TextAnnotation> text; QList<InkStroke> ink; };
        QList<State> states;
        auto capture=[&]{states.append({m.snapshot(),m.inkSnapshot()});};
        capture();
        const auto id=m.create(0,.1,.1); m.setText(id,QStringLiteral("answer"),24); m.finishEditing(); capture();
        stroke(m,.4); capture(); stroke(m,.6); capture();
        m.select(id); m.setText(id,QStringLiteral("中文 edited"),24); m.finishEditing(); capture();
        m.move(id,.3,.2); capture();
        QVERIFY(m.beginMoveInk(0,{.3,.4},1)); m.appendInk({.4,.45}); m.endInk(); capture();
        m.removeInk(); capture();
        QVERIFY(m.beginErase(0,{.3,.5},1)); m.appendInk({.3,.7}); m.endInk(); capture();
        m.select(id); m.removeSelected(); capture();
        QCOMPARE(states.size(),10);
        for(int round=0;round<50;++round) {
            for(qsizetype i=states.size()-2;i>=0;--i) {
                m.undo(); QCOMPARE(m.snapshot(),states[i].text); QCOMPARE(m.inkSnapshot(),states[i].ink);
                QVERIFY(m.historyBytes()<=AnnotationConfig::historyBytes);
            }
            QVERIFY(!m.canUndo());
            for(qsizetype i=1;i<states.size();++i) {
                m.redo(); QCOMPARE(m.snapshot(),states[i].text); QCOMPARE(m.inkSnapshot(),states[i].ink);
                QVERIFY(m.historyBytes()<=AnnotationConfig::historyBytes);
            }
            QVERIFY(!m.canRedo());
        }
    }
    void rotatedCoordinates() {
        ShellState s; s.updateViewport(0,700,1000,1);
        s.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/text.pdf"));
        QTRY_COMPARE(s.pageCount(),8);
        for(int page=0;page<8;++page) {
            s.goToPage(page+1); QTRY_VERIFY(s.annotations()->geometry(page).valid);
            const auto g=s.annotations()->geometry(page);
            QVERIFY(s.annotations()->beginInk(page,{.23,.31},Qt::green,4));
            s.annotations()->appendInk({.47,.56}); s.annotations()->endInk();
            const auto snapshot=s.annotations()->inkSnapshot();
            const auto it=std::find_if(snapshot.begin(),snapshot.end(),[&](const auto& v){return v.page==page;});
            QVERIFY(it!=snapshot.end());
            QVERIFY(QLineF(g.pdfToDisplay.map(it->points.first()),QPointF(.23,.31)).length()<1e-6);
            QVERIFY(QLineF(g.pdfToDisplay.map(it->points.last()),QPointF(.47,.56)).length()<1e-6);
            const auto before=s.annotations()->inkSnapshot();
            s.setZoom(.5); QCOMPARE(s.annotations()->inkSnapshot(),before);
        }
        s.clearSelection(); QTRY_VERIFY(s.annotations()->inkSnapshot().isEmpty()); QVERIFY(!s.annotations()->canUndo());
    }
    void actualWindowInputAndPixels() {
        ShellState s; QQmlApplicationEngine engine; QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,&engine,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings.append(e.toString());});
        engine.setInitialProperties({{"shell",QVariant::fromValue(&s)}}); engine.loadFromModule("Langlern.Shell","Main");
        QVERIFY(!engine.rootObjects().isEmpty()); auto* w=qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(w);
        w->show(); QVERIFY(QTest::qWaitForWindowExposed(w));
        s.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/scan.pdf"));
        QTRY_VERIFY(s.annotations()->geometry(0).valid); QTRY_VERIFY(!s.busy()); s.setZoom(.6); QTRY_VERIFY(!s.busy()); QTest::qWait(300);
        auto* viewport=find(w->contentItem(),"pdfViewport"); QVERIFY(viewport);
        auto* input=qobject_cast<InkInput*>(find(w->contentItem(),"inkInput")); QVERIFY(input);
        qInfo()<<"ink_window"<<w->size()<<"DPR"<<w->devicePixelRatio();
        const auto jobs=s.model().renderJobs;
        QTest::keyClick(w,Qt::Key_P,Qt::ControlModifier|Qt::ShiftModifier); QTRY_COMPARE(input->mode(),3);
        QTest::qWait(60); // Settle the contextual toolbar layout before projecting input points.
        const auto a=point(s,viewport,.2,.2), b=point(s,viewport,.6,.2);
        QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,a);
        QTest::mouseMove(w,b,20); QVERIFY(s.annotations()->gestureActive());
        QCOMPARE(s.annotations()->inkSnapshot().size(),0);
        QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,b);
        QTRY_COMPARE(s.annotations()->inkSnapshot().size(),1);
        const auto original=s.annotations()->inkSnapshot();
        // Right-button erasing overrides every tool without changing the selected mode.
        for(const auto key:{Qt::Key_A,Qt::Key_S,Qt::Key_T,Qt::Key_P,Qt::Key_E,Qt::Key_M}) {
            QTest::keyClick(w,key,Qt::ControlModifier|Qt::ShiftModifier);
            QTest::qWait(60);
            const int mode=input->mode();
            QTest::mousePress(w,Qt::RightButton,Qt::NoModifier,point(s,viewport,.4,.1));
            QTest::mouseMove(w,point(s,viewport,.4,.3),20);
            QTest::mouseRelease(w,Qt::RightButton,Qt::NoModifier,point(s,viewport,.4,.3));
            QTRY_VERIFY(s.annotations()->inkSnapshot().isEmpty());
            QCOMPARE(input->mode(),mode);
            QVERIFY(!s.annotations()->gestureActive());
            QVERIFY(s.annotations()->snapshot().isEmpty());
            s.annotations()->undo(); QCOMPARE(s.annotations()->inkSnapshot(),original);
        }
        // Review and reading tools also allow the override while left-button drawing is disabled.
        for(const auto key:{Qt::Key_C,Qt::Key_R}) {
            QTest::keyClick(w,key);
            QTRY_VERIFY(!input->drawingEnabled());
            QTest::mousePress(w,Qt::RightButton,Qt::NoModifier,point(s,viewport,.4,.1));
            QTest::mouseMove(w,point(s,viewport,.4,.3),20);
            QTest::mouseRelease(w,Qt::RightButton,Qt::NoModifier,point(s,viewport,.4,.3));
            QTRY_VERIFY(s.annotations()->inkSnapshot().isEmpty());
            s.annotations()->undo(); QCOMPARE(s.annotations()->inkSnapshot(),original);
        }
        QTest::keyClick(w,Qt::Key_P);
        QTRY_VERIFY(input->drawingEnabled());
        // The second press in a fast repeated right-button gesture arrives as a double click.
        QTest::mouseClick(w,Qt::RightButton,Qt::NoModifier,point(s,viewport,.4,.1),10);
        QTest::mousePress(w,Qt::RightButton,Qt::NoModifier,point(s,viewport,.4,.1),10);
        QVERIFY(s.annotations()->gestureActive());
        QTest::mouseMove(w,point(s,viewport,.4,.3),20);
        QTest::mouseRelease(w,Qt::RightButton,Qt::NoModifier,point(s,viewport,.4,.3));
        QTRY_VERIFY(s.annotations()->inkSnapshot().isEmpty());
        s.annotations()->undo(); QCOMPARE(s.annotations()->inkSnapshot(),original);
        // A deactivated window rolls back a right-button erase.
        QTest::mousePress(w,Qt::RightButton,Qt::NoModifier,point(s,viewport,.4,.2));
        QEvent rightDeactivate(QEvent::WindowDeactivate); QGuiApplication::sendEvent(w,&rightDeactivate);
        QTest::mouseRelease(w,Qt::RightButton,Qt::NoModifier,point(s,viewport,.4,.2));
        QCOMPARE(s.annotations()->inkSnapshot(),original);
        QTest::keyClick(w,Qt::Key_P,Qt::ControlModifier|Qt::ShiftModifier);
        QTest::keyClick(w,Qt::Key_Z,Qt::ControlModifier); QTRY_VERIFY(s.annotations()->inkSnapshot().isEmpty());
        QTest::keyClick(w,Qt::Key_Y,Qt::ControlModifier); QTRY_COMPARE(s.annotations()->inkSnapshot(),original);
        // Compare actual pixels at the middle of the new stroke, away from endpoints.
        QTest::qWait(60); const auto image=w->grabWindow(); QVERIFY(!image.isNull());
        const auto mid=point(s,viewport,.4,.2)*w->devicePixelRatio();
        const auto color=image.pixelColor(mid); QVERIFY2(color.blue()>color.red()+60,qPrintable(color.name()));
        const auto output=qEnvironmentVariable("LANGLERN_SCREENSHOT_DIR");
        if(!output.isEmpty()) {
            QFile file(QDir(output).filePath("ink-tools.png")); QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));
            QVERIFY(image.save(&file,"PNG"));
        }
        // Switching tools cancels an unfinished stroke.
        QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.2,.3));
        QTest::mouseMove(w,point(s,viewport,.6,.3),20);
        QTest::keyClick(w,Qt::Key_E,Qt::ControlModifier|Qt::ShiftModifier);
        QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.6,.3));
        QCOMPARE(s.annotations()->inkSnapshot(),original);
        // Fast eraser sweep commits one deletion, restored by one undo.
        QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.4,.1));
        QTest::mouseMove(w,point(s,viewport,.4,.3),20);
        QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.4,.3));
        QTRY_VERIFY(s.annotations()->inkSnapshot().isEmpty());
        QTest::keyClick(w,Qt::Key_Z,Qt::ControlModifier); QTRY_COMPARE(s.annotations()->inkSnapshot(),original);
        QTest::keyClick(w,Qt::Key_M,Qt::ControlModifier|Qt::ShiftModifier);
        QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.3,.2));
        QTest::mouseMove(w,point(s,viewport,.4,.25),20);
        QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.4,.25));
        QVERIFY(s.annotations()->inkSnapshot()!=original);
        QTest::keyClick(w,Qt::Key_Z,Qt::ControlModifier); QTRY_COMPARE(s.annotations()->inkSnapshot(),original);
        QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.3,.2));
        QVERIFY(!s.annotations()->inkSelection().isEmpty());
        QTest::keyClick(w,Qt::Key_Delete); QTRY_VERIFY(s.annotations()->inkSnapshot().isEmpty());
        s.annotations()->undo(); QCOMPARE(s.annotations()->inkSnapshot(),original);
        QTest::keyClick(w,Qt::Key_P,Qt::ControlModifier|Qt::ShiftModifier);
        QTest::qWait(60);
        QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.2,.3));
        QEvent deactivate(QEvent::WindowDeactivate); QGuiApplication::sendEvent(w,&deactivate);
        QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.3,.3));
        QCOMPARE(s.annotations()->inkSnapshot(),original);
        QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.2,.3));
        QTest::keyClick(w,Qt::Key_Escape);
        QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.3,.3));
        QCOMPARE(s.annotations()->inkSnapshot(),original);
        // Same-page scroll changes must roll back a pending draw or erase.
        QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.2,.3));
        viewport->setProperty("contentY",20);
        QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.3,.3));
        QCOMPARE(s.annotations()->inkSnapshot(),original);
        viewport->setProperty("contentY",0); QTest::qWait(60);
        // Real Qt tablet events use the same model; pressure intentionally does not alter width.
        QPointingDevice pen("phase6-test-pen",123,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        auto tablet=[&](QEvent::Type type,QPoint pos,Qt::MouseButtons buttons) {
            QTabletEvent e(type,&pen,QPointF(pos),QPointF(w->mapToGlobal(pos)),.8,0,0,0,0,0,
                Qt::NoModifier,type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,buttons);
            QGuiApplication::sendEvent(w,&e);
        };
        tablet(QEvent::TabletPress,point(s,viewport,.2,.35),Qt::LeftButton);
        tablet(QEvent::TabletMove,point(s,viewport,.6,.35),Qt::LeftButton);
        tablet(QEvent::TabletRelease,point(s,viewport,.6,.35),Qt::NoButton);
        QTRY_COMPARE(s.annotations()->inkSnapshot().size(),2);
        QCOMPARE(s.model().renderJobs,jobs); QVERIFY(s.annotations()->snapshot().isEmpty());
        // Leaving viewport cancels; zoom cancels without committing partial data.
        QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.2,.4));
        QTest::mouseMove(w,QPoint(2,2),20); QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,QPoint(2,2));
        QCOMPARE(s.annotations()->inkSnapshot().size(),2);
        QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.2,.4));
        s.setZoom(.5); QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.2,.4));
        QCOMPARE(s.annotations()->inkSnapshot().size(),2);
        QTest::qWait(300);
        // Erase 7 logical px beyond a line center (the radius is 8), after zoom.
        QTest::keyClick(w,Qt::Key_E,Qt::ControlModifier|Qt::ShiftModifier);
        QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.4,.2)+QPoint(0,7));
        QCOMPARE(s.annotations()->inkSnapshot().size(),1);
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        w->close(); QTRY_VERIFY(s.shutdownComplete());
    }
    void overlapOrderAndStyles() {
        ShellState s; QQmlApplicationEngine engine;
        engine.setInitialProperties({{"shell",QVariant::fromValue(&s)}}); engine.loadFromModule("Langlern.Shell","Main");
        auto* w=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(w);
        w->show(); QVERIFY(QTest::qWaitForWindowExposed(w));
        s.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/text.pdf"));
        QTRY_VERIFY(s.annotations()->geometry(0).valid); s.setZoom(.6); QTRY_VERIFY(!s.busy()); QTest::qWait(300);
        auto* viewport=find(w->contentItem(),"pdfViewport"); QVERIFY(viewport);
        auto* colors=find(w->contentItem(),"inkColors"); auto* size=find(w->contentItem(),"inkSize"); QVERIFY(colors); QVERIFY(size);
        QTest::keyClick(w,Qt::Key_P,Qt::ControlModifier|Qt::ShiftModifier);
        auto draw=[&](double ax,double ay,double bx,double by) {
            QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,ax,ay));
            QTest::mouseMove(w,point(s,viewport,bx,by),20);
            QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,bx,by));
        };
        size->setProperty("value",10); draw(.2,.2,.7,.2);
        colors->setProperty("currentIndex",1); size->setProperty("value",6); draw(.45,.1,.45,.3);
        const auto ink=s.annotations()->inkSnapshot(); QCOMPARE(ink.size(),2);
        auto display=s.annotations()->displayStrokes(0); QCOMPARE(display.first().color,QColor("#235fbd"));
        QCOMPARE(display.first().width,10.0); QCOMPARE(display.last().color,QColor("#ce3446")); QCOMPARE(display.last().width,6.0);
        auto checkRed=[&] {
            QTest::qWait(70); const auto image=w->grabWindow();
            const auto color=image.pixelColor(point(s,viewport,.45,.2)*w->devicePixelRatio());
            QVERIFY2(color.red()>color.blue()+70,qPrintable(color.name()));
        };
        checkRed(); s.setZoom(.75); checkRed(); s.setZoom(.6); checkRed();
        s.goToPage(8); QTest::qWait(70); s.goToPage(1); QTest::qWait(200); checkRed();
        // Picking uses the same creation order as rendering, regardless of random UUID order.
        QTest::keyClick(w,Qt::Key_M,Qt::ControlModifier|Qt::ShiftModifier);
        QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.45,.2));
        QCOMPARE(s.annotations()->inkSelection(),display.last().id);
        s.annotations()->removeInk(); s.annotations()->undo(); checkRed();
        const auto output=qEnvironmentVariable("LANGLERN_SCREENSHOT_DIR");
        if(!output.isEmpty()) {
            QFile file(QDir(output).filePath("ink-colors.png")); QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));
            QVERIFY(w->grabWindow().save(&file,"PNG"));
        }
        w->close(); QTRY_VERIFY(s.shutdownComplete());
    }
    void longStrokeTrace() {
        ShellState s; QQmlApplicationEngine engine;
        engine.setInitialProperties({{"shell",QVariant::fromValue(&s)}}); engine.loadFromModule("Langlern.Shell","Main");
        auto* w=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(w);
        w->show(); QVERIFY(QTest::qWaitForWindowExposed(w));
        s.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/scan.pdf"));
        QTRY_VERIFY(s.annotations()->geometry(0).valid); QTRY_VERIFY(s.storageReady()); QTRY_VERIFY(!s.busy()); s.setZoom(.6); QTRY_VERIFY(!s.busy()); QTest::qWait(300);
        auto* viewport=find(w->contentItem(),"pdfViewport"); QVERIFY(viewport);
        QTest::keyClick(w,Qt::Key_P,Qt::ControlModifier|Qt::ShiftModifier);
        const auto jobs=s.model().renderJobs;
        QMutex mutex; QList<double> frames;
        auto previous=std::chrono::steady_clock::now();
        const auto connection=connect(w,&QQuickWindow::frameSwapped,w,[&]{
            const auto now=std::chrono::steady_clock::now(); QMutexLocker lock(&mutex);
            frames.append(std::chrono::duration<double,std::milli>(now-previous).count()); previous=now;
        },Qt::DirectConnection);
        QElapsedTimer total; total.start(); QList<double> costs; QJsonArray trace;
        constexpr int samples=8192;
        const double visibleBottom=(viewport->height()+viewport->property("contentY").toDouble()-s.pageTop(0)-16)/s.pageHeight(0);
        const double bottom=(std::min)(.47,visibleBottom), amplitude=(std::min)(.12,(bottom-.1)/2), center=bottom-amplitude;
        QVERIFY(amplitude>0);
        qInfo()<<"ink_trace_window"<<w->size()<<"DPR"<<w->devicePixelRatio()<<"bottom"<<bottom;
        const auto start=point(s,viewport,.12,center);
        QTest::mousePress(w,Qt::LeftButton,Qt::NoModifier,start);
        QVERIFY(s.annotations()->gestureActive());
        for(int i=1;i<=samples;++i) {
            const auto p=point(s,viewport,.12+.7*i/samples,center+amplitude*std::sin(i*.05));
            QVERIFY(viewport->contains(viewport->mapFromScene(QPointF(p))));
            QMouseEvent e(QEvent::MouseMove,QPointF(p),QPointF(w->mapToGlobal(p)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
            QElapsedTimer timer; timer.start(); QGuiApplication::sendEvent(w,&e);
            const double cost=timer.nsecsElapsed()/1e6; costs.append(cost);
            trace.append(QJsonObject{{"sample",i},{"at_ms",total.nsecsElapsed()/1e6},{"handler_ms",cost}});
            if(i%64==0) { w->update(); QTest::qWait(8); }
        }
        QTest::mouseRelease(w,Qt::LeftButton,Qt::NoModifier,point(s,viewport,.82,center+amplitude*std::sin(samples*.05)));
        QTest::qWait(40); disconnect(connection);
        QTRY_COMPARE(s.annotations()->inkSnapshot().size(),1); QCOMPARE(s.model().renderJobs,jobs);
        const auto count=s.annotations()->inkSnapshot().first().points.size(); QVERIFY(count>7000);
        std::sort(costs.begin(),costs.end()); QList<double> sortedFrames;
        { QMutexLocker lock(&mutex); sortedFrames=frames; } std::sort(sortedFrames.begin(),sortedFrames.end());
        auto percentile=[](const QList<double>& values,double q){return values.isEmpty()?0:values[std::clamp(qsizetype(std::ceil(q*values.size()))-1,qsizetype(0),values.size()-1)];};
        QJsonArray rawFrames; { QMutexLocker lock(&mutex); for(double f:frames) rawFrames.append(f); }
        QJsonObject report{{"samples",samples},{"stored_points",count},{"elapsed_ms",total.nsecsElapsed()/1e6},
            {"handler_p50_ms",percentile(costs,.5)},{"handler_p95_ms",percentile(costs,.95)},{"handler_p99_ms",percentile(costs,.99)},
            {"frame_p95_ms",percentile(sortedFrames,.95)},{"frame_p99_ms",percentile(sortedFrames,.99)},
            {"dpr",w->devicePixelRatio()},{"history_bytes",s.annotations()->historyBytes()}};
#ifdef Q_OS_WIN
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
        QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)));
        report["working_set"]=qint64(memory.WorkingSetSize); report["private_bytes"]=qint64(memory.PrivateUsage);
        report["peak_working_set"]=qint64(memory.PeakWorkingSetSize);
#endif
        qInfo().noquote()<<QJsonDocument(report).toJson(QJsonDocument::Compact);
        report["input_trace"]=trace; report["frame_intervals_ms"]=rawFrames;
        const auto output=qEnvironmentVariable("LANGLERN_INK_TRACE");
        if(!output.isEmpty()) { QFile f(output); QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::NewOnly)); f.write(QJsonDocument(report).toJson()); }
        s.annotations()->undo(); QTRY_VERIFY(s.annotations()->inkSnapshot().isEmpty()); s.annotations()->redo();
        QCOMPARE(s.annotations()->inkSnapshot().first().points.size(),count);
        w->close(); QTRY_VERIFY(s.shutdownComplete());
    }
};
int main(int argc,char** argv) {
    QTemporaryDir workspace; qputenv("LANGLERN_WORKSPACE_DIR",workspace.path().toUtf8());

    QGuiApplication app(argc,argv); configureTestApplication(app); InkTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "ink_tests.moc"
