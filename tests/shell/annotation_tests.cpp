#include <QTemporaryDir>
#include "shellstate.h"
#include <QClipboard>
#include <QLineF>
#include <algorithm>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQmlApplicationEngine>
#include <QTest>
#include <QDir>
#include <QFile>
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
    void init() { caseWorkspace=std::make_unique<QTemporaryDir>(); qputenv("LANGLERN_WORKSPACE_DIR",caseWorkspace->path().toUtf8()); }

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
    void realPageCoordinates() {
        ShellState shell; shell.updateViewport(0,700,1000,1.5);
        shell.selectFile(QUrl::fromLocalFile(fixture));
        QTRY_COMPARE(shell.pageCount(),8);
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
    void viewportKeepsEditing() {
        ShellState shell; shell.updateViewport(0,700,1000,1);
        shell.selectFile(QUrl::fromLocalFile(fixture));
        QTRY_COMPARE(shell.pageCount(),8); QTRY_VERIFY(shell.annotations()->geometry(0).valid);
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
        QTRY_VERIFY(!shell.busy()); QTest::qWait(300);
        auto* viewport=find(window->contentItem(),QStringLiteral("pdfViewport")); QVERIFY(viewport);
        const auto posF=[&](double x,double y) {
            return viewport->mapToScene(QPointF((shell.contentWidth()-shell.pageWidth())/2+x*shell.pageWidth()-viewport->property("contentX").toDouble(),
                shell.pageTop(0)+y*shell.pageHeight(0)-viewport->property("contentY").toDouble()));
        };
        const auto pos=[&](double x,double y) { return posF(x,y).toPoint(); };
        qInfo()<<"annotation_window logical"<<window->size()<<"dpr"<<window->devicePixelRatio();
        const auto jobs=shell.model().renderJobs;
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,pos(.25,.2));
        QTRY_COMPARE(shell.annotations()->snapshot().size(),1);
        auto* editor=find(window->contentItem(),QStringLiteral("annotationEditor")); QVERIFY(editor);
        QTRY_VERIFY(editor->hasActiveFocus());
        for(const char c:QByteArray("answer")) QTest::keyClick(window,c);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("answer"));
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
        QGuiApplication::clipboard()->setText(QStringLiteral("\ne\u0301 😀\n多行答案"));
        QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
        QTRY_VERIFY(shell.annotations()->snapshot().first().text.contains(QStringLiteral("多行答案")));
        QVERIFY(shell.annotations()->snapshot().first().height>24);
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
        // Blank dragging inside a page scrolls, and does not accidentally create a note.
        const auto blank=pos(.85,.25);
        const double beforeScroll=viewport->property("contentY").toDouble();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,blank);
        for(int i=1;i<=5;++i) QTest::mouseMove(window,blank-QPoint(0,15*i),20);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,blank-QPoint(0,75));
        QTRY_VERIFY(viewport->property("contentY").toDouble()>beforeScroll);
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
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,pos(.25,.2));
        QTRY_COMPARE(shell.annotations()->snapshot().size(),1);
        QTest::keyClick(window,Qt::Key_A);
        QTRY_COMPARE(shell.annotations()->snapshot().first().text,QStringLiteral("a"));
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close(); QTRY_VERIFY(shell.shutdownComplete());
    }
};
int main(int argc,char** argv) {
    QTemporaryDir workspace; qputenv("LANGLERN_WORKSPACE_DIR",workspace.path().toUtf8());

    QGuiApplication app(argc,argv); QQuickStyle::setStyle("Basic");
    AnnotationTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "annotation_tests.moc"
