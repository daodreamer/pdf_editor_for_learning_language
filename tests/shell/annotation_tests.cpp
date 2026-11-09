#include "testapplication.h"
#include <QTemporaryDir>
#include "shellstate.h"
#include "annotationtext.h"
#include "answerlines.h"
#include <QPainter>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QClipboard>
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QThread>
#include <QQmlApplicationEngine>
#include <QTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtQml/QQmlExtensionPlugin>
Q_IMPORT_QML_PLUGIN(LanglernShellPlugin)

class AnnotationTests : public QObject {
    Q_OBJECT
    const QString fixture=QStringLiteral(LANGLERN_FIXTURES)+"/text.pdf";
    static QQuickItem* find(QQuickItem* root,const QString& name) {
        if(root->objectName()==name) return root;
        for(auto* child:root->childItems()) if(auto* item=find(child,name)) return item;
        return nullptr;
    }
    std::unique_ptr<QTemporaryDir> caseWorkspace;
private slots:
    void init() { caseWorkspace=std::make_unique<QTemporaryDir>(); setTestEnvironment("LANGLERN_WORKSPACE_DIR",caseWorkspace->path()); }

    void answerRuleDetection() {
        QImage image(600,800,QImage::Format_RGB32); image.fill(QColor(245,245,245));
        QPainter painter(&image); painter.setPen(QPen(Qt::black,1));
        painter.drawLine(100,300,280,300); painter.drawLine(100,400,340,400);
        painter.drawLine(100,200,280,200); painter.fillRect(110,185,150,10,Qt::black);
        painter.drawRect(350,400,200,100); painter.end();
        QElapsedTimer timer; timer.start(); const auto areas=AnswerLines::detect(image,{600,800});
        qInfo()<<"answer_detection_ms"<<timer.nsecsElapsed()/1e6<<"candidates"<<areas.size();
        QCOMPARE(areas.size(),2); QVERIFY(areas.first().contains(QPointF(.3,.36)));
        QVERIFY(AnswerLines::detect({}, {600,800}).isEmpty());
        QVERIFY(AnswerLines::detect(image, {NAN,800}).isEmpty());
        QImage patterns(600,800,QImage::Format_RGB32); patterns.fill(Qt::white);
        QPainter pen(&patterns); pen.setPen(QPen(Qt::black,1));
        for(int x=100;x<340;x+=10) pen.drawLine(x,100,x+5,100);
        for(int x=100;x<280;x+=4) pen.drawPoint(x,200);
        for(int x=100;x<124;x+=9) pen.drawLine(x,250,x+5,250);
        for(int x:{100,104,108}) pen.drawPoint(x,300); // Printed ellipsis is not a field.
        for(int x=100;x<340;x+=10) pen.drawLine(x,500,x+5,500);
        pen.drawLine(100,500,100,550); pen.drawLine(335,500,335,550); pen.end();
        const auto dashed=AnswerLines::detect(patterns,{600,800});
        QCOMPARE(dashed.size(),3);
    }
    void longDashedDetection_data() {
        QTest::addColumn<int>("scale");
        QTest::addColumn<int>("rise");
        QTest::addColumn<int>("markWidth");
        for(int scale:{1,2,4}) for(int rise:{-5,0,5}) for(int markWidth:{1,5})
            QTest::newRow(qPrintable(QStringLiteral("scale%1-rise%2-mark%3").arg(scale).arg(rise).arg(markWidth)))<<scale<<rise<<markWidth;
    }
    void longDashedDetection() {
        QFETCH(int,scale); QFETCH(int,rise); QFETCH(int,markWidth);
        QImage image(600*scale,800*scale,QImage::Format_RGB32); image.fill(Qt::white);
        QPainter painter(&image); painter.scale(scale,scale);
        painter.setPen(QPen(Qt::black,1));
        // A long scanned rule may cross several raster rows, with local jitter.
        const int pitch=markWidth==1?4:9;
        for(int x=30;x<=570;x+=pitch) {
            const int y=100+qRound(rise*(x-30)/540.0)+(x%12==0?1:0);
            painter.drawLine(x,y,x+markWidth-1,y);
        }
        for(int x=30;x<=190;x+=4) painter.drawPoint(x,200);
        for(int x=230;x<=570;x+=4) painter.drawPoint(x,200);
        for(int x=30;x<=570;x+=4) painter.drawPoint(x,218);
        painter.end();
        const auto areas=AnswerLines::detect(image,{600,800});
        qInfo()<<"long_dashed"<<scale<<rise<<areas;
        QCOMPARE(areas.size(),4);
        auto at=[&](double x,double y) {
            for(const auto& r:areas) if(r.contains(QPointF(x/600,y/800))) return r;
            return QRectF();
        };
        const auto longLine=at(300,90);
        QVERIFY(!longLine.isEmpty());
        QVERIFY(longLine.left()*600<=32); QVERIFY(longLine.right()*600>=568);
        const auto left=at(100,195),right=at(400,195),below=at(300,213);
        QVERIFY(!left.isEmpty()); QVERIFY(!right.isEmpty()); QVERIFY(!below.isEmpty());
        QVERIFY(left.right()*600<200); QVERIFY(right.left()*600>220);
        QVERIFY(below.left()*600<=32); QVERIFY(below.right()*600>=568);
        AnnotationModel model;
        PageGeometry g; g.displayToPdf=QTransform(600,0,0,-800,0,800);
        g.pdfToDisplay=g.displayToPdf.inverted(); g.units={600,800}; g.valid=true;
        model.setGeometry(0,g);
        const auto id=model.create(0,longLine.x(),longLine.y(),longLine.width()*600,longLine.height()*800);
        QVERIFY(!id.isEmpty());
        const QString answer=QStringLiteral("Une réponse longue qui doit tenir sur cette ligne. ").repeated(2);
        model.setText(id,answer,14);
        QCOMPARE(model.snapshot().first().height,14.0);
        model.setText(id,answer.repeated(5),14);
        QVERIFY(model.snapshot().first().height>14);
    }
    void faintMergedLinesStopAtObstacles() {
        QImage image(600,800,QImage::Format_RGB32); image.fill(Qt::white);
        QPainter painter(&image); painter.setPen(QPen(QColor(220,220,220),1));
        for(int x=60;x<=540;x+=4) painter.drawPoint(x,100);
        painter.drawLine(160,100,180,100); painter.drawLine(340,100,355,100);
        painter.fillRect(50,180,510,30,QColor(245,210,230)); painter.setPen(QPen(QColor(145,145,145),1));
        for(int x=60;x<=540;x+=4) painter.drawPoint(x,200);
        painter.drawLine(160,200,180,200); painter.drawLine(340,200,355,200);
        painter.setPen(QPen(Qt::black,1));
        for(int y:{300,400}) for(int x=100;x<=200;x+=4) painter.drawPoint(x,y);
        painter.setPen(QPen(QColor(220,220,220),1)); painter.drawRect(204,300,180,60);
        QFont font; font.setPixelSize(12); painter.setFont(font); painter.drawText(204,400,QStringLiteral("IIIIIIIIII"));
        painter.end();
        const auto areas=AnswerLines::detect(image,{600,800});
        auto at=[&](int x,int y) { for(const auto& area:areas) if(area.contains(QPointF(x/600.0,y/800.0))) return area; return QRectF(); };
        for(int y:{90,190}) {
            const auto area=at(300,y); QVERIFY(!area.isEmpty());
            QVERIFY(area.left()*600<=62); QVERIFY(area.right()*600>=538);
        }
        for(int y:{290,390}) {
            const auto area=at(150,y); QVERIFY(!area.isEmpty());
            QVERIFY2(area.right()*600<=203,"Extension must stop at faint table edges and printed text.");
        }
    }
    void longDashedAtCandidateLimit() {
        QImage image(1024,2048,QImage::Format_RGB32); image.fill(Qt::white);
        QPainter painter(&image); painter.setPen(QPen(Qt::black,1));
        for(int i=0;i<AnswerLines::maxLines-1;++i)
            for(int x=30;x<=150;x+=4) painter.drawPoint(x,40+i*15);
        const int lastY=40+(AnswerLines::maxLines-1)*15;
        for(int x=30;x<=982;x+=4) painter.drawPoint(x,lastY+qRound(5*(x-30)/952.0));
        for(int x=30;x<=150;x+=4) painter.drawPoint(x,lastY+20);
        painter.end();
        const auto areas=AnswerLines::detect(image,{1024,2048});
        QCOMPARE(areas.size(),AnswerLines::maxLines);
        const auto last=*std::max_element(areas.begin(),areas.end(),[](const auto& a,const auto& b){return a.top()<b.top();});
        QVERIFY(last.left()*1024<=32); QVERIFY(last.right()*1024>=980);
    }
    void previewKeepsFinalAnswerGeometry() {
        QThread thread; auto* worker=new PdfWorker;
        worker->moveToThread(&thread);
        connect(worker,&PdfWorker::stopped,&thread,&QThread::quit,Qt::DirectConnection);
        connect(&thread,&QThread::finished,worker,&QObject::deleteLater);
        QList<PdfFrame> frames;
        QObject deliveryContext;
        connect(worker,&PdfWorker::frameReady,&deliveryContext,[&](PdfFrame frame){frames.append(std::move(frame));},Qt::QueuedConnection);
        thread.start();
        auto render=[&](quint64 revision,int width,bool previewOnly) {
            PdfRequest request; request.session=1; request.revision=revision;
            request.path=QStringLiteral(LANGLERN_FIXTURES)+"/answer-lines.pdf";
            request.width=width; request.skipPreview=true; request.previewOnly=previewOnly;
            worker->submit(request);
            QElapsedTimer timer; timer.start();
            while(timer.elapsed()<10000) {
                for(const auto& frame:frames) {
                    if(frame.revision==revision && !frame.image.isNull()) return frame;
                }
                QTest::qWait(10);
            }
            return PdfFrame();
        };
        const auto final=render(1,1200,false);
        const auto preview=render(2,480,true);
        const auto refreshed=render(3,1200,false);
        // Clean up the PDFium thread even when a frame assertion below fails.
        worker->shutdown();
        QElapsedTimer timer; timer.start();
        while(thread.isRunning() && timer.elapsed()<10000) QTest::qWait(10);
        QVERIFY(thread.wait(1000));
        QVERIFY(final.answerLinesReady); QVERIFY(!final.answerLines.isEmpty());
        QVERIFY(preview.preview); QVERIFY(!preview.image.isNull());
        QVERIFY(!preview.answerLinesReady); QVERIFY(preview.answerLines.isEmpty());
        ReadingModel model; model.reset(final.pages);
        model.put(0,final.image,1200,true,final.answerLines,final.answerLinesReady);
        model.put(0,preview.image,480,false,preview.answerLines,preview.answerLinesReady);
        QCOMPARE(model.answerLines(0),final.answerLines);
        QVERIFY(refreshed.answerLinesReady);
        model.put(0,refreshed.image,1200,true,refreshed.answerLines,refreshed.answerLinesReady);
        QCOMPARE(model.answerLines(0),refreshed.answerLines);
    }
    void realTextbookFieldsStableAcrossRenderWidths() {
        // The detector runs on the zoom-dependent final render. Before the chain
        // detector, page 34 lost "Âge" at 800 px and page 39 lost three name
        // fields; at 600 px many fields absorbed the preceding colon.
        const auto path=qEnvironmentVariable("LANGLERN_REAL_PDF",QStringLiteral("C:/Users/jiong/Desktop/法语/你好法语3学生用书第二版.pdf"));
        if(!QFileInfo::exists(path)) QSKIP("The configured real textbook is unavailable on this runner; real-PDF acceptance was not performed.");
        QThread thread; auto* worker=new PdfWorker;
        worker->moveToThread(&thread);
        connect(worker,&PdfWorker::stopped,&thread,&QThread::quit,Qt::DirectConnection);
        connect(&thread,&QThread::finished,worker,&QObject::deleteLater);
        QList<PdfFrame> frames;
        QObject deliveryContext;
        connect(worker,&PdfWorker::frameReady,&deliveryContext,[&](PdfFrame frame){frames.append(std::move(frame));},Qt::QueuedConnection);
        thread.start();
        quint64 revision=0;
        auto fields=[&](int page,int width) {
            PdfRequest request; request.session=1; request.revision=++revision; request.page=page;
            request.path=path; request.width=width; request.skipPreview=true;
            worker->submit(request);
            QElapsedTimer timer; timer.start();
            while(timer.elapsed()<30000) {
                for(const auto& frame:frames) if(frame.revision==request.revision && !frame.image.isNull()) {
                    QList<QRectF> dotted;
                    for(const auto& r:frame.answerLines)
                        if(std::abs(r.height()*frame.geometry.units.height()-AnswerLines::dottedFieldHeight)<.01) dotted.append(r);
                    return dotted;
                }
                QTest::qWait(10);
            }
            return QList<QRectF>{QRectF(-1,-1,0,0)};
        };
        struct Expected { int page; QList<QPointF> spans; };
        // Endpoints read from the 2400 px rendering of each page.
        const QList<Expected> expected{
            {33,{{.621,.714},{.798,.889},{.613,.660},{.756,.889},{.639,.889},{.684,.889},{.695,.746},{.842,.890}}},
            {38,{{.144,.460},{.167,.460},{.144,.188},{.297,.460},{.252,.460},{.403,.460},{.306,.460},{.187,.460},{.209,.460}}}};
        QStringList failures;
        for(const auto& e:expected) for(int width:{600,800,1200}) {
            const auto found=fields(e.page,width);
            qInfo()<<"stable_fields page"<<e.page+1<<"width"<<width<<found;
            if(found.size()!=e.spans.size()) failures<<QStringLiteral("page %1 width %2: %3 fields").arg(e.page+1).arg(width).arg(found.size());
            for(const auto& span:e.spans) {
                bool match=false;
                for(const auto& r:found) match=match||(std::abs(r.left()-span.x())<.008 && std::abs(r.right()-span.y())<.008);
                if(!match) failures<<QStringLiteral("page %1 width %2: missing %3-%4").arg(e.page+1).arg(width).arg(span.x()).arg(span.y());
            }
        }
        worker->shutdown();
        QElapsedTimer timer; timer.start();
        while(thread.isRunning() && timer.elapsed()<10000) QTest::qWait(10);
        QVERIFY(thread.wait(1000));
        QVERIFY2(failures.isEmpty(),qPrintable(failures.join(QStringLiteral("; "))));
    }
    void realTextbookDashedFields() {
        const auto path=qEnvironmentVariable("LANGLERN_REAL_PDF",QStringLiteral("C:/Users/jiong/Desktop/法语/你好法语3学生用书第二版.pdf"));
        if(!QFileInfo::exists(path)) QSKIP("The configured real textbook is unavailable on this runner; real-PDF acceptance was not performed.");
        ShellState shell; QQmlApplicationEngine engine;
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}}); engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(window);
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
        shell.selectFile(QUrl::fromLocalFile(path)); QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),30000);
        QTRY_COMPARE(shell.pageCount(),153); shell.goToPage(34); shell.setZoom(.65);
        QTRY_VERIFY_WITH_TIMEOUT(!shell.busy(),30000);
        QTRY_VERIFY_WITH_TIMEOUT(shell.model().ready(33,shell.model().targetWidth(33,window->devicePixelRatio())),30000);
        auto settled=[&] { for(int page:shell.model().priority()) if(!shell.model().ready(page,shell.model().targetWidth(page,window->devicePixelRatio()))) return false; return true; };
        QTRY_VERIFY_WITH_TIMEOUT(settled(),30000);
        QTRY_VERIFY_WITH_TIMEOUT(shell.model().nextUnknown()<0,30000);
        qInfo()<<"real_textbook"<<QFileInfo(path).size()<<"page"<<34<<"candidates"<<shell.model().answerLines(33).size();
        qInfo()<<"real_fields"<<shell.model().answerLines(33);
        QCOMPARE(shell.model().answerLines(33).size(),8);
        const auto output=qEnvironmentVariable("LANGLERN_REAL_ARTIFACT_DIR");
        if(!output.isEmpty()) {
            auto overlay=shell.model().image(33).copy(); QVERIFY(overlay.save(output+"/pdfium-page-34.png"));
            QPainter p(&overlay); p.setPen(QPen(Qt::red,1));
            for(const auto& r:shell.model().answerLines(33)) p.drawRect(QRectF(r.x()*overlay.width(),r.y()*overlay.height(),r.width()*overlay.width(),r.height()*overlay.height()));
            p.end(); QVERIFY(overlay.save(output+"/candidate-overlay.png"));
        }
        const QList<QPointF> targets{{.67,.14},{.843,.14},{.637,.157},{.824,.157},{.768,.175},{.777,.193},{.721,.229},{.871,.229}};
        QList<QRectF> areas;
        for(const auto& p:targets) {
            const auto area=shell.answerBlankAt(shell.pageLeft(33)+p.x()*shell.pageWidth(),shell.pageTop(33)+p.y()*shell.pageHeight(33));
            qInfo()<<"real_target"<<p<<area; QVERIFY2(!area.isEmpty(),qPrintable(QStringLiteral("Missing real dotted field at %1,%2").arg(p.x()).arg(p.y())));
            areas.append(area);
        }
        auto* viewport=find(window->contentItem(),"pdfViewport"); QVERIFY(viewport);
        QTRY_VERIFY(std::abs(viewport->property("contentY").toDouble()-shell.scrollOffset())<.5);
        auto scene=[&](QPointF p) { return viewport->mapToScene(p-QPointF(viewport->property("contentX").toDouble(),viewport->property("contentY").toDouble())).toPoint(); };
        QTest::mouseMove(window,scene(areas.first().center()),30);
        auto* hint=find(window->contentItem(),"answerLineHint"); QVERIFY(hint); QTRY_VERIFY(hint->isVisible());
        if(!output.isEmpty()) QVERIFY(window->grabWindow().save(output+"/real-hover.png"));
        const auto click=hint->mapToScene(QPointF(hint->width()/2,hint->height()/2)).toPoint();
        qInfo()<<"real_click"<<scene(areas.first().center())<<click<<shell.annotations()->snapshot().size();
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,click);
        qInfo()<<"real_after_click"<<shell.annotations()->snapshot().size()<<shell.annotations()->selectedId();
        QTRY_COMPARE(shell.annotations()->snapshot().size(),1);
        auto* editor=find(window->contentItem(),"annotationEditor"); QVERIFY(editor); QTRY_VERIFY(editor->hasActiveFocus());
        QGuiApplication::clipboard()->setText(QStringLiteral("Marie Dupont avec une longue réponse")); QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
        QTRY_VERIFY(shell.annotations()->snapshot().first().text.startsWith("Marie"));
        const auto expanded=shell.annotations()->snapshot().first();
        editor->setProperty("cursorPosition",5); QTest::keyClick(window,Qt::Key_End,Qt::ControlModifier|Qt::ShiftModifier); QTest::keyClick(window,Qt::Key_Backspace);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("Marie"));
        QTRY_VERIFY(shell.annotations()->snapshot().first().height<expanded.height);
        QTest::keyClick(window,Qt::Key_Return,Qt::ControlModifier);
        // High-DPI focus/viewport updates can temporarily replace the cached frame.
        QTRY_VERIFY_WITH_TIMEOUT(!shell.answerBlankAt(areas[2].center().x(),areas[2].center().y()).isEmpty(),30000);
        // The long address rule must remain one editor across its full extent.
        const auto address=areas[4];
        const auto units=shell.annotations()->geometry(33).units;
        QVERIFY(address.width()/shell.pageWidth()>.24);
        const double addressLeft=(address.left()-shell.pageLeft(33))/shell.pageWidth();
        const double addressRight=(address.right()-shell.pageLeft(33))/shell.pageWidth();
        // Independently checked on the rendered source: the dots span .639 to .889.
        QVERIFY(std::abs(addressLeft-.639)<.004); QVERIFY(std::abs(addressRight-.889)<.004);
        QTest::mouseMove(window,scene(address.center()),30);
        QTRY_VERIFY(hint->isVisible());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hint->mapToScene(QPointF(hint->width()/2,hint->height()/2)).toPoint());
        QTRY_COMPARE(shell.annotations()->snapshot().size(),2);
        const auto addressId=shell.annotations()->selectedId(); QVERIFY(!addressId.isEmpty());
        const QString longAnswer=QStringLiteral("12 rue de la Paix, 75002 Paris");
        QGuiApplication::clipboard()->setText(longAnswer); QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
        auto addressNote=[&] { for(const auto& n:shell.annotations()->snapshot()) if(n.id==addressId) return n; return TextAnnotation(); };
        QTRY_COMPARE(addressNote().text,longAnswer);
        QVERIFY(addressNote().maximumWidth>.24*units.width());
        QTRY_COMPARE(addressNote().height,14.0);
        QTest::keyClick(window,Qt::Key_Return,Qt::ControlModifier);
        if(!output.isEmpty()) QVERIFY(window->grabWindow().save(output+"/real-answer.png"));
        const auto notes=shell.annotations()->snapshot();
        shell.saveNow(); QTRY_VERIFY(shell.storageStatus().contains(QStringLiteral("已保存")));
        shell.clearSelection(); QTRY_COMPARE(shell.pageCount(),0); shell.selectFile(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),30000); QTRY_COMPARE(shell.annotations()->snapshot().size(),2);
        QCOMPARE(shell.annotations()->snapshot(),notes);
        window->close(); QTRY_VERIFY(shell.shutdownComplete());
    }
    void realTextbookLongLineSamples_data() {
        QTest::addColumn<int>("page");
        QTest::addColumn<QRectF>("expected");
        QTest::newRow("page7-abbreviations")<<7<<QRectF();
        // Endpoints checked against the source page, not detector output.
        QTest::newRow("page39-name-answer")<<39<<QRectF(.144,.290,.316,0);
        // Seed 20261020 sampled 88, 67, 47 from visually checked separator pages.
        QTest::newRow("page88-separator")<<88<<QRectF(.124,.580,.751,0);
        QTest::newRow("page67-separator")<<67<<QRectF(.123,.824,.752,0);
        QTest::newRow("page47-separator")<<47<<QRectF(.121,.723,.753,0);
    }
    void realTextbookLongLineSamples() {
        QFETCH(int,page); QFETCH(QRectF,expected);
        const auto path=qEnvironmentVariable("LANGLERN_REAL_PDF",QStringLiteral("C:/Users/jiong/Desktop/法语/你好法语3学生用书第二版.pdf"));
        if(!QFileInfo::exists(path)) QSKIP("The configured real textbook is unavailable; real-PDF sampling was not performed.");
        ShellState shell; QQmlApplicationEngine engine;
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}}); engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(window);
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
        shell.selectFile(QUrl::fromLocalFile(path)); QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),30000);
        QTRY_COMPARE(shell.pageCount(),153); shell.goToPage(page); shell.setZoom(.65);
        QTRY_VERIFY_WITH_TIMEOUT(shell.model().ready(page-1,shell.model().targetWidth(page-1,window->devicePixelRatio())),30000);
        QTRY_VERIFY_WITH_TIMEOUT(shell.model().nextUnknown()<0,30000);
        const auto lines=shell.model().answerLines(page-1);
        qInfo()<<"sample_page"<<page<<"fields"<<lines;
        const auto output=qEnvironmentVariable("LANGLERN_REAL_ARTIFACT_DIR");
        if(!output.isEmpty()) {
            const auto folder=output+QStringLiteral("/page-%1").arg(page); QVERIFY(QDir().mkpath(folder));
            auto overlay=shell.model().image(page-1).copy(); QVERIFY(overlay.save(folder+"/source.png"));
            QPainter painter(&overlay); painter.setPen(QPen(Qt::red,1));
            for(const auto& r:lines) painter.drawRect(QRectF(r.x()*overlay.width(),r.y()*overlay.height(),r.width()*overlay.width(),r.height()*overlay.height()));
            painter.end(); QVERIFY(overlay.save(folder+"/candidates.png"));
        }
        if(page==7) {
            qInfo()<<"Page 7 is an abbreviations table with decorative solid rules; no long dashed answer field to accept.";
        } else {
            const auto g=shell.annotations()->geometry(page-1);
            const QPointF target(expected.center().x(),expected.y()-4/g.units.height());
            const auto area=shell.answerBlankAt(shell.pageLeft(page-1)+target.x()*shell.pageWidth(),shell.pageTop(page-1)+target.y()*shell.pageHeight(page-1));
            QVERIFY2(!area.isEmpty(),qPrintable(QStringLiteral("Missing long rule on page %1").arg(page)));
            const double left=(area.left()-shell.pageLeft(page-1))/shell.pageWidth();
            const double right=(area.right()-shell.pageLeft(page-1))/shell.pageWidth();
            qInfo()<<"sample_endpoints"<<page<<left<<right<<"expected"<<expected.left()<<expected.right();
            QVERIFY2(std::abs(left-expected.left())<.006,qPrintable(QStringLiteral("Page %1 left endpoint is incomplete: %2").arg(page).arg(left)));
            QVERIFY2(std::abs(right-expected.right())<.006,qPrintable(QStringLiteral("Page %1 right endpoint is incomplete: %2").arg(page).arg(right)));
            if(page==39) {
                auto* viewport=find(window->contentItem(),"pdfViewport"); QVERIFY(viewport);
                viewport->setProperty("contentY",std::max(0.0,area.center().y()-viewport->height()/2));
                QTRY_VERIFY(std::abs(viewport->property("contentY").toDouble()-shell.scrollOffset())<.5);
                auto* hint=find(window->contentItem(),"answerLineHint"); QVERIFY(hint);
                // Wait for this specific field after asynchronous viewport/frame
                // changes, rather than clicking a still-visible neighbouring hint.
                auto hoverTarget=[&] {
                    const QPointF content(shell.pageLeft(page-1)+target.x()*shell.pageWidth(),shell.pageTop(page-1)+target.y()*shell.pageHeight(page-1));
                    const auto current=shell.answerBlankAt(content.x(),content.y());
                    if(current.isEmpty()) return false;
                    const auto point=viewport->mapToScene(current.center()-QPointF(viewport->property("contentX").toDouble(),viewport->property("contentY").toDouble())).toPoint();
                    QTest::mouseMove(window,point,1);
                    const auto center=hint->mapToScene(QPointF(hint->width()/2,hint->height()/2));
                    return hint->isVisible() && QLineF(center,point).length()<2 && std::abs(hint->width()-current.width())<1;
                };
                QTRY_VERIFY_WITH_TIMEOUT(hoverTarget(),30000);
                QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,hint->mapToScene(QPointF(hint->width()/2,hint->height()/2)).toPoint());
                QTRY_COMPARE(shell.annotations()->snapshot().size(),1);
                QVERIFY(std::abs(shell.annotations()->snapshot().first().maximumWidth-expected.width()*g.units.width())<.006*g.units.width());
                auto* editor=find(window->contentItem(),"annotationEditor"); QVERIFY(editor); QTRY_VERIFY(editor->hasActiveFocus());
                const QString answer=QStringLiteral("Marie Dupont et Jean Martin à Paris");
                QGuiApplication::clipboard()->setText(answer); QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
                QTRY_COMPARE(shell.annotations()->snapshot().first().text,answer);
                const auto note=shell.annotations()->snapshot().first();
                QFont font(note.family); font.setPixelSize(qRound(note.fontSize));
                qInfo()<<"sample_text_layout"<<note.width<<note.maximumWidth<<note.fontSize<<QFontMetricsF(font).horizontalAdvance(answer)<<editor->property("contentWidth")<<editor->property("contentHeight")<<note.styles.size();
                QTRY_COMPARE(shell.annotations()->snapshot().first().height,14.0);
                if(!output.isEmpty()) QVERIFY(window->grabWindow().save(output+"/page-39/answer.png"));
            }
        }
        window->close(); QTRY_VERIFY(shell.shutdownComplete());
    }
    void hoverRuleReusesEditorAndShrinksAfterDeletion() {
        ShellState shell; QQmlApplicationEngine engine; QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors)warnings.append(e.toString());});
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}}); engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(window);
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
        shell.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/answer-lines.pdf"));
        QTRY_VERIFY(shell.storageReady()); QTRY_COMPARE(shell.pageCount(),4); QTRY_VERIFY(!shell.busy());
        shell.setZoom(.65); QTRY_VERIFY(!shell.busy());
        auto* viewport=find(window->contentItem(),"pdfViewport"); QVERIFY(viewport);
        auto area=[&]() {
            for(int y=3;y<90;++y) {
                auto rect=shell.answerBlankAt(shell.pageLeft(shell.currentPage()-1)+.3*shell.pageWidth(),shell.pageTop(shell.currentPage()-1)+y*.01*shell.pageHeight(shell.currentPage()-1));
                if(!rect.isEmpty()) return rect;
            }
            return QRectF();
        };
        // Real PDFium rendering: vector, cropped, rotated and raster pages all have rules.
        for(int page=0;page<4;++page) {
            shell.goToPage(page+1); QTRY_VERIFY_WITH_TIMEOUT(!area().isEmpty(),10000);
        }
        shell.goToPage(1); QTRY_COMPARE(shell.currentPage(),1); QTRY_VERIFY(!area().isEmpty());
        QTRY_VERIFY(std::abs(viewport->property("contentY").toDouble()-shell.scrollOffset())<.5);
        auto settled=[&] {
            for(int page:shell.model().priority()) if(!shell.model().ready(page,shell.model().targetWidth(page,window->devicePixelRatio()))) return false;
            return true;
        };
        QTRY_VERIFY(settled());
        const auto target=area();
        auto scene=[&](QPointF p) { return viewport->mapToScene(p-QPointF(viewport->property("contentX").toDouble(),viewport->property("contentY").toDouble())).toPoint(); };
        QTest::mouseMove(window,scene(target.center()),30);
        auto* hover=engine.rootObjects().first()->findChild<QObject*>("answerHover"); QVERIFY(hover);
        qInfo()<<"answer_hover"<<target<<scene(target.center())<<viewport->property("contentY")<<hover->property("hovered")<<viewport->property("moving");
        auto* hint=find(window->contentItem(),"answerLineHint"); QVERIFY(hint); QTRY_VERIFY(hint->isVisible());
        const auto output=qEnvironmentVariable("LANGLERN_ANSWER_ARTIFACT_DIR");
        if(!output.isEmpty()) QVERIFY(window->grabWindow().save(output+"/hover-rule.png"));
        const auto jobs=shell.model().renderJobs;
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,scene(target.center()));
        QTRY_COMPARE(shell.annotations()->snapshot().size(),1);
        auto* editor=find(window->contentItem(),"annotationEditor"); QVERIFY(editor); QTRY_VERIFY(editor->hasActiveFocus());
        auto note=shell.annotations()->snapshot().first();
        QVERIFY(note.maximumWidth>150 && note.maximumWidth<190);
        QVERIFY(std::abs(note.width-note.maximumWidth)<.01);
        const QString text=QStringLiteral("short answer with many words ").repeated(7);
        QGuiApplication::clipboard()->setText(text); QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,text);
        QTRY_VERIFY(shell.annotations()->snapshot().first().height>48);
        const auto expanded=shell.annotations()->snapshot().first();
        QVERIFY(expanded.width<=expanded.maximumWidth);
        editor->setProperty("cursorPosition",5);
        QTest::keyClick(window,Qt::Key_End,Qt::ControlModifier|Qt::ShiftModifier); QTest::keyClick(window,Qt::Key_Backspace);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("short"));
        QTRY_VERIFY(shell.annotations()->snapshot().first().width<expanded.width);
        QTRY_COMPARE(shell.annotations()->snapshot().first().height,24.0);
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,text);
        QTest::keyClick(window,Qt::Key_Y,Qt::ControlModifier);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("short"));
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); QTest::keyClick(window,Qt::Key_Backspace);
        QTRY_VERIFY(shell.annotations()->snapshot().first().text.isEmpty());
        QTRY_VERIFY(shell.annotations()->snapshot().first().width<=24);
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("short"));
        const auto saved=AnnotationSnapshot{shell.annotations()->snapshot(),{},0};
        AnnotationSnapshot restored; QString error;
        QVERIFY(StorageWorker::decode(StorageWorker::encode(saved,"test"),"test",restored,error)); QCOMPARE(restored.notes,saved.notes);
        QTest::keyClick(window,Qt::Key_Return,Qt::ControlModifier);
        QVERIFY(shell.answerBlankAt(target.center().x(),target.center().y()).isEmpty());
        QTest::mouseMove(window,scene(target.center()),20); QVERIFY(!hint->isVisible());
        if(!output.isEmpty()) QVERIFY(window->grabWindow().save(output+"/answer-shrunk.png"));
        QCOMPARE(shell.model().renderJobs,jobs);
        if(!output.isEmpty()) {
            shell.exportPdf(QUrl::fromLocalFile(output+"/answer-export.pdf")); QTRY_VERIFY_WITH_TIMEOUT(!shell.exporting(),30000);
            QVERIFY2(shell.exportStatus().contains(QStringLiteral("已导出")),qPrintable(shell.exportStatus()));
        }
        shell.saveNow(); QTRY_VERIFY(shell.storageStatus().contains(QStringLiteral("已保存")));
        shell.clearSelection(); QTRY_COMPARE(shell.pageCount(),0);
        shell.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/answer-lines.pdf"));
        QTRY_VERIFY(shell.storageReady()); QTRY_COMPARE(shell.annotations()->snapshot().size(),1);
        QCOMPARE(shell.annotations()->snapshot().first(),saved.notes.first());
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close(); QTRY_VERIFY(shell.shutdownComplete());
    }

    void modelHistoryAndVirtualization() {
        AnnotationModel model;
        PageGeometry g; g.displayToPdf=QTransform(570,0,0,-780,10,800);
        g.pdfToDisplay=g.displayToPdf.inverted(); g.units={570,780}; g.valid=true;
        model.setGeometry(0,g); model.setGeometry(1,g); model.setActivePages({0});
        const auto id=model.create(0,.2,.3); QVERIFY(!id.isEmpty());
        model.setText(id,QStringLiteral("中文 e\u0301\nanswer"),48);
        const auto initial=model.snapshot().first();
        QCOMPARE(initial.anchor,g.displayToPdf.map(QPointF(.2,.3)));
        model.finishEditing(); QVERIFY(model.canUndo());
        model.undo(); QVERIFY(model.snapshot().isEmpty());
        model.redo(); QCOMPARE(model.snapshot().first(),initial);
        model.select(id); model.setText(id,QStringLiteral("edited"),24); model.finishEditing();
        model.undo(); QCOMPARE(model.snapshot().first(),initial);
        model.redo(); QCOMPARE(model.snapshot().first().text,QStringLiteral("edited"));
        model.move(id,.5,.6); const auto moved=model.snapshot().first();
        QCOMPARE(moved.anchor,g.displayToPdf.map(QPointF(.5,.6)));
        model.undo(); QCOMPARE(model.snapshot().first().anchor,initial.anchor);
        model.redo(); QCOMPARE(model.snapshot().first().anchor,moved.anchor);
        model.select(id); model.removeSelected(); QVERIFY(model.snapshot().isEmpty());
        model.undo(); QCOMPARE(model.snapshot().first(),moved);
        model.setActivePages({1}); QCOMPARE(model.rowCount(),0); QCOMPARE(model.snapshot().size(),1);
        model.setActivePages({0}); QCOMPARE(model.rowCount(),1);
        const auto empty=model.create(0,.99,.99); QVERIFY(!empty.isEmpty());
        model.finishEditing(); QCOMPARE(model.snapshot().size(),1);
        model.reset(); QVERIFY(model.snapshot().isEmpty()); QVERIFY(!model.canUndo());
    }
    void growingWidthAndPageBoundary() {
        AnnotationModel model;
        PageGeometry g; g.displayToPdf=QTransform(570,0,0,-780,10,800);
        g.pdfToDisplay=g.displayToPdf.inverted(); g.units={570,780}; g.valid=true;
        model.setGeometry(0,g); model.setActivePages({0});
        const auto id=model.create(0,.2,.3);
        const double initial=model.snapshot().first().width;
        QVERIFY(initial<48); QVERIFY(!model.snapshot().first().widthLocked);
        model.setText(id,QStringLiteral("this is a longer answer"),24);
        QVERIFY(model.snapshot().first().width>initial);
        QVERIFY(!model.snapshot().first().widthLocked);
        model.setText(id,QString(200,'w'),48);
        QCOMPARE(model.snapshot().first().width,456.0);
        QVERIFY(!model.snapshot().first().widthLocked);
        model.setText(id,QStringLiteral("short"),24);
        QVERIFY(model.snapshot().first().width<100);
        model.finishEditing(); const auto saved=model.snapshot();
        model.undo(); QVERIFY(model.snapshot().isEmpty());
        model.redo(); QCOMPARE(model.snapshot(),saved);
        AnnotationSnapshot snapshot; snapshot.notes=saved;
        model.restore(snapshot); model.setGeometry(0,g); model.setActivePages({0}); model.select(id);
        model.setText(id,QString(300,'w'),48);
        QCOMPARE(model.snapshot().first().width,456.0);
    }
    void resizeHistoryAndBounds() {
        AnnotationModel model;
        PageGeometry g; g.displayToPdf=QTransform(570,0,0,-780,10,800);
        g.pdfToDisplay=g.displayToPdf.inverted(); g.units={570,780}; g.valid=true;
        model.setGeometry(0,g); model.setActivePages({0});
        const auto id=model.create(0,.2,.3);
        model.setText(id,QStringLiteral("resize this answer"),24); model.finishEditing();
        const auto before=model.snapshot().first();
        model.resize(id,.1,.2,200,80);
        const auto after=model.snapshot().first();
        QCOMPARE(after.width,200.0); QCOMPARE(after.minimumHeight,80.0); QCOMPARE(after.height,80.0);
        QCOMPARE(after.anchor,g.displayToPdf.map(QPointF(.1,.2))); QVERIFY(after.widthLocked);
        model.undo(); QCOMPARE(model.snapshot().first(),before);
        model.redo(); QCOMPARE(model.snapshot().first(),after);
        model.select(id); model.setText(id,QStringLiteral("short"),24);
        QCOMPARE(model.snapshot().first().height,24.0); QVERIFY(model.snapshot().first().width<200); model.finishEditing();
        model.resize(id,2,2,10000,10000);
        const auto bounded=model.snapshot().first();
        QCOMPARE(bounded.width,570.0); QCOMPARE(bounded.height,780.0);
        QCOMPARE(bounded.anchor,g.displayToPdf.map(QPointF(0,0)));
        model.resize(id,.2,.3,std::nan(""),24); QCOMPARE(model.snapshot().first(),bounded);
    }
    void hoverAndCornerResize() {
        ShellState shell; QQmlApplicationEngine engine; QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,&engine,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings.append(e.toString());});
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}}); engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(window);
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
        shell.selectFile(QUrl::fromLocalFile(fixture));
        QTRY_VERIFY(shell.storageReady()); QTRY_VERIFY(shell.annotations()->geometry(0).valid);
        shell.setZoom(.6); QTRY_VERIFY(!shell.busy()); QTest::qWait(100);
        const auto id=shell.annotations()->create(0,.3,.2);
        auto* editor=find(window->contentItem(),"annotationEditor"); QVERIFY(editor); editor->forceActiveFocus();
        for(const char c:QByteArray("resize this answer")) QTest::keyClick(window,c);
        QTest::keyClick(window,Qt::Key_Escape);
        auto* note=editor->parentItem()->parentItem(); auto* frame=find(note,"annotationFrame"); QVERIFY(frame);
        QTest::mouseMove(window,QPoint(2,2)); QTRY_COMPARE(frame->opacity(),0.0);
        QTest::mouseMove(window,note->mapToScene(QPointF(note->width()/2,8)).toPoint());
        QTRY_COMPARE(frame->opacity(),1.0); QVERIFY(shell.annotations()->selectedId().isEmpty());
        const auto jobs=shell.model().renderJobs;
        for(int i=0;i<4;++i) {
            auto* handle=find(note,"annotationResizeHandle"+QString::number(i)); QVERIFY(handle);
            const auto before=shell.annotations()->snapshot().first();
            const auto p=handle->mapToScene(QPointF(5,5)).toPoint();
            const QPoint delta(i%2==0?-24:24,i<2?-18:18);
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,p);
            QTest::mouseMove(window,p+delta,25);
            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,p+delta);
            const auto after=shell.annotations()->snapshot().first();
            QVERIFY(after.width>before.width); QVERIFY(after.height>=before.height);
            QVERIFY(after.widthLocked); QCOMPARE(after.text,before.text);
            if(i%2==0 || i<2) QVERIFY(after.anchor!=before.anchor);
            shell.annotations()->undo(); QCOMPARE(shell.annotations()->snapshot().first(),before);
            shell.annotations()->redo(); QCOMPARE(shell.annotations()->snapshot().first(),after);
        }
        auto* handle=find(note,"annotationResizeHandle3"); QVERIFY(handle);
        const auto before=shell.annotations()->snapshot().first(); const auto p=handle->mapToScene(QPointF(5,5)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,p);
        QTest::mouseMove(window,p+QPoint(30,20),25);
        QTest::keyClick(window,Qt::Key_Escape);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,p+QPoint(30,20));
        QCOMPARE(shell.annotations()->snapshot().first(),before);
        QCOMPARE(shell.model().renderJobs,jobs);
        auto* moveHandle=find(note,"annotationMoveHandle"); QVERIFY(moveHandle);
        QTest::mouseMove(window,note->mapToScene(QPointF(20,8)).toPoint()); QTRY_COMPARE(frame->opacity(),1.0);
        const auto moveStart=moveHandle->mapToScene(QPointF(20,8)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,moveStart);
        QTest::mouseMove(window,moveStart+QPoint(25,20),25);
        shell.setZoom(.7);
        QTest::mouseMove(window,moveStart+QPoint(35,25),25);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,moveStart+QPoint(35,25));
        QCOMPARE(shell.annotations()->snapshot().first(),before);
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close(); QTRY_VERIFY(shell.shutdownComplete());
    }
    void selectedTextFormatting() {
        ShellState shell; QQmlApplicationEngine engine; QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,&engine,[&](const QList<QQmlError>& errors){for(const auto& e:errors)warnings.append(e.toString());});
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}}); engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(window);
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
        shell.selectFile(QUrl::fromLocalFile(fixture)); QTRY_VERIFY(shell.storageReady()); QTRY_VERIFY(shell.annotations()->geometry(0).valid);
        shell.setZoom(.8); QTRY_VERIFY(!shell.busy()); QTest::qWait(100);
        const auto id=shell.annotations()->create(0,.3,.2);
        auto* editor=find(window->contentItem(),"annotationEditor"); QVERIFY(editor); editor->forceActiveFocus();
        for(const char c:QByteArray("hello answer")) QTest::keyClick(window,c);
        QTest::keyClick(window,Qt::Key_Home);
        for(int i=0;i<5;++i) QTest::keyClick(window,Qt::Key_Right,Qt::ShiftModifier);
        auto* popup=editor->parentItem()->parentItem()->property("formatPopover").value<QObject*>(); QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        auto click=[&](const QString& name) {
            auto* button=find(window->contentItem(),name); QVERIFY(button);
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());
        };
        click("formatBold");
        QTRY_VERIFY(!shell.annotations()->snapshot().first().styles.isEmpty());
        auto style=shell.annotations()->snapshot().first().styles.first();
        QCOMPARE(style.start,0); QCOMPARE(style.length,5); QVERIFY(style.bold);
        QVERIFY(editor->hasActiveFocus()); QCOMPARE(editor->property("selectedText").toString(),QStringLiteral("hello"));
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier); QTRY_VERIFY(shell.annotations()->snapshot().first().styles.isEmpty());
        QTest::keyClick(window,Qt::Key_Y,Qt::ControlModifier); QTRY_VERIFY(shell.annotations()->snapshot().first().styles.first().bold);
        QVERIFY(QMetaObject::invokeMethod(editor,"select",Q_ARG(int,0),Q_ARG(int,5)));
        QTRY_VERIFY(popup->property("visible").toBool());
        click("formatItalic"); click("formatColor2"); click("formatLarger");
        QTest::qWait(20);
        style=shell.annotations()->snapshot().first().styles.first();
        QVERIFY(style.bold && style.italic); QCOMPARE(style.size,15.0); QCOMPARE(style.color,QColor("#e5484d"));
        QTRY_COMPARE(popup->property("current").toMap()["size"].toDouble(),15.0);
        QVERIFY(popup->property("current").toMap()["italic"].toBool());
        QCOMPARE(style.start,0); QCOMPARE(style.length,5);
        QCOMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("hello answer"));
        auto* controller=editor->parentItem()->parentItem()->findChild<AnnotationText*>(); QVERIFY(controller);
        const auto rest=controller->selectionStyle(6,12); QVERIFY(!rest["bold"].toBool()); QCOMPARE(rest["size"].toDouble(),14.0);
        const auto artifacts=qEnvironmentVariable("LANGLERN_FORMAT_ARTIFACT_DIR");
        if(!artifacts.isEmpty()) {
            QTest::qWait(80); QFile image(QDir(artifacts).filePath("format-popover.png")); QVERIFY(image.open(QIODevice::WriteOnly|QIODevice::NewOnly));
            QVERIFY(window->grabWindow().save(&image,"PNG"));
            const auto theme=shell.themeMode();
            while(!shell.dark()) shell.cycleTheme();
            QTest::qWait(80); QFile darkImage(QDir(artifacts).filePath("format-popover-dark.png")); QVERIFY(darkImage.open(QIODevice::WriteOnly|QIODevice::NewOnly));
            QVERIFY(window->grabWindow().save(&darkImage,"PNG"));
            while(shell.themeMode()!=theme) shell.cycleTheme();
        }
        QTest::keyClick(window,Qt::Key_Escape); QTRY_VERIFY(!popup->property("visible").toBool());
        const auto saved=shell.annotations()->snapshot();
        shell.annotations()->undo(); QVERIFY(shell.annotations()->snapshot().isEmpty());
        shell.annotations()->redo(); QCOMPARE(shell.annotations()->snapshot(),saved);
        editor=find(window->contentItem(),"annotationEditor"); QVERIFY(editor); QTest::qWait(20);
        controller=editor->parentItem()->parentItem()->findChild<AnnotationText*>(); QVERIFY(controller);
        QVERIFY(controller->selectionStyle(0,5)["bold"].toBool());
        shell.annotations()->select(id); editor->forceActiveFocus();
        QInputMethodEvent input; input.setCommitString(QStringLiteral("中文")); QGuiApplication::sendEvent(window,&input);
        shell.finishTextEditing();
        QVERIFY(shell.annotations()->snapshot().first().text.contains(QStringLiteral("中文")));
        shell.annotations()->select(id);
        const QList<TextStyle> external={{0,8,14,QColor("#30a46c"),false,false}};
        shell.annotations()->setText(id,QStringLiteral("external replacement"),24,encodeTextStyles(external));
        QTest::qWait(30);
        QCOMPARE(shell.annotations()->snapshot().first().styles,external);
        QCOMPARE(controller->styles(),encodeTextStyles(external));
        shell.finishTextEditing();
        QCOMPARE(shell.annotations()->snapshot().first().styles,external);
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close(); QTRY_VERIFY(shell.shutdownComplete());
    }
    void realPageCoordinates() {
        ShellState shell; shell.updateViewport(0,700,1000,1.5);
        shell.selectFile(QUrl::fromLocalFile(fixture));
        QTRY_COMPARE(shell.pageCount(),8);
        QTRY_VERIFY(shell.storageReady());
        for(int page=0;page<8;++page) {
            shell.goToPage(page+1);
            QTRY_VERIFY(shell.annotations()->geometry(page).valid);
            const auto g=shell.annotations()->geometry(page);
            const QList<QPointF> points={{0,0},{1,0},{0,1},{1,1},{.123456,.654321}};
            for(const auto& p:points) {
                const auto raw=g.displayToPdf.map(p);
                const auto back=g.pdfToDisplay.map(raw);
                const auto rawAgain=g.displayToPdf.map(back);
                QVERIFY(QLineF(raw,rawAgain).length()<1e-6);
            }
            // Fixture CropBox is [10 20 580 800], Rotate=0/90/180/270, UserUnit=1/2.
            const QList<QPointF> origins={{10,800},{10,20},{580,20},{580,800}};
            QVERIFY(QLineF(g.displayToPdf.map(QPointF()),origins[page%4]).length()<.001);
            const QList<QPointF> rights={{580,800},{10,800},{10,20},{580,20}};
            const QList<QPointF> downs={{10,20},{580,20},{580,800},{10,800}};
            QVERIFY(QLineF(g.displayToPdf.map(QPointF(1,0)),rights[page%4]).length()<.001);
            QVERIFY(QLineF(g.displayToPdf.map(QPointF(0,1)),downs[page%4]).length()<.001);
            const auto id=shell.annotations()->create(page,.2,.3);
            shell.annotations()->setText(id,QStringLiteral("answer 中文"),24);
            shell.finishTextEditing();
            const auto notes=shell.annotations()->snapshot();
            auto it=std::find_if(notes.begin(),notes.end(),[page](const auto& n){return n.page==page;});
            QVERIFY(it!=notes.end());
            QVERIFY(QLineF(g.pdfToDisplay.map(it->anchor),QPointF(.2,.3)).length()<1e-6);
            QCOMPARE(it->right,g.right()); QCOMPARE(it->down,g.down());
        }
        QCOMPARE(shell.annotations()->snapshot().size(),8);
        shell.setZoom(.25);
        QCOMPARE(shell.annotations()->rowCount(),shell.model().priority().size());
        shell.setZoom(4);
        QCOMPARE(shell.annotations()->rowCount(),shell.model().priority().size());
        shell.clearSelection(); QTRY_VERIFY(shell.annotations()->snapshot().isEmpty());
        QVERIFY(!shell.annotations()->geometry(0).valid);
    }
    void pasteToBoundaryAndEraseOverEditor() {
        ShellState shell; QQmlApplicationEngine engine;
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}});
        engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(window);
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
        shell.selectFile(QUrl::fromLocalFile(fixture));
        QTRY_VERIFY(shell.storageReady()); QTRY_VERIFY(shell.annotations()->geometry(0).valid);
        shell.setZoom(.6); QTRY_VERIFY(!shell.busy()); QTest::qWait(100);
        auto* viewport=find(window->contentItem(),"pdfViewport"); QVERIFY(viewport);
        const auto id=shell.annotations()->create(0,.2,.2);
        auto* editor=find(window->contentItem(),"annotationEditor"); QVERIFY(editor);
        editor->forceActiveFocus();
        const QString text=QStringLiteral("a long answer with several words ").repeated(8);
        QGuiApplication::clipboard()->setText(text);
        QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,text);
        QVERIFY(!shell.annotations()->snapshot().first().widthLocked);
        QVERIFY(editor->property("lineCount").toInt()>1);
        QTRY_COMPARE(shell.annotations()->snapshot().first().height,editor->property("contentHeight").toDouble());
        QTest::keyClick(window,Qt::Key_Return,Qt::ControlModifier);
        QVERIFY(shell.annotations()->selectedId().isEmpty());
        const auto saved=shell.annotations()->snapshot().first();
        QVERIFY(shell.annotations()->beginInk(0,{.2,.2},Qt::blue,2));
        shell.annotations()->appendInk({.3,.2}); shell.annotations()->endInk();
        shell.annotations()->select(id); editor->forceActiveFocus();
        const auto point=viewport->mapToScene(QPointF(shell.pageLeft(0)+.25*shell.pageWidth()-viewport->property("contentX").toDouble(),
            shell.pageTop(0)+.2*shell.pageHeight(0)-viewport->property("contentY").toDouble())).toPoint();
        QTest::mousePress(window,Qt::RightButton,Qt::NoModifier,point);
        QTest::mouseRelease(window,Qt::RightButton,Qt::NoModifier,point);
        QTRY_VERIFY(shell.annotations()->inkSnapshot().isEmpty());
        QCOMPARE(shell.annotations()->snapshot().first(),saved);
        shell.annotations()->undo(); QCOMPARE(shell.annotations()->inkSnapshot().size(),1);
        window->close(); QTRY_VERIFY(shell.shutdownComplete());
    }
    void viewportKeepsEditing() {
        ShellState shell; shell.updateViewport(0,700,1000,1);
        shell.selectFile(QUrl::fromLocalFile(fixture));
        QTRY_COMPARE(shell.pageCount(),8); QTRY_VERIFY(shell.annotations()->geometry(0).valid);
        QTRY_VERIFY(shell.storageReady());
        const auto id=shell.annotations()->create(0,.2,.3);
        shell.annotations()->setText(id,QStringLiteral("keep editing"),24);
        const auto before=shell.model().priority();
        shell.updateViewport(1000,700,1000,1);
        QVERIFY(before!=shell.model().priority());
        QVERIFY(shell.model().priority().contains(0));
        QCOMPARE(shell.annotations()->selectedId(),id);
        shell.updateViewport(shell.pageTop(6),700,1000,1);
        QVERIFY(shell.annotations()->selectedId().isEmpty());
        QCOMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("keep editing"));
        QCOMPARE(shell.annotations()->rowCount(),0);
    }
    void mouseKeyboardImeAndLayout() {
        ShellState shell; QQmlApplicationEngine engine; QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,&engine,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings.append(e.toString());});
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}});
        engine.loadFromModule("Langlern.Shell","Main"); QVERIFY(!engine.rootObjects().isEmpty());
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
        shell.selectFile(QUrl::fromLocalFile(fixture));
        QTRY_VERIFY(shell.annotations()->geometry(0).valid); QTRY_VERIFY(shell.textSelection()->snapshot(0));
        QTRY_VERIFY(shell.storageReady());
        QTRY_VERIFY(!shell.busy()); QTest::qWait(300);
        auto* viewport=find(window->contentItem(),QStringLiteral("pdfViewport")); QVERIFY(viewport);
        const auto posF=[&](double x,double y) {
            return viewport->mapToScene(QPointF((shell.contentWidth()-shell.pageWidth())/2+x*shell.pageWidth()-viewport->property("contentX").toDouble(),
                shell.pageTop(0)+y*shell.pageHeight(0)-viewport->property("contentY").toDouble()));
        };
        const auto pos=[&](double x,double y) { return posF(x,y).toPoint(); };
        qInfo()<<"annotation_window logical"<<window->size()<<"dpr"<<window->devicePixelRatio();
        const auto jobs=shell.model().renderJobs;
        // The fixture's third text line is near y=.2; use blank paper for auto input.
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,pos(.25,.3));
        QTRY_COMPARE(shell.annotations()->snapshot().size(),1);
        auto* editor=find(window->contentItem(),QStringLiteral("annotationEditor")); QVERIFY(editor);
        QTRY_VERIFY(editor->hasActiveFocus());
        const double initialWidth=shell.annotations()->snapshot().first().width;
        QVERIFY(initialWidth<48);
        for(const char c:QByteArray("answer")) QTest::keyClick(window,c);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("answer"));
        QVERIFY(shell.annotations()->snapshot().first().width>initialWidth);
        QCOMPARE(editor->property("lineCount").toInt(),1);
        // Real Qt input-method event delivery through the focused window object.
        QInputMethodEvent preedit(QStringLiteral("zhong"),{});
        QGuiApplication::sendEvent(window,&preedit);
        QCOMPARE(editor->property("preeditText").toString(),QStringLiteral("zhong"));
        QCOMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("answer"));
        QInputMethodEvent commit; commit.setCommitString(QStringLiteral("中文"));
        QGuiApplication::sendEvent(window,&commit);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("answer中文"));
        QVERIFY(editor->property("preeditText").toString().isEmpty());
        QInputMethodEvent cancelPreedit(QStringLiteral("discard"),{}); QGuiApplication::sendEvent(window,&cancelPreedit);
        QInputMethodEvent cancel; QGuiApplication::sendEvent(window,&cancel);
        QCOMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("answer中文"));
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("answer"));
        QTest::keyClick(window,Qt::Key_Y,Qt::ControlModifier);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("answer中文"));
        const double beforeReturn=shell.annotations()->snapshot().first().width;
        QTest::keyClick(window,Qt::Key_Return);
        QVERIFY(!shell.annotations()->selectedId().isEmpty());
        QVERIFY(!shell.annotations()->snapshot().first().widthLocked);
        QCOMPARE(shell.annotations()->snapshot().first().width,beforeReturn);
        QGuiApplication::clipboard()->setText(QStringLiteral("\ne\u0301 😀\n多行答案"));
        QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
        QTRY_VERIFY(shell.annotations()->snapshot().first().text.contains(QStringLiteral("多行答案")));
        QVERIFY(shell.annotations()->snapshot().first().height>24);
        QCOMPARE(shell.annotations()->snapshot().first().width,beforeReturn);
        QCOMPARE(shell.model().renderJobs,jobs);
        const auto anchor=shell.annotations()->snapshot().first().anchor;
        QTest::keyClick(window,Qt::Key_Escape);
        QVERIFY(shell.annotations()->selectedId().isEmpty());
        auto* note=editor->parentItem()->parentItem();
        const auto normalizedAnchor=shell.annotations()->geometry(0).pdfToDisplay.map(anchor);
        const auto expected=posF(normalizedAnchor.x(),normalizedAnchor.y());
        QVERIFY(QLineF(note->mapToScene(QPointF()),expected).length()*window->devicePixelRatio()<=1);
        shell.setZoom(.75); QTest::qWait(250);
        QCOMPARE(shell.annotations()->snapshot().first().anchor,anchor);
        QVERIFY(QLineF(note->mapToScene(QPointF()),posF(normalizedAnchor.x(),normalizedAnchor.y())).length()*window->devicePixelRatio()<=1);
        window->resize(1000,740); QTest::qWait(250);
        QVERIFY(QLineF(note->mapToScene(QPointF()),posF(normalizedAnchor.x(),normalizedAnchor.y())).length()*window->devicePixelRatio()<=1);
        // Re-enter existing note and move using its actual handle.
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,note->mapToScene(QPointF(8,8)).toPoint());
        QTRY_VERIFY(editor->hasActiveFocus());
        auto* handle=find(window->contentItem(),QStringLiteral("annotationMoveHandle")); QVERIFY(handle);
        const auto start=handle->mapToScene(QPointF(15,8)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QVERIFY2(note->property("moving").toBool(),"Move handle did not receive the press");
        QTest::mouseMove(window,start+QPoint(65,35),40);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(65,35));
        QTRY_VERIFY(shell.annotations()->snapshot().first().anchor!=anchor);
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);
        QTRY_COMPARE(shell.annotations()->snapshot().first().anchor,anchor);
        // Leave virtualized range, reconstruct editor, preserve data.
        shell.goToPage(8); QTest::qWait(200); QCOMPARE(shell.annotations()->rowCount(),0);
        shell.goToPage(1); QTRY_COMPARE(shell.annotations()->rowCount(),1);
        editor=find(window->contentItem(),QStringLiteral("annotationEditor")); QVERIFY(editor);
        QVERIFY(editor->property("text").toString().contains(QStringLiteral("多行答案")));
        const auto output=qEnvironmentVariable("LANGLERN_SCREENSHOT_DIR");
        if(!output.isEmpty()) {
            QTest::qWait(100); QFile f(QDir(output).filePath("text-answers.png"));
            QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::NewOnly)); QVERIFY(window->grabWindow().save(&f,"PNG"));
        }
        // Native text still takes priority in automatic mode, no extra note on glyph click.
        QTest::keyClick(window,Qt::Key_T,Qt::ControlModifier|Qt::ShiftModifier);
        const auto glyph=shell.textSelection()->snapshot(0)->characters.first().box.center();
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,pos(glyph.x(),glyph.y()));
        QTRY_COMPARE(shell.annotations()->snapshot().size(),2);
        QTest::keyClick(window,Qt::Key_Escape); // empty input-mode note is discarded
        QCOMPARE(shell.annotations()->snapshot().size(),1);
        QTest::keyClick(window,Qt::Key_S,Qt::ControlModifier|Qt::ShiftModifier);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,pos(.7,.2));
        QCOMPARE(shell.annotations()->snapshot().size(),1);
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier|Qt::ShiftModifier);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,pos(glyph.x(),glyph.y()));
        QCOMPARE(shell.annotations()->snapshot().size(),1);
        // The approved design uses automatic blank drags for handwriting.
        const auto blank=pos(.85,.25);
        const double beforeScroll=viewport->property("contentY").toDouble();
        const auto beforeInk=shell.annotations()->inkSnapshot().size();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,blank);
        for(int i=1;i<=5;++i) QTest::mouseMove(window,blank-QPoint(0,15*i),20);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,blank-QPoint(0,75));
        QCOMPARE(viewport->property("contentY").toDouble(),beforeScroll);
        QTRY_COMPARE(shell.annotations()->inkSnapshot().size(),beforeInk+1);
        QCOMPARE(shell.annotations()->snapshot().size(),1);
        viewport->metaObject()->invokeMethod(viewport,"cancelFlick");
        shell.goToPage(1);
        // Natural focus changes must not be stolen by the page history focus target.
        const auto originalId=shell.annotations()->snapshot().first().id;
        shell.annotations()->select(originalId);
        auto* pageNumber=window->findChild<QQuickItem*>(QStringLiteral("pageNumber")); QVERIFY(pageNumber);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,pageNumber->mapToScene(QPointF(pageNumber->width()/2,pageNumber->height()/2)).toPoint());
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        QTest::keyClick(window,Qt::Key_1);
        auto* pageInput=pageNumber->property("contentItem").value<QQuickItem*>(); QVERIFY(pageInput);
        QVERIFY(pageInput->hasActiveFocus());
        QCOMPARE(pageInput->property("text").toString(),QStringLiteral("1"));
        QTest::keyClick(window,Qt::Key_Return);
        QTest::qWait(80);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,pos(.7,.12));
        QTRY_COMPARE(shell.annotations()->snapshot().size(),2);
        const auto secondId=shell.annotations()->selectedId(); QVERIFY(secondId!=originalId);
        QTest::keyClick(window,Qt::Key_B);
        editor=find(window->contentItem(),QStringLiteral("annotationEditor"));
        // Find the original editor using its owning delegate ID, not UUID sort order.
        const auto originalAnchor=shell.annotations()->geometry(0).pdfToDisplay.map(anchor);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,pos(originalAnchor.x()+.01,originalAnchor.y()+.01));
        QTRY_COMPARE(shell.annotations()->selectedId(),originalId);
        QTest::keyClick(window,Qt::Key_X);
        auto notesAfter=shell.annotations()->snapshot();
        auto original=std::find_if(notesAfter.begin(),notesAfter.end(),[&](const auto& n){return n.id==originalId;});
        QVERIFY(original!=notesAfter.end()); QVERIFY(original->text.contains('x'));
        shell.annotations()->select(secondId);
        QTest::keyClick(window,Qt::Key_Delete,Qt::ControlModifier);
        QTRY_COMPARE(shell.annotations()->snapshot().size(),1);
        shell.annotations()->select(originalId);
        QTest::keyClick(window,Qt::Key_Delete,Qt::ControlModifier);
        QTRY_VERIFY(shell.annotations()->snapshot().isEmpty());
        shell.annotations()->undo(); QCOMPARE(shell.annotations()->snapshot().size(),1);
        shell.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/scan.pdf"));
        QTRY_VERIFY(shell.annotations()->snapshot().isEmpty()); QVERIFY(!shell.annotations()->canUndo());
        QTRY_VERIFY(shell.annotations()->geometry(0).valid);
        QTRY_VERIFY(shell.textSelection()->snapshot(0));
        QTRY_VERIFY(shell.storageReady());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,pos(.25,.2));
        QTRY_COMPARE(shell.annotations()->snapshot().size(),1);
        QTest::keyClick(window,Qt::Key_A);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("a"));
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close(); QTRY_VERIFY(shell.shutdownComplete());
    }
};
int main(int argc,char** argv) {
    QTemporaryDir workspace; setTestEnvironment("LANGLERN_WORKSPACE_DIR",workspace.path());

    QGuiApplication app(argc,argv); configureTestApplication(app);
    AnnotationTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "annotation_tests.moc"
