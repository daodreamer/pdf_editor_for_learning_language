#include "testsupport.h"
#include <QTemporaryDir>
#include "shellstate.h"
#include "annotationtext.h"
#include "answerlines.h"
#include "checkboxes.h"
#include "exporttext.h"
#include "answerfonts.h"
#include "runtimepaths.h"
#include <QRawFont>
#include <QUuid>
#include <QPainter>
#include <QRandomGenerator>
#include <QFontMetrics>
#include <QElapsedTimer>
#include <QClipboard>
#include <QLineF>
#include <algorithm>
#include <map>
#include <functional>
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
#include <QJsonDocument>
#include <QJsonObject>

// A PDFium worker on its own thread, for detector checks that need no window.
struct RenderWorker {
    QThread thread;
    PdfWorker* worker=new PdfWorker;
    QList<PdfFrame> frames;
    QObject delivery;
    quint64 revision=0;
    RenderWorker() {
        worker->moveToThread(&thread);
        QObject::connect(worker,&PdfWorker::stopped,&thread,&QThread::quit,Qt::DirectConnection);
        QObject::connect(&thread,&QThread::finished,worker,&QObject::deleteLater);
        QObject::connect(worker,&PdfWorker::frameReady,&delivery,[this](PdfFrame frame){frames.append(std::move(frame));},Qt::QueuedConnection);
        thread.start();
    }
    // Returns the rendered frame, or a null-image frame after 30 s.
    PdfFrame render(PdfRequest request) {
        request.session=1; request.revision=++revision; worker->submit(request);
        QElapsedTimer timer; timer.start();
        while(timer.elapsed()<30000) {
            for(const auto& frame:frames) if(frame.revision==request.revision && !frame.image.isNull()) return frame;
            QTest::qWait(10);
        }
        return {};
    }
    // The tick boxes that follow a final render requested with detectBoxes.
    PdfFrame renderBoxes(PdfRequest request) {
        request.detectBoxes=true; request.skipPreview=true;
        if(render(request).image.isNull()) return {};
        QElapsedTimer timer; timer.start();
        while(timer.elapsed()<30000) {
            for(const auto& frame:frames) if(frame.revision==revision && frame.boxesOnly) return frame;
            QTest::qWait(10);
        }
        return {};
    }
    bool stop() {
        if(!thread.isRunning()) return true;
        worker->shutdown();
        QElapsedTimer timer; timer.start();
        while(thread.isRunning() && timer.elapsed()<10000) QTest::qWait(10);
        return thread.wait(1000);
    }
    ~RenderWorker() { stop(); }
};

