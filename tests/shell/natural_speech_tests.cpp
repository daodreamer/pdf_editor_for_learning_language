#include "testapplication.h"
#include "pocketspeech.h"
#include "offlineservices.h"
#include "shellstate.h"
#include <QClipboard>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/QQmlExtensionPlugin>
#include <algorithm>
#include <cmath>
#include <future>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <mmsystem.h>
#endif
Q_IMPORT_QML_PLUGIN(LanglernShellPlugin)

namespace {
struct Result {QString error;SpeechMetrics metrics;std::vector<float> pcm;};
struct Run {
    std::atomic<quint64> revision{1},played{0};
    std::atomic<bool> paused{false},first{false};
    std::atomic<double> rate{1};
    std::future<Result> future;
    ~Run(){++revision;paused.store(false);if(future.valid())future.wait();}
    SpeechControl control(){return {revision,1,paused,rate,[this]{first.store(true);},[this](quint64 count){played.store(count);}};}
    bool ready(){return future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready;}
};
QQuickItem* find(QQuickItem* item,const QString& name){if(item->objectName()==name)return item;for(auto* c:item->childItems())if(auto* r=find(c,name))return r;return nullptr;}
QPoint middle(QQuickItem* item){return item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();}
void wave(const QString& path,const std::vector<float>& pcm) {
    QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))return;
    QDataStream data(&file);data.setByteOrder(QDataStream::LittleEndian);
    data.writeRawData("RIFF",4);data<<quint32(36+pcm.size()*2);data.writeRawData("WAVEfmt ",8);
    data<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(24000)<<quint32(48000)<<quint16(2)<<quint16(16);
    data.writeRawData("data",4);data<<quint32(pcm.size()*2);
    for(float value:pcm)data<<qint16(std::round(std::clamp(value,-1.f,1.f)*32767.f));
}
QByteArray hash(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(&file);return hash.result();}
}

