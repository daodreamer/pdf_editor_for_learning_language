#include "pocketapi.h"
#include <onnxruntime_cxx_api.h>
#include <sentencepiece_processor.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

#define POCKET_EXPORT extern "C" __declspec(dllexport)
namespace {
constexpr int dimensions=1024,latentDimensions=32,layers=6,voiceFrames=154;
constexpr int maxTokens=50;
using Values=std::vector<Ort::Value>;
QByteArray read(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error(file.errorString().toStdString());
    return file.readAll();
}
void errorText(char* output,std::size_t capacity,const char* message) noexcept {
    if(!output || !capacity)return;
    const auto size=std::min(capacity-1,std::strlen(message));std::memcpy(output,message,size);output[size]=0;
}
std::vector<float> halfFile(const QString& path,std::size_t count) {
    const auto bytes=read(path);
    if(std::size_t(bytes.size())!=count*2)throw std::runtime_error("Invalid half-precision speech resource size");
    std::vector<float> result(count);
    for(std::size_t i=0;i<count;++i) {
        uint16_t bits;std::memcpy(&bits,bytes.constData()+i*2,2);Ort::Float16_t value;value.val=bits;result[i]=value.ToFloat();
        if(!std::isfinite(result[i]))throw std::runtime_error("Invalid speech resource value");
    }
    return result;
}

// Ordinary Run owns returned state tensors; no input/output aliases or shared mutable buffers.
class Graph {
    Ort::Session session_;
    std::vector<std::string> inputStorage_,outputStorage_;
    std::vector<const char*> inputs_,outputs_;
public:
    Graph(Ort::Env& env,const QString& path,const Ort::SessionOptions& options)
        :session_(env,reinterpret_cast<const wchar_t*>(path.utf16()),options) {
        Ort::AllocatorWithDefaultOptions allocator;
        for(std::size_t i=0;i<session_.GetInputCount();++i)inputStorage_.emplace_back(session_.GetInputNameAllocated(i,allocator).get());
        for(std::size_t i=0;i<session_.GetOutputCount();++i)outputStorage_.emplace_back(session_.GetOutputNameAllocated(i,allocator).get());
        for(const auto& name:inputStorage_)inputs_.push_back(name.c_str());
        for(const auto& name:outputStorage_)outputs_.push_back(name.c_str());
    }
    Values run(Values& values) {
        if(values.size()!=inputs_.size())throw std::runtime_error("Speech graph input contract mismatch");
        return session_.Run(Ort::RunOptions{nullptr},inputs_.data(),values.data(),values.size(),outputs_.data(),outputs_.size());
    }
};

class Model {
    Ort::Env environment_{ORT_LOGGING_LEVEL_ERROR,"Langlern speech"};
    Ort::SessionOptions options_;
    Ort::MemoryInfo memory_=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
    Ort::AllocatorWithDefaultOptions allocator_;
    std::unique_ptr<Graph> main_,flow_,decoder_;
    sentencepiece::SentencePieceProcessor tokenizer_;
    std::vector<float> table_,voice_;
    QJsonObject manifest_;
    std::mt19937 random_{std::random_device{}()};

