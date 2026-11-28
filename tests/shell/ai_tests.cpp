#include "testsupport.h"
#include "aihandoff.h"
#include "aiprompt.h"
#include "workspacestate.h"
#include "shellstate.h"
#include "annotationmodel.h"
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickView>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

// The system around the hand-off: installed apps, the app in front, the permission, and a log of
// what reached the clipboard, which links opened and when Command/Ctrl+V was pressed.
struct FakeDesktop {
    QMap<QString,AiPlatform::App> apps;
    QString front=QStringLiteral("linguapdf");
    QString defaultBrowser=QStringLiteral("browser");
    bool allowed=true;
    QList<bool> permissionPrompts;
    QList<QUrl> opened;
    QStringList log; // "image", "text:<…>", "paste"
    // The app the opened link brings to the front, after `delay` polls (-1: never).
    QString bringsToFront;
    int delay=2, polls=0;
    AiHandoff::Backend backend() {
        AiHandoff::Backend b;
        b.desktopApp=[this](const QString& target) { return apps.value(target); };
        b.frontmost=[this] { if(!bringsToFront.isEmpty() && delay>=0 && ++polls>delay) front=bringsToFront; return front; };
        b.self=[] { return QStringLiteral("linguapdf"); };
        b.browser=[this] { return defaultBrowser; };
        b.pasteAllowed=[this](bool prompt) { if(prompt) permissionPrompts.append(prompt); return allowed; };
        b.paste=[this] { log.append(QStringLiteral("paste")); return true; };
        b.open=[this](const QUrl& url) { opened.append(url); return true; };
        b.setImage=[this](const QImage&) { log.append(QStringLiteral("image")); };
        b.setText=[this](const QString& text) { log.append(QStringLiteral("text:")+text); };
        b.permissionUrl=QStringLiteral("x-test:permission");
        return b;
    }
};

class AiTests : public QObject {
    Q_OBJECT
    static AiHandoff::Timing fast() { return {5,400,20,40,20}; }
    static QImage picture() { QImage image(64,40,QImage::Format_RGB32); image.fill(Qt::white); return image; }
private slots:
    void linksOnlyFillTheMessageBox() {
        const auto prompt=QStringLiteral("第 3 页：a+b & c#1\n«Où est-il ?»");
        const auto desktop=AiHandoff::chatUrl(QStringLiteral("claude"),true,prompt);
        QCOMPARE(desktop.scheme(),QStringLiteral("claude"));
        QVERIFY(desktop.toEncoded().startsWith("claude://claude.ai/new?q="));
        // '+', '&', '#' and the line break stay part of the prompt for URLSearchParams.
        const auto encoded=desktop.toEncoded();
        QVERIFY(encoded.contains("a%2Bb%20%26%20c%231%0A"));QVERIFY(!encoded.contains('#'));
        QCOMPARE(QUrlQuery(desktop).queryItemValue(QStringLiteral("q"),QUrl::FullyDecoded),prompt);
        QVERIFY(AiHandoff::chatUrl(QStringLiteral("claude"),false,prompt).toEncoded().startsWith("https://claude.ai/new?q="));
        const auto chatgpt=AiHandoff::chatUrl(QStringLiteral("chatgpt"),false,prompt);
        QVERIFY(chatgpt.toEncoded().startsWith("https://chatgpt.com/?prompt="));
        QCOMPARE(QUrlQuery(chatgpt).queryItemValue(QStringLiteral("prompt"),QUrl::FullyDecoded),prompt);
        // The ChatGPT app has no prompt link: the prompt is pasted.
        QVERIFY(AiHandoff::chatUrl(QStringLiteral("chatgpt"),true,prompt).isEmpty());
    }

