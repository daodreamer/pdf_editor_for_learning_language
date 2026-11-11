#include "offlineservices.h"
#include "naturalspeech.h"
#include "scancleanup.h"
#include "frenchdictionary.h"
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
    lookupWorker_=new QObject;lookupWorker_->moveToThread(&lookupThread_);
    connect(&lookupThread_,&QThread::finished,lookupWorker_,&QObject::deleteLater);
    lookupThread_.setObjectName("Offline dictionary");lookupThread_.start();
    QMetaObject::invokeMethod(speechWorker_,[this]{
#ifdef Q_OS_WIN
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        QVariantList voices;
        if(NaturalSpeech::installed()) {
            voices.append(QVariantMap{{"label",tr("男声 · 自然法语（离线）")},{"id",QStringLiteral("natural:fr:male")},{"locale",QStringLiteral("fr-FR")}});
            voices.append(QVariantMap{{"label",tr("女声 · 自然法语（离线）")},{"id",QStringLiteral("natural:fr:female")},{"locale",QStringLiteral("fr-FR")}});
        }
        Microsoft::WRL::ComPtr<ISpObjectTokenCategory> category;
        Microsoft::WRL::ComPtr<IEnumSpObjectTokens> tokens;
        if(SUCCEEDED(CoCreateInstance(CLSID_SpObjectTokenCategory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&category)))
            && SUCCEEDED(category->SetId(SPCAT_VOICES,FALSE)) && SUCCEEDED(category->EnumTokens(nullptr,nullptr,&tokens))) {
            Microsoft::WRL::ComPtr<ISpObjectToken> token;
            while(tokens->Next(1,&token,nullptr)==S_OK) {
                wchar_t *name=nullptr,*id=nullptr,*language=nullptr;
                Microsoft::WRL::ComPtr<ISpDataKey> attributes;
                token->GetStringValue(nullptr,&name);token->GetId(&id);
                QString locale;
                if(SUCCEEDED(token->OpenKey(L"Attributes",&attributes)) && SUCCEEDED(attributes->GetStringValue(L"Language",&language))) {
                    wchar_t tag[LOCALE_NAME_MAX_LENGTH]{};
                    const auto first=QString::fromWCharArray(language).split(';').first();
                    if(LCIDToLocaleName(first.toUInt(nullptr,16),tag,LOCALE_NAME_MAX_LENGTH,0))locale=QString::fromWCharArray(tag);
                }
                if(name && id)voices.append(QVariantMap{{"label",QString::fromWCharArray(name)},{"id",QString::fromWCharArray(id)},{"locale",locale}});
                CoTaskMemFree(name);CoTaskMemFree(id);CoTaskMemFree(language);token.Reset();
            }
        }
        QMetaObject::invokeMethod(this,[this,voices]{speechVoices_=voices;emit speechChanged();},Qt::QueuedConnection);
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
    speechIdleTimer_.setSingleShot(true);speechIdleTimer_.setInterval(180000);
    connect(&speechIdleTimer_,&QTimer::timeout,this,[this]{
        const auto revision=speechRevision_.load();
        QMetaObject::invokeMethod(speechWorker_,[this,revision]{if(revision==speechRevision_)natural_.reset();},Qt::QueuedConnection);
    });
    if(qEnvironmentVariableIsSet("LANGLERN_STARTUP_DETAIL"))qInfo("offline_service_construct_ms=%.3f",initialization.nsecsElapsed()/1e6);
}
OfflineServices::~OfflineServices() {
    if(translator_)QCoreApplication::removeTranslator(translator_.get());
    ++ocrRevision_;++speechRevision_;++lookupRevision_;++previewRevision_;
    QMetaObject::invokeMethod(speechWorker_,[this]{
        natural_.reset();
#ifdef Q_OS_WIN
        CoUninitialize();
#endif
    },Qt::BlockingQueuedConnection);
    speechThread_.quit();speechThread_.wait();
    lookupThread_.quit();lookupThread_.wait();
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
namespace {
QString dictionaryPath(const char* variable, const QString& name) {
    auto path=qEnvironmentVariable(variable);
    return path.isEmpty()?QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("dictionary/")+name):path;
}
}
void OfflineServices::setLookupLanguage(QString language) {
    language=language==QLatin1String("en")?QStringLiteral("en"):QStringLiteral("fr");
    if(lookupLanguage_==language)return;
    lookupLanguage_=std::move(language);emit lookupLanguageChanged();
    if(!lookupWord_.isEmpty())lookup(lookupWord_);
}
QUrl OfflineServices::externalDictionaryUrl(const QString& lemma) const {
    const auto word=lemma.trimmed();
    if(word.isEmpty()||lookupLanguage_!=QLatin1String("fr"))return {};
    return QUrl(QStringLiteral("https://www.frdic.com/dicts/fr/")+QString::fromLatin1(QUrl::toPercentEncoding(word)));
}
void OfflineServices::lookup(QString word) {
    word=word.trimmed().left(200);word.remove(QRegularExpression("^[^\\p{L}]+|[^\\p{L}]+$"));
    lookupWord_=word;meaning_.clear();phonetic_.clear();lookupEntries_.clear();lookupFound_=false;lookupBusy_=!word.isEmpty();emit lookupChanged();
    if(word.isEmpty())return;
    const auto revision=++lookupRevision_;
    QMetaObject::invokeMethod(lookupWorker_,[this,word,revision,french=lookupLanguage_==QLatin1String("fr")] {
        if(revision!=lookupRevision_)return;
        const auto path=french?dictionaryPath("LANGLERN_FR_DICTIONARY",QStringLiteral("fr-wiktionary.sqlite")):dictionaryPath("LANGLERN_DICTIONARY",QStringLiteral("ecdict.sqlite"));
        QString meaning,phonetic;QVariantList entries;
        if(!QFileInfo::exists(path))meaning=french?tr("法语离线词库未安装。可手动填写释义，或在法语助手中查看。"):tr("离线词库未安装。可手动填写释义。");
        else {
            const auto name=QUuid::createUuid().toString();
            {
                auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setConnectOptions("QSQLITE_OPEN_READONLY");db.setDatabaseName(path);
                if(!db.open())meaning=tr("离线词库无法读取：%1").arg(db.lastError().text());
                else if(french) {
                    entries=FrenchDictionary::lookup(db,word);
                    if(entries.isEmpty())meaning=tr("离线词库未收录此词，可手动填写释义，或在法语助手中查看。");
                } else {
                    QSqlQuery query(db);query.prepare("SELECT phonetic,meaning,definition FROM words WHERE word=? COLLATE NOCASE LIMIT 1");query.addBindValue(word);
                    if(query.exec()&&query.next()) {
                        phonetic=query.value(0).toString();meaning=query.value(1).toString();if(meaning.isEmpty())meaning=query.value(2).toString();
                        entries.append(QVariantMap{{"lemma",word},{"ipa",phonetic},{"glosses",meaning.split(u'\n',Qt::SkipEmptyParts)},{"posLabel",QString()},{"analyses",QStringList()}});
                    } else meaning=tr("离线词库未收录此词，可手动填写释义。");
                }
            }
            QSqlDatabase::removeDatabase(name);
        }
        if(french&&!entries.isEmpty()) {
            const auto first=entries.first().toMap();
            phonetic=first.value("ipa").toString();
            meaning=first.value("glosses").toStringList().mid(0,4).join(u'\n');
        }
        QMetaObject::invokeMethod(this,[this,revision,meaning,phonetic,entries] {
            if(revision!=lookupRevision_)return;
            meaning_=meaning;phonetic_=phonetic;lookupEntries_=entries;lookupFound_=!entries.isEmpty();lookupBusy_=false;emit lookupChanged();
        },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void OfflineServices::stopSpeaking() { ++speechRevision_;speaking_=false;speechLoading_=false;speechPaused_.store(false);speechIdleTimer_.start();emit speechChanged(); }
void OfflineServices::toggleSpeechPause() { if(speaking_){speechPaused_.store(!speechPaused_.load());emit speechChanged();} }
void OfflineServices::setSpeechRate(double rate) { if(std::isfinite(rate)){speechRate_.store(std::clamp(rate,.5,2.0));emit speechChanged();} }
void OfflineServices::setSpeechLocale(QString locale) { if(speechLocale_!=locale){stopSpeaking();speechLocale_=std::move(locale);speechVoice_.clear();emit speechChanged();} }
void OfflineServices::setSpeechVoice(QString voice) {
    if(speechVoice_==voice)return;
    stopSpeaking();
    if(voice==QLatin1String("natural:fr:male") || voice==QLatin1String("natural:fr:female"))frenchVoice_=voice.mid(11);
    speechVoice_=std::move(voice);emit speechChanged();
}
void OfflineServices::setFrenchVoice(QString voice) {
    voice=voice==QLatin1String("female")?QStringLiteral("female"):QStringLiteral("male");
    if(frenchVoice_==voice)return;
    stopSpeaking();frenchVoice_=std::move(voice);
    if(speechVoice_.startsWith(QLatin1String("natural:fr:")))speechVoice_=QStringLiteral("natural:fr:")+frenchVoice_;
    emit speechChanged();
}
void OfflineServices::speak(QString text,QString locale) {
    if(text.trimmed().isEmpty())return;
    if(text.size()>1024*1024){status_=tr("朗读选区过长，请分段选择。");emit changed();return;}
    speechIdleTimer_.stop();
    const auto revision=++speechRevision_;speaking_=true;speechLoading_=false;speechPaused_.store(false);
    if(locale.isEmpty())locale=speechLocale_;
    QString voiceId;
    const auto target=QLocale(locale);
    // Explicit choice first, then the preferred bundled French voice, then the first voice of the locale.
    auto preferred=locale==speechLocale_?speechVoice_:QString{};
    if(preferred.isEmpty() && target.language()==QLocale::French)preferred=QStringLiteral("natural:fr:")+frenchVoice_;
    for(const auto& entry:speechVoices_) {
        const auto id=entry.toMap()["id"].toString();
        if(id==preferred){voiceId=id;break;}
        if(voiceId.isEmpty() && QLocale(entry.toMap()["locale"].toString()).name()==target.name())voiceId=id;
    }
    const bool natural=voiceId.startsWith(QLatin1String("natural:fr:"));
    speechLoading_=natural;emit speechChanged();
    QMetaObject::invokeMethod(speechWorker_,[this,text,locale,voiceId,natural,revision] {
        if(revision!=speechRevision_)return;
        QString error;
#ifdef Q_OS_WIN
        if(natural) {
            if(!natural_)natural_=std::make_unique<NaturalSpeech>();
            SpeechMetrics metrics;
            const SpeechControl control{speechRevision_,revision,speechPaused_,speechRate_,[this,revision]{
                QMetaObject::invokeMethod(this,[this,revision]{if(revision==speechRevision_){speechLoading_=false;emit speechChanged();}},Qt::QueuedConnection);
            }};
            error=natural_->speak(text,voiceId.endsWith(QLatin1String(":female"))?QString::fromLatin1(NaturalVoices::female):QString::fromLatin1(NaturalVoices::male),control,metrics);
            qInfo("speech_natural request=%llu voice=%s load_ms=%.3f first_pcm_ms=%.3f generation_ms=%.3f audio_s=%.3f played_samples=%llu queue_peak_bytes=%llu canceled=%d",
                revision,qPrintable(voiceId),metrics.loadMs,metrics.firstPcmMs,metrics.generationMs,metrics.audioSeconds,metrics.playedSamples,metrics.peakQueueBytes,metrics.canceled);
            QMetaObject::invokeMethod(this,[this,error,revision]{
                if(revision!=speechRevision_)return;
                speaking_=false;speechLoading_=false;speechPaused_.store(false);speechIdleTimer_.start();emit speechChanged();
                if(!error.isEmpty()){status_=tr("自然法语朗读失败：%1").arg(error);emit changed();}
            },Qt::QueuedConnection);
            return;
        }
        Microsoft::WRL::ComPtr<ISpVoice> voice;
        auto result=CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&voice));
        if(SUCCEEDED(result)) {
            Microsoft::WRL::ComPtr<ISpObjectToken> token;
            if(!voiceId.isEmpty() && SUCCEEDED(CoCreateInstance(CLSID_SpObjectToken,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&token)))
                && SUCCEEDED(token->SetId(nullptr,voiceId.toStdWString().c_str(),FALSE)))result=voice->SetVoice(token.Get());
            else {error=tr("本机朗读引擎没有可用的 %1 离线声音，请在系统中安装对应声音。").arg(locale);result=E_FAIL;}
        }
        bool paused=false;
        for(qsizetype start=0;SUCCEEDED(result) && start<text.size() && revision==speechRevision_;) {
            qsizetype end=std::min(start+2000,text.size());
            if(end<text.size() && text[end].isLowSurrogate())--end;
            const auto chunk=text.mid(start,end-start);start=end;
            voice->SetRate(long(std::round(10*std::log2(speechRate_.load()))));
            result=voice->Speak(reinterpret_cast<const wchar_t*>(chunk.utf16()),SPF_ASYNC|SPF_IS_NOT_XML,nullptr);
            while(SUCCEEDED(result) && voice->WaitUntilDone(30)==S_FALSE) {
                if(revision!=speechRevision_) {voice->Speak(nullptr,SPF_PURGEBEFORESPEAK,nullptr);break;}
                const bool pause=speechPaused_.load();
                if(pause!=paused){if(pause)voice->Pause();else voice->Resume();paused=pause;}
                voice->SetRate(long(std::round(10*std::log2(speechRate_.load()))));
            }
        }
        if(FAILED(result)&&error.isEmpty())error=tr("离线朗读不可用，请检查系统语音包。");
#else
        error=tr("此平台尚未接入离线语音引擎。");
#endif
        QMetaObject::invokeMethod(this,[this,error,revision]{if(revision!=speechRevision_)return;speaking_=false;speechLoading_=false;speechPaused_.store(false);speechIdleTimer_.start();emit speechChanged();if(!error.isEmpty()){status_=error;emit changed();}},Qt::QueuedConnection);
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
