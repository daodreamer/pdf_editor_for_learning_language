#include "testsupport.h"
#include "inputsourceswitcher.h"
#include "languagedetect.h"
#include "shellstate.h"
#include <QTemporaryDir>
#include <QTest>

// A fake system: enabled sources, the current one and every selection made.
struct FakeSystem {
    QList<InputSources::Source> sources{{"com.apple.keylayout.ABC","ABC","en"},{"com.apple.inputmethod.SCIM.ITABC","Pinyin","zh"}};
    QString current=QStringLiteral("com.apple.inputmethod.SCIM.ITABC");
    QStringList selected;
    InputSourceSwitcher::Backend backend() {
        return {[this] { return sources; },[this] { return current; },
            [this](const QString& id) { selected.append(id); current=id; return true; }};
    }
};

class InputTests : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> caseWorkspace;
private slots:
    void init() { caseWorkspace=std::make_unique<QTemporaryDir>(); setTestEnvironment("LANGLERN_WORKSPACE_DIR",caseWorkspace->path()); }

    void dominantLanguage() {
        QCOMPARE(languageEvidence(QStringLiteral("Bonjour ! Je suis étudiant et vous êtes le professeur. Nous avons une leçon dans la classe avec les élèves, et il est très sympa. C'est pour vous.")).dominant(),QStringLiteral("fr"));
        QCOMPARE(languageEvidence(QStringLiteral("This is the book and you are in the class. It is what your teacher said to you, and that is for this lesson with the group.")).dominant(),QStringLiteral("en"));
        QCOMPARE(languageEvidence(QStringLiteral("Der Lehrer und die Schüler sind nicht da. Ich gehe mit ihr zu den Freunden, und wir sind müde. Das ist eine Straße auf der Karte.")).dominant(),QStringLiteral("de"));
        // A French textbook for Chinese learners: Chinese explanations, French exercises.
        const auto mixed=QStringLiteral("请听录音，并完成下列练习。根据课文内容回答问题，注意动词变位和阴阳性的配合。"
            "Complétez avec le verbe être : je suis, vous êtes, il est. Les élèves sont dans la classe avec le professeur et une amie.");
        QCOMPARE(languageEvidence(mixed).dominant(),QStringLiteral("fr"));
        QCOMPARE(languageEvidence(QStringLiteral("请听录音，并完成下列练习。根据课文内容回答问题，注意动词变位和阴阳性的配合。我们今天学习新的语法知识，然后做练习。")).dominant(),QStringLiteral("zh"));
        QVERIFY(languageEvidence(QStringLiteral("Page 12")).dominant().isEmpty());
        QCOMPARE(languageFromName(QStringLiteral("你好法语3学生用书第二版")),QStringLiteral("fr"));
        QCOMPARE(languageFromName(QStringLiteral("English Grammar in Use")),QStringLiteral("en"));
        QVERIFY(languageFromName(QStringLiteral("新世纪法英词典 French-English")).isEmpty());
        QVERIFY(languageFromName(QStringLiteral("scan-0042")).isEmpty());
        QCOMPARE(languageFromOcr(QStringLiteral("fra+eng")),QStringLiteral("fr"));
        QCOMPARE(languageFromOcr(QStringLiteral("chi_sim")),QStringLiteral("zh"));
    }

    void switchesWhileActiveAndRestores() {
        FakeSystem system; system.sources.append({"com.apple.keylayout.French","French","fr"});
        {
            InputSourceSwitcher switcher(system.backend(),true);
            switcher.setSwitching(true); switcher.setLanguage(QStringLiteral("fr"));
            QVERIFY(system.selected.isEmpty()); // no document yet
            switcher.setDocumentOpen(true);
            QCOMPARE(system.current,QStringLiteral("com.apple.keylayout.French"));
            QVERIFY(switcher.engaged()); QCOMPARE(switcher.targetName(),QStringLiteral("French"));
            // Leaving the app puts the user's keyboard back; returning switches again.
            switcher.setApplicationActive(false);
            QCOMPARE(system.current,QStringLiteral("com.apple.inputmethod.SCIM.ITABC"));
            system.current=QStringLiteral("com.apple.keylayout.ABC"); // picked in another app
            switcher.setApplicationActive(true);
            QCOMPARE(system.current,QStringLiteral("com.apple.keylayout.French"));
            // A source picked by hand while reading stays until something relevant changes.
            system.current=QStringLiteral("com.apple.inputmethod.SCIM.ITABC");
            switcher.setChoices({});
            QCOMPARE(system.current,QStringLiteral("com.apple.inputmethod.SCIM.ITABC"));
            switcher.setApplicationActive(false);
            QCOMPARE(system.current,QStringLiteral("com.apple.keylayout.ABC"));
            switcher.setApplicationActive(true);
            // Closing the document and turning the feature off both restore.
            switcher.setDocumentOpen(false);
            QCOMPARE(system.current,QStringLiteral("com.apple.keylayout.ABC")); QVERIFY(!switcher.engaged());
            switcher.setDocumentOpen(true); QCOMPARE(system.current,QStringLiteral("com.apple.keylayout.French"));
            switcher.setSwitching(false); QCOMPARE(system.current,QStringLiteral("com.apple.keylayout.ABC"));
            switcher.setSwitching(true); QCOMPARE(system.current,QStringLiteral("com.apple.keylayout.French"));
        }
        // Quitting while active restores too.
        QCOMPARE(system.current,QStringLiteral("com.apple.keylayout.ABC"));
    }

    void choiceAndAutomaticPick() {
        FakeSystem system;
        system.sources.append({"com.apple.keylayout.French","French","fr"});
        system.sources.append({"com.apple.keylayout.French-PC","French – PC","fr"});
        system.current=QStringLiteral("com.apple.keylayout.French-PC");
        InputSourceSwitcher switcher(system.backend(),true);
        switcher.setSwitching(true); switcher.setLanguage(QStringLiteral("fr")); switcher.setDocumentOpen(true);
        // Already on a French keyboard: automatic mode keeps it.
        QVERIFY(system.selected.isEmpty()); QCOMPARE(switcher.target(),QStringLiteral("com.apple.keylayout.French-PC"));
        // The user's pick wins, also one made for another language (ABC Extended for French, say).
        switcher.setChoices({{"fr",QStringLiteral("com.apple.keylayout.ABC")}});
        QCOMPARE(system.current,QStringLiteral("com.apple.keylayout.ABC"));
        // A pick that is no longer installed falls back to automatic.
        switcher.setChoices({{"fr",QStringLiteral("removed.source")}});
        QCOMPARE(system.current,QStringLiteral("com.apple.keylayout.French"));
        switcher.setApplicationActive(false);
        QCOMPARE(system.current,QStringLiteral("com.apple.keylayout.French-PC"));
    }

    void missingSourceAndLaterInstall() {
        FakeSystem system;
        InputSourceSwitcher switcher(system.backend(),true);
        switcher.setSwitching(true); switcher.setLanguage(QStringLiteral("fr")); switcher.setDocumentOpen(true);
        QVERIFY(switcher.missing()); QVERIFY(!switcher.engaged()); QVERIFY(system.selected.isEmpty());
        // Added in System Settings while the user was away: used on return.
        switcher.setApplicationActive(false);
        system.sources.append({"com.apple.keylayout.French","French","fr"});
        switcher.setApplicationActive(true);
        QVERIFY(!switcher.missing()); QCOMPARE(system.current,QStringLiteral("com.apple.keylayout.French"));
        switcher.setSwitching(false); QVERIFY(!switcher.missing());
    }

    void realTextbookLanguage() {
        const auto path=realTextbookPath();
        if(path.isEmpty()) QSKIP("The configured real textbook is unavailable on this runner; real-PDF acceptance was not performed.");
        ShellState shell;
        // A scan without a text layer: with an English OCR setting, "fr" can only come from the
        // book itself (its name, "你好法语…").
        shell.setOcrLanguage(QStringLiteral("eng"));
        shell.updateViewport(0,900,1200,2);
        shell.selectFile(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(shell.pageCount()>40,20000);
        for(int page:{12,34}) {
            shell.goToPage(page);
            QTRY_VERIFY_WITH_TIMEOUT(shell.textSelection()->snapshot(page-1),20000);
            QCOMPARE(shell.documentLanguage(),QStringLiteral("fr"));
        }
        shell.clearSelection(); QTRY_VERIFY(shell.documentLanguage().isEmpty());
        shell.beginShutdown(); QTRY_VERIFY_WITH_TIMEOUT(shell.shutdownComplete(),20000);
    }
};
LANGLERN_TEST_SUITE(InputTests,"input")
#include "input_tests.moc"