    void promptAsksForEverythingInTheChosenLanguage() {
        // Interface language by default; the system or a picked language when chosen and available.
        QCOMPARE(AiPrompt::resolve(QStringLiteral("app"),false,{QStringLiteral("fr-FR")}),QStringLiteral("zh"));
        QCOMPARE(AiPrompt::resolve(QStringLiteral("app"),true,{QStringLiteral("fr-FR")}),QStringLiteral("en"));
        QCOMPARE(AiPrompt::resolve(QStringLiteral("system"),false,{QStringLiteral("fr-FR"),QStringLiteral("en-US")}),QStringLiteral("fr"));
        QCOMPARE(AiPrompt::resolve(QStringLiteral("system"),true,{QStringLiteral("pl-PL"),QStringLiteral("ru")}),QStringLiteral("en"));
        QCOMPARE(AiPrompt::resolve(QStringLiteral("ja"),false,{}),QStringLiteral("ja"));
        QCOMPARE(AiPrompt::resolve(QStringLiteral("xx"),false,{}),QStringLiteral("zh"));
        QCOMPARE(AiPrompt::fromSystem({QStringLiteral("zh-Hans-CN")}),QStringLiteral("zh"));
        QCOMPARE(AiPrompt::fromSystem({QStringLiteral("zh-Hant-TW")}),QStringLiteral("zh-Hant"));
        QCOMPARE(AiPrompt::fromSystem({QStringLiteral("zh_HK")}),QStringLiteral("zh-Hant"));
        QCOMPARE(AiPrompt::fromSystem({QStringLiteral("pt-BR"),QStringLiteral("de-AT")}),QStringLiteral("de"));
        QVERIFY(AiPrompt::fromSystem({QStringLiteral("pt-BR")}).isEmpty());
        QCOMPARE(AiPrompt::languages().size(),9);
        for(const auto& language:AiPrompt::languages()) {
            const auto prompt=AiPrompt::build(language,QStringLiteral("fr"),QStringLiteral("Book.PDF"),34);
            // Seven sections (French is not a prompt language other than for "fr"), book and page.
            for(int n=1;n<=7;++n) QVERIFY2(prompt.contains(QStringLiteral("## %1. ").arg(n))==(language!=QLatin1String("fr") || n<7),qPrintable(language+"\n"+prompt));
            QVERIFY2(prompt.contains(QStringLiteral("Book")) && !prompt.contains(QStringLiteral(".PDF")) && prompt.contains(QStringLiteral("34")),qPrintable(prompt));
            QVERIFY(prompt.contains(QStringLiteral("✓")) && prompt.contains(QStringLiteral("✗")));
        }
        const auto zh=AiPrompt::build(QStringLiteral("zh"),QStringLiteral("fr"),QStringLiteral("你好法语3学生用书第二版.pdf"),34);
        for(const auto& part:{"目标语言：法语","《你好法语3学生用书第二版》第 34 页","## 1. 翻译","讲解","语法点","易错点","易混淆点","日常、学术、工作，还是很少使用","## 7. 检查答案","给出正确答案","为什么错","不要猜测"})
            QVERIFY2(zh.contains(QString::fromUtf8(part)),part);
        // The book in the prompt language: no translation section, the rest renumbered.
        const auto french=AiPrompt::build(QStringLiteral("fr"),QStringLiteral("fr"),QStringLiteral("Livre"),3);
        QVERIFY(!french.contains(QStringLiteral("Traduction")));QVERIFY(french.contains(QStringLiteral("## 1. Explications")));QVERIFY(french.contains(QStringLiteral("## 6. Correction")));
        QVERIFY(AiPrompt::build(QStringLiteral("en"),QString(),QStringLiteral("x"),1).contains(QStringLiteral("Target language: (please identify it from the screenshot)")));
        QVERIFY(AiPrompt::build(QStringLiteral("zh-Hant"),QStringLiteral("it"),QStringLiteral("x"),1).contains(QStringLiteral("目標語言：義大利語")));
    }

