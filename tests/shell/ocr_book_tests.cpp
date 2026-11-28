// Whole-book background OCR: queue order and gating, the per-document store (round trip, damage,
// profiles, budgets), and ShellState integration on the scanned fixture with the real engine.
#include "testsupport.h"
#include "shellstate.h"
#include "ocrbookqueue.h"
#include "ocrbookstore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QScreen>
#include <chrono>
#include <mutex>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <sys/resource.h>
#endif
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <psapi.h>
#endif
#include <QPainter>
#include <QPdfWriter>

namespace {
QByteArray hexHash(const QString& path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(&file); return hash.result().toHex();
}
QByteArray fakeHex(char c) { return QByteArray(64, c); }
TextSnapshot sampleText(const QString& content) {
    auto page = std::make_shared<TextPage>(); page->ocr = true;
    double x = 0.1;
    for (auto u : content.toUcs4()) {
        const bool visible = u != U' ' && u != U'\n';
        page->characters.append({u, visible ? QRectF(x, 0.2, 0.01, 0.02) : QRectF(), visible});
        x += 0.011;
    }
    return page;
}
QString content(const TextSnapshot& text) {
    QString result; if (!text) return result;
    for (const auto& c : text->characters) result += QString::fromUcs4(&c.unicode, 1);
    return result;
}
int storedPages(const QString& root, const QByteArray& identity) {
    int count = 0;
    QDirIterator it(root + '/' + QString::fromLatin1(identity), {"*.lpo"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); ++count; }
    return count;
}
// Image-only "scanned" pages (no text layer), except `nativePages` which get real PDF text.
QString makeBook(const QString& path, int pages, QList<int> nativePages = {}) {
    QPdfWriter writer(path);
    writer.setResolution(150);
    writer.setPageSize(QPageSize(QPageSize::A5));
    QPainter painter(&writer);
    const QRect target = painter.viewport();
    for (int p = 0; p < pages; ++p) {
        if (p > 0) writer.newPage();
        // Dense enough that Tesseract takes a noticeable time, so tests can act mid-page.
        QString line = QStringLiteral("Page %1 : Bonjour, voici la leçon %1.").arg(p + 1);
        for (int i = 0; i < 22; ++i) line += QStringLiteral("\nNous étudions le français pendant l'été, ligne %1.").arg(i + 1);
        if (nativePages.contains(p)) {
            painter.setFont(QFont("Arial", 9));
            painter.drawText(target.adjusted(60, 60, -60, -60), Qt::AlignLeft | Qt::TextWordWrap, line);
            continue;
        }
        QImage image(target.width(), target.height(), QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter scan(&image);
        QFont font("Arial"); font.setPixelSize(26); scan.setFont(font); scan.setPen(Qt::black);
        scan.drawText(image.rect().adjusted(60, 60, -60, -60), Qt::AlignLeft | Qt::TextWordWrap, line);
        scan.end();
        painter.drawImage(target, image);
    }
    painter.end();
    return path;
}
// Whole-process figures (OCR thread, PDFium, GUI, models): not OCR-only resources.
QJsonObject processSample() {
    QJsonObject row;
#ifdef Q_OS_MACOS
    task_vm_info_data_t vm{}; mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) == KERN_SUCCESS) {
        row["resident_bytes"] = double(vm.resident_size);
        row["footprint_bytes"] = double(vm.phys_footprint);
    }
    rusage usage{}; getrusage(RUSAGE_SELF, &usage);
    row["cpu_seconds"] = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 + usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
    row["peak_resident_bytes"] = double(usage.ru_maxrss); // bytes on macOS
#elif defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
        row["resident_bytes"] = double(memory.WorkingSetSize);
        row["private_bytes"] = double(memory.PrivateUsage);
        row["peak_resident_bytes"] = double(memory.PeakWorkingSetSize);
    }
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        const auto seconds = [](FILETIME t) { return (double(t.dwHighDateTime) * 4294967296.0 + t.dwLowDateTime) / 1e7; };
        row["cpu_seconds"] = seconds(kernel) + seconds(user);
    }
