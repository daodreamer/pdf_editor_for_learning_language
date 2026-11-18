#pragma once
// macOS system services behind OfflineServices: local audio (AVFoundation), system speech
// (AVSpeechSynthesizer) and text recognition (Vision). All are offline. Each object is created,
// used and destroyed on one OfflineServices worker thread; none is thread-safe on its own.
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <memory>

namespace PlatformMedia {

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&)=delete;
    AudioPlayer& operator=(const AudioPlayer&)=delete;
    // Replaces any open file. Returns an empty string on success.
    QString open(const QString& path,double& durationSeconds);
    bool isOpen() const;
    bool play();
    void pause();
    void seek(double seconds);
    bool setRate(double rate);
    double position() const;
    bool playing() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct Voice { QString id,name,locale; };
QList<Voice> systemVoices();

class Speaker {
public:
    Speaker();
    ~Speaker();
    Speaker(const Speaker&)=delete;
    Speaker& operator=(const Speaker&)=delete;
    // Queues one utterance; rate uses the application's 0.5–2.0 scale.
    bool speak(const QString& voiceId,const QString& text,double rate);
    bool speaking() const;
    void pause();
    void resume();
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

QStringList recognitionLanguages();
// Coordinates in words are normalized to the image, origin top-left (as with Windows OCR).
QString recognizeText(const QImage& image,const QString& language,QString& text,QVariantList& words);

}
