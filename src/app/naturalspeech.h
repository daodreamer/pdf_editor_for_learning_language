#pragma once
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <cstddef>

struct SpeechControl {
    const std::atomic<quint64>& revision;
    quint64 request;
    const std::atomic<bool>& paused;
    const std::atomic<double>& rate;
    std::function<void()> firstAudio;
    std::function<void(quint64)> playbackProgress;
    bool current() const { return revision.load()==request; }
};
struct SpeechMetrics {
    double loadMs=0,firstPcmMs=0,generationMs=0,audioSeconds=0;
    quint64 peakQueueBytes=0,playedSamples=0;
    int sampleRate=0;
    bool canceled=false;
};

// Supertonic 3 French voices bundled with the application.
namespace NaturalVoices {
inline constexpr const char* male="M1";
inline constexpr const char* female="F3";
}

// Owned and invoked exclusively by OfflineServices' speech worker.
class NaturalSpeech {
public:
    static QString defaultRoot();
    static bool installed(const QString& root=defaultRoot());
    explicit NaturalSpeech(QString root=defaultRoot());
    ~NaturalSpeech();
    NaturalSpeech(const NaturalSpeech&)=delete;
    NaturalSpeech& operator=(const NaturalSpeech&)=delete;
    QString speak(const QString& text,const QString& voice,const SpeechControl& control,SpeechMetrics& metrics);
    // Generated PCM is mono float at metrics.sampleRate (filled before the first sink call).
    QString generate(const QString& text,const QString& voice,const SpeechControl& control,SpeechMetrics& metrics,
                     const std::function<bool(const float*,std::size_t)>& sink);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
