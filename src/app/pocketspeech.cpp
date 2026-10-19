#include "pocketspeech.h"
#include "../speech/pocketapi.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QThread>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#endif

namespace {
constexpr qint64 queueBudget=2*1024*1024;
constexpr unsigned sampleRate=24000;
using Clock=std::chrono::steady_clock;
void briefWait() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
template<class Function> Function resolve(QLibrary& library,const char* name) {
    auto function=reinterpret_cast<Function>(library.resolve(name));
    if(!function)throw std::runtime_error((QString::fromLatin1(name)+": "+library.errorString()).toStdString());
    return function;
}
bool waitWhilePaused(const SpeechControl& control) {
    while(control.current() && control.paused.load())briefWait();
    return control.current();
}
struct TempoApi {
    using Create=void*(*)();using Destroy=void(*)(void*);
    Create create=nullptr;Destroy destroy=nullptr;
    void (*setTempo)(void*,float)=nullptr;
    int (*setRate)(void*,unsigned)=nullptr;
    int (*setChannels)(void*,unsigned)=nullptr;
    int (*put)(void*,const float*,unsigned)=nullptr;
    unsigned (*receive)(void*,float*,unsigned)=nullptr;
    int (*flush)(void*)=nullptr;
    void load(QLibrary& library) {
        create=resolve<Create>(library,"soundtouch_createInstance");destroy=resolve<Destroy>(library,"soundtouch_destroyInstance");
        setTempo=resolve<decltype(setTempo)>(library,"soundtouch_setTempo");
        setRate=resolve<decltype(setRate)>(library,"soundtouch_setSampleRate");
        setChannels=resolve<decltype(setChannels)>(library,"soundtouch_setChannels");
        put=resolve<decltype(put)>(library,"soundtouch_putSamples");receive=resolve<decltype(receive)>(library,"soundtouch_receiveSamples");
        flush=resolve<decltype(flush)>(library,"soundtouch_flush");
    }
};
class Tempo {
    const TempoApi& api_;void* handle_=nullptr;
public:
    explicit Tempo(const TempoApi& api):api_(api),handle_(api.create()) {
        if(!handle_)throw std::runtime_error("Cannot create speech tempo processor");
        if(!api_.setChannels(handle_,1) || !api_.setRate(handle_,sampleRate)) {
            api_.destroy(handle_);handle_=nullptr;throw std::runtime_error("Cannot configure speech tempo processor");
        }
    }
    ~Tempo(){if(handle_)api_.destroy(handle_);}
    void put(const std::vector<float>& values,double rate) {
        api_.setTempo(handle_,float(std::clamp(rate,.5,2.0)));
        if(!api_.put(handle_,values.data(),unsigned(values.size())))throw std::runtime_error("Speech tempo input failed");
    }
    void flush(){if(!api_.flush(handle_))throw std::runtime_error("Speech tempo flush failed");}
    unsigned receive(float* data,unsigned capacity){return api_.receive(handle_,data,capacity);}
};

#ifdef Q_OS_WIN
void checkAudio(MMRESULT result) {
    if(result==MMSYSERR_NOERROR)return;
    wchar_t message[256]{};waveOutGetErrorTextW(result,message,256);
    throw std::runtime_error(QString::fromWCharArray(message).toStdString());
}
class WaveOutput {
    struct Block {std::vector<short> samples;WAVEHDR header{};bool prepared=false;};
    HWAVEOUT device_=nullptr;
    std::deque<std::unique_ptr<Block>> blocks_;
    const SpeechControl& control_;
    SpeechMetrics& metrics_;
    const std::atomic<bool>& aborted_;
    bool paused_=false;
    void collect() {
        while(!blocks_.empty() && (blocks_.front()->header.dwFlags&WHDR_DONE)) {
            auto& block=*blocks_.front();checkAudio(waveOutUnprepareHeader(device_,&block.header,sizeof(WAVEHDR)));block.prepared=false;
            metrics_.playedSamples+=block.samples.size();blocks_.pop_front();
            if(control_.playbackProgress)control_.playbackProgress(metrics_.playedSamples);
        }
    }
public:
    WaveOutput(const SpeechControl& control,SpeechMetrics& metrics,const std::atomic<bool>& aborted):control_(control),metrics_(metrics),aborted_(aborted) {
        WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=1;format.nSamplesPerSec=sampleRate;
        format.wBitsPerSample=16;format.nBlockAlign=2;format.nAvgBytesPerSec=sampleRate*2;
        checkAudio(waveOutOpen(&device_,WAVE_MAPPER,&format,0,0,CALLBACK_NULL));
    }
    ~WaveOutput() {
        if(!device_)return;
        waveOutReset(device_);
        for(auto& block:blocks_)if(block->prepared)waveOutUnprepareHeader(device_,&block->header,sizeof(WAVEHDR));
        blocks_.clear();waveOutClose(device_);
    }
    bool poll() {
        if(!control_.current() || aborted_.load())return false;
        const bool pause=control_.paused.load();
        if(pause!=paused_) {checkAudio(pause?waveOutPause(device_):waveOutRestart(device_));paused_=pause;}
        collect();return true;
    }
    bool write(const float* samples,std::size_t count) {
        for(std::size_t offset=0;offset<count;) {
            while(control_.current() && (blocks_.size()>=8 || control_.paused.load())) {if(!poll())return false;briefWait();}
            if(!poll())return false;
            auto block=std::make_unique<Block>();const auto length=std::min(std::size_t(960),count-offset);
            block->samples.resize(length);
            for(std::size_t i=0;i<length;++i) {
                const auto value=samples[offset+i];block->samples[i]=std::isfinite(value)?short(std::round(std::clamp(value,-1.f,1.f)*32767.f)):0;
            }
            block->header.lpData=reinterpret_cast<char*>(block->samples.data());block->header.dwBufferLength=DWORD(length*sizeof(short));
            checkAudio(waveOutPrepareHeader(device_,&block->header,sizeof(WAVEHDR)));block->prepared=true;
            // Keep the prepared buffer alive even if waveOutWrite fails.
            blocks_.push_back(std::move(block));checkAudio(waveOutWrite(device_,&blocks_.back()->header,sizeof(WAVEHDR)));offset+=length;
        }
        return true;
    }
    void drain() {while(!blocks_.empty() && poll())briefWait();}
};
#endif
}