    template<class T> Ort::Value tensor(T* data,std::size_t count,std::initializer_list<int64_t> shape) {
        return Ort::Value::CreateTensor<T>(memory_,data,count,shape.begin(),shape.size());
    }
    Ort::Value zeros(const std::vector<int64_t>& shape,ONNXTensorElementDataType type,bool ones=false) {
        auto value=Ort::Value::CreateTensor(allocator_,shape.data(),shape.size(),type);
        const auto count=value.GetTensorTypeAndShapeInfo().GetElementCount();
        const auto bytes=count*(type==ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT?4:type==ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64?8:1);
        if(bytes)std::memset(value.GetTensorMutableRawData(),ones?1:0,bytes);
        return value;
    }
    Values initialVoice() {
        Values result;
        constexpr std::size_t count=2*voiceFrames*16*64;
        for(int layer=0;layer<layers;++layer) {
            auto value=zeros({2,1,voiceFrames,16,64},ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
            std::memcpy(value.GetTensorMutableData<float>(),voice_.data()+layer*count,count*sizeof(float));result.push_back(std::move(value));
            auto offset=zeros({1},ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);offset.GetTensorMutableData<int64_t>()[0]=voiceFrames;result.push_back(std::move(offset));
        }
        return result;
    }
    Values initialDecoder() {
        Values result;
        for(const auto& item:manifest_["mimi_states"].toArray()) {
            const auto entry=item.toObject();std::vector<int64_t> shape;
            for(const auto& value:entry["shape"].toArray())shape.push_back(value.toInteger());
            const auto type=entry["dtype"].toString();
            result.push_back(zeros(shape,type=="float32"?ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:type=="int64"?ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL,entry["key"]=="first"));
        }
        return result;
    }
    static void carry(Values& state,Values& output,std::size_t begin) {
        state.clear();for(std::size_t i=begin;i<output.size();++i)state.push_back(std::move(output[i]));
    }
    std::vector<int> tokens(const QString& text) const {
        const auto bytes=text.toUtf8();std::vector<int> ids;
        const auto status=tokenizer_.Encode(std::string(bytes.constData(),bytes.size()),&ids);
        if(!status.ok())throw std::runtime_error(status.ToString());return ids;
    }
    static QString terminal(QString text) {
        if(text.isEmpty())return text;
        text[0]=text[0].toUpper();
        const auto last=text.back();if(last!='.' && last!='!' && last!='?' && last!=QChar(0x2026)) {
            while(!text.isEmpty() && QStringLiteral(",;:-–—").contains(text.back()))text.chop(1);
            text+='.';
        }
        return text;
    }
    std::vector<QString> chunks(QString text) const {
        const auto replacements=manifest_["replace_characters"].toObject();
        for(auto it=replacements.begin();it!=replacements.end();++it)text.replace(it.key(),it.value().toString());
        text.replace(';',',');text=text.simplified();
        std::vector<QString> result;QString current;
        // Bound every chunk, including long OCR lines without sentence punctuation.
        for(const auto& word:text.split(' ',Qt::SkipEmptyParts)) {
            const auto candidate=current.isEmpty()?word:current+' '+word;
            if(tokens(terminal(candidate)).size()>maxTokens) {
                if(current.isEmpty())throw std::runtime_error("Selected text contains an excessively long word");
                result.push_back(terminal(current));current=word;
                if(tokens(terminal(current)).size()>maxTokens)throw std::runtime_error("Selected text contains an excessively long word");
            } else current=candidate;
        }
        if(!current.isEmpty())result.push_back(terminal(current));return result;
    }
    bool generateChunk(const QString& text,PocketSamples callback,void* context) {
        const auto ids=tokens(text);if(ids.empty())return true;
        std::vector<float> embeddings;embeddings.reserve(ids.size()*dimensions);
        for(int id:ids) {
            if(id<0 || id>=4000)throw std::runtime_error("Invalid speech token");
            embeddings.insert(embeddings.end(),table_.begin()+id*dimensions,table_.begin()+(id+1)*dimensions);
        }
        Values state=initialVoice(),decoderState=initialDecoder();
        Values prefill;prefill.push_back(tensor<float>(nullptr,0,{1,0,latentDimensions}));
        prefill.push_back(tensor(embeddings.data(),embeddings.size(),{1,int64_t(ids.size()),dimensions}));
        for(auto& value:state)prefill.push_back(std::move(value));auto output=main_->run(prefill);carry(state,output,2);
        std::array<float,latentDimensions> latent;latent.fill(std::numeric_limits<float>::quiet_NaN());
        const int limit=int(std::ceil((double(ids.size())/3+2)*12.5));
        const int extra=text.split(' ',Qt::SkipEmptyParts).size()<=4?5:3;
        std::normal_distribution<float> noise(0,std::sqrt(.3f));int eos=-1;
        float zero=0,one=1;bool first=true;
        for(int step=0;step<limit;++step) {
            Values inputs;inputs.push_back(tensor(latent.data(),latent.size(),{1,1,latentDimensions}));inputs.push_back(tensor<float>(nullptr,0,{1,0,dimensions}));
            for(auto& value:state)inputs.push_back(std::move(value));output=main_->run(inputs);
            const float logit=output[1].GetTensorData<float>()[0];
            if(!std::isfinite(logit))throw std::runtime_error("Speech model produced an invalid end signal");
            if(eos<0 && step>=6 && logit>-4)eos=step;
            if(eos>=0 && step>=eos+extra)return true;
            carry(state,output,2);
            for(auto& value:latent)value=noise(random_);
            Values flowInputs;flowInputs.push_back(std::move(output[0]));flowInputs.push_back(tensor(&zero,1,{1,1}));flowInputs.push_back(tensor(&one,1,{1,1}));flowInputs.push_back(tensor(latent.data(),latent.size(),{1,latentDimensions}));
            auto flowOutput=flow_->run(flowInputs);const auto* direction=flowOutput[0].GetTensorData<float>();
            for(std::size_t i=0;i<latent.size();++i){latent[i]+=direction[i];if(!std::isfinite(latent[i]))throw std::runtime_error("Speech model produced invalid audio latents");}
            Values decoderInputs;decoderInputs.push_back(tensor(latent.data(),latent.size(),{1,1,latentDimensions}));
            for(auto& value:decoderState)decoderInputs.push_back(std::move(value));auto decoded=decoder_->run(decoderInputs);carry(decoderState,decoded,1);
            const auto count=decoded[0].GetTensorTypeAndShapeInfo().GetElementCount();auto* pcm=decoded[0].GetTensorMutableData<float>();
            if(count!=1920)throw std::runtime_error("Speech decoder frame size mismatch");
            for(std::size_t i=0;i<count;++i)if(!std::isfinite(pcm[i]))throw std::runtime_error("Speech decoder produced invalid audio");
            if(first){for(std::size_t i=0;i<120;++i)pcm[i]*=float(i)/119;first=false;}
            if(!callback(pcm,count,context))return false;
        }
        throw std::runtime_error("Speech did not finish within its generation limit; select a shorter passage and retry");
    }
public:
    explicit Model(const QString& root) {
        const auto directory=root+"/models/";
        manifest_=QJsonDocument::fromJson(read(directory+"manifest.json")).object();
        if(manifest_["format"]!="volume-board/pocket-tts-onnx@1" || manifest_["language"]!="french" || manifest_["flow_layers"].toArray().size()!=layers || manifest_["mimi_states"].toArray().size()!=24)
            throw std::runtime_error("Unsupported Estelle voice pack format");
        const auto bytes=read(directory+"tokenizer.model");const auto status=tokenizer_.LoadFromSerializedProto(std::string(bytes.constData(),bytes.size()));
        if(!status.ok())throw std::runtime_error(status.ToString());
        table_=halfFile(directory+"text_embeddings.f16",4001*dimensions);voice_=halfFile(root+"/voices/estelle.f16",layers*2*voiceFrames*16*64);
        options_.SetIntraOpNumThreads(1);options_.SetInterOpNumThreads(1);options_.DisableMemPattern();options_.DisableCpuMemArena();
        main_=std::make_unique<Graph>(environment_,directory+"flow_lm_main_int8.onnx",options_);
        flow_=std::make_unique<Graph>(environment_,directory+"flow_lm_flow_int8.onnx",options_);
        decoder_=std::make_unique<Graph>(environment_,directory+"mimi_decoder_int8.onnx",options_);
    }
    bool generate(const char* utf8,PocketSamples callback,void* context) {
        for(const auto& chunk:chunks(QString::fromUtf8(utf8)))if(!generateChunk(chunk,callback,context))return false;
        return true;
    }
};
}
POCKET_EXPORT void* langlernPocketCreate(const char* root,char* error,std::size_t capacity) {
    try{return new Model(QString::fromUtf8(root));}catch(const std::exception& e){errorText(error,capacity,e.what());return nullptr;}
}
POCKET_EXPORT int langlernPocketGenerate(void* handle,const char* text,PocketSamples callback,void* context,char* error,std::size_t capacity) {
    if(!handle || !text || !callback){errorText(error,capacity,"Invalid Pocket speech request");return -1;}
    try{return static_cast<Model*>(handle)->generate(text,callback,context)?1:0;}catch(const std::exception& e){errorText(error,capacity,e.what());return -1;}
}
POCKET_EXPORT void langlernPocketDestroy(void* handle) {delete static_cast<Model*>(handle);}
