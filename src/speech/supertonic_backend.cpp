// Native CPU adapter for the Supertonic 3 four-graph text-to-speech pipeline.
// Text normalization and chunking follow the MIT-licensed reference helper of
// supertone-inc/supertonic (commit recorded in speech.lock.json); this is an independent C++ port.
#include "speechapi.h"
#include <onnxruntime_cxx_api.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#define SPEECH_EXPORT extern "C" __declspec(dllexport)
namespace {
// Fixed by the pinned model release and the 2026-11-11 French evaluation; see docs/NATURAL_SPEECH.md.
constexpr int denoiseSteps=8;
constexpr float speakingSpeed=1.05f;
constexpr int maxChunkCharacters=200;
constexpr double maxChunkSeconds=30;
constexpr double pauseSeconds=0.3;
constexpr double targetDbfs=-16,ceilingDbfs=-1,gateDbfs=-45;
constexpr std::size_t outputSlice=4096;

using Values=std::vector<Ort::Value>;
QByteArray read(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error((path+": "+file.errorString()).toStdString());
    return file.readAll();
}
QJsonDocument json(const QString& path) {
    QJsonParseError failure;const auto document=QJsonDocument::fromJson(read(path),&failure);
    if(failure.error!=QJsonParseError::NoError)throw std::runtime_error(("Invalid speech resource: "+path).toStdString());
    return document;
}
void errorText(char* output,std::size_t capacity,const char* message) noexcept {
    if(!output || !capacity)return;
    const auto size=std::min(capacity-1,std::strlen(message));std::memcpy(output,message,size);output[size]=0;
}
void flatten(const QJsonValue& value,std::vector<float>& out) {
    if(value.isArray()){for(const auto& item:value.toArray())flatten(item,out);return;}
    const double number=value.toDouble(std::numeric_limits<double>::quiet_NaN());
    if(!std::isfinite(number))throw std::runtime_error("Invalid voice style value");
    out.push_back(float(number));
}
struct Tensor {
    std::vector<int64_t> shape;std::vector<float> data;
    static Tensor from(const QJsonObject& object) {
        Tensor result;std::size_t count=1;
        for(const auto& dimension:object["dims"].toArray()){result.shape.push_back(dimension.toInteger());count*=std::size_t(std::max<int64_t>(0,result.shape.back()));}
        flatten(object["data"],result.data);
        if(result.shape.size()!=3 || result.shape[0]!=1 || result.data.size()!=count || !count)throw std::runtime_error("Unsupported voice style shape");
        return result;
    }
};
struct Voice {Tensor ttl,dp;};

class Graph {
    Ort::Session session_;
    std::vector<std::string> inputStorage_,outputStorage_;
    std::vector<const char*> outputs_;
public:
    Graph(Ort::Env& env,const QString& path,const Ort::SessionOptions& options)
        :session_(env,reinterpret_cast<const wchar_t*>(path.utf16()),options) {
        Ort::AllocatorWithDefaultOptions allocator;
        for(std::size_t i=0;i<session_.GetInputCount();++i)inputStorage_.emplace_back(session_.GetInputNameAllocated(i,allocator).get());
        for(std::size_t i=0;i<session_.GetOutputCount();++i)outputStorage_.emplace_back(session_.GetOutputNameAllocated(i,allocator).get());
        for(const auto& name:outputStorage_)outputs_.push_back(name.c_str());
    }
    // Inputs are bound by name so a graph contract change fails loudly instead of silently misbinding.
    Values run(std::map<std::string,Ort::Value>& named) {
        if(named.size()!=inputStorage_.size())throw std::runtime_error("Speech graph input contract mismatch");
        std::vector<const char*> names;Values values;
        for(const auto& name:inputStorage_) {
            auto found=named.find(name);if(found==named.end())throw std::runtime_error("Speech graph input missing: "+name);
            names.push_back(name.c_str());values.push_back(std::move(found->second));
        }
        named.clear();
        return session_.Run(Ort::RunOptions{nullptr},names.data(),values.data(),values.size(),outputs_.data(),outputs_.size());
    }
};

bool emoji(char32_t c) {
    return (c>=0x1f600&&c<=0x1f64f)||(c>=0x1f300&&c<=0x1f5ff)||(c>=0x1f680&&c<=0x1f6ff)||(c>=0x1f700&&c<=0x1faff)
        ||(c>=0x2600&&c<=0x27bf)||(c>=0x1f1e6&&c<=0x1f1ff);
}
QString normalizeText(QString text) {
    text=text.normalized(QString::NormalizationForm_KD);
    QString filtered;filtered.reserve(text.size());
    for(const char32_t c:text.toUcs4())if(!emoji(c))filtered+=QString::fromUcs4(&c,1);
    text=filtered;
    static const std::pair<const char*,const char*> replacements[]={
        {"–","-"},{"‑","-"},{"—","-"},{"_"," "},{"“","\""},{"”","\""},{"‘","'"},{"’","'"},
        {"´","'"},{"`","'"},{"["," "},{"]"," "},{"|"," "},{"/"," "},{"#"," "},{"→"," "},{"←"," "}};
    for(const auto& [from,to]:replacements)text.replace(QString::fromUtf8(from),QString::fromUtf8(to));
    static const QRegularExpression symbols(QStringLiteral("[♥☆♡©\\\\]"));text.remove(symbols);
    static const QRegularExpression spaceBefore(QStringLiteral(" ([,.!?;:'])"));text.replace(spaceBefore,QStringLiteral("\\1"));
    while(text.contains(QStringLiteral("\"\"")))text.replace(QStringLiteral("\"\""),QStringLiteral("\""));
    while(text.contains(QStringLiteral("''")))text.replace(QStringLiteral("''"),QStringLiteral("'"));
    text=text.simplified();
    static const QRegularExpression terminal(QStringLiteral("[.!?;:,'\"')\\]}…。」』】〉》›»]$"));
    if(!text.isEmpty() && !terminal.match(text).hasMatch())text+='.';
    return text;
}
// One sentence per chunk keeps the first audible sentence short; overlong OCR lines split at
// clause punctuation, then at spaces.
std::vector<QString> chunks(const QString& input) {
    static const QRegularExpression paragraphs(QStringLiteral("\\n\\s*\\n+"));
    static const QRegularExpression sentences(QStringLiteral(
        "(?<!\\bM\\.)(?<!Mme\\.)(?<!Mlle\\.)(?<!Dr\\.)(?<!Mr\\.)(?<!Mrs\\.)(?<!Pr\\.)(?<!etc\\.)(?<!\\b[A-Z]\\.)(?<=[.!?…])\\s+"));
    static const QRegularExpression clauses(QStringLiteral("(?<=[,;:])\\s+"));
    std::vector<QString> result;
    const auto push=[&](QString piece){piece=piece.trimmed();if(!piece.isEmpty())result.push_back(piece);};
    const auto split=[&](const QString& text,const QRegularExpression& at,const auto& smaller) {
        QString current;
        for(const auto& part:text.split(at,Qt::SkipEmptyParts)) {
            if(part.size()>maxChunkCharacters){push(current);current.clear();smaller(part);continue;}
            if(!current.isEmpty() && current.size()+1+part.size()>maxChunkCharacters){push(current);current.clear();}
            current=current.isEmpty()?part:current+' '+part;
        }
        push(current);
    };
    const auto words=[&](const QString& text){split(text,QRegularExpression(QStringLiteral("\\s+")),[](const QString&){throw std::runtime_error("Selected text contains an excessively long word");});};
    for(const auto& paragraph:input.split(paragraphs,Qt::SkipEmptyParts))
        for(const auto& sentence:paragraph.simplified().split(sentences,Qt::SkipEmptyParts)) {
            if(sentence.size()<=maxChunkCharacters)push(sentence);
            else split(sentence,clauses,words);
        }
    return result;
}
// Per-sentence loudness normalization with a zero-phase look-ahead peak limiter: the model's raw
// output sits near -24 dBFS active RMS, well below typical speech playback level.
void normalizeLoudness(std::vector<float>& pcm,int sampleRate) {
    if(pcm.empty())return;
    const std::size_t frame=std::max(1,sampleRate/50);double energy=0;std::size_t active=0;
    for(std::size_t start=0;start+frame<=pcm.size();start+=frame) {
        double sum=0;for(std::size_t i=start;i<start+frame;++i)sum+=double(pcm[i])*pcm[i];
        if(sum/frame>std::pow(10.0,gateDbfs/10)){energy+=sum;active+=frame;}
    }
    if(!active)return;
    const double gain=std::clamp(std::pow(10.0,targetDbfs/20)/std::sqrt(energy/active),0.25,8.0);
    const float ceiling=float(std::pow(10.0,ceilingDbfs/20));
    std::vector<float> envelope(pcm.size());
    for(std::size_t i=0;i<pcm.size();++i){pcm[i]=float(pcm[i]*gain);envelope[i]=std::min(1.f,ceiling/(std::abs(pcm[i])+1e-9f));}
    const std::size_t radius=std::size_t(sampleRate*0.003);std::vector<float> held(pcm.size());std::deque<std::size_t> window;
    for(std::size_t i=0,next=0;i<pcm.size();++i) {
        for(;next<pcm.size() && next<=i+radius;++next){while(!window.empty() && envelope[window.back()]>=envelope[next])window.pop_back();window.push_back(next);}
        while(window.front()+radius<i)window.pop_front();
        held[i]=envelope[window.front()];
    }
    const float release=float(1-std::exp(-1.0/(sampleRate*0.05)));
    for(std::size_t i=1;i<held.size();++i)held[i]=std::min(held[i],held[i-1]+(1-held[i-1])*release);
    for(std::size_t i=held.size()-1;i-->0;)held[i]=std::min(held[i],held[i+1]+(1-held[i+1])*release);
    for(std::size_t i=0;i<pcm.size();++i)pcm[i]=std::clamp(pcm[i]*held[i],-ceiling,ceiling);
}

class Model {
    Ort::Env environment_{ORT_LOGGING_LEVEL_ERROR,"Langlern speech"};
    Ort::SessionOptions options_;
    Ort::MemoryInfo memory_=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
    std::unique_ptr<Graph> duration_,encoder_,estimator_,vocoder_;
    std::vector<int> indexer_;
    std::map<std::string,Voice> voices_;
    int sampleRate_=0,chunkSamples_=0,latentChannels_=0;
    std::mt19937 random_{std::random_device{}()};