class AnnotationTests : public QObject {
    Q_OBJECT
    const QString fixture=QStringLiteral(LANGLERN_FIXTURES)+"/text.pdf";
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
    void faintFineDottedRules() {
        // Workbook blanks: fine grey dots only ~10-20 levels below white. They never
        // reach the ink mask, so the faint-rule pass must find them end to end.
        QImage image(600,800,QImage::Format_RGB32); image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(QPen(QColor(238,238,238),1));
        for(int x=60;x<=540;x+=2) painter.drawPoint(x,300);
        painter.setPen(QPen(QColor(245,245,245),1));
        for(int x=100;x<=400;x+=2) painter.drawPoint(x,500);
        // A textured banner fill must not produce faint hits.
        QRandomGenerator noise(20261110);
        for(int y=600;y<640;++y) for(int x=50;x<550;++x) { const int v=223+int(noise.bounded(30)); image.setPixelColor(x,y,QColor(v,v,v)); }
        // Text directly above a faint rule still disqualifies it (no writing space).
        painter.setPen(Qt::black); QFont font; font.setPixelSize(14); painter.setFont(font);
        const QString sentence=QStringLiteral("Printed sentence directly above the dots");
        painter.drawText(QPointF(100,697),sentence);
        const int textEnd=100+QFontMetrics(font).horizontalAdvance(sentence);
        painter.setPen(QPen(QColor(238,238,238),1));
        for(int x=104;x<=textEnd-4;x+=2) painter.drawPoint(x,700);
        painter.end();
        const auto areas=AnswerLines::detect(image,{600,800});
        qInfo()<<"faint_rules"<<areas;
        auto at=[&](double x,double y) { for(const auto& r:areas) if(r.contains(QPointF(x/600,y/800))) return r; return QRectF(); };
        const auto first=at(300,292); QVERIFY(!first.isEmpty());
        QVERIFY(first.left()*600<=64); QVERIFY(first.right()*600>=536);
        const auto second=at(250,492); QVERIFY(!second.isEmpty());
        QVERIFY(second.left()*600<=104); QVERIFY(second.right()*600>=396);
        for(const auto& r:areas) {
            QVERIFY2(r.bottom()*800<590 || r.top()*800>650,"textured band produced a field");
            QVERIFY2(r.bottom()*800<680 || r.top()*800>710,"rule under text produced a field");
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
    void realTextbookCheckBoxScan() {
        // Opt-in survey of a whole book: LANGLERN_CHECKBOX_SURVEY=<pdf>. Logs every page's
        // tick boxes and, with LANGLERN_REAL_ARTIFACT_DIR, writes a contact sheet of crops.
        const auto path=qEnvironmentVariable("LANGLERN_CHECKBOX_SURVEY");
        if(path.isEmpty() || !QFileInfo::exists(path)) QSKIP("Opt-in survey: set LANGLERN_CHECKBOX_SURVEY to a local PDF.");
        const auto output=qEnvironmentVariable("LANGLERN_REAL_ARTIFACT_DIR");
        RenderWorker worker; int total=0; QElapsedTimer timer; qint64 detectNs=0; double worstMs=0;
        QList<QImage> crops; QStringList labels;
        PdfRequest first; first.page=0; first.path=path; first.width=200; first.skipPreview=true;
        const int pages=worker.render(first).pages; QVERIFY(pages>0);
        QSet<int> only; for(const auto& v:qEnvironmentVariable("LANGLERN_CHECKBOX_PAGES").split(',',Qt::SkipEmptyParts)) only.insert(v.toInt()-1);
        for(int page=0;page<pages;++page) {
            if(!only.isEmpty() && !only.contains(page)) continue;
            PdfRequest request; request.page=page; request.path=path; request.skipPreview=true;
            request.width=qEnvironmentVariableIntValue("LANGLERN_CHECKBOX_WIDTH"); if(request.width<=0) request.width=1200;
            timer.restart(); const auto frame=worker.render(request);
            const auto found=worker.renderBoxes(request);
            const auto ns=timer.nsecsElapsed(); detectNs+=ns; worstMs=std::max(worstMs,ns/1e6);
            QVERIFY(!frame.image.isNull()); QVERIFY(found.checkBoxesReady);
            const auto boxes=found.checkBoxes;
            total+=int(boxes.size());
            if(boxes.isEmpty()) continue;
            qInfo().noquote()<<"checkboxes page"<<page+1<<boxes.size()<<boxes;
            const double iw=frame.image.width(),ih=frame.image.height();
            if(!output.isEmpty()) {
                auto overlay=frame.image.copy(); QPainter p(&overlay); p.setPen(QPen(Qt::blue,2));
                for(const auto& r:boxes) p.drawRect(QRectF(r.x()*iw-3,r.y()*ih-3,r.width()*iw+6,r.height()*ih+6));
                p.end(); QDir().mkpath(output); overlay.save(output+QStringLiteral("/page-%1.png").arg(page+1));
            }
            for(const auto& r:boxes) {
                const QRectF px(r.x()*iw,r.y()*ih,r.width()*iw,r.height()*ih);
                auto crop=frame.image.copy(QRect(int(px.x()-px.width()*1.5),int(px.y()-px.height()*.8),int(px.width()*8),int(px.height()*2.6)));
                QPainter p(&crop); p.setPen(QPen(Qt::blue,1)); p.drawRect(QRectF(px.width()*1.5-2,px.height()*.8-2,px.width()+4,px.height()+4)); p.end();
                crops.append(crop.scaled(240,78,Qt::KeepAspectRatio,Qt::SmoothTransformation)); labels.append(QString::number(page+1));
            }
        }
        qInfo()<<"checkbox_total"<<total<<"pages"<<pages<<"render_and_detect_ms_avg"<<detectNs/1e6/pages<<"max"<<worstMs;
        if(!output.isEmpty() && !crops.isEmpty()) {
            const int columns=6,rows=int((crops.size()+columns-1)/columns);
            QImage sheet(columns*250,rows*100,QImage::Format_RGB32); sheet.fill(Qt::white);
            QPainter p(&sheet);
            for(qsizetype i=0;i<crops.size();++i) {
                const int x=int(i%columns)*250,y=int(i/columns)*100;
                p.drawImage(x+5,y+18,crops[i]); p.setPen(Qt::red); p.drawText(x+5,y+14,QStringLiteral("p%1").arg(labels[i]));
            }
            p.end(); QDir().mkpath(output); QVERIFY(sheet.save(output+"/contact-sheet.png"));
        }
        QVERIFY(worker.stop());
    }
    void answerFontSet() {
        // New answers: Langlern Sans (Andika) + Langlern Kai (LXGW WenKai), with Noto's
        // vertical metrics so ruled-line layout is unchanged; bold uses real bold faces.
        AnnotationModel model;
        PageGeometry g; g.displayToPdf=QTransform(600,0,0,-800,0,800);
        g.pdfToDisplay=g.displayToPdf.inverted(); g.units={600,800}; g.valid=true;
        model.setGeometry(0,g);
        const auto id=model.create(0,.2,.2); QVERIFY(!id.isEmpty());
        QCOMPARE(model.snapshot().first().family,AnswerFonts::learning);
        for(int size:{9,12,18}) QCOMPARE(QFontMetricsF(AnswerFonts::font(AnswerFonts::learning,size)).height(),QFontMetricsF(AnswerFonts::font(AnswerFonts::legacy,size)).height());
        // The bold file itself must be chosen (a synthesized bold of Regular also reports
        // "Bold"): compare the head table of the face Qt resolves with the bundled file.
        auto boldFace=[](const QString& family,const QString& file) {
            QFont f(family); f.setPixelSize(12); f.setBold(true);
            QFile bundled(runtimeDataDir()+"/fonts/"+file); if(!bundled.open(QIODevice::ReadOnly)) return false;
            return QRawFont::fromFont(f).fontTable("head")==QRawFont(bundled.readAll(),12).fontTable("head");
        };
        QVERIFY(boldFace("Langlern Sans","LanglernSans-Bold.ttf"));
        QVERIFY(boldFace("Langlern Kai","LanglernKai-Bold.ttf"));
        // Export: one embedded font per face used; real bold faces are not stroked, while
        // bold on the legacy single-face font still is. Every glyph keeps its own text
        // (mixed-face lines once attributed a space to the following 中).
        TextAnnotation n; n.id=QUuid::createUuid().toString(QUuid::WithoutBraces); n.anchor={100,700}; n.right={1,0}; n.down={0,-1};
        n.family=AnswerFonts::learning; n.width=400; n.height=40; n.text=QStringLiteral("Bonjour 中文练习 oui");
        n.styles={{0,7,14,QColor("#1e468c"),true,false}};
        auto legacy=n; legacy.id=QUuid::createUuid().toString(QUuid::WithoutBraces); legacy.family=AnswerFonts::legacy; legacy.order=1;
        const auto prepared=prepareExportText({n,legacy});
        QCOMPARE(prepared.notes.size(),2);
        QVERIFY(prepared.fonts.size()>=4); // Sans Bold, Sans Regular, Kai, Noto
        for(const auto& glyph:prepared.notes[0].glyphs) QVERIFY(!glyph.syntheticBold);
        bool stroked=false; for(const auto& glyph:prepared.notes[1].glyphs) stroked=stroked||glyph.syntheticBold;
        QVERIFY(stroked);
        int fontsWithZhong=0;
        for(const auto& font:prepared.fonts) { QVERIFY(!font.cmap.contains("<200b>")); fontsWithZhong+=font.cmap.contains("<4e2d>"); }
        QCOMPARE(fontsWithZhong,2); // Langlern Kai (new answer) and Noto (legacy answer)
    }
    void checkBoxFixtureDetection() {
        // Fixed-scale detection render: the same boxes at any display width.
        RenderWorker worker;
        auto boxes=[&](int page,int width) {
            PdfRequest request; request.page=page; request.path=LANGLERN_FIXTURE("check-boxes.pdf"); request.width=width;
            const auto frame=worker.renderBoxes(request);
            return frame.checkBoxesReady?frame.checkBoxes:QList<QRectF>{QRectF(-1,-1,0,0)};
        };
        // Page 1: true boxes (pt, lower-left corner, 8 pt side); distractors must be ignored.
        const QList<QPointF> expected{{72,740},{180,740},{300,740},{72,700},{180,700},{300,700}};
        for(int width:{600,1200,2000}) {
            const auto found=boxes(0,width);
            qInfo()<<"fixture boxes width"<<width<<found;
            QCOMPARE(found.size(),expected.size());
            for(const auto& corner:expected) {
                const QPointF centre((corner.x()+4)/595,(842-corner.y()-4)/842);
                bool hit=false; for(const auto& r:found) hit=hit||(r.contains(centre) && std::abs(r.width()*595-8)<1.5 && std::abs(r.height()*842-8)<1.5);
                QVERIFY2(hit,qPrintable(QStringLiteral("missing box at %1,%2 (width %3)").arg(corner.x()).arg(corner.y()).arg(width)));
            }
            QCOMPARE(boxes(0,width),found);
            // Rotated page: display coordinates after /Rotate 90 (842 pt wide source, 595 x 842 shown).
            const auto rotated=boxes(1,width);
            QCOMPARE(rotated.size(),3);
            for(const auto& r:rotated) QVERIFY(std::abs(r.width()*595-8)<1.5 && std::abs(r.height()*842-8)<1.5);
        }
        QVERIFY(boxes(0,600)==boxes(0,2000));
        QVERIFY(CheckBoxes::detect({}, {600,800}).isEmpty());
        QVERIFY(CheckBoxes::detect(QImage(10,10,QImage::Format_RGB32),{NAN,800}).isEmpty());
        QVERIFY(worker.stop());
    }
    void checkBoxMarkToggleAndHistory() {
        AnnotationModel model;
        PageGeometry g; g.displayToPdf=QTransform(600,0,0,-800,0,800);
        g.pdfToDisplay=g.displayToPdf.inverted(); g.units={600,800}; g.valid=true;
        model.setGeometry(0,g);
        // Unrelated ink nearby stays untouched.
        QVERIFY(model.beginInk(0,{.3,.3},QColor("#235fbd"),2)); model.appendInk({.4,.4}); model.endInk();
        QCOMPARE(model.inkSnapshot().size(),1);
        const QRectF box(.1,.1,8./600,8./800);
        QVERIFY(model.toggleBoxMark(0,box,true,QColor("#e5484d")));
        QCOMPARE(model.inkSnapshot().size(),3);
        QCOMPARE(model.boxMark(0,box).size(),2);
        for(const auto& s:model.boxMark(0,box)) {
            QCOMPARE(s.color,QColor("#e5484d")); QVERIFY(s.width>=.8 && s.width<=2.5); QVERIFY(s.pressure.isEmpty());
            for(const auto& p:s.points) QVERIFY(box.contains(g.pdfToDisplay.map(p)));
        }
        QVERIFY(!model.toggleBoxMark(0,box,true,QColor("#e5484d"))); // second click clears
        QCOMPARE(model.inkSnapshot().size(),1);
        model.undo(); QCOMPARE(model.boxMark(0,box).size(),2);
        model.undo(); QCOMPARE(model.boxMark(0,box).size(),0); QCOMPARE(model.inkSnapshot().size(),1);
        model.redo(); QCOMPARE(model.boxMark(0,box).size(),2);
        QVERIFY(!model.toggleBoxMark(0,box,false,Qt::black));
        QVERIFY(model.toggleBoxMark(0,box,false,Qt::black)); // tick: one stroke with a sharp turn
        QCOMPARE(model.boxMark(0,box).size(),1); QCOMPARE(model.boxMark(0,box).first().points.size(),5);
        QVERIFY(!model.toggleBoxMark(5,box,false,Qt::black)); // no geometry
        QVERIFY(!model.toggleBoxMark(0,QRectF(),false,Qt::black));
    }
    void checkMarkFollowsLanguage() {
        ShellState shell;
        QCOMPARE(shell.ocrLanguage(),QStringLiteral("fra"));
        QVERIFY(shell.crossMarks(0)); // no page text: the document language decides
        shell.setOcrLanguage("eng"); QVERIFY(!shell.crossMarks(0));
        shell.setOcrLanguage("chi_sim"); QVERIFY(!shell.crossMarks(0));
        shell.setOcrLanguage("fra+eng"); QVERIFY(shell.crossMarks(0));
        auto page=[](const QString& text) {
            auto t=std::make_shared<TextPage>();
            for(auto u:text.toUcs4()) { TextCharacter c; c.unicode=u; t->characters.append(c); }
            return TextSnapshot(t);
        };
        shell.textSelection()->put(0,page(QStringLiteral("Tick the box that matches what you hear and write your answer.")));
        QVERIFY(!shell.crossMarks(0));
        shell.setOcrLanguage("eng");
        shell.textSelection()->put(1,page(QStringLiteral("Cochez la case qui correspond à ce que vous entendez.")));
        QVERIFY(shell.crossMarks(1));
        shell.textSelection()->put(2,page(QStringLiteral("Kreuzen Sie an, was Sie hören und was richtig ist.")));
        QVERIFY(shell.crossMarks(2));
        shell.textSelection()->put(3,page(QStringLiteral("请在正确的选项前面的方框里打勾，然后朗读句子。")));
        QVERIFY(!shell.crossMarks(3));
        shell.setCheckMarkStyle(2); QVERIFY(shell.crossMarks(0)); QVERIFY(shell.crossMarks(3));
        shell.setCheckMarkStyle(1); QVERIFY(!shell.crossMarks(1));
        shell.setCheckMarkStyle(7); QCOMPARE(shell.checkMarkStyle(),2);
    }
    void checkBoxClickAndTypingDefaults() {
        ShellState shell; QQmlApplicationEngine engine; QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors)warnings.append(e.toString());});
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}}); engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(window);
        window->resize(1300,900); QVERIFY(showTestWindow(window));
        shell.selectFile(QUrl::fromLocalFile(LANGLERN_FIXTURE("check-boxes.pdf")));
        QTRY_VERIFY(shell.storageReady()); QTRY_COMPARE(shell.pageCount(),2);
        QTRY_VERIFY_WITH_TIMEOUT(shell.model().hasCheckBoxes(0),10000);
        QCOMPARE(shell.model().checkBoxes(0).size(),6);
        auto* viewport=findItem(window->contentItem(),"pdfViewport"); QVERIFY(viewport);
        QTRY_VERIFY(std::abs(viewport->property("contentY").toDouble()-shell.scrollOffset())<.5);
        auto scene=[&](QPointF p) { return viewport->mapToScene(p-QPointF(viewport->property("contentX").toDouble(),viewport->property("contentY").toDouble())).toPoint(); };
        // Let the window, layout and nearby-page rendering settle before taking positions:
        // a late resize moves the page, and a late frame clears the hover hint and counts
        // as a render.
        auto settled=[&] {
            for(int page:shell.model().priority()) if(!shell.model().ready(page,shell.model().targetWidth(page,window->devicePixelRatio()))) return false;
            return !shell.busy() && shell.model().nextUnknown()<0;
        };
        QTRY_VERIFY_WITH_TIMEOUT(settled(),10000); QTest::qWait(200);
        QTRY_VERIFY(std::abs(viewport->property("contentY").toDouble()-shell.scrollOffset())<.5);
        const auto box=shell.model().checkBoxes(0).first();
        const QPointF centre(shell.pageLeft(0)+box.center().x()*shell.pageWidth(),shell.pageTop(0)+box.center().y()*shell.pageHeight(0));
        QVERIFY(!shell.checkBoxAt(centre.x(),centre.y()).isEmpty());
        QVERIFY(shell.checkBoxAt(centre.x()+60,centre.y()).isEmpty());
        // Automatic mode: hovering shows the box hint, a click marks it (French page: cross).
        QTest::mouseMove(window,scene(centre),30);
        auto* hint=findItem(window->contentItem(),"answerLineHint"); QVERIFY(hint);
        QTRY_VERIFY(hint->isVisible());
        const auto jobs=shell.model().renderJobs;
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,scene(centre));
        QTRY_COMPARE(shell.annotations()->inkSnapshot().size(),2);
        QVERIFY(shell.annotations()->snapshot().isEmpty()); // no text box was created
        QTRY_VERIFY(hint->isVisible());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,scene(centre));
        QTRY_COMPARE(shell.annotations()->inkSnapshot().size(),0);
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);
        QTRY_COMPARE(shell.annotations()->inkSnapshot().size(),2);
        QCOMPARE(shell.model().renderJobs,jobs); // marking never re-renders the PDF
        const auto output=qEnvironmentVariable("LANGLERN_CHECKBOX_ARTIFACT_DIR");
        if(!output.isEmpty()) QVERIFY(window->grabWindow().save(output+"/fixture-marked.png"));
        // Typing tool: its settings panel sets the style of new answers.
        QTest::keyClick(window,Qt::Key_T);
        auto* panel=findItem(window->contentItem(),"textSettingsPanel"); QVERIFY(panel); QTRY_VERIFY(panel->isVisible());
        auto* red=findItem(window->contentItem(),"textColor2"); QVERIFY(red);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,itemCenter(red));
        QTRY_COMPARE(shell.annotations()->defaultTextColor(),QColor("#e5484d"));
        auto* bold=findItem(window->contentItem(),"textDefaultBold"); QVERIFY(bold);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,itemCenter(bold));
        QTRY_VERIFY(shell.annotations()->defaultBold());
        auto* cross=findItem(window->contentItem(),"checkMarkStyle1"); QVERIFY(cross);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,itemCenter(cross));
        QTRY_COMPARE(shell.checkMarkStyle(),1);
        if(!output.isEmpty()) QVERIFY(window->grabWindow().save(output+"/typing-settings.png"));
        // Close the panel with its button, then type an answer on blank paper.
        auto* button=findItem(window->contentItem(),"textSettingsButton"); QVERIFY(button);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,itemCenter(button)); QTRY_VERIFY(!panel->isVisible());
        const QPointF blank(shell.pageLeft(0)+.5*shell.pageWidth(),shell.pageTop(0)+.45*shell.pageHeight(0));
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,scene(blank));
        QTRY_COMPARE(shell.annotations()->snapshot().size(),1);
        auto* editor=findItem(window->contentItem(),"annotationEditor"); QVERIFY(editor); QTRY_VERIFY(editor->hasActiveFocus());
        for(char c:{'o','u','i'}) QTest::keyClick(window,c);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("oui"));
        const auto note=shell.annotations()->snapshot().first();
        QCOMPARE(note.color,QColor("#e5484d"));
        QTRY_VERIFY(!shell.annotations()->snapshot().first().styles.isEmpty());
        const auto styles=shell.annotations()->snapshot().first().styles;
        QCOMPARE(styles.size(),1); QCOMPARE(styles.first().start,0); QCOMPARE(styles.first().length,3); QVERIFY(styles.first().bold);
        QTest::keyClick(window,Qt::Key_Escape);
        // Preferences reach the library file used on the next launch.
        QTRY_VERIFY_WITH_TIMEOUT([&]{
            QFile f(caseWorkspace->path()+"/library-v1.json"); if(!f.open(QIODevice::ReadOnly)) return false;
            const auto v=QJsonDocument::fromJson(f.readAll()).object();
            return v.value("textColor").toString()==QStringLiteral("#e5484d") && v.value("textBold").toBool() && v.value("checkMark").toInt()==1;
        }(),5000);
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
    void realTextbookCheckBoxes() {
        // Fixed acceptance sample (book 3, p.34: "oui / non"), plus the workbook and, when
        // configured, book 2 p.51 (the profile form with 20 boxes) — at two display widths.
        const auto path=realTextbookPath();
        if(path.isEmpty()) QSKIP("The configured real textbook is unavailable on this runner; real-PDF acceptance was not performed.");
        // PDFium is process-global: one worker (one document session) at a time.
        auto check=[&](const QString& pdf,const std::function<void(const std::function<QList<QRectF>(int,int)>&)>& body) {
            RenderWorker worker;
            body([&](int page,int width) { PdfRequest r; r.page=page; r.path=pdf; r.width=width; return worker.renderBoxes(r).checkBoxes; });
            return worker.stop();
        };
        QVERIFY(check(path,[&](const auto& boxes) {
            for(int width:{700,1500}) {
                const auto p34=boxes(33,width);
                qInfo()<<"real p34"<<width<<p34;
                QCOMPARE(p34.size(),2);
                QVERIFY(std::abs(p34[0].center().x()-.704)<.006 && std::abs(p34[0].center().y()-.211)<.006);
                QVERIFY(std::abs(p34[1].center().x()-.765)<.006 && std::abs(p34[1].center().y()-.211)<.006);
                // Chinese running text around 口 (p.20 "口语", p.78 "入口") is not a box.
                QVERIFY(boxes(19,width).isEmpty()); QVERIFY(boxes(77,width).isEmpty());
            }
        }));
        const auto workbook=realWorkbookPath();
        if(!workbook.isEmpty()) QVERIFY(check(workbook,[&](const auto& boxes) { for(int width:{700,1500}) QCOMPARE(boxes(13,width).size(),14); }));
        const auto book2=qEnvironmentVariable("LANGLERN_LARGE_PDF");
        if(QFileInfo(book2).fileName().contains(QStringLiteral("你好法语2学生用书")))
            QVERIFY(check(book2,[&](const auto& boxes) { for(int width:{700,1500}) QCOMPARE(boxes(50,width).size(),20); }));
    }
    void previewKeepsFinalAnswerGeometry() {
        RenderWorker worker;
        PdfRequest request; request.path=LANGLERN_FIXTURE("answer-lines.pdf"); request.skipPreview=true;
        request.width=1200; const auto final=worker.render(request);
        request.width=480; request.previewOnly=true; const auto preview=worker.render(request);
        request.width=1200; request.previewOnly=false; const auto refreshed=worker.render(request);
        QVERIFY(worker.stop());
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
        const auto path=realTextbookPath();
        if(path.isEmpty()) QSKIP("The configured real textbook is unavailable on this runner; real-PDF acceptance was not performed.");
        RenderWorker worker;
        struct Expected { int page; QList<QPointF> spans; };
        // Endpoints read from the 2400 px rendering of each page.
        const QList<Expected> expected{
            {33,{{.621,.714},{.798,.889},{.613,.660},{.756,.889},{.639,.889},{.684,.889},{.695,.746},{.842,.890}}},
            {38,{{.144,.460},{.167,.460},{.144,.188},{.297,.460},{.252,.460},{.403,.460},{.306,.460},{.187,.460},{.209,.460}}}};
        QStringList failures;
        for(const auto& e:expected) for(int width:{600,800,1200}) {
            PdfRequest request; request.page=e.page; request.path=path; request.width=width; request.skipPreview=true;
            const auto frame=worker.render(request);
            QList<QRectF> found;
            for(const auto& r:frame.answerLines)
                if(std::abs(r.height()*frame.geometry.units.height()-AnswerLines::dottedFieldHeight)<.01) found.append(r);
            qInfo()<<"stable_fields page"<<e.page+1<<"width"<<width<<found;
            if(found.size()!=e.spans.size()) failures<<QStringLiteral("page %1 width %2: %3 fields").arg(e.page+1).arg(width).arg(found.size());
            for(const auto& span:e.spans) {
                bool match=false;
                for(const auto& r:found) match=match||(std::abs(r.left()-span.x())<.008 && std::abs(r.right()-span.y())<.008);
                if(!match) failures<<QStringLiteral("page %1 width %2: missing %3-%4").arg(e.page+1).arg(width).arg(span.x()).arg(span.y());
            }
        }
        QVERIFY(worker.stop());
        QVERIFY2(failures.isEmpty(),qPrintable(failures.join(QStringLiteral("; "))));
    }
    void realWorkbookFaintFields() {
        // Workbook pages print blanks as very light, fine dots (~10-25 levels below
        // white). Before the faint-rule pass p.16's eleven inline blanks were all
        // missed and p.14's lines stopped where the print was lightest.
        const auto path=realWorkbookPath();
        if(path.isEmpty()) QSKIP("The real workbook is unavailable on this runner; workbook acceptance was not performed.");
        RenderWorker worker;
        auto fields=[&](int page,int width) {
            PdfRequest request; request.page=page; request.path=path; request.width=width; request.skipPreview=true;
            return worker.render(request).answerLines;
        };
        QStringList failures;
        for(int width:{800,1200}) {
            const auto p14=fields(13,width);
            qInfo()<<"workbook p14 width"<<width<<p14;
            int full=0; for(const auto& r:p14) full+=r.left()<.14 && r.right()>.91;
            if(full<12) failures<<QStringLiteral("p14@%1: %2 of 12 lines span .14-.91").arg(width).arg(full);
            const auto p16=fields(15,width);
            qInfo()<<"workbook p16 width"<<width<<p16;
            int inline_=0; for(const auto& r:p16) inline_+=r.top()>.31 && r.bottom()<.48 && r.width()>.06 && r.width()<.13;
            if(inline_<11) failures<<QStringLiteral("p16@%1: %2 of 11 inline blanks").arg(width).arg(inline_);
        }
        QVERIFY(worker.stop());
        QVERIFY2(failures.isEmpty(),qPrintable(failures.join(QStringLiteral("; "))));
    }
    void realWorkbookRuledBlockAnswer() {
        // Several stacked rules are one answer: it wraps at the rule ends, Enter goes to
        // the next rule, every text line sits on its rule, and it is deleted as a whole.
        const auto path=realWorkbookPath();
        if(path.isEmpty()) QSKIP("The real workbook is unavailable on this runner; ruled-block acceptance was not performed.");
        ShellState shell; QQmlApplicationEngine engine;
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}}); engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(window);
        window->resize(1300,950); QVERIFY(showTestWindow(window));
        shell.selectFile(QUrl::fromLocalFile(path)); QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),30000);
        QTRY_VERIFY_WITH_TIMEOUT(shell.pageCount()>0,30000);
        auto settled=[&] { for(int page:shell.model().priority()) if(!shell.model().ready(page,shell.model().targetWidth(page,window->devicePixelRatio()))) return false; return true; };
        QCOMPARE(shell.pageCount(),66);
        shell.goToPage(14); QTRY_VERIFY_WITH_TIMEOUT(settled(),30000);
        const int page=13;
        const auto g=shell.annotations()->geometry(page); QVERIFY(g.valid);
        auto content=[&](QPointF n) { return QPointF(shell.pageLeft(page)+n.x()*shell.pageWidth(),shell.pageTop(page)+n.y()*shell.pageHeight(page)); };
        // Exercise 2: seven rules from y ~ .40 to .57 (normalized field rects).
        QRectF top; for(const auto& l:shell.model().answerLines(page)) if(l.top()>.38 && l.top()<.42 && l.left()<.2) top=l;
        QVERIFY(!top.isEmpty());
        const auto block=shell.answerBlockAt(page,top.center());
        qInfo()<<"ruled_block"<<block.area<<block.lines<<block.pitch*g.units.height();
        QCOMPARE(block.lines,7);
        const double pitch=block.pitch*g.units.height();
        QVERIFY(pitch>17 && pitch<23);
        const auto hint=shell.answerBlankAt(content(top.center()).x(),content(top.center()).y());
        QVERIFY(hint.height()>6*pitch/g.units.height()*shell.pageHeight(page));
        const auto id=shell.createAnswerAt(content(top.center()).x(),content(top.center()).y()); QVERIFY(!id.isEmpty());
        QCOMPARE(shell.annotations()->snapshot().size(),1);
        auto note=[&]{ return shell.annotations()->snapshot().first(); };
        QVERIFY(std::abs(note().lineSpacing-pitch)<.01);
        QVERIFY(std::abs(note().height-(top.height()*g.units.height()+6*pitch))<.01);
        auto* editor=findItem(window->contentItem(),"annotationEditor"); QVERIFY(editor); QTRY_VERIFY(editor->hasActiveFocus());
        const QString first=QStringLiteral("La série a duré dix ans et le dernier épisode a été regardé par des millions de téléspectateurs ");
        QGuiApplication::clipboard()->setText(first.repeated(2)); QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
        QTest::keyClick(window,Qt::Key_Return);
        QGuiApplication::clipboard()->setText(QStringLiteral("Une nouvelle idée sur la ligne suivante.")); QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
        QTRY_VERIFY(note().text.contains(QStringLiteral("ligne suivante")));
        // Each visual line's baseline lands on its own rule (rule = field bottom - 3 pt).
        QTRY_VERIFY(editor->property("lineCount").toInt()>=3);
        const int lines=editor->property("lineCount").toInt();
        QVERIFY(lines<=7);
        QVERIFY(std::abs(note().height-(top.height()*g.units.height()+6*pitch))<.01); // still the block, not grown
        const double ruleTop=top.bottom()*g.units.height()-3-top.top()*g.units.height(); // rule offset inside the box
        int position=0; QList<double> lineTops;
        for(int i=0;i<lines;++i) {
            QRectF r; QMetaObject::invokeMethod(editor,"positionToRectangle",Q_RETURN_ARG(QRectF,r),Q_ARG(int,position));
            lineTops<<r.top();
            // advance to next visual line
            QPointF below(r.left(),r.top()+pitch*1.0+1); int next=0;
            QMetaObject::invokeMethod(editor,"positionAt",Q_RETURN_ARG(int,next),Q_ARG(qreal,2.0),Q_ARG(qreal,below.y()));
            if(next<=position) break; position=next;
        }
        qInfo()<<"ruled_line_tops"<<lineTops<<"pitch"<<pitch<<"rule offset"<<ruleTop;
        for(int i=1;i<lineTops.size();++i) QVERIFY2(std::abs(lineTops[i]-lineTops[i-1]-pitch)<.6,qPrintable(QStringLiteral("line %1 pitch %2").arg(i).arg(lineTops[i]-lineTops[i-1])));
        QVERIFY(lineTops.size()>=3);
        QTest::keyClick(window,Qt::Key_Return,Qt::ControlModifier);
        {
            // Saved and exported exactly like the editor shows it.
            const auto saved=AnnotationSnapshot{shell.annotations()->snapshot(),{},0};
            AnnotationSnapshot restored; QString error;
            QVERIFY(StorageWorker::decode(StorageWorker::encode(saved,"test"),"test",restored,error)); QCOMPARE(restored.notes,saved.notes);
            QVERIFY(std::abs(restored.notes.first().lineSpacing-pitch)<.01);
            const auto exported=prepareExportText({note()});
            QList<double> baselines;
            for(const auto& glyph:exported.notes.first().glyphs) {
                bool known=false; for(double b:baselines) known=known||std::abs(b-glyph.baseline.y())<.5;
                if(!known) baselines<<glyph.baseline.y();
            }
            std::sort(baselines.begin(),baselines.end());
            qInfo()<<"ruled_export_baselines"<<baselines<<"rule offset"<<ruleTop;
            QVERIFY(baselines.size()>=3);
            for(int i=1;i<baselines.size();++i) QVERIFY2(std::abs(baselines[i]-baselines[i-1]-pitch)<.6,qPrintable(QString::number(baselines[i]-baselines[i-1])));
            QVERIFY2(std::abs(baselines.first()-ruleTop)<2.5,"first exported line is not on the first rule");
        }
        // Other rules of the block offer no separate hint while the answer exists.
        const auto third=shell.model().answerLines(page);
        for(const auto& l:third) if(l.top()>top.top()+1.5*block.pitch && l.top()<block.area.bottom())
            QVERIFY(shell.answerBlankAt(content(l.center()).x(),content(l.center()).y()).isEmpty());
        const auto output=qEnvironmentVariable("LANGLERN_REAL_ARTIFACT_DIR");
        if(!output.isEmpty()) { QTest::qWait(150); QVERIFY(window->grabWindow().save(output+"/ruled-block.png")); }
        // Deleting removes the whole block answer at once; the hint returns.
        shell.annotations()->select(id); QTest::keyClick(window,Qt::Key_Delete,Qt::ControlModifier);
        QTRY_VERIFY(shell.annotations()->snapshot().isEmpty());
        QTRY_VERIFY(!shell.answerBlankAt(content(top.center()).x(),content(top.center()).y()).isEmpty());
        window->close(); QTRY_VERIFY(shell.shutdownComplete());
    }
    void realTextbookAnswerFields() {
        // Page 34's dotted fields and four long rules (pages 39, 47, 67, 88; endpoints checked
        // against the source pages, not detector output), answered through the hover hint,
        // then saved and restored. One open of the real textbook covers all of them.
        const auto path=realTextbookPath();
        if(path.isEmpty()) QSKIP("The configured real textbook is unavailable on this runner; real-PDF acceptance was not performed.");
        ShellState shell; QQmlApplicationEngine engine;
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}}); engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(window);
        QVERIFY(showTestWindow(window));
        shell.selectFile(QUrl::fromLocalFile(path)); QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),30000);
        QTRY_COMPARE(shell.pageCount(),153); shell.setZoom(.65);
        auto settled=[&] { for(int page:shell.model().priority()) if(!shell.model().ready(page,shell.model().targetWidth(page,window->devicePixelRatio()))) return false; return true; };
        auto showPage=[&](int page) {
            shell.goToPage(page);
            return QTest::qWaitFor([&]{ return !shell.busy() && shell.model().ready(page-1,shell.model().targetWidth(page-1,window->devicePixelRatio()))
                && settled() && shell.model().nextUnknown()<0; },30000);
        };
        const auto output=qEnvironmentVariable("LANGLERN_REAL_ARTIFACT_DIR");
        auto saveOverlay=[&](int page) {
            if(output.isEmpty()) return true;
            const auto folder=output+QStringLiteral("/page-%1").arg(page);
            auto overlay=shell.model().image(page-1).copy();
            if(!QDir().mkpath(folder) || !overlay.save(folder+"/source.png")) return false;
            QPainter p(&overlay); p.setPen(QPen(Qt::red,1));
            for(const auto& r:shell.model().answerLines(page-1)) p.drawRect(QRectF(r.x()*overlay.width(),r.y()*overlay.height(),r.width()*overlay.width(),r.height()*overlay.height()));
            p.end(); return overlay.save(folder+"/candidates.png");
        };
        QVERIFY(showPage(34));
        qInfo()<<"real_fields"<<shell.model().answerLines(33);
        QCOMPARE(shell.model().answerLines(33).size(),8);
        QVERIFY(saveOverlay(34));
        const QList<QPointF> targets{{.67,.14},{.843,.14},{.637,.157},{.824,.157},{.768,.175},{.777,.193},{.721,.229},{.871,.229}};
        QList<QRectF> areas;
        for(const auto& p:targets) {
            const auto area=shell.answerBlankAt(shell.pageLeft(33)+p.x()*shell.pageWidth(),shell.pageTop(33)+p.y()*shell.pageHeight(33));
            QVERIFY2(!area.isEmpty(),qPrintable(QStringLiteral("Missing real dotted field at %1,%2").arg(p.x()).arg(p.y())));
            areas.append(area);
        }
        auto* viewport=findItem(window->contentItem(),"pdfViewport"); QVERIFY(viewport);
        QTRY_VERIFY(std::abs(viewport->property("contentY").toDouble()-shell.scrollOffset())<.5);
        auto scene=[&](QPointF p) { return viewport->mapToScene(p-QPointF(viewport->property("contentX").toDouble(),viewport->property("contentY").toDouble())).toPoint(); };
        auto* hint=findItem(window->contentItem(),"answerLineHint"); QVERIFY(hint);
        auto clickHint=[&] { QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,itemCenter(hint)); };
        auto noteById=[&](const QString& id) { for(const auto& n:shell.annotations()->snapshot()) if(n.id==id) return n; return TextAnnotation(); };
        QTest::mouseMove(window,scene(areas.first().center()),30);
        QTRY_VERIFY(hint->isVisible());
        if(!output.isEmpty()) QVERIFY(window->grabWindow().save(output+"/real-hover.png"));
        clickHint();
        QTRY_COMPARE(shell.annotations()->snapshot().size(),1);
        auto* editor=findItem(window->contentItem(),"annotationEditor"); QVERIFY(editor); QTRY_VERIFY(editor->hasActiveFocus());
        QGuiApplication::clipboard()->setText(QStringLiteral("Marie Dupont avec une longue réponse")); QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
        QTRY_VERIFY(shell.annotations()->snapshot().first().text.startsWith("Marie"));
        const auto expanded=shell.annotations()->snapshot().first();
        editor->setProperty("cursorPosition",5); QTest::keySequence(window,QKeySequence::SelectEndOfDocument); QTest::keyClick(window,Qt::Key_Backspace);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("Marie"));
        QTRY_VERIFY(shell.annotations()->snapshot().first().height<expanded.height);
        QTest::keyClick(window,Qt::Key_Return,Qt::ControlModifier);
        // High-DPI focus/viewport updates can temporarily replace the cached frame.
        QTRY_VERIFY_WITH_TIMEOUT(!shell.answerBlankAt(areas[2].center().x(),areas[2].center().y()).isEmpty(),30000);
        // The long address rule must remain one editor across its full extent.
        const auto address=areas[4];
        const auto units=shell.annotations()->geometry(33).units;
        QVERIFY(address.width()/shell.pageWidth()>.24);
        // Independently checked on the rendered source: the dots span .639 to .889.
        QVERIFY(std::abs((address.left()-shell.pageLeft(33))/shell.pageWidth()-.639)<.004);
        QVERIFY(std::abs((address.right()-shell.pageLeft(33))/shell.pageWidth()-.889)<.004);
        QTest::mouseMove(window,scene(address.center()),30);
        QTRY_VERIFY(hint->isVisible());
        clickHint();
        QTRY_COMPARE(shell.annotations()->snapshot().size(),2);
        const auto addressId=shell.annotations()->selectedId(); QVERIFY(!addressId.isEmpty());
        const QString longAnswer=QStringLiteral("12 rue de la Paix, 75002 Paris");
        QGuiApplication::clipboard()->setText(longAnswer); QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
        QTRY_COMPARE(noteById(addressId).text,longAnswer);
        QVERIFY(noteById(addressId).maximumWidth>.24*units.width());
        // One line. Fuzzy: layout yields 14 plus rounding noise, which an exact == poll never matches.
        QTRY_VERIFY(qFuzzyCompare(noteById(addressId).height,14.0));
        QTest::keyClick(window,Qt::Key_Return,Qt::ControlModifier);
        if(!output.isEmpty()) QVERIFY(window->grabWindow().save(output+"/real-answer.png"));
        // Long rules are found end to end, at the expected source endpoints.
        struct Rule { int page; double left,y,right; };
        const QList<Rule> rules{{39,.144,.290,.460},{47,.121,.723,.874},{67,.123,.824,.875},{88,.124,.580,.875}};
        for(const auto& rule:rules) {
            QVERIFY2(showPage(rule.page),qPrintable(QStringLiteral("page %1 did not settle").arg(rule.page)));
            const int index=rule.page-1;
            qInfo()<<"sample_page"<<rule.page<<"fields"<<shell.model().answerLines(index);
            QVERIFY(saveOverlay(rule.page));
            const auto g=shell.annotations()->geometry(index);
            const QPointF target((rule.left+rule.right)/2,rule.y-4/g.units.height());
            const QPointF content(shell.pageLeft(index)+target.x()*shell.pageWidth(),shell.pageTop(index)+target.y()*shell.pageHeight(index));
            const auto area=shell.answerBlankAt(content.x(),content.y());
            QVERIFY2(!area.isEmpty(),qPrintable(QStringLiteral("Missing long rule on page %1").arg(rule.page)));
            const double left=(area.left()-shell.pageLeft(index))/shell.pageWidth();
            const double right=(area.right()-shell.pageLeft(index))/shell.pageWidth();
            QVERIFY2(std::abs(left-rule.left)<.006,qPrintable(QStringLiteral("Page %1 left endpoint is incomplete: %2").arg(rule.page).arg(left)));
            QVERIFY2(std::abs(right-rule.right)<.006,qPrintable(QStringLiteral("Page %1 right endpoint is incomplete: %2").arg(rule.page).arg(right)));
            if(rule.page!=39) continue;
            // Answer the name rule. Wait for this specific field after asynchronous
            // viewport/frame changes, rather than clicking a still-visible neighbouring hint.
            viewport->setProperty("contentY",std::max(0.0,area.center().y()-viewport->height()/2));
            QTRY_VERIFY(std::abs(viewport->property("contentY").toDouble()-shell.scrollOffset())<.5);
            auto hoverTarget=[&] {
                const auto current=shell.answerBlankAt(content.x(),content.y());
                if(current.isEmpty()) return false;
                const auto point=scene(current.center());
                QTest::mouseMove(window,point,1);
                return hint->isVisible() && QLineF(QPointF(itemCenter(hint)),QPointF(point)).length()<2 && std::abs(hint->width()-current.width())<1;
            };
            QTRY_VERIFY_WITH_TIMEOUT(hoverTarget(),30000);
            const auto before=shell.annotations()->snapshot().size();
            clickHint();
            QTRY_COMPARE(shell.annotations()->snapshot().size(),before+1);
            const auto id=shell.annotations()->selectedId(); QVERIFY(!id.isEmpty());
            QVERIFY(std::abs(noteById(id).maximumWidth-(rule.right-rule.left)*g.units.width())<.006*g.units.width());
            editor=findItem(window->contentItem(),"annotationEditor"); QVERIFY(editor); QTRY_VERIFY(editor->hasActiveFocus());
            const QString answer=QStringLiteral("Marie Dupont et Jean Martin à Paris");
            QGuiApplication::clipboard()->setText(answer); QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
            QTRY_COMPARE(noteById(id).text,answer);
            QTRY_VERIFY(qFuzzyCompare(noteById(id).height,14.0));
            QTest::keyClick(window,Qt::Key_Return,Qt::ControlModifier);
            if(!output.isEmpty()) QVERIFY(window->grabWindow().save(output+"/page-39/answer.png"));
        }
        const auto notes=shell.annotations()->snapshot(); QCOMPARE(notes.size(),3);
        shell.saveNow(); QTRY_VERIFY(shell.storageStatus().contains(QStringLiteral("已保存")));
        shell.clearSelection(); QTRY_COMPARE(shell.pageCount(),0); shell.selectFile(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),30000); QTRY_COMPARE(shell.annotations()->snapshot().size(),3);
        QCOMPARE(shell.annotations()->snapshot(),notes);
        window->close(); QTRY_VERIFY(shell.shutdownComplete());
    }
    void hoverRuleReusesEditorAndShrinksAfterDeletion() {
        ShellState shell; QQmlApplicationEngine engine; QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors)warnings.append(e.toString());});
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}}); engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(window);
        QVERIFY(showTestWindow(window));
        shell.selectFile(QUrl::fromLocalFile(QStringLiteral(LANGLERN_FIXTURES)+"/answer-lines.pdf"));
        QTRY_VERIFY(shell.storageReady()); QTRY_COMPARE(shell.pageCount(),4); QTRY_VERIFY(!shell.busy());
        shell.setZoom(.65); QTRY_VERIFY(!shell.busy());
        auto* viewport=findItem(window->contentItem(),"pdfViewport"); QVERIFY(viewport);
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
        auto* hint=findItem(window->contentItem(),"answerLineHint"); QVERIFY(hint); QTRY_VERIFY(hint->isVisible());
        const auto output=qEnvironmentVariable("LANGLERN_ANSWER_ARTIFACT_DIR");
        if(!output.isEmpty()) QVERIFY(window->grabWindow().save(output+"/hover-rule.png"));
        const auto jobs=shell.model().renderJobs;
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,scene(target.center()));
        QTRY_COMPARE(shell.annotations()->snapshot().size(),1);
        auto* editor=findItem(window->contentItem(),"annotationEditor"); QVERIFY(editor); QTRY_VERIFY(editor->hasActiveFocus());
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
        QTest::keySequence(window,QKeySequence::SelectEndOfDocument); QTest::keyClick(window,Qt::Key_Backspace);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("short"));
        QTRY_VERIFY(shell.annotations()->snapshot().first().width<expanded.width);
        QTRY_COMPARE(shell.annotations()->snapshot().first().height,shell.annotations()->defaultTextHeight());
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,text);
        QTest::keySequence(window,QKeySequence::Redo);
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
        {
            // Holding the right button erases typed answers too, with any tool, and the
            // line offers its answer hint again afterwards.
            auto* box=editor->parentItem()->parentItem(); QVERIFY(box);
            const auto start=box->mapToScene(QPointF(box->width()/2,box->height()/2)).toPoint();
            const auto committed=shell.annotations()->snapshot();
            QTest::mousePress(window,Qt::RightButton,Qt::NoModifier,start-QPoint(30,0));
            QTest::mouseMove(window,start,20); QTest::mouseMove(window,start+QPoint(20,0),20);
            QTest::mouseRelease(window,Qt::RightButton,Qt::NoModifier,start+QPoint(20,0));
            QTRY_VERIFY(shell.annotations()->snapshot().isEmpty());
            QVERIFY(!shell.answerBlankAt(target.center().x(),target.center().y()).isEmpty());
            QTest::mouseMove(window,scene(target.center())+QPoint(3,0),20); QTRY_VERIFY(hint->isVisible());
            QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);
            QTRY_COMPARE(shell.annotations()->snapshot(),committed);
            QTest::keySequence(window,QKeySequence::Redo);
            QTRY_VERIFY(shell.annotations()->snapshot().isEmpty());
            QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);
            QTRY_COMPARE(shell.annotations()->snapshot(),committed);
            editor=findItem(window->contentItem(),"annotationEditor"); QVERIFY(editor);
            QTest::mouseMove(window,scene(target.center()),20); QTRY_VERIFY(!hint->isVisible());
        }
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
        QCOMPARE(model.snapshot().first().height,model.defaultTextHeight()); QVERIFY(model.snapshot().first().width<200); model.finishEditing();
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
        QVERIFY(showTestWindow(window));
        shell.selectFile(QUrl::fromLocalFile(fixture));
        QTRY_VERIFY(shell.storageReady()); QTRY_VERIFY(shell.annotations()->geometry(0).valid);
        shell.setZoom(.6); QTRY_VERIFY(!shell.busy()); QTest::qWait(100);
        const auto id=shell.annotations()->create(0,.3,.2);
        auto* editor=findItem(window->contentItem(),"annotationEditor"); QVERIFY(editor); editor->forceActiveFocus();
        for(const char c:QByteArray("resize this answer")) QTest::keyClick(window,c);
        QTest::keyClick(window,Qt::Key_Escape);
        auto* note=editor->parentItem()->parentItem(); auto* frame=findItem(note,"annotationFrame"); QVERIFY(frame);
        QTest::mouseMove(window,QPoint(2,2)); QTRY_COMPARE(frame->opacity(),0.0);
        QTest::mouseMove(window,note->mapToScene(QPointF(note->width()/2,8)).toPoint());
        QTRY_COMPARE(frame->opacity(),1.0); QVERIFY(shell.annotations()->selectedId().isEmpty());
        const auto jobs=shell.model().renderJobs;
        for(int i=0;i<4;++i) {
            auto* handle=findItem(note,"annotationResizeHandle"+QString::number(i)); QVERIFY(handle);
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
        auto* handle=findItem(note,"annotationResizeHandle3"); QVERIFY(handle);
        const auto before=shell.annotations()->snapshot().first(); const auto p=handle->mapToScene(QPointF(5,5)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,p);
        QTest::mouseMove(window,p+QPoint(30,20),25);
        QTest::keyClick(window,Qt::Key_Escape);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,p+QPoint(30,20));
        QCOMPARE(shell.annotations()->snapshot().first(),before);
        QCOMPARE(shell.model().renderJobs,jobs);
        // No visible move tab: dragging an answer that is not being edited moves it.
        QVERIFY(!findItem(note,"annotationMoveHandle"));
        auto* moveArea=findItem(note,"annotationMoveArea"); QVERIFY(moveArea);
        QVERIFY(shell.annotations()->selectedId().isEmpty());
        const auto moveStart=moveArea->mapToScene(QPointF(20,8)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,moveStart);
        QTest::mouseMove(window,moveStart+QPoint(25,20),25);
        QVERIFY(note->property("moving").toBool());
        shell.setZoom(.7); // A layout change cancels the drag without committing.
        QTest::mouseMove(window,moveStart+QPoint(35,25),25);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,moveStart+QPoint(35,25));
        QCOMPARE(shell.annotations()->snapshot().first(),before);
        QVERIFY(!editor->hasActiveFocus());
        QTest::qWait(250);
        const auto dragStart=moveArea->mapToScene(QPointF(note->width()/2,note->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,dragStart);
        QTest::mouseMove(window,dragStart+QPoint(12,6),20);
        QTest::mouseMove(window,dragStart+QPoint(40,30),20);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,dragStart+QPoint(40,30));
        QTRY_VERIFY(shell.annotations()->snapshot().first().anchor!=before.anchor);
        QCOMPARE(shell.annotations()->snapshot().first().text,before.text);
        QVERIFY(!editor->hasActiveFocus());
        const auto moved=shell.annotations()->snapshot().first();
        shell.annotations()->undo(); QCOMPARE(shell.annotations()->snapshot().first(),before);
        shell.annotations()->redo(); QCOMPARE(shell.annotations()->snapshot().first(),moved);
        // A plain click edits instead, with the caret where the user clicked.
        QTest::qWait(250);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,moveArea->mapToScene(QPointF(note->width()-3,note->height()/2)).toPoint());
        QTRY_VERIFY(editor->hasActiveFocus());
        QCOMPARE(shell.annotations()->snapshot().first().anchor,moved.anchor);
        QVERIFY(editor->property("cursorPosition").toInt()>0); // at the click, not reset to the start
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close(); QTRY_VERIFY(shell.shutdownComplete());
    }
    void selectedTextFormatting() {
        ShellState shell; QQmlApplicationEngine engine; QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,&engine,[&](const QList<QQmlError>& errors){for(const auto& e:errors)warnings.append(e.toString());});
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}}); engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(window);
        QVERIFY(showTestWindow(window));
        shell.selectFile(QUrl::fromLocalFile(fixture)); QTRY_VERIFY(shell.storageReady()); QTRY_VERIFY(shell.annotations()->geometry(0).valid);
        shell.setZoom(.8); QTRY_VERIFY(!shell.busy()); QTest::qWait(100);
        const auto id=shell.annotations()->create(0,.3,.2);
        auto* editor=findItem(window->contentItem(),"annotationEditor"); QVERIFY(editor); editor->forceActiveFocus();
        for(const char c:QByteArray("hello answer")) QTest::keyClick(window,c);
        QTest::keyClick(window,Qt::Key_Home);
        for(int i=0;i<5;++i) QTest::keyClick(window,Qt::Key_Right,Qt::ShiftModifier);
        auto* popup=editor->parentItem()->parentItem()->property("formatPopover").value<QObject*>(); QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        auto click=[&](const QString& name) {
            auto* button=findItem(window->contentItem(),name); QVERIFY(button);
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());
        };
        click("formatBold");
        QTRY_VERIFY(!shell.annotations()->snapshot().first().styles.isEmpty());
        auto style=shell.annotations()->snapshot().first().styles.first();
        QCOMPARE(style.start,0); QCOMPARE(style.length,5); QVERIFY(style.bold);
        QVERIFY(editor->hasActiveFocus()); QCOMPARE(editor->property("selectedText").toString(),QStringLiteral("hello"));
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier); QTRY_VERIFY(shell.annotations()->snapshot().first().styles.isEmpty());
        QTest::keySequence(window,QKeySequence::Redo); QTRY_VERIFY(shell.annotations()->snapshot().first().styles.first().bold);
        QVERIFY(QMetaObject::invokeMethod(editor,"select",Q_ARG(int,0),Q_ARG(int,5)));
        QTRY_VERIFY(popup->property("visible").toBool());
        click("formatItalic"); click("formatColor2"); click("formatLarger");
        QTest::qWait(20);
        style=shell.annotations()->snapshot().first().styles.first();
        QVERIFY(style.bold && style.italic); QCOMPARE(style.size,13.0); QCOMPARE(style.color,QColor("#e5484d"));
        QTRY_COMPARE(popup->property("current").toMap()["size"].toDouble(),13.0);
        // The chosen size becomes the size of the next new answer.
        QCOMPARE(shell.annotations()->defaultFontSize(),13.0);
        QVERIFY(popup->property("current").toMap()["italic"].toBool());
        QCOMPARE(style.start,0); QCOMPARE(style.length,5);
        QCOMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("hello answer"));
        auto* controller=editor->parentItem()->parentItem()->findChild<AnnotationText*>(); QVERIFY(controller);
        const auto rest=controller->selectionStyle(6,12); QVERIFY(!rest["bold"].toBool()); QCOMPARE(rest["size"].toDouble(),12.0);
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
        editor=findItem(window->contentItem(),"annotationEditor"); QVERIFY(editor); QTest::qWait(20);
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
        QVERIFY(showTestWindow(window));
        shell.selectFile(QUrl::fromLocalFile(fixture));
        QTRY_VERIFY(shell.storageReady()); QTRY_VERIFY(shell.annotations()->geometry(0).valid);
        shell.setZoom(.6); QTRY_VERIFY(!shell.busy()); QTest::qWait(100);
        auto* viewport=findItem(window->contentItem(),"pdfViewport"); QVERIFY(viewport);
        const auto id=shell.annotations()->create(0,.2,.2);
        auto* editor=findItem(window->contentItem(),"annotationEditor"); QVERIFY(editor);
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
        // The right button erases typed answers as well as strokes, even while the
        // answer is being edited; each removal is its own undo step.
        QTRY_VERIFY(shell.annotations()->inkSnapshot().isEmpty());
        QTRY_VERIFY(shell.annotations()->snapshot().isEmpty());
        shell.annotations()->undo(); QCOMPARE(shell.annotations()->snapshot().size(),1); QCOMPARE(shell.annotations()->snapshot().first(),saved);
        QVERIFY(shell.annotations()->inkSnapshot().isEmpty());
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
        QVERIFY(showTestWindow(window));
        shell.selectFile(QUrl::fromLocalFile(fixture));
        QTRY_VERIFY(shell.annotations()->geometry(0).valid); QTRY_VERIFY(shell.textSelection()->snapshot(0));
        QTRY_VERIFY(shell.storageReady());
        QTRY_VERIFY(!shell.busy()); QTest::qWait(300);
        auto* viewport=findItem(window->contentItem(),QStringLiteral("pdfViewport")); QVERIFY(viewport);
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
        auto* editor=findItem(window->contentItem(),QStringLiteral("annotationEditor")); QVERIFY(editor);
        QTRY_VERIFY(editor->hasActiveFocus());
        {
            // The click marks the caret: left edge of the box, centred on its single line.
            const auto created=shell.annotations()->snapshot().first(); const auto g=shell.annotations()->geometry(0);
            const auto origin=g.pdfToDisplay.map(created.anchor);
            QCOMPARE(created.fontSize,12.0); QCOMPARE(created.height,shell.annotations()->defaultTextHeight());
            const double pixel=1.5/shell.pageHeight(0);
            QVERIFY2(std::abs(origin.x()-.25)<pixel,qPrintable(QString::number(origin.x())));
            QVERIFY2(std::abs(origin.y()+created.height/2/g.units.height()-.3)<pixel,qPrintable(QString::number(origin.y())));
        }
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
        QTest::keySequence(window,QKeySequence::Redo);
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
        // Re-enter existing note; while editing, its edge moves it.
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,note->mapToScene(QPointF(8,8)).toPoint());
        QTRY_VERIFY(editor->hasActiveFocus());
        auto* edge=findItem(note,QStringLiteral("annotationMoveEdge0")); QVERIFY(edge); QVERIFY(edge->isEnabled());
        const auto start=edge->mapToScene(QPointF(note->width()/2,edge->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QVERIFY2(note->property("moving").toBool(),"Move edge did not receive the press");
        QTest::mouseMove(window,start+QPoint(65,35),40);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(65,35));
        QTRY_VERIFY(shell.annotations()->snapshot().first().anchor!=anchor);
        QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);
        QTRY_COMPARE(shell.annotations()->snapshot().first().anchor,anchor);
        // Leave virtualized range, reconstruct editor, preserve data.
        shell.goToPage(8); QTest::qWait(200); QCOMPARE(shell.annotations()->rowCount(),0);
        shell.goToPage(1); QTRY_COMPARE(shell.annotations()->rowCount(),1);
        editor=findItem(window->contentItem(),QStringLiteral("annotationEditor")); QVERIFY(editor);
        QVERIFY(editor->property("text").toString().contains(QStringLiteral("多行答案")));
        const auto output=qEnvironmentVariable("LANGLERN_SCREENSHOT_DIR");
        if(!output.isEmpty()) {
            QTest::qWait(100); QFile f(QDir(output).filePath("text-answers.png"));
            QVERIFY(f.open(QIODevice::WriteOnly|QIODevice::NewOnly)); QVERIFY(window->grabWindow().save(&f,"PNG"));
        }
        // Native text still takes priority in automatic mode, no extra note on glyph click.
        QTest::keyClick(window,Qt::Key_T,Qt::ControlModifier|Qt::ShiftModifier);
        // Leaving the range released page 0's text; it is requested again on return.
        QTRY_VERIFY(shell.textSelection()->snapshot(0) && !shell.textSelection()->snapshot(0)->characters.isEmpty());
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
        editor=findItem(window->contentItem(),QStringLiteral("annotationEditor"));
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
LANGLERN_TEST_SUITE(AnnotationTests,"annotation")
#include "annotation_tests.moc"
