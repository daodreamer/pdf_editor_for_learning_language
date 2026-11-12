#include "testsupport.h"
#include "naturalspeech.h"
#include "offlineservices.h"
#include "workspacestate.h"
#include <QDataStream>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <future>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <mmsystem.h>
#endif

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
void wave(const QString& path,const std::vector<float>& pcm,int rate) {
    QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))return;
    QDataStream data(&file);data.setByteOrder(QDataStream::LittleEndian);
    data.writeRawData("RIFF",4);data<<quint32(36+pcm.size()*2);data.writeRawData("WAVEfmt ",8);
    data<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(rate)<<quint32(rate*2)<<quint16(2)<<quint16(16);
    data.writeRawData("data",4);data<<quint32(pcm.size()*2);
    for(float value:pcm)data<<qint16(std::round(std::clamp(value,-1.f,1.f)*32767.f));
}
// Same gate as the backend's normalizer: mean energy of 20 ms frames above -45 dBFS.
struct Loudness {double activeDbfs=-120,peakDbfs=-120;};
Loudness loudness(const std::vector<float>& pcm,int rate) {
    const std::size_t frame=std::size_t(rate/50);double energy=0;std::size_t active=0;float peak=0;
    for(float value:pcm)peak=std::max(peak,std::abs(value));
    for(std::size_t start=0;start+frame<=pcm.size();start+=frame) {
        double sum=0;for(std::size_t i=start;i<start+frame;++i)sum+=double(pcm[i])*pcm[i];
        if(sum/frame>std::pow(10.0,-4.5)){energy+=sum;active+=frame;}
    }
    return {active?10*std::log10(energy/active):-120,20*std::log10(peak+1e-9)};
}
QString check(const std::vector<float>& pcm,int rate,double minimumSeconds) {
    for(float value:pcm)if(!std::isfinite(value))return QStringLiteral("Non-finite PCM");
    if(rate!=44100)return QStringLiteral("Unexpected sample rate %1").arg(rate);
    if(pcm.size()<std::size_t(minimumSeconds*rate))return QStringLiteral("Speech ended prematurely: %1 samples").arg(pcm.size());
    const auto level=loudness(pcm,rate);
    if(level.activeDbfs<-19 || level.activeDbfs>-13)return QStringLiteral("Loudness not normalized: %1 dBFS").arg(level.activeDbfs);
    if(level.peakDbfs>-0.99)return QStringLiteral("Peak exceeds ceiling: %1 dBFS").arg(level.peakDbfs);
    return {};
}
}