    void claudeAppGetsThePictureOnceItIsInFront() {
        FakeDesktop system; system.apps.insert(QStringLiteral("claude"),{QStringLiteral("com.anthropic.claudefordesktop"),QUrl::fromLocalFile(QStringLiteral("/Applications/Claude.app"))});
        system.bringsToFront=QStringLiteral("com.anthropic.claudefordesktop");
        AiHandoff ai(system.backend(),fast());
        QCOMPARE(ai.targets().value(0).toMap().value("desktop").toBool(),true);
        QCOMPARE(ai.targets().value(1).toMap().value("desktop").toBool(),false);
        QVERIFY(!ai.send(QStringLiteral("claude"),QStringLiteral("x"))); // nothing captured yet
        QVERIFY(ai.setCapture(picture()));QVERIFY(QFileInfo::exists(QUrl(ai.preview()).toLocalFile()));
        QSignalSpy done(&ai,&AiHandoff::finished);
        QVERIFY(ai.send(QStringLiteral("claude"),QStringLiteral("讲解")));
        QCOMPARE(system.opened.size(),1);QCOMPARE(system.opened.first().scheme(),QStringLiteral("claude"));
        QCOMPARE(system.log,QStringList{QStringLiteral("image")}); // on the clipboard before anything opens
        QVERIFY(ai.busy());
        QTRY_COMPARE(done.size(),1);
        QCOMPARE(done.first().first().toBool(),true);
        QCOMPARE(system.log,(QStringList{QStringLiteral("image"),QStringLiteral("paste")}));
        QVERIFY(ai.status().contains(QStringLiteral("自己点发送")));
        QCOMPARE(system.permissionPrompts,QList<bool>{true}); // asked once
        QVERIFY(ai.send(QStringLiteral("claude"),QStringLiteral("again")));QTRY_COMPARE(done.size(),2);
        QCOMPARE(system.permissionPrompts,QList<bool>{true});
    }

    void chatgptAppGetsPictureThenPrompt() {
        FakeDesktop system; system.apps.insert(QStringLiteral("chatgpt"),{QStringLiteral("com.openai.chat"),QUrl::fromLocalFile(QStringLiteral("/Applications/ChatGPT.app"))});
        system.bringsToFront=QStringLiteral("com.openai.chat");
        AiHandoff ai(system.backend(),fast());QVERIFY(ai.setCapture(picture()));
        QSignalSpy done(&ai,&AiHandoff::finished);
        QVERIFY(ai.send(QStringLiteral("chatgpt"),QStringLiteral("批改")));
        QCOMPARE(system.opened,QList<QUrl>{QUrl::fromLocalFile(QStringLiteral("/Applications/ChatGPT.app"))});
        QTRY_COMPARE(done.size(),1);QVERIFY(done.first().first().toBool());
        QCOMPARE(system.log,(QStringList{QStringLiteral("image"),QStringLiteral("paste"),QStringLiteral("text:批改"),QStringLiteral("paste")}));
    }

    void withoutPermissionNothingIsPasted() {
        FakeDesktop system; system.allowed=false;
        system.apps.insert(QStringLiteral("chatgpt"),{QStringLiteral("com.openai.chat"),QUrl::fromLocalFile(QStringLiteral("/Applications/ChatGPT.app"))});
        AiHandoff ai(system.backend(),fast());QVERIFY(ai.setCapture(picture()));
        QSignalSpy done(&ai,&AiHandoff::finished);
        QVERIFY(ai.send(QStringLiteral("chatgpt"),QStringLiteral("翻译")));
        // The app cannot receive the prompt without a paste; the web page takes it in the link.
        QCOMPARE(system.opened.size(),1);QVERIFY(system.opened.first().toEncoded().startsWith("https://chatgpt.com/?prompt="));
        QCOMPARE(done.size(),1);QVERIFY(!done.first().first().toBool());QVERIFY(!ai.busy());
        QCOMPARE(system.log,QStringList{QStringLiteral("image")});
        QVERIFY(ai.status().contains(QStringLiteral("剪贴板")));QVERIFY(ai.status().contains(QStringLiteral("辅助功能")));
    }

    void pastesOnlyIntoTheChatApp() {
        // The web page: waits for the default browser, never pastes elsewhere.
        FakeDesktop slow; slow.delay=-1; slow.bringsToFront=QStringLiteral("browser");
        AiHandoff a(slow.backend(),fast());QVERIFY(a.setCapture(picture()));
        QSignalSpy timedOut(&a,&AiHandoff::finished);
        QVERIFY(a.send(QStringLiteral("claude"),QStringLiteral("x")));
        QVERIFY(slow.opened.first().toEncoded().startsWith("https://claude.ai/new?q="));
        QTRY_COMPARE(timedOut.size(),1);QVERIFY(!timedOut.first().first().toBool());
        QVERIFY(!slow.log.contains(QStringLiteral("paste")));QVERIFY(a.status().contains(QStringLiteral("前台")));
        // The user switches to another app while the page loads.
        FakeDesktop moved; moved.bringsToFront=QStringLiteral("browser");
        AiHandoff b(moved.backend(),{5,400,20,200,20});QVERIFY(b.setCapture(picture()));
        QSignalSpy left(&b,&AiHandoff::finished);
        QVERIFY(b.send(QStringLiteral("claude"),QStringLiteral("x")));
        QTRY_COMPARE(moved.front,QStringLiteral("browser"));
        moved.bringsToFront=QStringLiteral("com.apple.mail");moved.front=moved.bringsToFront;
        QTRY_COMPARE(left.size(),1);QVERIFY(!left.first().first().toBool());
        QVERIFY(!moved.log.contains(QStringLiteral("paste")));
        // Cancel stops a pending hand-off.
        FakeDesktop cancelled; cancelled.bringsToFront=QStringLiteral("browser");
        AiHandoff c(cancelled.backend(),{5,400,20,200,20});QVERIFY(c.setCapture(picture()));
        QVERIFY(c.send(QStringLiteral("claude"),QStringLiteral("x")));c.cancel();QVERIFY(!c.busy());
        QTest::qWait(300);QVERIFY(!cancelled.log.contains(QStringLiteral("paste")));
    }

