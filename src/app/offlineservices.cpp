#include "offlineservices.h"
#include "scancleanup.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTranslator>
#include <QFile>
#include <QJsonDocument>
#include <QQmlEngine>
#include <QLocale>
#include <QElapsedTimer>
#include <QUuid>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <sapi.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

struct OfflineServices::Native {
#ifdef Q_OS_WIN
    MCIDEVICEID audio=0;
#endif
    double loopStart=-1,loopEnd=-1;
};
namespace {
class EnglishTranslator : public QTranslator {
    QVariantMap values_;
public:
    EnglishTranslator() { QFile file(":/i18n/en.json");if(file.open(QIODevice::ReadOnly))values_=QJsonDocument::fromJson(file.readAll()).toVariant().toMap(); }
    bool isEmpty() const override { return values_.isEmpty(); }
    QString translate(const char*,const char* source,const char*,int) const override { return values_.value(QString::fromUtf8(source)).toString(); }
};
}
void OfflineServices::setEnglish(bool value) {
    if(english_==value)return;
    if(!translator_)translator_=std::make_unique<EnglishTranslator>();
    if(value)QCoreApplication::installTranslator(translator_.get());else QCoreApplication::removeTranslator(translator_.get());
    english_=value;if(auto* engine=qmlEngine(this))engine->retranslate();emit languageChanged();
}
OfflineServices::OfflineServices(QObject* parent):QObject(parent),native_(std::make_shared<Native>()) {
    QElapsedTimer initialization;initialization.start();
    worker_=new QObject;worker_->moveToThread(&thread_);
    connect(&thread_,&QThread::finished,worker_,&QObject::deleteLater);
    thread_.setObjectName("Offline learning services");thread_.start();
    speechWorker_=new QObject;speechWorker_->moveToThread(&speechThread_);
    connect(&speechThread_,&QThread::finished,speechWorker_,&QObject::deleteLater);
    speechThread_.setObjectName("Offline speech");speechThread_.start();
    QMetaObject::invokeMethod(speechWorker_,[]{
#ifdef Q_OS_WIN
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);
#endif
    },Qt::QueuedConnection);
    QMetaObject::invokeMethod(worker_,[this] {
        QStringList langs;
#ifdef Q_OS_WIN
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            for(const auto& language:winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages())
                langs.append(QString::fromWCharArray(language.LanguageTag().c_str()));
        } catch(const winrt::hresult_error&) {}
#endif
        QMetaObject::invokeMethod(this,[this,langs]{languages_=langs;emit changed();},Qt::QueuedConnection);
    },Qt::QueuedConnection);
    audioTimer_.setInterval(300);connect(&audioTimer_,&QTimer::timeout,this,&OfflineServices::pollAudio);
    if(qEnvironmentVariableIsSet("LANGLERN_STARTUP_DETAIL"))qInfo("offline_service_construct_ms=%.3f",initialization.nsecsElapsed()/1e6);
}
OfflineServices::~OfflineServices() {
    if(translator_)QCoreApplication::removeTranslator(translator_.get());
    ++ocrRevision_;++speechRevision_;++lookupRevision_;++previewRevision_;
    QMetaObject::invokeMethod(speechWorker_,[]{
#ifdef Q_OS_WIN
        CoUninitialize();
#endif
    },Qt::BlockingQueuedConnection);
    speechThread_.quit();speechThread_.wait();
    QMetaObject::invokeMethod(worker_,[state=native_] {
#ifdef Q_OS_WIN
        if(state->audio)mciSendCommandW(state->audio,MCI_CLOSE,0,0);
        winrt::uninit_apartment();
#endif
    },Qt::BlockingQueuedConnection);
    thread_.quit();thread_.wait();
}
void OfflineServices::previewScan(QImage image,QVariantMap options) {
    const auto revision=++previewRevision_;
    QMetaObject::invokeMethod(worker_,[this,image=std::move(image),options,revision] {
        if(revision!=previewRevision_)return;
        auto preview=cleanScan(image.scaled(1200,1600,Qt::KeepAspectRatio,Qt::SmoothTransformation),options);
        QMetaObject::invokeMethod(this,[this,preview,revision]{if(revision!=previewRevision_)return;preview_=preview;emit previewChanged();},Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void OfflineServices::invalidate() { ++ocrRevision_;++previewRevision_;preview_={};emit previewChanged();words_.clear();text_.clear();document_.clear();status_=busy_?tr("正在等待上一页识别结束…"):QString{};emit changed();stopSpeaking(); }
void OfflineServices::recognize(QImage image,QString document,int page,QString language) {
    if(image.isNull() || busy_)return;
    const auto revision=++ocrRevision_;busy_=true;words_.clear();text_.clear();
    document_=std::move(document);page_=page;status_=tr("正在离线识别当前页…");emit changed();
    // The immutable QImage snapshot owns its buffer independently of PDF/cache lifetimes.
    QMetaObject::invokeMethod(worker_,[this,image=std::move(image),revision,language] {
        if(revision!=ocrRevision_){QMetaObject::invokeMethod(this,[this]{busy_=false;status_.clear();emit changed();},Qt::QueuedConnection);return;}
        QVariantList words;QString text,error;
#ifdef Q_OS_WIN
        try {
            using namespace winrt::Windows;
            auto engine=language.isEmpty()?Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages():
                Media::Ocr::OcrEngine::TryCreateFromLanguage(Globalization::Language(language.toStdWString()));
            if(!engine) error=tr("此 OCR 语言包未安装。请选择系统已有语言包。");
            else {
                const int limit=int(Media::Ocr::OcrEngine::MaxImageDimension());
                auto pixels=image.size().width()>limit||image.size().height()>limit?image.scaled(limit,limit,Qt::KeepAspectRatio,Qt::SmoothTransformation):image;
                pixels=pixels.convertToFormat(QImage::Format_ARGB32);
                Graphics::Imaging::SoftwareBitmap bitmap(Graphics::Imaging::BitmapPixelFormat::Bgra8,pixels.width(),pixels.height(),Graphics::Imaging::BitmapAlphaMode::Ignore);
                Storage::Streams::DataWriter writer;
                writer.WriteBytes(winrt::array_view<const uint8_t>(pixels.constBits(),pixels.constBits()+pixels.sizeInBytes()));
                bitmap.CopyFromBuffer(writer.DetachBuffer());
                const auto result=engine.RecognizeAsync(bitmap).get();
                text=QString::fromWCharArray(result.Text().c_str());
                for(const auto& line:result.Lines()) {
                    const auto context=QString::fromWCharArray(line.Text().c_str());
                    for(const auto& word:line.Words()) {
                        const auto rect=word.BoundingRect();
                        words.append(QVariantMap{{"word",QString::fromWCharArray(word.Text().c_str())},{"context",context},
                            {"x",rect.X/pixels.width()},{"y",rect.Y/pixels.height()},{"w",rect.Width/pixels.width()},{"h",rect.Height/pixels.height()}});
                    }
                }
            }
        } catch(const winrt::hresult_error& e) { error=tr("OCR 失败：%1").arg(QString::fromWCharArray(e.message().c_str())); }
#else
        Q_UNUSED(image);Q_UNUSED(language);error=tr("此平台尚未接入本地 OCR 引擎。");
#endif
        QMetaObject::invokeMethod(this,[this,revision,words,text,error] {
            if(revision!=ocrRevision_){busy_=false;status_.clear();emit changed();return;}
            busy_=false;words_=words;text_=text;
            status_=error.isEmpty()?tr("当前页识别完成 · %1 个词；结果仅供学习，请核对原文").arg(words.size()):error;
            emit changed();
        },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void OfflineServices::lookup(QString word) {
    word=word.trimmed().left(200);word.remove(QRegularExpression("^[^\\p{L}]+|[^\\p{L}]+$"));
    lookupWord_=word;meaning_.clear();phonetic_.clear();lookupFound_=false;emit lookupChanged();const auto revision=++lookupRevision_;
    QMetaObject::invokeMethod(worker_,[this,word,revision] {
        if(revision!=lookupRevision_)return;
        auto path=qEnvironmentVariable("LANGLERN_DICTIONARY");
        if(path.isEmpty())path=QDir(QCoreApplication::applicationDirPath()).filePath("dictionary/ecdict.sqlite");
        QString meaning,phonetic;bool found=false;
        if(!QFileInfo::exists(path))meaning=tr("离线词库未安装。可手动填写释义。");
        else {
            const auto name=QUuid::createUuid().toString();
            {
                auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setConnectOptions("QSQLITE_OPEN_READONLY");db.setDatabaseName(path);
                if(db.open()) {
                    QSqlQuery query(db);query.prepare("SELECT phonetic,meaning,definition FROM words WHERE word=? COLLATE NOCASE LIMIT 1");query.addBindValue(word);
                    if(query.exec()&&query.next()) {found=true;phonetic=query.value(0).toString();meaning=query.value(1).toString();if(meaning.isEmpty())meaning=query.value(2).toString();}
                    else meaning=tr("离线词库未收录此词，可手动填写释义。");
                } else meaning=tr("离线词库无法读取：%1").arg(db.lastError().text());
            }
            QSqlDatabase::removeDatabase(name);
        }
        QMetaObject::invokeMethod(this,[this,revision,meaning,phonetic,found] {if(revision!=lookupRevision_)return;meaning_=meaning;phonetic_=phonetic;lookupFound_=found;emit lookupChanged();},Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void OfflineServices::stopSpeaking() { ++speechRevision_; }
void OfflineServices::speak(QString text,QString locale) {
    const auto revision=++speechRevision_;text=text.left(4000);
    QMetaObject::invokeMethod(speechWorker_,[this,text,locale,revision] {
        if(revision!=speechRevision_)return;
        QString error;
#ifdef Q_OS_WIN
        Microsoft::WRL::ComPtr<ISpVoice> voice;
        auto result=CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&voice));
        if(SUCCEEDED(result)) {
            const auto language=locale.isEmpty()?(text.contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]"))?QStringLiteral("zh-CN"):QStringLiteral("en-US")):locale;
            const auto filter=QStringLiteral("Language=%1").arg(QLocale(language).language()==QLocale::Chinese?"804":language=="en-GB"?"809":"409").toStdWString();
            Microsoft::WRL::ComPtr<ISpObjectTokenCategory> category;Microsoft::WRL::ComPtr<IEnumSpObjectTokens> tokens;Microsoft::WRL::ComPtr<ISpObjectToken> token;
            if(SUCCEEDED(CoCreateInstance(CLSID_SpObjectTokenCategory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&category)))
                &&SUCCEEDED(category->SetId(SPCAT_VOICES,FALSE))&&SUCCEEDED(category->EnumTokens(filter.c_str(),nullptr,&tokens))
                &&tokens->Next(1,&token,nullptr)==S_OK) voice->SetVoice(token.Get());
            else if(!locale.isEmpty()) {error=tr("本机未安装 %1 离线语音包。").arg(locale);result=E_FAIL;}
        }
        if(SUCCEEDED(result)) {
            result=voice->Speak(reinterpret_cast<const wchar_t*>(text.utf16()),SPF_ASYNC|SPF_IS_NOT_XML,nullptr);
            while(SUCCEEDED(result) && voice->WaitUntilDone(50)==S_FALSE) {
                if(revision!=speechRevision_) { voice->Speak(nullptr,SPF_PURGEBEFORESPEAK,nullptr);break; }
            }
        }
        if(FAILED(result)&&error.isEmpty())error=tr("离线朗读不可用，请检查系统语音包。");
#else
        error=tr("此平台尚未接入离线语音引擎。");
#endif
        if(!error.isEmpty()) QMetaObject::invokeMethod(this,[this,error]{status_=error;emit changed();},Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void OfflineServices::openAudio(QUrl file) {
    if(!file.isLocalFile())return;
    QMetaObject::invokeMethod(worker_,[this,file,state=native_] {
        QString error;double duration=0;
#ifdef Q_OS_WIN
        if(state->audio)mciSendCommandW(state->audio,MCI_CLOSE,0,0);state->audio=0;
        const auto path=file.toLocalFile().toStdWString();MCI_OPEN_PARMSW open{};open.lpstrElementName=path.c_str();
        const auto result=mciSendCommandW(0,MCI_OPEN,MCI_OPEN_ELEMENT|MCI_WAIT,reinterpret_cast<DWORD_PTR>(&open));
        if(result) error=tr("无法播放该本地音频，系统解码器可能不支持。请尝试 WAV 或 MP3。");
        else {
            state->audio=open.wDeviceID;state->loopStart=state->loopEnd=-1;
            MCI_SET_PARMS format{};format.dwTimeFormat=MCI_FORMAT_MILLISECONDS;
            mciSendCommandW(state->audio,MCI_SET,MCI_SET_TIME_FORMAT,reinterpret_cast<DWORD_PTR>(&format));
            MCI_STATUS_PARMS value{};value.dwItem=MCI_STATUS_LENGTH;mciSendCommandW(state->audio,MCI_STATUS,MCI_STATUS_ITEM,reinterpret_cast<DWORD_PTR>(&value));duration=double(value.dwReturn)/1000;
        }
#else
        error=tr("此平台尚未接入本地音频播放器。");
#endif
        QMetaObject::invokeMethod(this,[this,file,duration,error]{duration_=duration;position_=0;playing_=false;audioName_=error.isEmpty()?QFileInfo(file.toLocalFile()).fileName():QString{};if(!error.isEmpty()){status_=error;emit changed();}emit audioChanged();audioTimer_.start();},Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void OfflineServices::toggleAudio() {
    const bool playing=!playing_;
    QMetaObject::invokeMethod(worker_,[this,playing,state=native_] {
#ifdef Q_OS_WIN
        if(!state->audio)return;
        const auto code=mciSendCommandW(state->audio,playing?MCI_PLAY:MCI_PAUSE,0,0);
        if(code==0)QMetaObject::invokeMethod(this,[this,playing]{playing_=playing;emit audioChanged();},Qt::QueuedConnection);
#endif
    },Qt::QueuedConnection);
}
void OfflineServices::seek(double seconds) {
    if(!std::isfinite(seconds))return;
    const auto ms=static_cast<unsigned long>(std::clamp(seconds,0.0,duration_)*1000);
    const bool playing=playing_;
    QMetaObject::invokeMethod(worker_,[ms,playing,state=native_] {
#ifdef Q_OS_WIN
        if(!state->audio)return;MCI_SEEK_PARMS seek{};seek.dwTo=ms;mciSendCommandW(state->audio,MCI_SEEK,MCI_TO,reinterpret_cast<DWORD_PTR>(&seek));if(playing)mciSendCommandW(state->audio,MCI_PLAY,0,0);
#endif
    },Qt::QueuedConnection);
}
void OfflineServices::setRate(double rate) {
    if(!std::isfinite(rate))return;
    const auto speed=static_cast<unsigned long>(std::clamp(rate,.5,2.0)*1000);
    QMetaObject::invokeMethod(worker_,[this,speed,state=native_] {
#ifdef Q_OS_WIN
        if(!state->audio)return;
        const auto command=L"set "+std::to_wstring(state->audio)+L" speed "+std::to_wstring(speed);
        if(mciSendStringW(command.c_str(),nullptr,0,nullptr)!=0)
            QMetaObject::invokeMethod(this,[this]{status_=tr("当前音频解码器不支持变速。");emit changed();},Qt::QueuedConnection);
#endif
    },Qt::QueuedConnection);
}
void OfflineServices::setLoop(double start,double end) {
    if(!std::isfinite(start)||!std::isfinite(end))return;
    QMetaObject::invokeMethod(worker_,[state=native_,start,end]{state->loopStart=start;state->loopEnd=end>start?end:-1;},Qt::QueuedConnection);
}
void OfflineServices::pollAudio() {
    if(audioPollPending_.exchange(true))return;
    QMetaObject::invokeMethod(worker_,[this,state=native_] {
        struct Reset { std::atomic<bool>& flag; ~Reset(){flag.store(false);} } reset{audioPollPending_};
#ifdef Q_OS_WIN
        if(!state->audio)return;
        MCI_STATUS_PARMS pos{};pos.dwItem=MCI_STATUS_POSITION;
        if(mciSendCommandW(state->audio,MCI_STATUS,MCI_STATUS_ITEM,reinterpret_cast<DWORD_PTR>(&pos)))return;
        MCI_STATUS_PARMS mode{};mode.dwItem=MCI_STATUS_MODE;mciSendCommandW(state->audio,MCI_STATUS,MCI_STATUS_ITEM,reinterpret_cast<DWORD_PTR>(&mode));
        auto seconds=double(pos.dwReturn)/1000;bool playing=mode.dwReturn==MCI_MODE_PLAY;
        if(playing&&state->loopStart>=0&&state->loopEnd>state->loopStart&&seconds>=state->loopEnd) {
            MCI_SEEK_PARMS seek{};seek.dwTo=static_cast<DWORD>(state->loopStart*1000);mciSendCommandW(state->audio,MCI_SEEK,MCI_TO,reinterpret_cast<DWORD_PTR>(&seek));mciSendCommandW(state->audio,MCI_PLAY,0,0);seconds=state->loopStart;
        }
        QMetaObject::invokeMethod(this,[this,seconds,playing]{position_=seconds;playing_=playing;emit audioChanged();},Qt::QueuedConnection);
#endif
    },Qt::QueuedConnection);
}