    template<class T> Ort::Value tensor(T* data,std::size_t count,const std::vector<int64_t>& shape) {
        return Ort::Value::CreateTensor<T>(memory_,data,count,shape.data(),shape.size());
    }
    std::vector<int64_t> ids(const QString& sentence) const {
        const auto text=QStringLiteral("<fr>")+sentence+QStringLiteral("</fr>");std::vector<int64_t> result;
        for(const char32_t c:text.toUcs4())if(c<indexer_.size() && indexer_[c]>=0)result.push_back(indexer_[c]);
        return result;
    }
    bool chunk(const QString& sentence,const Voice& voice,SpeechSamples callback,void* context) {
        auto text=ids(sentence);if(text.size()<=10)return true; // nothing but the language tags and at most one symbol
        const auto length=int64_t(text.size());std::vector<float> mask(text.size(),1.f);
        auto style=[&](const Tensor& t){return tensor(const_cast<float*>(t.data.data()),t.data.size(),t.shape);};
        std::map<std::string,Ort::Value> inputs;
        inputs.emplace("text_ids",tensor(text.data(),text.size(),{1,length}));inputs.emplace("style_dp",style(voice.dp));
        inputs.emplace("text_mask",tensor(mask.data(),mask.size(),{1,1,length}));
        const double seconds=double(duration_->run(inputs)[0].GetTensorData<float>()[0])/speakingSpeed;
        if(!std::isfinite(seconds) || seconds<=0 || seconds>maxChunkSeconds)throw std::runtime_error("Speech model produced an invalid duration; select a shorter passage and retry");
        if(!callback(nullptr,0,context))return false;
        inputs.emplace("text_ids",tensor(text.data(),text.size(),{1,length}));inputs.emplace("style_ttl",style(voice.ttl));
        inputs.emplace("text_mask",tensor(mask.data(),mask.size(),{1,1,length}));
        auto embedding=encoder_->run(inputs);
        const auto samples=std::size_t(seconds*sampleRate_);const auto frames=int64_t((samples+chunkSamples_-1)/chunkSamples_);
        std::vector<float> latent(std::size_t(latentChannels_*frames)),latentMask(std::size_t(frames),1.f);
        std::normal_distribution<float> noise(0,1);for(auto& value:latent)value=noise(random_);
        const auto embeddingShape=embedding[0].GetTensorTypeAndShapeInfo().GetShape();
        auto* embeddingData=embedding[0].GetTensorMutableData<float>();const auto embeddingCount=embedding[0].GetTensorTypeAndShapeInfo().GetElementCount();
        float total=float(denoiseSteps);
        for(int step=0;step<denoiseSteps;++step) {
            if(!callback(nullptr,0,context))return false;
            float current=float(step);
            inputs.emplace("noisy_latent",tensor(latent.data(),latent.size(),{1,latentChannels_,frames}));
            inputs.emplace("text_emb",tensor(embeddingData,embeddingCount,embeddingShape));
            inputs.emplace("style_ttl",style(voice.ttl));inputs.emplace("text_mask",tensor(mask.data(),mask.size(),{1,1,length}));
            inputs.emplace("latent_mask",tensor(latentMask.data(),latentMask.size(),{1,1,frames}));
            inputs.emplace("current_step",tensor(&current,1,{1}));inputs.emplace("total_step",tensor(&total,1,{1}));
            auto output=estimator_->run(inputs);
            if(output[0].GetTensorTypeAndShapeInfo().GetElementCount()!=latent.size())throw std::runtime_error("Speech latent shape mismatch");
            const auto* values=output[0].GetTensorData<float>();
            for(std::size_t i=0;i<latent.size();++i){if(!std::isfinite(values[i]))throw std::runtime_error("Speech model produced invalid audio latents");latent[i]=values[i];}
        }
        if(!callback(nullptr,0,context))return false;
        inputs.emplace("latent",tensor(latent.data(),latent.size(),{1,latentChannels_,frames}));
        auto decoded=vocoder_->run(inputs);
        const auto count=decoded[0].GetTensorTypeAndShapeInfo().GetElementCount();const auto* wav=decoded[0].GetTensorData<float>();
        std::vector<float> pcm(wav,wav+std::min<std::size_t>(count,samples));
        for(float value:pcm)if(!std::isfinite(value))throw std::runtime_error("Speech decoder produced invalid audio");
        normalizeLoudness(pcm,sampleRate_);
        for(std::size_t offset=0;offset<pcm.size();offset+=outputSlice)
            if(!callback(pcm.data()+offset,std::min(outputSlice,pcm.size()-offset),context))return false;
        return true;
    }
public:
    Model(const QString& root,int threads) {
        const auto directory=root+"/models/";
        const auto config=json(directory+"tts.json").object();
        const auto ae=config["ae"].toObject(),ttl=config["ttl"].toObject();
        sampleRate_=ae["sample_rate"].toInt();chunkSamples_=ae["base_chunk_size"].toInt()*ttl["chunk_compress_factor"].toInt();
        latentChannels_=ttl["latent_dim"].toInt()*ttl["chunk_compress_factor"].toInt();
        if(config["tts_version"].toString()!=QStringLiteral("v1.7.3") || sampleRate_!=44100 || chunkSamples_<=0 || latentChannels_<=0)
            throw std::runtime_error("Unsupported Supertonic voice pack format");
        const auto table=json(directory+"unicode_indexer.json").array();
        if(table.size()!=65536)throw std::runtime_error("Unsupported speech text indexer");
        indexer_.reserve(table.size());for(const auto& value:table)indexer_.push_back(value.toInt(-1));
        for(const char* name:{"M1","F3"}) {
            const auto style=json(root+"/voices/"+name+".json").object();
            voices_.emplace(name,Voice{Tensor::from(style["style_ttl"].toObject()),Tensor::from(style["style_dp"].toObject())});
        }
        options_.SetIntraOpNumThreads(std::clamp(threads,1,8));options_.SetInterOpNumThreads(1);
        options_.DisableMemPattern();options_.DisableCpuMemArena();
        duration_=std::make_unique<Graph>(environment_,directory+"duration_predictor.onnx",options_);
        encoder_=std::make_unique<Graph>(environment_,directory+"text_encoder.onnx",options_);
        estimator_=std::make_unique<Graph>(environment_,directory+"vector_estimator.onnx",options_);
        vocoder_=std::make_unique<Graph>(environment_,directory+"vocoder.onnx",options_);
    }
    int sampleRate() const {return sampleRate_;}
    bool generate(const char* utf8,const char* voiceName,SpeechSamples callback,void* context) {
        const auto voice=voices_.find(voiceName);if(voice==voices_.end())throw std::runtime_error("Unknown speech voice");
        const auto pieces=chunks(QString::fromUtf8(utf8));
        std::vector<float> pause(std::size_t(pauseSeconds*sampleRate_),0.f);bool first=true;
        for(const auto& piece:pieces) {
            const auto sentence=normalizeText(piece);if(sentence.isEmpty())continue;
            if(!first)for(std::size_t offset=0;offset<pause.size();offset+=outputSlice)
                if(!callback(pause.data()+offset,std::min(outputSlice,pause.size()-offset),context))return false;
            if(!chunk(sentence,voice->second,callback,context))return false;
            first=false;
        }
        return true;
    }
};
}
SPEECH_EXPORT void* langlernSpeechCreate(const char* root,int threads,char* error,std::size_t capacity) {
    try{return new Model(QString::fromUtf8(root),threads);}catch(const std::exception& e){errorText(error,capacity,e.what());return nullptr;}
}
SPEECH_EXPORT int langlernSpeechSampleRate(void* handle) {return handle?static_cast<Model*>(handle)->sampleRate():0;}
SPEECH_EXPORT int langlernSpeechGenerate(void* handle,const char* text,const char* voice,SpeechSamples callback,void* context,char* error,std::size_t capacity) {
    if(!handle || !text || !voice || !callback){errorText(error,capacity,"Invalid speech request");return -1;}
    try{return static_cast<Model*>(handle)->generate(text,voice,callback,context)?1:0;}catch(const std::exception& e){errorText(error,capacity,e.what());return -1;}
}
SPEECH_EXPORT void langlernSpeechDestroy(void* handle) {delete static_cast<Model*>(handle);}