    void captureKeepsTheDraggedAreaInDevicePixels() {
        QQuickView view;view.setColor(Qt::white);view.resize(300,200);
        QQmlComponent component(view.engine());
        component.setData("import QtQuick\nItem { width: 300; height: 200; Rectangle { x: 100; y: 50; width: 80; height: 60; color: '#d02040' } }",QUrl());
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem*>(component.create()));QVERIFY2(root,qPrintable(component.errorString()));
        root->setParentItem(view.contentItem());
        QVERIFY(showTestWindow(&view));
        FakeDesktop system;AiHandoff ai(system.backend(),fast());
        QVERIFY(!ai.capture(root.get(),QRectF(10,10,3,3))); // too small
        QVERIFY(ai.capture(root.get(),QRectF(100,50,80,60)));
        const auto image=ai.captured();
        const qreal scale=view.effectiveDevicePixelRatio();
        QVERIFY2(qAbs(image.width()-80*scale)<=2 && qAbs(image.height()-60*scale)<=2,qPrintable(QStringLiteral("%1x%2 at %3").arg(image.width()).arg(image.height()).arg(scale)));
        for(const auto& p:{QPoint(2,2),QPoint(image.width()-3,image.height()-3),QPoint(image.width()/2,image.height()/2)})
            QCOMPARE(image.pixelColor(p),QColor(QStringLiteral("#d02040")));
    }

    // Real textbook: drag over part of a page in the reader, with a typed answer on it.
    void readerCaptureOfRealPage() {
        auto path=realTextbookPath();
        if(path.isEmpty()) { qInfo("Real textbook unavailable; using the scanned fixture"); path=LANGLERN_FIXTURE("scan.pdf"); }
        const QFileInfo before(path);
        ShellState shell;QQmlApplicationEngine engine;QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors)warnings.append(e.toString());});
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}});engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));QVERIFY(window);
        window->resize(1280,860);QVERIFY(showTestWindow(window));
        shell.selectFile(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),30000);QTRY_VERIFY_WITH_TIMEOUT(!shell.busy(),30000);
        const int page=shell.pageCount()>=34 ? 34 : 1;
        shell.goToPage(page);QTRY_COMPARE(shell.currentPage(),page);
        QTRY_VERIFY_WITH_TIMEOUT(shell.annotations()->geometry(page-1).valid,20000);
        const auto id=shell.annotations()->create(page-1,.3,.3);shell.annotations()->setText(id,QStringLiteral("je suis étudiant"),16);shell.finishTextEditing();
        QTest::qWait(800); // let the page reach its target resolution
        auto* button=window->findChild<QQuickItem*>("askAiButton");QVERIFY(button);
        auto* ai=window->findChild<AiHandoff*>();QVERIFY(ai);
        auto* popup=window->findChild<QObject*>("askAiPopup");QVERIFY(popup);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,itemCenter(button));
        // Drag over the middle of the reading area.
        const QPoint from(window->width()/2-220,window->height()/2-140),to(window->width()/2+220,window->height()/2+140);
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,from);
        for(int i=1;i<=8;++i) QTest::mouseMove(window,from+(to-from)*i/8,10);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,to);
        QTRY_VERIFY(popup->property("opened").toBool());
        const auto image=ai->captured();QVERIFY(!image.isNull());
        auto* shown=window->findChild<QQuickItem*>("askAiPreview");QVERIFY(shown);
        QTRY_COMPARE(shown->property("status").toInt(),1); // Image.Ready
        QVERIFY(shown->property("implicitWidth").toReal()>100);
        const auto previewFile=QUrl(ai->preview()).toLocalFile();
        const qreal scale=window->effectiveDevicePixelRatio();
        QVERIFY2(qAbs(image.width()-440*scale)<=3 && qAbs(image.height()-280*scale)<=3,qPrintable(QStringLiteral("%1x%2").arg(image.width()).arg(image.height())));
        // A real page, not an empty frame: many distinct tones.
        QSet<QRgb> tones;for(int y=0;y<image.height();y+=7)for(int x=0;x<image.width();x+=7)tones.insert(image.pixel(x,y)&0xF0F0F0);
        QVERIFY2(tones.size()>12,qPrintable(QString::number(tones.size())));
        auto* prompt=window->findChild<QObject*>("askAiPrompt");QVERIFY(prompt);
        const auto text=prompt->property("text").toString();
        QVERIFY2(text.contains(QStringLiteral("第 %1 页").arg(page)),qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("## 7. 检查答案")),qPrintable(text));
        if(path!=LANGLERN_FIXTURE("scan.pdf")) QVERIFY2(text.contains(QStringLiteral("目标语言：法语")) && text.contains(QStringLiteral("《你好法语3学生用书第二版》")),qPrintable(text));
        // A picked prompt language rewrites the prompt; it is remembered.
        auto* workspace=window->findChild<WorkspaceState*>();QVERIFY(workspace);
        QCOMPARE(workspace->aiPromptLanguage(),QStringLiteral("app"));
        auto* language=window->findChild<QObject*>("askAiLanguage");QVERIFY(language);
        const auto choices=language->property("model").toList();QCOMPARE(choices.size(),11);
        int french=-1;for(int i=0;i<choices.size();++i) if(choices[i].toMap().value("choice").toString()==QLatin1String("fr")) french=i;
        QVERIFY(french>1);
        QVERIFY(language->setProperty("currentIndex",french));QVERIFY(QMetaObject::invokeMethod(language,"activated",Q_ARG(int,french)));
        QTRY_COMPARE(workspace->aiPromptLanguage(),QStringLiteral("fr"));
        QTRY_VERIFY2(prompt->property("text").toString().startsWith(QStringLiteral("Tu es un professeur")),qPrintable(prompt->property("text").toString()));
        workspace->setAiPromptLanguage(QStringLiteral("app"));
        QTRY_VERIFY(prompt->property("text").toString().startsWith(QStringLiteral("你是一位")));
        const auto output=qEnvironmentVariable("LANGLERN_AI_ARTIFACT_DIR");
        if(!output.isEmpty()) { QVERIFY(image.save(QDir(output).filePath(QStringLiteral("ask-ai-capture.png")))); QVERIFY(window->grabWindow().save(QDir(output).filePath(QStringLiteral("ask-ai-popup.png")))); }
        // Back in the tool the user had; Escape cancels a capture without a popup.
        QMetaObject::invokeMethod(popup,"close");QTRY_VERIFY(!popup->property("visible").toBool());QVERIFY(!ai->hasCapture());
        auto* document=window->findChild<QQuickItem*>("readerDocument");
        if(!document) for(auto* item:window->findChildren<QQuickItem*>()) if(item->property("aiCapturing").isValid()) { document=item; break; }
        QVERIFY(document);QCOMPARE(document->property("inputMode").toInt(),0);QVERIFY(!document->property("aiCapturing").toBool());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,itemCenter(button));QTRY_VERIFY(document->property("aiCapturing").toBool());
        QTest::keyClick(window,Qt::Key_Escape);QTRY_VERIFY(!document->property("aiCapturing").toBool());
        QCOMPARE(document->property("inputMode").toInt(),0);QVERIFY(!popup->property("visible").toBool());
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close();QTRY_VERIFY_WITH_TIMEOUT(shell.shutdownComplete(),20000);
        const QFileInfo after(path);QCOMPARE(after.size(),before.size());QCOMPARE(after.lastModified(),before.lastModified());
    }
};
LANGLERN_TEST_SUITE(AiTests,"ai")
#include "ai_tests.moc"