class NaturalSpeechTests : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> workspace_;
private slots:
    void initTestCase() {
#ifndef Q_OS_WIN
        QSKIP("Estelle CPU backend/playback is not yet validated on this platform");
#endif
        QVERIFY2(PocketSpeech::installed(),"Run tools/build-windows.ps1 to build and install the actual Estelle voice pack");
    }
    void init(){workspace_=std::make_unique<QTemporaryDir>();setTestEnvironment("LANGLERN_WORKSPACE_DIR",workspace_->path());}
    void nativePcmUnicodeAndIntegrity() {
        Run run;
        run.future=std::async(std::launch::async,[&]{
            PocketSpeech voice;Result result;
            result.error=voice.generate(QStringLiteral("Bonjour ! C'est assez petit pour tenir dans votre poche."),run.control(),result.metrics,[&](const float* p,std::size_t n){
                if(result.pcm.size()+n>24'000*30)return false;result.pcm.insert(result.pcm.end(),p,p+n);return true;
            });return result;
        });
        QTRY_VERIFY_WITH_TIMEOUT(run.ready(),30000);const auto normal=run.future.get();
        QVERIFY2(normal.error.isEmpty(),qPrintable(normal.error));QVERIFY(normal.pcm.size()>24000);QVERIFY(normal.metrics.firstPcmMs>0);
        double energy=0;for(float value:normal.pcm){QVERIFY(std::isfinite(value));energy+=double(value)*value;}
        QVERIFY(std::sqrt(energy/normal.pcm.size())>.005);
        qInfo("estelle_native load_ms=%.3f first_pcm_ms=%.3f generation_ms=%.3f audio_s=%.3f",normal.metrics.loadMs,normal.metrics.firstPcmMs,normal.metrics.generationMs,normal.metrics.audioSeconds);
        const auto out=qEnvironmentVariable("LANGLERN_SPEECH_ARTIFACT_DIR");if(!out.isEmpty())wave(out+"/estelle-native.wav",normal.pcm);
        const auto source=PocketSpeech::defaultRoot();const auto copy=workspace_->filePath(QStringLiteral("中文目录/法语语音包"));
        QDirIterator files(source,QDir::Files,QDirIterator::Subdirectories);
        while(files.hasNext()) {const auto file=files.next();const auto target=copy+'/'+QDir(source).relativeFilePath(file);QVERIFY(QDir().mkpath(QFileInfo(target).absolutePath()));QVERIFY(QFile::copy(file,target));}
        Run unicode;
        unicode.future=std::async(std::launch::async,[&]{PocketSpeech voice(copy);Result result;result.error=voice.generate(QStringLiteral("Bonjour Estelle."),unicode.control(),result.metrics,[](const float*,std::size_t){return true;});return result;});
        QTRY_VERIFY_WITH_TIMEOUT(unicode.ready(),30000);const auto unicodeResult=unicode.future.get();QVERIFY2(unicodeResult.error.isEmpty(),qPrintable(unicodeResult.error));
        QFile corrupt(copy+"/voices/estelle.f16");QVERIFY(corrupt.open(QIODevice::ReadWrite));QVERIFY(corrupt.seek(100));QCOMPARE(corrupt.write("bad"),qint64(3));corrupt.close();
        Run broken;
        broken.future=std::async(std::launch::async,[&]{PocketSpeech voice(copy);Result result;result.error=voice.generate("Bonjour",broken.control(),result.metrics,[](const float*,std::size_t){return true;});return result;});
        QTRY_VERIFY_WITH_TIMEOUT(broken.ready(),10000);QVERIFY(broken.future.get().error.contains(QStringLiteral("校验失败")));
    }
    void realPlaybackPauseRateAndStop() {
#ifdef Q_OS_WIN
        if(waveOutGetNumDevs()==0)QSKIP("No audio output device: actual Estelle playback/pause/stop NOT executed");
#endif
        Run run;
        run.future=std::async(std::launch::async,[&]{PocketSpeech voice;Result result;result.error=voice.speak(QStringLiteral("Bonjour à toutes et à tous. Aujourd'hui, nous allons lire un texte en français et découvrir de nouveaux mots. Prenez votre temps pour écouter cette phrase."),run.control(),result.metrics);return result;});
        QTRY_VERIFY_WITH_TIMEOUT(run.played.load()>0 || run.ready(),30000);
        if(run.ready()){const auto early=run.future.get();QFAIL(qPrintable(early.error.isEmpty()?QStringLiteral("Playback unexpectedly finished before pause test"):early.error));}
        run.paused.store(true);QTest::qWait(200);const auto paused=run.played.load();QTest::qWait(250);QCOMPARE(run.played.load(),paused);
        run.rate.store(1.5);run.paused.store(false);QTRY_VERIFY_WITH_TIMEOUT(run.played.load()>paused,5000);
        QElapsedTimer stop;stop.start();++run.revision;
        QTRY_VERIFY_WITH_TIMEOUT(run.ready(),4000);const auto result=run.future.get();
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(result.metrics.canceled);QVERIFY(result.metrics.playedSamples>0);QVERIFY(result.metrics.peakQueueBytes<=2*1024*1024);
        qInfo("estelle_stop_completion_ms=%lld played_samples=%llu queue_peak_bytes=%llu",stop.elapsed(),result.metrics.playedSamples,result.metrics.peakQueueBytes);
    }
    void shortSentencesAndRequestReuse() {
        Run run;
        run.future=std::async(std::launch::async,[&]{
            PocketSpeech voice;Result result;
            const QStringList sentences={QStringLiteral("Bonjour Estelle, je parle français."),QStringLiteral("Bonjour Estelle."),QStringLiteral("C'est assez petit pour tenir dans votre poche.")};
            for(int round=0;round<3;++round)for(int index=0;index<sentences.size();++index) {
                SpeechMetrics metrics;std::vector<float> pcm;
                result.error=voice.generate(sentences[index],run.control(),metrics,[&](const float* p,std::size_t n){pcm.insert(pcm.end(),p,p+n);return pcm.size()<=24000*20;});
                if(!result.error.isEmpty())return result;
                double energy=0;for(float value:pcm){if(!std::isfinite(value)){result.error="Non-finite PCM";return result;}energy+=double(value)*value;}
                if(pcm.size()<24000 || std::sqrt(energy/pcm.size())<.005){result.error="Short speech ended prematurely or produced silence";return result;}
                const auto out=qEnvironmentVariable("LANGLERN_SPEECH_ARTIFACT_DIR");
                if(!out.isEmpty())wave(out+QStringLiteral("/estelle-short-%1-%2.wav").arg(round).arg(index),pcm);
                qInfo("estelle_short round=%d sentence=%d load_ms=%.3f first_pcm_ms=%.3f generation_ms=%.3f audio_s=%.3f",round,index,metrics.loadMs,metrics.firstPcmMs,metrics.generationMs,metrics.audioSeconds);
            }
            return result;
        });
        QTRY_VERIFY_WITH_TIMEOUT(run.ready(),60000);QVERIFY2(run.future.get().error.isEmpty(),"Repeated short speech failed; inspect generated PCM artifacts");
    }
    void realTextbookAndAnswerButtons() {
#ifdef Q_OS_WIN
        if(waveOutGetNumDevs()==0)QSKIP("No audio output device: real textbook speech button playback NOT executed");
#endif
        auto path=qEnvironmentVariable("LANGLERN_REAL_PDF");if(path.isEmpty())path=QStringLiteral("C:/Users/jiong/Desktop/法语/你好法语3学生用书第二版.pdf");
        if(!QFileInfo::exists(path))QSKIP("Fixed real textbook unavailable: real textbook speech validation NOT executed");
        const auto original=hash(path);QVERIFY(!original.isEmpty());
        ShellState shell;QQmlApplicationEngine engine;QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& error:errors)warnings.append(error.toString());});
        engine.setInitialProperties({{"shell",QVariant::fromValue(&shell)}});engine.loadFromModule("Langlern.Shell","Main");
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));QVERIFY(window);window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* service=window->findChild<OfflineServices*>();QVERIFY(service);QTRY_VERIFY(!service->speechVoices().isEmpty());
        bool estelle=false;for(const auto& value:service->speechVoices())estelle|=value.toMap()["id"].toString()=="pocket:fr:estelle";QVERIFY(estelle);
        shell.selectFile(QUrl::fromLocalFile(path));QTRY_VERIFY_WITH_TIMEOUT(shell.storageReady(),30000);QTRY_VERIFY(shell.pageCount()>34);shell.goToPage(34);QTRY_VERIFY(!shell.busy());
        shell.recognizeCurrentPage();QTRY_VERIFY_WITH_TIMEOUT(shell.textSelection()->snapshot(33) && shell.textSelection()->snapshot(33)->ocr && !shell.textSelection()->snapshot(33)->pending,60000);
        const auto text=shell.textSelection()->snapshot(33);QVERIFY(text->error.isEmpty());QVERIFY(text->characters.size()>100);
        int first=0;while(first<text->characters.size()&&!text->characters[first].visible)++first;const int last=std::min(first+75,int(text->characters.size())-1);
        auto* viewport=find(window->contentItem(),"pdfViewport");QVERIFY(viewport);
        const auto scene=[&](QPointF p){return viewport->mapToScene(QPointF(shell.pageLeft(33)+p.x()*shell.pageWidth()-viewport->property("contentX").toDouble(),shell.pageTop(33)+p.y()*shell.pageHeight(33)-viewport->property("contentY").toDouble())).toPoint();};
        const auto a=scene(text->characters[first].box.center()),b=scene(text->characters[last].box.center());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,a,1);QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,a,1);QTest::mouseMove(window,b,25);QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,b);
        QTRY_VERIFY(shell.textSelection()->ready());const auto selected=shell.textSelection()->text();QVERIFY(selected.size()>20);
        auto* button=find(window->contentItem(),"speakPageSelection");QVERIFY(button);QTRY_VERIFY(button->isVisible());QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,middle(button));
        QTRY_VERIFY(service->speaking());QTRY_VERIFY_WITH_TIMEOUT(!service->speechLoading(),30000);QVERIFY2(service->speaking(),qPrintable(service->status()));QCOMPARE(shell.textSelection()->text(),selected);QTest::qWait(500);service->stopSpeaking();
        shell.createTextBox(shell.pageLeft(33)+.63*shell.pageWidth(),shell.pageTop(33)+.25*shell.pageHeight(33));
        auto* editor=find(window->contentItem(),"annotationEditor");QVERIFY(editor);editor->forceActiveFocus();
        QGuiApplication::clipboard()->setText(QStringLiteral("Bonjour Estelle, je parle français."));QTest::keyClick(window,Qt::Key_V,Qt::ControlModifier);
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);QTRY_VERIFY(!editor->property("selectedText").toString().isEmpty());
        button=find(window->contentItem(),"speakEditorSelection");QVERIFY(button);QTRY_VERIFY(button->isVisible());QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,middle(button));
        QTRY_VERIFY(service->speaking());QTRY_VERIFY_WITH_TIMEOUT(!service->speechLoading(),30000);QVERIFY2(service->speaking(),qPrintable(service->status()));QVERIFY(editor->hasActiveFocus());QTest::qWait(500);
        const auto out=qEnvironmentVariable("LANGLERN_SPEECH_ARTIFACT_DIR");if(!out.isEmpty())QVERIFY(window->grabWindow().save(out+"/real-textbook-estelle.png"));
        window->close();QTRY_VERIFY(!service->speaking());QTRY_VERIFY(shell.shutdownComplete());
        QCOMPARE(hash(path),original);QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
};
int main(int argc,char** argv){QGuiApplication app(argc,argv);configureTestApplication(app);NaturalSpeechTests tests;return QTest::qExec(&tests,argc,argv);}
#include "natural_speech_tests.moc"