QString PocketSpeech::defaultRoot() {
    const auto root=qEnvironmentVariable("LANGLERN_POCKET_DIR");
    return root.isEmpty()?QCoreApplication::applicationDirPath()+"/voices/pocket-fr-estelle-v2":root;
}
bool PocketSpeech::installed(const QString& root) {
#ifdef Q_OS_WIN
    return QFileInfo::exists(root+"/manifest.json") && QFileInfo::exists(root+"/langlern_pocket.dll")
        && QFileInfo::exists(root+"/onnxruntime.dll") && QFileInfo::exists(root+"/SoundTouchDLL.dll");
#else
    Q_UNUSED(root);return false;
#endif
}
struct PocketSpeech::Impl {
    QString root;
    QThread* owner=QThread::currentThread();
    QLibrary ort,backend,tempoLibrary;
    PocketCreate create=nullptr;PocketGenerate generate=nullptr;PocketDestroy destroy=nullptr;
    TempoApi tempo;
    void* model=nullptr;
    explicit Impl(QString path):root(std::move(path)) {}
    ~Impl(){Q_ASSERT(owner==QThread::currentThread());if(model)destroy(model);backend.unload();tempoLibrary.unload();ort.unload();}
    QString load(const SpeechControl& control,SpeechMetrics& metrics) {
        Q_ASSERT(owner==QThread::currentThread());QElapsedTimer timer;timer.start();
        if(model){metrics.loadMs=0;return {};}
        if(!PocketSpeech::installed(root))return QCoreApplication::translate("PocketSpeech","Estelle 自然语音包未安装或不完整。");
        QFile expected(":/speech/pocket.lock.json");
        if(!expected.open(QIODevice::ReadOnly))return QStringLiteral("Missing embedded Pocket model manifest");
        const auto manifest=QJsonDocument::fromJson(expected.readAll()).object();
        const auto canonical=QFileInfo(root).canonicalFilePath()+'/';
        for(const auto& value:manifest.value("files").toArray()) {
            const auto item=value.toObject();const auto relative=item.value("file").toString();QFile file(root+'/'+relative);
            const auto resolved=QFileInfo(file).canonicalFilePath();
            if(relative.isEmpty() || !resolved.startsWith(canonical,Qt::CaseInsensitive) || !file.open(QIODevice::ReadOnly) || file.size()!=item.value("bytes").toInteger())
                return QCoreApplication::translate("PocketSpeech","自然语音包文件缺失或损坏：%1").arg(relative);
            QCryptographicHash hash(QCryptographicHash::Sha256);
            while(!file.atEnd()) {
                if(!control.current()){metrics.canceled=true;return {};}
                const auto bytes=file.read(1024*1024);if(bytes.isEmpty() && file.error()!=QFileDevice::NoError)return file.errorString();hash.addData(bytes);
            }
            if(hash.result().toHex()!=item.value("sha256").toString().toLatin1())return QCoreApplication::translate("PocketSpeech","自然语音包校验失败：%1").arg(relative);
        }
        if(!control.current()){metrics.canceled=true;return {};}
        try {
            ort.setFileName(root+"/onnxruntime.dll");if(!ort.load())return ort.errorString();
            backend.setFileName(root+"/langlern_pocket.dll");if(!backend.load())return backend.errorString();
            tempoLibrary.setFileName(root+"/SoundTouchDLL.dll");if(!tempoLibrary.load())return tempoLibrary.errorString();
            create=resolve<PocketCreate>(backend,"langlernPocketCreate");generate=resolve<PocketGenerate>(backend,"langlernPocketGenerate");destroy=resolve<PocketDestroy>(backend,"langlernPocketDestroy");
            tempo.load(tempoLibrary);std::array<char,2048> error{};
            model=create(root.toUtf8().constData(),error.data(),error.size());
            if(!model)return QString::fromUtf8(error.data());
        }catch(const std::exception& e){return QString::fromUtf8(e.what());}
        metrics.loadMs=timer.nsecsElapsed()/1e6;return {};
    }
    QString stream(const QString& text,const SpeechControl& control,SpeechMetrics& metrics,const std::function<bool(const float*,std::size_t)>& sink) {
        if(!control.current()){metrics.canceled=true;return {};}
        QElapsedTimer timer;timer.start();
        struct Context {const SpeechControl& control;SpeechMetrics& metrics;const std::function<bool(const float*,std::size_t)>& sink;QElapsedTimer& timer;bool first=true;quint64 samples=0;};
        Context context{control,metrics,sink,timer};std::array<char,2048> error{};
        const auto output=[](const float* values,std::size_t count,void* opaque)->int {
            auto& c=*static_cast<Context*>(opaque);
            if(!c.control.current())return 0;
            if(count>256*1024)return 0;
            if(c.first){c.first=false;c.metrics.firstPcmMs=c.metrics.loadMs+c.timer.nsecsElapsed()/1e6;if(c.control.firstAudio)c.control.firstAudio();}
            c.samples+=count;
            try{return c.sink(values,count)?1:0;}catch(...){return 0;}
        };
        const auto bytes=text.toUtf8();const int result=generate(model,bytes.constData(),output,&context,error.data(),error.size());
        metrics.generationMs=timer.nsecsElapsed()/1e6;metrics.audioSeconds=double(context.samples)/sampleRate;metrics.canceled=!control.current();
        if(result<0)return QString::fromUtf8(error.data());
        if(result==0 && control.current())return QStringLiteral("Speech stream consumer stopped unexpectedly");
        if(result>0 && !context.samples)return QCoreApplication::translate("PocketSpeech","自然语音未生成有效音频。");
        return {};
    }
};
PocketSpeech::PocketSpeech(QString root):impl_(std::make_unique<Impl>(std::move(root))) {}
PocketSpeech::~PocketSpeech()=default;
QString PocketSpeech::generate(const QString& text,const SpeechControl& control,SpeechMetrics& metrics,const std::function<bool(const float*,std::size_t)>& sink) {
    const auto error=impl_->load(control,metrics);if(!error.isEmpty() || metrics.canceled)return error;
    return impl_->stream(text,control,metrics,[&](const float* values,std::size_t count){return waitWhilePaused(control) && sink(values,count);});
}
QString PocketSpeech::speak(const QString& text,const SpeechControl& control,SpeechMetrics& metrics) {
#ifndef Q_OS_WIN
    Q_UNUSED(text);Q_UNUSED(control);Q_UNUSED(metrics);return QStringLiteral("Natural speech playback is not implemented on this platform");
#else
    const auto loading=impl_->load(control,metrics);if(!loading.isEmpty() || metrics.canceled)return loading;
    struct Queue {
        std::mutex mutex;std::condition_variable cv;std::deque<std::vector<float>> chunks;
        qint64 bytes=0;bool done=false;std::atomic<bool> failed{false};QString error;
    } queue;
    std::thread player([&] {
        try {
            WaveOutput output(control,metrics,queue.failed);Tempo tempo(impl_->tempo);std::array<float,4096> processed{};
            while(control.current() && !queue.failed.load()) {
                if(!output.poll())break;
                std::vector<float> values;bool done=false;
                {
                    std::unique_lock lock(queue.mutex);
                    if(queue.chunks.empty() && !queue.done)queue.cv.wait_for(lock,std::chrono::milliseconds(10));
                    if(!queue.chunks.empty()){values=std::move(queue.chunks.front());queue.chunks.pop_front();queue.bytes-=qint64(values.size()*sizeof(float));}
                    done=queue.done && queue.chunks.empty();
                }
                queue.cv.notify_all();
                if(!values.empty())tempo.put(values,control.rate.load());
                if(done)tempo.flush();
                while(const auto count=tempo.receive(processed.data(),unsigned(processed.size())))if(!output.write(processed.data(),count))break;
                if(done){output.drain();break;}
            }
        }catch(const std::exception& e){std::lock_guard lock(queue.mutex);queue.error=QString::fromUtf8(e.what());queue.failed.store(true);queue.cv.notify_all();}
    });
    QString error;
    try {
        error=impl_->stream(text,control,metrics,[&](const float* values,std::size_t count){
            while(control.current() && control.paused.load() && !queue.failed.load())briefWait();
            const auto bytes=qint64(count*sizeof(float));
            std::unique_lock lock(queue.mutex);
            while(queue.bytes+bytes>queueBudget && control.current() && !queue.failed.load())queue.cv.wait_for(lock,std::chrono::milliseconds(10));
            if(!control.current() || queue.failed.load())return false;
            queue.chunks.emplace_back(values,values+count);queue.bytes+=bytes;metrics.peakQueueBytes=std::max(metrics.peakQueueBytes,quint64(queue.bytes));
            lock.unlock();queue.cv.notify_all();return true;
        });
    }catch(const std::exception& e){error=QString::fromUtf8(e.what());}
    {std::lock_guard lock(queue.mutex);queue.done=true;if(!error.isEmpty())queue.failed.store(true);}
    queue.cv.notify_all();player.join();
    if(!queue.error.isEmpty())error=queue.error;
    metrics.canceled=!control.current();return error;
#endif
}