class NaturalSpeechTests : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> workspace_;
private slots:
    void initTestCase() {
#ifndef Q_OS_WIN
        QSKIP("Supertonic CPU backend/playback is not yet validated on this platform");
#endif
        QVERIFY2(NaturalSpeech::installed(),"Run tools/build-windows.ps1 to build and install the actual Supertonic French voice pack");
    }
    void init(){workspace_=std::make_unique<QTemporaryDir>();setTestEnvironment("LANGLERN_WORKSPACE_DIR",workspace_->path());}
    void nativePcmBothVoicesUnicodeAndIntegrity() {
        for(const char* voiceName:{NaturalVoices::male,NaturalVoices::female}) {
            Run run;const QString voice=QString::fromLatin1(voiceName);
            run.future=std::async(std::launch::async,[&]{
                NaturalSpeech speech;Result result;
                result.error=speech.generate(QStringLiteral("Bonjour ! Nous allons au cinéma avec nos amis samedi soir."),voice,run.control(),result.metrics,[&](const float* p,std::size_t n){
                    if(result.pcm.size()+n>44'100*30)return false;result.pcm.insert(result.pcm.end(),p,p+n);return true;
                });return result;
            });
            QTRY_VERIFY_WITH_TIMEOUT(run.ready(),60000);const auto normal=run.future.get();
            QVERIFY2(normal.error.isEmpty(),qPrintable(normal.error));QVERIFY(normal.metrics.firstPcmMs>0);
            const auto problem=check(normal.pcm,normal.metrics.sampleRate,1.5);QVERIFY2(problem.isEmpty(),qPrintable(problem));
            const auto level=loudness(normal.pcm,normal.metrics.sampleRate);
            qInfo("speech_native voice=%s load_ms=%.3f first_pcm_ms=%.3f generation_ms=%.3f audio_s=%.3f active_dbfs=%.2f peak_dbfs=%.2f",
                  voiceName,normal.metrics.loadMs,normal.metrics.firstPcmMs,normal.metrics.generationMs,normal.metrics.audioSeconds,level.activeDbfs,level.peakDbfs);
            const auto out=qEnvironmentVariable("LANGLERN_SPEECH_ARTIFACT_DIR");if(!out.isEmpty())wave(out+QStringLiteral("/native-%1.wav").arg(voice),normal.pcm,normal.metrics.sampleRate);
        }
        Run unknown;
        unknown.future=std::async(std::launch::async,[&]{NaturalSpeech speech;Result result;result.error=speech.generate("Bonjour",QStringLiteral("X9"),unknown.control(),result.metrics,[](const float*,std::size_t){return true;});return result;});
        QTRY_VERIFY_WITH_TIMEOUT(unknown.ready(),10000);QVERIFY(!unknown.future.get().error.isEmpty());
        const auto source=NaturalSpeech::defaultRoot();const auto copy=workspace_->filePath(QStringLiteral("中文目录/法语语音包"));
        QDirIterator files(source,QDir::Files,QDirIterator::Subdirectories);
        while(files.hasNext()) {const auto file=files.next();const auto target=copy+'/'+QDir(source).relativeFilePath(file);QVERIFY(QDir().mkpath(QFileInfo(target).absolutePath()));QVERIFY(QFile::copy(file,target));}
        Run unicode;
        unicode.future=std::async(std::launch::async,[&]{NaturalSpeech speech(copy);Result result;result.error=speech.generate(QStringLiteral("Bonjour, ça va ?"),QString::fromLatin1(NaturalVoices::male),unicode.control(),result.metrics,[](const float*,std::size_t){return true;});return result;});
        QTRY_VERIFY_WITH_TIMEOUT(unicode.ready(),60000);const auto unicodeResult=unicode.future.get();QVERIFY2(unicodeResult.error.isEmpty(),qPrintable(unicodeResult.error));
        QFile corrupt(copy+"/voices/M1.json");QVERIFY(corrupt.open(QIODevice::ReadWrite));QVERIFY(corrupt.seek(100));QCOMPARE(corrupt.write("bad"),qint64(3));corrupt.close();
        Run broken;
        broken.future=std::async(std::launch::async,[&]{NaturalSpeech speech(copy);Result result;result.error=speech.generate("Bonjour",QString::fromLatin1(NaturalVoices::male),broken.control(),result.metrics,[](const float*,std::size_t){return true;});return result;});
        QTRY_VERIFY_WITH_TIMEOUT(broken.ready(),20000);QVERIFY(broken.future.get().error.contains(QStringLiteral("校验失败")));
    }
    void realPlaybackPauseRateAndStop() {
#ifdef Q_OS_WIN
        if(waveOutGetNumDevs()==0)QSKIP("No audio output device: actual natural speech playback/pause/stop NOT executed");
#endif
        Run run;
        run.future=std::async(std::launch::async,[&]{NaturalSpeech speech;Result result;result.error=speech.speak(QStringLiteral("Bonjour à toutes et à tous. Aujourd'hui, nous allons lire un texte en français et découvrir de nouveaux mots. Prenez votre temps pour écouter cette phrase."),QString::fromLatin1(NaturalVoices::male),run.control(),result.metrics);return result;});
        QTRY_VERIFY_WITH_TIMEOUT(run.played.load()>0 || run.ready(),60000);
        if(run.ready()){const auto early=run.future.get();QFAIL(qPrintable(early.error.isEmpty()?QStringLiteral("Playback unexpectedly finished before pause test"):early.error));}
        run.paused.store(true);QTest::qWait(200);const auto paused=run.played.load();QTest::qWait(250);QCOMPARE(run.played.load(),paused);
        run.rate.store(1.5);run.paused.store(false);QTRY_VERIFY_WITH_TIMEOUT(run.played.load()>paused,5000);
        QElapsedTimer stop;stop.start();++run.revision;
        QTRY_VERIFY_WITH_TIMEOUT(run.ready(),4000);const auto result=run.future.get();
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(result.metrics.canceled);QVERIFY(result.metrics.playedSamples>0);QVERIFY(result.metrics.peakQueueBytes<=2*1024*1024);
        qInfo("speech_stop_completion_ms=%lld played_samples=%llu queue_peak_bytes=%llu",stop.elapsed(),result.metrics.playedSamples,result.metrics.peakQueueBytes);
    }
    void shortSentencesAndRequestReuse() {
        Run run;
        run.future=std::async(std::launch::async,[&]{
            NaturalSpeech speech;Result result;
            const QStringList sentences={QStringLiteral("Bonjour, je m'appelle Léa."),QStringLiteral("Merci beaucoup !"),QStringLiteral("Écoutez et répétez.")};
            // Three requests on one engine cover reuse; benchmarks/run-speech.py asks for more rounds.
            const int rounds=std::max(1,qEnvironmentVariableIntValue("LANGLERN_SPEECH_ROUNDS"));
            for(int round=0;round<rounds;++round)for(int index=0;index<sentences.size();++index) {
                const auto voice=QString::fromLatin1(index%2?NaturalVoices::female:NaturalVoices::male);
                SpeechMetrics metrics;std::vector<float> pcm;
                result.error=speech.generate(sentences[index],voice,run.control(),metrics,[&](const float* p,std::size_t n){pcm.insert(pcm.end(),p,p+n);return pcm.size()<=44100*20;});
                if(!result.error.isEmpty())return result;
                if(const auto problem=check(pcm,metrics.sampleRate,0.6);!problem.isEmpty()){result.error=problem;return result;}
                const auto out=qEnvironmentVariable("LANGLERN_SPEECH_ARTIFACT_DIR");
                if(!out.isEmpty())wave(out+QStringLiteral("/short-%1-%2-%3.wav").arg(round).arg(index).arg(voice),pcm,metrics.sampleRate);
                qInfo("speech_short round=%d sentence=%d voice=%s load_ms=%.3f first_pcm_ms=%.3f generation_ms=%.3f audio_s=%.3f",round,index,qPrintable(voice),metrics.loadMs,metrics.firstPcmMs,metrics.generationMs,metrics.audioSeconds);
            }
            return result;
        });
        QTRY_VERIFY_WITH_TIMEOUT(run.ready(),120000);const auto result=run.future.get();QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
    }
    void voicePreferencePersists() {
        {WorkspaceState workspace;QCOMPARE(workspace.frenchVoice(),QStringLiteral("male"));workspace.setFrenchVoice(QStringLiteral("female"));workspace.setFrenchVoice(QStringLiteral("bogus"));QCOMPARE(workspace.frenchVoice(),QStringLiteral("male"));workspace.setFrenchVoice(QStringLiteral("female"));}
        WorkspaceState reloaded;QCOMPARE(reloaded.frenchVoice(),QStringLiteral("female"));
        OfflineServices service;QCOMPARE(service.frenchVoice(),QStringLiteral("male"));
        service.setSpeechVoice(QStringLiteral("natural:fr:female"));QCOMPARE(service.frenchVoice(),QStringLiteral("female"));
        service.setFrenchVoice(QStringLiteral("male"));QCOMPARE(service.speechVoice(),QStringLiteral("natural:fr:male"));
    }
};
LANGLERN_TEST_SUITE(NaturalSpeechTests,"speech")
#include "natural_speech_tests.moc"
