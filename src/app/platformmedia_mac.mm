#include "platformmedia.h"
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace PlatformMedia {
namespace {
NSString* ns(const QString& value) { return value.toNSString(); }
QString qt(NSString* value) { return value ? QString::fromNSString(value) : QString{}; }
QString describe(NSError* error) { return error ? qt(error.localizedDescription) : QString{}; }
}

// ---- Local audio ------------------------------------------------------------------------------
struct AudioPlayer::Impl { AVAudioPlayer* player=nil; };
AudioPlayer::AudioPlayer():impl_(std::make_unique<Impl>()) {}
AudioPlayer::~AudioPlayer() { [impl_->player stop]; impl_->player=nil; }
QString AudioPlayer::open(const QString& path,double& durationSeconds) {
    @autoreleasepool {
        [impl_->player stop]; impl_->player=nil; durationSeconds=0;
        NSError* error=nil;
        AVAudioPlayer* player=[[AVAudioPlayer alloc] initWithContentsOfURL:[NSURL fileURLWithPath:ns(path)] error:&error];
        if(!player) return describe(error).isEmpty() ? QStringLiteral("unsupported audio") : describe(error);
        player.enableRate=YES;  // must precede prepareToPlay for variable speed
        if(![player prepareToPlay]) return QStringLiteral("cannot prepare audio");
        impl_->player=player; durationSeconds=player.duration;
        return {};
    }
}
bool AudioPlayer::isOpen() const { return impl_->player!=nil; }
bool AudioPlayer::play() { return impl_->player && [impl_->player play]; }
void AudioPlayer::pause() { [impl_->player pause]; }
void AudioPlayer::seek(double seconds) { if(impl_->player) impl_->player.currentTime=std::clamp(seconds,0.0,impl_->player.duration); }
bool AudioPlayer::setRate(double rate) {
    if(!impl_->player || !impl_->player.enableRate) return false;
    impl_->player.rate=float(std::clamp(rate,.5,2.0)); return true;
}
double AudioPlayer::position() const { return impl_->player ? impl_->player.currentTime : 0; }
bool AudioPlayer::playing() const { return impl_->player && impl_->player.playing; }

// ---- System speech ----------------------------------------------------------------------------
QList<Voice> systemVoices() {
    @autoreleasepool {
        QList<Voice> voices;
        for(AVSpeechSynthesisVoice* voice in [AVSpeechSynthesisVoice speechVoices]) {
            if(@available(macOS 14.0,*)) { if(voice.voiceTraits & AVSpeechSynthesisVoiceTraitIsNoveltyVoice) continue; }
            const auto locale=qt(voice.language);
            voices.append({qt(voice.identifier),qt(voice.name)+QStringLiteral(" · ")+locale,locale});
        }
        std::stable_sort(voices.begin(),voices.end(),[](const Voice& a,const Voice& b){return a.locale<b.locale;});
        return voices;
    }
}
struct Speaker::Impl { AVSpeechSynthesizer* synthesizer=[[AVSpeechSynthesizer alloc] init]; };
Speaker::Speaker():impl_(std::make_unique<Impl>()) {}
Speaker::~Speaker() { [impl_->synthesizer stopSpeakingAtBoundary:AVSpeechBoundaryImmediate]; impl_->synthesizer=nil; }
bool Speaker::speak(const QString& voiceId,const QString& text,double rate) {
    @autoreleasepool {
        AVSpeechSynthesisVoice* voice=[AVSpeechSynthesisVoice voiceWithIdentifier:ns(voiceId)];
        if(!voice) return false;
        AVSpeechUtterance* utterance=[AVSpeechUtterance speechUtteranceWithString:ns(text)];
        utterance.voice=voice;
        // AVSpeech rates are perceptual, not multiples: 0.5 is normal speech and 1.0 very fast. A square
        // root keeps the application's 0.5–2.0 range near half to double speed.
        const double scaled=AVSpeechUtteranceDefaultSpeechRate*std::sqrt(std::clamp(rate,.5,2.0));
        utterance.rate=float(std::clamp(scaled,double(AVSpeechUtteranceMinimumSpeechRate),double(AVSpeechUtteranceMaximumSpeechRate)));
        [impl_->synthesizer speakUtterance:utterance];
        return true;
    }
}
bool Speaker::speaking() const { return impl_->synthesizer.speaking; }
void Speaker::pause() { [impl_->synthesizer pauseSpeakingAtBoundary:AVSpeechBoundaryImmediate]; }
void Speaker::resume() { [impl_->synthesizer continueSpeaking]; }
void Speaker::stop() { [impl_->synthesizer stopSpeakingAtBoundary:AVSpeechBoundaryImmediate]; }

// ---- Text recognition -------------------------------------------------------------------------
QStringList recognitionLanguages() {
    @autoreleasepool {
        VNRecognizeTextRequest* request=[[VNRecognizeTextRequest alloc] init];
        request.recognitionLevel=VNRequestTextRecognitionLevelAccurate;
        NSError* error=nil;
        QStringList languages;
        for(NSString* language in [request supportedRecognitionLanguagesAndReturnError:&error]) languages.append(qt(language));
        return languages;
    }
}
QString recognizeText(const QImage& image,const QString& language,QString& text,QVariantList& words) {
    @autoreleasepool {
        const auto pixels=image.convertToFormat(QImage::Format_ARGB32);
        CGImageRef cgImage=pixels.toCGImage();
        if(!cgImage) return QStringLiteral("cannot convert image");
        VNRecognizeTextRequest* request=[[VNRecognizeTextRequest alloc] init];
        request.recognitionLevel=VNRequestTextRecognitionLevelAccurate;
        request.usesLanguageCorrection=YES;
        if(language.isEmpty()) request.automaticallyDetectsLanguage=YES;
        else request.recognitionLanguages=@[ns(language)];
        VNImageRequestHandler* handler=[[VNImageRequestHandler alloc] initWithCGImage:cgImage options:@{}];
        NSError* error=nil;
        const bool ok=[handler performRequests:@[request] error:&error];
        CGImageRelease(cgImage);
        if(!ok) return describe(error).isEmpty() ? QStringLiteral("recognition failed") : describe(error);
        QStringList lines;
        for(VNRecognizedTextObservation* observation in request.results) {
            VNRecognizedText* candidate=[observation topCandidates:1].firstObject;
            if(!candidate) continue;
            NSString* line=candidate.string;
            const auto context=qt(line);
            lines.append(context);
            [line enumerateSubstringsInRange:NSMakeRange(0,line.length) options:NSStringEnumerationByWords
                usingBlock:^(NSString* word,NSRange range,NSRange,BOOL*) {
                    NSError* boxError=nil;
                    VNRectangleObservation* box=[candidate boundingBoxForRange:range error:&boxError];
                    if(!box) return;
                    const CGRect r=box.boundingBox;  // normalized, origin bottom-left
                    words.append(QVariantMap{{"word",qt(word)},{"context",context},{"x",double(r.origin.x)},
                        {"y",1.0-double(r.origin.y+r.size.height)},{"w",double(r.size.width)},{"h",double(r.size.height)}});
                }];
        }
        text=lines.join(u'\n');
        return {};
    }
}

}