#endif
    return row;
}
struct Trace {
    QFile file; QElapsedTimer clock;
    explicit Trace(const QString& path) : file(path) { clock.start(); }
    bool open() { return file.open(QIODevice::WriteOnly | QIODevice::NewOnly); }
    void record(QJsonObject row) {
        row["elapsed_ms"] = clock.nsecsElapsed() / 1e6;
        file.write(QJsonDocument(row).toJson(QJsonDocument::Compact) + "\n"); file.flush();
    }
};
const OcrBookStore::Limits limits{OcrBookConfig::bookBytes, OcrBookConfig::totalBytes, OcrBookConfig::profilesPerBook};
// Enables the background queue for one ShellState (test main turns it off for other suites).
struct BackgroundOcr {
    TestEnvironmentRestore restore{"LANGLERN_NO_BACKGROUND_OCR"};
    BackgroundOcr() { qunsetenv("LANGLERN_NO_BACKGROUND_OCR"); }
};
bool waitFor(const std::function<bool()>& condition, int ms) {
    QElapsedTimer timer; timer.start();
    while (!condition()) {
        if (timer.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }
    return true;
}
}

class OcrBookTests : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> workspace_;
    QString scan_, text_ = LANGLERN_FIXTURE("text.pdf");
    void openDocument(ShellState& shell, const QString& path) {
        shell.selectFile(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(shell.ocrBookReady(), 15000);
    }
    void shutdown(ShellState& shell) {
        shell.beginShutdown();
        QTRY_VERIFY_WITH_TIMEOUT(shell.shutdownComplete(), 30000);
    }
private slots:
    void init() {
        workspace_ = std::make_unique<QTemporaryDir>();
        setTestEnvironment("LANGLERN_WORKSPACE_DIR", workspace_->path());
        // Six pages: five scanned, page 3 with a real text layer (mixed book).
        scan_ = makeBook(workspace_->filePath("book.pdf"), 6, {2});
    }

    void queueOrderStatesAndIdle() {
        OcrBookQueue queue; queue.reset(10);
        QCOMPARE(queue.next(4), 4);
        queue.set(4, OcrBookQueue::State::Done);
        QCOMPARE(queue.next(4), 5);               // equal distance: following page first
        queue.set(5, OcrBookQueue::State::Native);
        QCOMPARE(queue.next(4), 3);
        queue.set(3, OcrBookQueue::State::Busy);
        QCOMPARE(queue.next(4), 6);
        queue.release(3);
        QCOMPARE(queue.next(4), 3);
        queue.release(4);                          // only Busy pages are released
        QCOMPARE(queue.state(4), OcrBookQueue::State::Done);
        QCOMPARE(queue.done(), 1); QCOMPARE(queue.native(), 1);
        queue.set(4, OcrBookQueue::State::Failed);
        QCOMPARE(queue.done(), 0); QCOMPARE(queue.failed(), 1);
        QCOMPARE(queue.next(-5), 0); QCOMPARE(queue.next(99), 9);
        queue.setEnabled(true); QVERIFY(!queue.active()); // not ready yet
        queue.setReady(true); QVERIFY(queue.active());
        queue.setPaused(true); QVERIFY(!queue.active()); queue.setPaused(false);
        for (int p = 0; p < 10; ++p) if (queue.state(p) == OcrBookQueue::State::Unknown) queue.set(p, OcrBookQueue::State::Done);
        QVERIFY(queue.complete()); QVERIFY(!queue.active()); QCOMPARE(queue.next(0), -1);
        queue.noteInteraction(1000);
        QCOMPARE(queue.idleRemaining(1200, 1500), qint64(1300));
        QCOMPARE(queue.idleRemaining(2600, 1500), qint64(0));
    }

    void storeRoundTripDamageAndBudgets() {
        const auto root = workspace_->filePath("books");
        const auto a = fakeHex('a'), b = fakeHex('b'), c = fakeHex('c');
        const auto p1 = fakeHex('1'), p2 = fakeHex('2'), p3 = fakeHex('3');
        OcrBookStore store(root, limits);
        auto index = store.open(a, p1, "fra", 5);
        QVERIFY2(index.error.isEmpty(), qPrintable(index.error));
        QVERIFY(index.ocr.isEmpty());
        // Native pages wait in memory until the first OCR page creates the profile directory.
        store.recordNative(0);
        QVERIFY(!QFileInfo::exists(root + "/" + a));
        const QTransform toPdf(612, 0, 0, -792, 0, 792);
        QString error;
        QVERIFY(store.write({2, false, sampleText(QStringLiteral("Été français\n第二行")), toPdf}, &error));
        QVERIFY(store.write({3, false, sampleText(QString()), toPdf}, &error)); // blank page: done, no characters
        OcrBookReader reader(root, a, p1);
        QVERIFY(reader.exists());
        QCOMPARE(reader.pages(), (QList<int>{2, 3}));
        QCOMPARE(reader.nativePages(), QList<int>{0});
        QCOMPARE(reader.text(2), QStringLiteral("Été français\n第二行"));
        const auto record = reader.page(2);
        QVERIFY(record && record->text && !record->native);
        QCOMPARE(record->displayToPdf, toPdf);
        const auto original = sampleText(QStringLiteral("Été français\n第二行"));
        for (int i = 0; i < original->characters.size(); ++i) {
            const auto& x = original->characters[i]; const auto& y = record->text->characters[i];
            QCOMPARE(y.visible, x.visible);
            QVERIFY(std::abs(y.box.x() - x.box.x()) < 2e-5 && std::abs(y.box.height() - x.box.height()) < 2e-5);
        }
        int visited = 0;
        QVERIFY(reader.forEachPage([&](const OcrBookPage& page) { ++visited; return page.page >= 0; }));
        QCOMPARE(visited, 2);
        QVERIFY(!reader.forEachPage([](const OcrBookPage&) { return false; }));

        // Reopen ("restart"): finished pages come back from disk.
        store.close();
        index = store.open(a, p1, "fra", 5);
        QCOMPARE(index.ocr, (QList<int>{2, 3})); QCOMPARE(index.native, QList<int>{0});

        // A damaged page fails its checksum, is removed and reported as missing.
        {
            QFile file(reader.pagePath(2)); QVERIFY(file.open(QIODevice::ReadWrite));
            file.seek(file.size() - 40); file.write("X"); file.close();
        }
        QVERIFY(!store.read(2));
        QVERIFY(!QFileInfo::exists(reader.pagePath(2)));
        // A truncated page record is not listed as valid content either.
        { QFile file(reader.pagePath(3)); QVERIFY(file.open(QIODevice::ReadWrite)); file.resize(20); }
        QVERIFY(!reader.page(3));
        // A damaged manifest drops the whole profile: rebuilt from nothing.
        { QFile file(reader.directory() + "/manifest.json"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{broken"); }
        store.close();
        index = store.open(a, p1, "fra", 5);
        QVERIFY(index.ocr.isEmpty() && index.native.isEmpty());
        QVERIFY(!reader.exists());
        QVERIFY(store.write({1, false, sampleText("again"), toPdf}, &error));
        QCOMPARE(OcrBookReader(root, a, p1).pages(), QList<int>{1});
        // A page count that no longer matches the manifest is a different document state.
        store.close();
        QVERIFY(store.open(a, p1, "fra", 6).ocr.isEmpty());
        QVERIFY(store.write({1, false, sampleText("again"), toPdf}, &error));

        // Another profile (language/model) never sees these pages; two profiles are kept.
        store.close();
        QVERIFY(store.open(a, p2, "eng", 6).ocr.isEmpty());
        QVERIFY(store.write({4, false, sampleText("other"), toPdf}, &error));
        QTest::qWait(1100); // manifest times order the profiles
        store.close();
        QCOMPARE(store.open(a, p1, "fra", 6).ocr, QList<int>{1});
        QTest::qWait(1100);
        store.close();
        QVERIFY(store.open(a, p3, "chi_sim", 6).ocr.isEmpty());
        // p3 is current, p1 newest other -> kept, p2 oldest -> removed.
        QVERIFY(QFileInfo::exists(root + "/" + a + "/" + p1));
        QVERIFY(!QFileInfo::exists(root + "/" + a + "/" + p2));

        // Per-book cap: a write past it is refused with a visible reason.
        OcrBookStore small(root, {2048, OcrBookConfig::totalBytes, 2});
        small.open(b, p1, "fra", 3);
        QVERIFY(small.write({0, false, sampleText("short"), toPdf}, &error));
        QVERIFY(!small.write({1, false, sampleText(QString(400, QChar('x'))), toPdf}, &error));
        QVERIFY2(error.contains("MiB"), qPrintable(error));

        // Total cap: least recently used other books go first, never the open one.
        const qint64 aBytes = [&] { qint64 n = 0; QDirIterator it(root + "/" + a, QDir::Files, QDirIterator::Subdirectories); while (it.hasNext()) { it.next(); n += it.fileInfo().size(); } return n; }();
        QTest::qWait(1100);
        OcrBookStore tight(root, {OcrBookConfig::bookBytes, aBytes + 600, 2});
        tight.open(c, p1, "fra", 3); // b is newer than a: a goes first
        QVERIFY(tight.write({0, false, sampleText(QString(20, QChar('y'))), toPdf}, &error));
        QVERIFY(!QFileInfo::exists(root + "/" + a));
        QVERIFY(QFileInfo::exists(root + "/" + c));

        // Clearing one book removes all of its profiles.
        QVERIFY(tight.clearBook(c, &error));
        QVERIFY(!QFileInfo::exists(root + "/" + c));
        QVERIFY(!tight.clearBook("not-hex", &error));
    }

    void profileFollowsLanguageAndModel() {
        const auto models = QCoreApplication::applicationDirPath() + "/tessdata";
        if (!QFileInfo::exists(models + "/fra.traineddata")) QSKIP("tessdata not built; OCR profile test NOT executed");
        QString error;
        const auto fra = OcrBook::profile("fra", models, &error);
        QVERIFY2(fra.size() == 64, qPrintable(error));
        QCOMPARE(OcrBook::profile("fra", models), fra);
        QVERIFY(OcrBook::profile("eng", models) != fra);
        QVERIFY(OcrBook::profile("fra+eng", models) != fra);
        const auto copy = workspace_->filePath("models");
        QVERIFY(QDir().mkpath(copy));
        QVERIFY(QFile::copy(models + "/fra.traineddata", copy + "/fra.traineddata"));
        QCOMPARE(OcrBook::profile("fra", copy), fra); // same content, other directory
        {
            QFile file(copy + "/fra.traineddata"); QVERIFY(file.open(QIODevice::Append)); file.write("changed model");
        }
        QVERIFY(OcrBook::profile("fra", copy) != fra);
        QVERIFY(OcrBook::profile("deu", copy, &error).isEmpty());
        QVERIFY(error.contains("deu"));
    }

    void scannedBookCompletesAndResumes() {
        BackgroundOcr enable;
        const auto identity = hexHash(scan_);
        const auto root = OcrBook::defaultRoot();
        const auto before = hexHash(scan_);
        int firstRun = 0;
        {
            ShellState shell;
            QVERIFY(shell.ocrBookEnabled());
            openDocument(shell, scan_);
            QCOMPARE(shell.ocrBookPages(), shell.pageCount());
            QCOMPARE(shell.pageCount(), 6);
            // Interrupt part way: quit after two pages, like closing the window.
            QVERIFY2(waitFor([&] { return shell.ocrBookDone() >= 2; }, 90000), qPrintable(shell.ocrBookStatus()));
            firstRun = shell.ocrBookDone();
            shutdown(shell);
        }
        QVERIFY(storedPages(root, identity) >= firstRun);
        {
            // Restart: finished pages are known at once and only the rest is recognised.
            ShellState shell;
            openDocument(shell, scan_);
            QVERIFY(shell.ocrBookDone() >= firstRun);
            QVERIFY2(waitFor([&] { return shell.ocrBookQueue().complete(); }, 120000), qPrintable(shell.ocrBookStatus()));
            QCOMPARE(shell.ocrBookDone() + shell.ocrBookNative() + shell.ocrBookQueue().failed(), shell.pageCount());
            QCOMPARE(shell.ocrBookQueue().failed(), 0);
            QCOMPARE(shell.ocrBookNative(), 1);
            QCOMPARE(shell.ocrBookQueue().state(2), OcrBookQueue::State::Native);
            QVERIFY(shell.ocrBookStatus().contains(QStringLiteral("完成")));
            QVERIFY2(shell.ocrBookReader().text(4).contains(QStringLiteral("leçon 5")), qPrintable(shell.ocrBookReader().text(4)));
            // Read-only query interface: per page characters and the whole book.
            const auto reader = shell.ocrBookReader();
            QCOMPARE(reader.pages().size(), shell.ocrBookDone());
            int characters = 0;
            reader.forEachPage([&](const OcrBookPage& page) {
                characters += int(page.text->characters.size());
                for (const auto& c : page.text->characters) if (c.visible) Q_ASSERT(QRectF(0,0,1.001,1.001).contains(c.box));
                return true;
            });
            QVERIFY(characters > 0);
            shutdown(shell);
        }
        {
            // Third open: complete from disk, nothing runs; visible OCR pages load without rendering.
            ShellState shell;
            openDocument(shell, scan_);
            QVERIFY(shell.ocrBookQueue().complete());
            QVERIFY(!shell.ocrBookRunning());
            QTRY_VERIFY_WITH_TIMEOUT(!shell.busy(), 15000);
            QElapsedTimer timer; timer.start();
            shell.setOcrSelectionEnabled(true);
            QTRY_VERIFY_WITH_TIMEOUT(shell.textSelection()->snapshot(0) && shell.textSelection()->snapshot(0)->ocr
                && !shell.textSelection()->snapshot(0)->pending, 10000);
            qInfo("stored_page_load_ms=%lld", timer.elapsed());
            QCOMPARE(content(shell.textSelection()->snapshot(0)), shell.ocrBookReader().text(0));
            shutdown(shell);
        }
        QCOMPARE(hexHash(scan_), before); // the PDF stays read-only
    }

    void interactionPauseAndForegroundFirst() {
        BackgroundOcr enable;
        ShellState shell;
        openDocument(shell, scan_);
        QTRY_VERIFY_WITH_TIMEOUT(!shell.busy(), 15000);
        // Continuous scrolling (every 200 ms, well inside the idle period) keeps the queue waiting.
        QElapsedTimer timer; timer.start();
        bool started = false; double y = 0;
        while (timer.elapsed() < 3000) {
            y = y > 400 ? 0 : y + 40;
            shell.updateViewport(y, 700, 900, 1);
            started = started || shell.ocrBookRunning();
            QTest::qWait(200);
        }
        QVERIFY2(!started, "background OCR started while the user kept scrolling");
        QCOMPARE(shell.ocrBookDone(), 0);
        // Idle: it starts by itself.
        QVERIFY(waitFor([&] { return shell.ocrBookRunning(); }, OcrBookConfig::idleMs + 5000));
        // Pause cancels the running page and holds the queue.
        shell.setOcrBookPaused(true);
        QVERIFY(!shell.ocrBookRunning());
        const int pausedAt = shell.ocrBookDone();
        QTest::qWait(OcrBookConfig::idleMs + 2000);
        QCOMPARE(shell.ocrBookDone(), pausedAt);
        QVERIFY(!shell.ocrBookRunning());
        QVERIFY(shell.ocrBookStatus().contains(QStringLiteral("暂停")));
        shell.setOcrBookPaused(false);
        QVERIFY(waitFor([&] { return shell.ocrBookRunning(); }, OcrBookConfig::idleMs + 5000));
        // Foreground OCR overtakes: wait until the background page is in Tesseract, then ask
        // for an explicit page OCR. It must finish without waiting for the background page.
        QVERIFY(waitFor([&] { return shell.ocrBookRecognizing(); }, 20000));
        const int target = shell.currentPage() - 1;
        QElapsedTimer foreground; foreground.start();
        shell.recognizeCurrentPage();
        QTRY_VERIFY_WITH_TIMEOUT(shell.textSelection()->snapshot(target) && shell.textSelection()->snapshot(target)->ocr
            && !shell.textSelection()->snapshot(target)->pending, 30000);
        qInfo("foreground_with_background_ms=%lld", foreground.elapsed());
        QVERIFY2(waitFor([&] { return shell.ocrBookQueue().complete(); }, 120000), qPrintable(shell.ocrBookStatus()));
        QCOMPARE(shell.ocrBookQueue().failed(), 0);
        // Switching the preference off stops further work (nothing left here, just the state).
        shell.setOcrBookEnabled(false);
        QVERIFY(!shell.ocrBookRunning());
        shutdown(shell);
    }

    void staleResultsSwitchAndCloseOrder() {
        BackgroundOcr enable;
        const auto scanIdentity = hexHash(scan_);
        const auto root = OcrBook::defaultRoot();
        ShellState shell;
        openDocument(shell, scan_);
        QVERIFY(waitFor([&] { return shell.ocrBookRecognizing(); }, 30000));
        const int storedBefore = storedPages(root, scanIdentity);
        // Switch documents mid-page: the old page must neither be stored nor counted.
        openDocument(shell, text_);
        QCOMPARE(shell.ocrBookDone(), 0);
        QTest::qWait(4000);
        QCOMPARE(storedPages(root, scanIdentity), storedBefore);
        // The text document: every page has a text layer; nothing is written for it.
        QVERIFY(waitFor([&] { return shell.ocrBookQueue().complete(); }, 30000));
        QCOMPARE(shell.ocrBookNative(), shell.pageCount());
        QCOMPARE(shell.ocrBookDone(), 0);
        QVERIFY(!QFileInfo::exists(root + "/" + hexHash(text_)));
        // Closing while a background page is in Tesseract shuts both threads down promptly.
        openDocument(shell, scan_);
        QVERIFY(waitFor([&] { return shell.ocrBookRecognizing(); }, 30000));
        QElapsedTimer timer; timer.start();
        shutdown(shell);
        qInfo("shutdown_with_background_ms=%lld", timer.elapsed());
        QVERIFY(timer.elapsed() < 10000);
    }

    void languageChangeAndClear() {
        BackgroundOcr enable;
        ShellState shell;
        openDocument(shell, scan_);
        QVERIFY2(waitFor([&] { return shell.ocrBookQueue().complete(); }, 120000), qPrintable(shell.ocrBookStatus()));
        const int pages = shell.ocrBookDone();
        QVERIFY(pages > 0);
        const auto fraReader = shell.ocrBookReader();
        QCOMPARE(fraReader.pages().size(), pages);
        // Another language is another profile: old results are not used for it.
        shell.setOcrLanguage("eng");
        QTRY_VERIFY_WITH_TIMEOUT(shell.ocrBookReady(), 15000);
        QCOMPARE(shell.ocrBookDone(), 0);
        QVERIFY(shell.ocrBookReader().directory() != fraReader.directory());
        QVERIFY(waitFor([&] { return shell.ocrBookDone() >= 1; }, 60000));
        // Back to French: its profile was kept (two per book) and is complete at once.
        shell.setOcrLanguage("fra");
        QTRY_VERIFY_WITH_TIMEOUT(shell.ocrBookReady(), 15000);
        QCOMPARE(shell.ocrBookDone(), pages);
        // Clear this book: all languages go, recognition starts over.
        shell.setOcrBookPaused(true);
        shell.clearBookOcr();
        QTRY_VERIFY_WITH_TIMEOUT(shell.ocrBookReady(), 15000);
        QCOMPARE(shell.ocrBookDone(), 0);
        QVERIFY(!QFileInfo::exists(OcrBook::defaultRoot() + "/" + hexHash(scan_)));
        QVERIFY(shell.ocrStatus().contains(QStringLiteral("清除")));
        shutdown(shell);
    }

    void realTextbookBackgroundResume() {
        const auto path = realTextbookPath();
        if (path.isEmpty()) QSKIP("Fixed real textbook unavailable; real textbook background OCR NOT executed");
        BackgroundOcr enable;
        const auto before = hexHash(path);
        int first = 0;
        {
            ShellState shell;
            openDocument(shell, path);
            shell.goToPage(34);
            QTRY_VERIFY_WITH_TIMEOUT(!shell.busy(), 30000);
            QVERIFY2(waitFor([&] { return shell.ocrBookDone() + shell.ocrBookNative() >= 3; }, 120000), qPrintable(shell.ocrBookStatus()));
            // Pages near the current page come first.
            for (int p : shell.ocrBookReader().pages()) QVERIFY2(std::abs(p - 33) <= 3, qPrintable(QString::number(p)));
            first = shell.ocrBookDone();
            shutdown(shell);
        }
        {
            ShellState shell;
            openDocument(shell, path);
            QVERIFY(shell.ocrBookDone() >= first);
            const int resumed = shell.ocrBookDone();
            QVERIFY(waitFor([&] { return shell.ocrBookDone() > resumed; }, 120000));
            qInfo("real_textbook_book_ocr resumed=%d now=%d pages=%d", resumed, shell.ocrBookDone(), shell.ocrBookPages());
            shutdown(shell);
        }
        QCOMPARE(hexHash(path), before);
    }

    // ---- Opt-in benchmarks (benchmarks/run-ocr-book.py); skipped in CTest ----
    void benchmarkFullBook() {
        const auto tracePath = qEnvironmentVariable("LANGLERN_OCR_BOOK_TRACE");
        if (tracePath.isEmpty() || qEnvironmentVariable("LANGLERN_OCR_BOOK_BENCH") != "full") QSKIP("Opt-in benchmark (run-ocr-book.py)");
        const auto path = realTextbookPath();
        QVERIFY2(!path.isEmpty(), "LANGLERN_REAL_PDF required");
        // The script keeps one workspace across processes to measure resume after a kill.
        const auto workspace = qEnvironmentVariable("LANGLERN_OCR_BOOK_WORKSPACE");
        QVERIFY(!workspace.isEmpty());
        setTestEnvironment("LANGLERN_WORKSPACE_DIR", workspace);
        const int stopAfter = qEnvironmentVariableIntValue("LANGLERN_OCR_BOOK_STOP_AFTER");
        BackgroundOcr enable;
        Trace trace(tracePath); QVERIFY(trace.open());
        ShellState shell;
        auto start = processSample(); start["event"] = "begin"; trace.record(start);
        shell.selectFile(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(shell.ocrBookReady(), 60000);
        trace.record({{"event", "ready"}, {"pages", shell.ocrBookPages()}, {"stored", shell.ocrBookDone()}, {"native", shell.ocrBookNative()},
            {"complete", shell.ocrBookQueue().complete()}, {"idle_ms", OcrBookConfig::idleMs}});
        QObject sink;
        connect(&shell, &ShellState::ocrBookChanged, &sink, [&] {
            static int last = -1;
            const int done = shell.ocrBookDone() + shell.ocrBookNative() + shell.ocrBookQueue().failed();
            if (done != last) { last = done; trace.record({{"event", "progress"}, {"done", shell.ocrBookDone()}, {"native", shell.ocrBookNative()}, {"failed", shell.ocrBookQueue().failed()}}); }
        });
        QElapsedTimer limit; limit.start();
        while (!shell.ocrBookQueue().complete() && limit.elapsed() < 3600000) {
            QTest::qWait(1000);
            auto row = processSample(); row["event"] = "sample"; row["done"] = shell.ocrBookDone(); row["running"] = shell.ocrBookRunning();
            trace.record(row);
            if (stopAfter > 0 && shell.ocrBookDone() >= stopAfter) {
                trace.record({{"event", "ready_to_kill"}, {"done", shell.ocrBookDone()}});
                QTest::qWait(120000); // the script kills the process here (no orderly shutdown)
                QFAIL("not killed");
            }
        }
        QVERIFY(shell.ocrBookQueue().complete());
        auto end = processSample(); end["event"] = "complete"; end["done"] = shell.ocrBookDone(); end["native"] = shell.ocrBookNative();
        end["failed"] = shell.ocrBookQueue().failed(); end["store_bytes"] = double([&] {
            qint64 n = 0; QDirIterator it(shell.ocrBookReader().directory(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) { it.next(); n += it.fileInfo().size(); } return n; }());
        int characters = 0;
        shell.ocrBookReader().forEachPage([&](const OcrBookPage& page) { characters += int(page.text->characters.size()); return true; });
        end["characters"] = characters;
        trace.record(end);
        shutdown(shell);
    }
    void benchmarkScroll() {
        const auto tracePath = qEnvironmentVariable("LANGLERN_OCR_BOOK_TRACE");
        const auto mode = qEnvironmentVariable("LANGLERN_OCR_BOOK_BENCH");
        if (tracePath.isEmpty() || (mode != "on" && mode != "off")) QSKIP("Opt-in benchmark (run-ocr-book.py)");
        const auto path = realTextbookPath();
        QVERIFY2(!path.isEmpty(), "LANGLERN_REAL_PDF required");
        const int seconds = std::max(10, qEnvironmentVariableIntValue("LANGLERN_OCR_BOOK_SECONDS"));
        BackgroundOcr enable;
        Trace trace(tracePath); QVERIFY(trace.open());
        ShellState shell;
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"shell", QVariant::fromValue(&shell)}});
        engine.loadFromModule("Langlern.Shell", "Main");
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0)); QVERIFY(window);
        window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(window));
        shell.setOcrBookEnabled(mode == "on");
        shell.selectFile(QUrl::fromLocalFile(path));
        QQuickItem* viewport = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((viewport = window->findChild<QQuickItem*>("pdfViewport")), 60000);
        QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady() && !shell.busy(), 60000);
        QTRY_VERIFY_WITH_TIMEOUT(shell.ocrBookReady(), 60000);
        shell.setOcrBookEnabled(mode == "on"); // Main.qml applies the saved preference on load
        trace.record({{"event", "begin"}, {"mode", mode}, {"pages", shell.pageCount()}, {"dpr", window->devicePixelRatio()},
            {"screen_hz", window->screen()->refreshRate()}, {"graphics_api", int(window->rendererInterface()->graphicsApi())},
            {"enabled", shell.ocrBookEnabled()}});
        // Bursts: 3 s scrolling at one viewport height per second, then 3 s still (longer than the
        // idle delay, so background OCR resumes and is mid-page when the next burst starts).
        struct Frames { std::mutex mutex; QList<double> scroll, still; qint64 previous = 0; bool scrolling = false; };
        auto frames = std::make_shared<Frames>();
        const auto frameConnection = connect(window, &QQuickWindow::frameSwapped, &shell, [frames] {
            const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            std::lock_guard lock(frames->mutex);
            if (frames->previous) (frames->scrolling ? frames->scroll : frames->still).append((now - frames->previous) / 1e6);
            frames->previous = now;
        }, Qt::DirectConnection);
        QElapsedTimer clock; clock.start();
        qint64 previousStep = clock.nsecsElapsed();
        int direction = 1, lastSecond = -1;
        QEventLoop loop;
        const auto pump = connect(window, &QQuickWindow::frameSwapped, &loop, [&] {
            const auto now = clock.nsecsElapsed();
            const double stepMs = std::min(100.0, (now - previousStep) / 1e6); previousStep = now;
            const qint64 elapsed = clock.elapsed();
            if (elapsed >= qint64(seconds) * 1000) { loop.quit(); return; }
            const bool scrolling = (elapsed / 3000) % 2 == 0;
            { std::lock_guard lock(frames->mutex); frames->scrolling = scrolling; }
            if (int(elapsed / 1000) != lastSecond) {
                lastSecond = int(elapsed / 1000);
                auto row = processSample(); row["event"] = "sample"; row["scrolling"] = scrolling;
                row["book_done"] = shell.ocrBookDone(); row["book_running"] = shell.ocrBookRunning(); row["page"] = shell.currentPage();
                row["cpu_image_bytes"] = double(shell.model().cpuBytes());
                trace.record(row);
            }
            if (scrolling) {
                const double maxY = std::max(0.0, shell.contentHeight() - viewport->height());
                double y = viewport->property("contentY").toDouble() + direction * stepMs * viewport->height() / 1000;
                if (y < 0 || y > maxY) direction = -direction;
                viewport->setProperty("contentY", std::clamp(y, 0.0, maxY));
            }
            window->update();
        }, Qt::QueuedConnection);
        window->update();
        loop.exec();
        disconnect(pump); disconnect(frameConnection);
        const auto stats = [](QList<double> v) {
            std::sort(v.begin(), v.end());
            const auto pct = [&](double p) { return v.isEmpty() ? 0.0 : v[std::max(0, int(std::ceil(p * v.size())) - 1)]; };
            return QJsonObject{{"frames", v.size()}, {"p50_ms", pct(.5)}, {"p95_ms", pct(.95)}, {"p99_ms", pct(.99)}, {"max_ms", pct(1)},
                {"over_16_67_fraction", double(std::count_if(v.begin(), v.end(), [](double t) { return t > 16.67; })) / std::max(qsizetype(1), v.size())}};
        };
        auto end = processSample(); end["event"] = "end";
        { std::lock_guard lock(frames->mutex); end["scroll"] = stats(frames->scroll); end["still"] = stats(frames->still);
          QJsonArray raw; for (double t : frames->scroll) raw.append(t); end["scroll_raw_ms"] = raw; }
        end["book_done"] = shell.ocrBookDone();
        trace.record(end);
        shutdown(shell);
    }
};
LANGLERN_TEST_SUITE(OcrBookTests, "ocrbook")
#include "ocr_book_tests.moc"
