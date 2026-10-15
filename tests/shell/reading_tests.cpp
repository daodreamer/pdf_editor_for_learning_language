#include "shellstate.h"
#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QtQml/QQmlExtensionPlugin>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <memory>
#include <chrono>
#include <QScopeGuard>
#include <QScreen>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif
Q_IMPORT_QML_PLUGIN(LanglernShellPlugin)

class ReadingTests : public QObject {
    Q_OBJECT
    const QString fixture = QStringLiteral(LANGLERN_FIXTURES) + "/geometry.pdf";
private slots:
    void geometryAndBudgets() {
        ReadingModel m;
        QAbstractItemModelTester tester(&m,QAbstractItemModelTester::FailureReportingMode::QtTest);
        m.reset(1000);
        m.setViewport(0,700,1000,1);
        QVERIFY(m.rowCount() < 5);
        m.setSizes(0,{QSizeF(600,800),QSizeF(800,600),QSizeF(400,1200)});
        QCOMPARE(m.top(1),16.0 + 1280.0 + 16.0);
        m.setViewport(m.top(500)+120,700,1000,1);
        QCOMPARE(m.pageAt(m.offset()),500);
        const double fraction=(m.offset()-m.top(500))/m.height(500);
        m.setSizes(4,{QSizeF(1200,200)});
        QCOMPARE(m.pageAt(m.offset()),500);
        QVERIFY(std::abs((m.offset()-m.top(500))/m.height(500)-fraction)<1e-10);
        m.setViewport(m.offset(),700,1200,2);
        QCOMPARE(m.pageAt(m.offset()),500);
        QVERIFY(std::abs((m.offset()-m.top(500))/m.height(500)-fraction)<1e-10);
        m.setBudgets(256*1024,128*1024);
        QImage pixels(64,64,QImage::Format_ARGB32); pixels.fill(Qt::red);
        for(int p=0;p<50;++p) {
            m.setViewport(m.top(p),700,1000,1);
            m.put(p,pixels,64,true);
            QVERIFY(m.cpuBytes()<=m.config.cpuBytes);
            QVERIFY(m.gpuBytes()<=m.config.gpuBytes);
            QVERIFY(m.ready(p,64));
            QVERIFY(m.rowCount()<6);
        }
        QVERIFY(m.evictions>0);
        QVERIFY(m.image(0).isNull());
        m.setViewport(m.top(48),700,1000,1);
        QVERIFY(!m.image(48).isNull());
        const auto key=m.image(48).cacheKey();
        QSignalSpy changes(&m,&QAbstractItemModel::dataChanged);
        m.setViewport(m.offset()+1,700,1000,1);
        QCOMPARE(m.image(48).cacheKey(),key);
        QCOMPARE(changes.size(),0); // scrolling inside the same range cannot re-publish images
        m.setViewport(m.offset(),700,1100,1);
        QVERIFY(!changes.isEmpty());
        for(int p:m.priority()) {
            const int width=m.targetWidth(p,4);
            QVERIFY(qint64(width)*qint64(std::ceil(width*m.ratio(p)))*4<=m.pageBudget());
        }
        m.reset(0);
        QCOMPARE(m.cpuBytes(),0);
        QCOMPARE(m.gpuBytes(),0);
        QCOMPARE(m.rowCount(),0);
    }
    void schedulerAndAnchors() {
        ShellState s;
        QSignalSpy closed(&s,&ShellState::documentClosed);
        QSignalSpy discarded(&s,&ShellState::renderDiscarded);
        s.model().setBudgets(4*1024*1024,2*1024*1024);
        s.updateViewport(0,700,1000,2);
        s.selectFile(QUrl::fromLocalFile(fixture));
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),15000);
        QCOMPARE(s.pageCount(),8);
        QVERIFY(!s.pageImage().isNull());
        QTRY_COMPARE_WITH_TIMEOUT(s.model().nextUnknown(),-1,15000);
        for(int p=0;p<8;++p) QVERIFY(s.model().known(p));
        QVERIFY(s.model().ratio(1)<1);
        s.goToPage(5);
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),15000);
        QCOMPARE(s.currentPage(),5);
        const int closeCount=int(closed.size());
        const double fraction=(s.scrollOffset()-s.model().top(4))/s.model().height(4);
        s.setZoom(2);
        QCOMPARE(s.model().pageAt(s.scrollOffset()),4);
        QVERIFY(std::abs((s.scrollOffset()-s.model().top(4))/s.model().height(4)-fraction)<1e-10);
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),15000);
        QCOMPARE(closed.size(),closeCount); // scrolling/zoom never reopens the PDF
        for(int i=0;i<80;++i) {
            s.goToPage(1+(i*7)%8);
            s.setZoom(i%2 ? 0.5 : 2);
            QTest::qWait(1);
            QVERIFY(s.model().rowCount()<=8);
            QVERIFY(s.model().cpuBytes()<=s.model().config.cpuBytes);
            QVERIFY(s.model().gpuBytes()<=s.model().config.gpuBytes);
        }
        s.goToPage(3);
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),15000);
        QCOMPARE(s.currentPage(),3);
        QVERIFY(!s.pageImage().isNull());
        QVERIFY(!discarded.isEmpty());
        s.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/scan.pdf"));
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),15000);
        QCOMPARE(s.pageCount(),1);
        QVERIFY(!s.pageImage().isNull());
        s.clearSelection();
        QTRY_VERIFY(!closed.isEmpty());
        QCOMPARE(s.model().cpuBytes(),0);
        QCOMPARE(s.model().rowCount(),0);
        s.beginShutdown();
        QTRY_VERIFY(s.shutdownComplete());
        QCOMPARE(closed.last().at(1).toInt(),0);
    }
    void continuousViewportUpdatesDoNotStarve() {
        ShellState s;
        s.updateViewport(0,700,1000,1);
        s.selectFile(QUrl::fromLocalFile(fixture));
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),15000);
        QTRY_COMPARE_WITH_TIMEOUT(s.model().nextUnknown(),-1,15000);
        const auto y=s.model().top(5);
        QTimer rapid;
        rapid.setTimerType(Qt::PreciseTimer);
        rapid.setInterval(4);
        connect(&rapid,&QTimer::timeout,&s,[&] { s.updateViewport(y,700,1000,1); });
        s.updateViewport(y,700,1000,1);
        rapid.start();
        QTRY_VERIFY_WITH_TIMEOUT(!s.pageImage().isNull(),1500);
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),1500);
        rapid.stop();
        QCOMPARE(s.currentPage(),6);
        s.beginShutdown();
        QTRY_VERIFY(s.shutdownComplete());
    }
    void readingStress() {
        const int seconds=qEnvironmentVariableIntValue("LANGLERN_STRESS_SECONDS");
        if (seconds<=0) QSKIP("Opt-in: LANGLERN_STRESS_SECONDS and LANGLERN_LARGE_PDF; 1800 seconds for phase 3 acceptance");
        const auto path=qEnvironmentVariable("LANGLERN_LARGE_PDF");
        QVERIFY(!path.isEmpty());
        ShellState s;
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine,&QQmlEngine::warnings);
        engine.setInitialProperties({{"shell",QVariant::fromValue(&s)}});
        engine.loadFromModule("Langlern.Shell","Main");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->setPersistentSceneGraph(false);
        window->setPersistentGraphics(false);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        auto* viewport=window->findChild<QQuickItem*>("pdfViewport");
        QVERIFY(viewport);
        QElapsedTimer clock; clock.start();
        double firstPixels=-1, firstSharp=-1;
        double firstPixelsSubmitted=-1, firstSharpSubmitted=-1;
        QObject firstFrameObserver;
        connect(&s,&ShellState::imageChanged,&firstFrameObserver,[&] {
            if (!s.pageImage().isNull() && firstPixels<0) firstPixels=clock.nsecsElapsed()/1e6;
            if (!s.busy() && !s.pageImage().isNull() && firstSharp<0) firstSharp=clock.nsecsElapsed()/1e6;
        });
        connect(window,&QQuickWindow::frameSwapped,&firstFrameObserver,[&] {
            if (firstPixels>=0 && firstPixelsSubmitted<0) firstPixelsSubmitted=clock.nsecsElapsed()/1e6;
            if (firstSharp>=0 && firstSharpSubmitted<0) firstSharpSubmitted=clock.nsecsElapsed()/1e6;
        },Qt::QueuedConnection);
        s.updateViewport(0,viewport->height(),viewport->width(),window->devicePixelRatio());
        s.selectFile(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(!s.busy(),60000);
        QTRY_VERIFY_WITH_TIMEOUT(firstSharpSubmitted>=0,10000);
        QVERIFY2(s.errorCode().isEmpty(),qPrintable(s.statusText()));
        struct FrameSamples {
            std::mutex mutex;
            QList<double> times;
            qint64 previous = 0;
        };
        const auto frameState = std::make_shared<FrameSamples>();
        const auto frameConnection=connect(window,&QQuickWindow::frameSwapped,&s,[frameState] {
            const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            std::lock_guard lock(frameState->mutex);
            if(frameState->previous) frameState->times.append((now-frameState->previous)/1e6);
            frameState->previous=now;
        },Qt::DirectConnection);
        const auto disconnectFrames=qScopeGuard([frameConnection] { QObject::disconnect(frameConnection); });
        QFile trace(qEnvironmentVariable("LANGLERN_STRESS_TRACE"));
        QVERIFY(trace.open(QIODevice::WriteOnly|QIODevice::NewOnly));
        const auto record=[&](QJsonObject row) {
            row["elapsed_ms"]=clock.nsecsElapsed()/1e6;
            trace.write(QJsonDocument(row).toJson(QJsonDocument::Compact)+"\n"); trace.flush();
        };
        record({{"event","begin"},{"pages",s.pageCount()},{"first_pixels_ms",firstPixels},{"first_sharp_ms",firstSharp},{"first_pixels_submitted_ms",firstPixelsSubmitted},{"first_sharp_submitted_ms",firstSharpSubmitted},
            {"graphics_api",int(window->rendererInterface()->graphicsApi())},{"dpr",window->devicePixelRatio()},{"screen_hz",window->screen()->refreshRate()}});
        int lastSecond=-1;
        int direction=1;
        const auto start=clock.elapsed();
        QEventLoop loop;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        bool timedOut=false;
        connect(&watchdog,&QTimer::timeout,&loop,[&] { timedOut=true; loop.quit(); });
        qint64 previousStep=clock.nsecsElapsed();
        const auto pumpConnection=connect(window,&QQuickWindow::frameSwapped,&loop,[&] {
            const auto stepNow=clock.nsecsElapsed();
            const double stepMs=std::min(200.0,(stepNow-previousStep)/1e6);
            previousStep=stepNow;
            if (clock.elapsed()-start >= qint64(seconds)*1000) { loop.quit(); return; }
            const int second=int((clock.elapsed()-start)/1000);
            if (second!=lastSecond) {
                lastSecond=second;
                if (second%11==10) { s.goToPage(1+(second*97)%s.pageCount()); direction=-direction; }
                if (second%19==18) s.setZoom(s.zoom()>1 ? 0.75 : 1.5);
                if (second%61==60) {
                    window->hide(); window->releaseResources(); window->show();
                }
                QJsonObject row{{"event","sample"},{"page",s.currentPage()},{"zoom",s.zoom()},
                    {"window_active",window->isActive()},{"window_visible",window->isVisible()},
                    {"scroll_offset",viewport->property("contentY").toDouble()},{"model_offset",s.scrollOffset()},
                    {"active_pages",s.model().rowCount()},{"cpu_image_bytes",double(s.model().cpuBytes())},
                    {"gpu_pixel_estimate",double(s.model().gpuBytes())},{"evictions",double(s.model().evictions)},
                    {"cache_lookups",double(s.model().lookups)},{"exact_hits",double(s.model().exactHits)},
                    {"degraded_hits",double(s.model().degradedHits)},{"misses",double(s.model().misses)},
                    {"render_jobs",double(s.model().renderJobs)}};
#ifdef Q_OS_WIN
                PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
                QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)));
                row["working_set_bytes"]=double(memory.WorkingSetSize);
                row["private_bytes"]=double(memory.PrivateUsage);
#endif
                record(row);
            }
            double y=viewport->property("contentY").toDouble()+direction*stepMs*0.5;
            const double maxY=std::max(0.0,s.contentHeight()-viewport->height());
            if(y<0 || y>maxY) direction=-direction;
            y=std::clamp(y,0.0,maxY);
            viewport->setProperty("contentY",y);
            if (std::abs(s.scrollOffset()-viewport->property("contentY").toDouble()) > std::max(64.0,viewport->height()/4)) {
                loop.quit();
                QFAIL("Continuous input left the virtualized viewport behind");
            }
            // Record actual Qt frame events. Does not measure display scanout or OS input.
            window->update();
            QVERIFY(s.model().cpuBytes()<=s.model().config.cpuBytes);
            QVERIFY(s.model().gpuBytes()<=s.model().config.gpuBytes);
            QVERIFY(s.model().rowCount()<16);
            if (!s.errorCode().isEmpty() || s.model().cpuBytes()>s.model().config.cpuBytes
                || s.model().gpuBytes()>s.model().config.gpuBytes || s.model().rowCount()>=16) loop.quit();
            QVERIFY2(s.errorCode().isEmpty(),qPrintable(s.statusText()));
        },Qt::QueuedConnection);
        watchdog.start((seconds+30)*1000);
        window->update();
        loop.exec();
        watchdog.stop();
        disconnect(pumpConnection);
        QVERIFY2(!timedOut,"Qt frame pump stalled");
        disconnect(frameConnection);
        QList<double> frameTimes;
        {
            std::lock_guard frameLock(frameState->mutex);
            frameTimes = frameState->times;
        }
        QFile frames(trace.fileName()+".frames.json");
        QVERIFY(frames.open(QIODevice::WriteOnly|QIODevice::NewOnly));
        QJsonArray rawFrames;
        for(double t:frameTimes) rawFrames.append(t);
        frames.write(QJsonDocument(rawFrames).toJson(QJsonDocument::Compact));
        std::sort(frameTimes.begin(),frameTimes.end());
        const auto percentile=[&](double p) { return frameTimes.isEmpty()?0.0:frameTimes[std::max(0,int(std::ceil(p*frameTimes.size()))-1)]; };
        record({{"event","end"},{"frames",frameTimes.size()},{"p50_ms",percentile(.5)},{"p95_ms",percentile(.95)},
            {"p99_ms",percentile(.99)},{"max_ms",percentile(1)},
            {"over_16_67_fraction",double(std::count_if(frameTimes.begin(),frameTimes.end(),[](double t){return t>16.67;}))/std::max(qsizetype(1),frameTimes.size())}});
        QCOMPARE(warnings.size(),0);
        s.beginShutdown();
        QTRY_VERIFY_WITH_TIMEOUT(s.shutdownComplete(),60000);
    }
};
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    QQuickStyle::setStyle("Basic");
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#endif
    ReadingTests tests;
    return QTest::qExec(&tests,argc,argv);
}
#include "reading_tests.moc"
