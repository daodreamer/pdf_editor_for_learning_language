#include "workspacestate.h"
#include "inkstroke.h"
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <QElapsedTimer>

static double textSizeValue(const QVariant& v) {
    bool ok=false; const double size=std::round(v.toDouble(&ok));
    return ok && std::isfinite(size) ? std::clamp(size,AnnotationConfig::minTextSize,AnnotationConfig::maxTextSize) : AnnotationConfig::defaultTextSize;
}
static QString textColorValue(const QVariant& v) {
    const QColor color(v.toString());
    return color.isValid() ? color.name(QColor::HexRgb) : QStringLiteral("#1e468c");
}
// Short language codes and system source ids only; anything else in the file is dropped.
static QVariantMap inputChoicesValue(const QVariant& v) {
    QVariantMap result; const auto map=v.toMap();
    for(auto it=map.begin();it!=map.end() && result.size()<32;++it) {
        const auto id=it.value().toString();
        if(it.key().size()<=8 && !it.key().isEmpty() && !id.isEmpty() && id.size()<=256) result.insert(it.key(),id);
    }
    return result;
}
static QStringList inputNoticesValue(const QVariant& v) {
    QStringList result;
    for(const auto& value:v.toList()) if(const auto code=value.toString(); !code.isEmpty() && code.size()<=8 && !result.contains(code) && result.size()<32) result.append(code);
    return result;
}
static double inkWidthValue(const QVariant& v) {
    bool ok=false; const double width=std::round(v.toDouble(&ok)*2)/2; // slider step 0.5 pt
    return ok && std::isfinite(width) ? std::clamp(width,AnnotationConfig::minInkWidth,AnnotationConfig::maxInkWidth) : AnnotationConfig::defaultInkWidth;
}
WorkspaceState::WorkspaceState(QObject* parent):QObject(parent),textSize_(AnnotationConfig::defaultTextSize),inkWidth_(AnnotationConfig::defaultInkWidth) {
    QElapsedTimer initialization;initialization.start();
    auto root=qEnvironmentVariable("LANGLERN_WORKSPACE_DIR");
    if(root.isEmpty()) root=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    path_=QDir(root).filePath("library-v1.json");
    QFile file(path_);
    if(file.exists()) {
        if(file.open(QIODevice::ReadOnly) && file.size()<=8*1024*1024) {
            QJsonParseError failure;
            const auto doc=QJsonDocument::fromJson(file.readAll(),&failure);
            const auto data=doc.toVariant().toMap();
            if(failure.error==QJsonParseError::NoError && data.value("version").toInt()==1) {
                books_=data.value("books").toList(); words_=data.value("words").toList();
                bookmarks_=data.value("bookmarks").toMap();
                layout_=qBound(0,data.value("layout").toInt(),2); toolbar_=qBound(0,data.value("toolbar").toInt(),2);
                paper_=data.value("paper").toBool();
                nightDim_=data.value("nightDim",true).toBool();
                english_=data.value("english").toBool();
                lookupLanguage_=data.value("lookupLanguage").toString()==QLatin1String("en")?QStringLiteral("en"):QStringLiteral("fr");
                frenchVoice_=data.value("frenchVoice").toString()==QLatin1String("female")?QStringLiteral("female"):QStringLiteral("male");
                if(data.contains("textSize")) textSize_=textSizeValue(data.value("textSize"));
                if(data.contains("inkWidth")) inkWidth_=inkWidthValue(data.value("inkWidth"));
                if(data.contains("textColor")) textColor_=textColorValue(data.value("textColor"));
                textBold_=data.value("textBold").toBool(); textItalic_=data.value("textItalic").toBool();
                checkMark_=qBound(0,data.value("checkMark").toInt(),2);
                inputSwitching_=data.value("inputSwitching",true).toBool();
                inputChoices_=inputChoicesValue(data.value("inputChoices"));
                inputNoticesOff_=inputNoticesValue(data.value("inputNoticesOff"));
            } else error_=tr("书架数据无法读取，原文件已保留。本次不自动覆盖。");
        } else error_=tr("书架数据无法读取，原文件已保留。本次不自动覆盖。");
    }
    writer_=new QObject;
    loadBlocked_=!error_.isEmpty();accepted_=snapshot();
    writer_->moveToThread(&writerThread_);
    connect(&writerThread_,&QThread::finished,writer_,&QObject::deleteLater);
    writerThread_.setObjectName("Library storage"); writerThread_.start();
    timer_.setSingleShot(true); timer_.setInterval(400);
    connect(&timer_,&QTimer::timeout,this,&WorkspaceState::save);
    if(qEnvironmentVariableIsSet("LANGLERN_STARTUP_DETAIL"))qInfo("library_construct_ms=%.3f",initialization.nsecsElapsed()/1e6);
}
WorkspaceState::~WorkspaceState() {
    if(timer_.isActive()) save();
    QMetaObject::invokeMethod(writer_,[]{},Qt::BlockingQueuedConnection);
    writerThread_.quit(); writerThread_.wait();
}
void WorkspaceState::save() {
    if(loadBlocked_) return;
    const auto bytes=QJsonDocument::fromVariant(snapshot()).toJson(QJsonDocument::Compact);
    const auto path=path_;
    QMetaObject::invokeMethod(writer_,[this,path,bytes] {
        QString error;
        if(!QDir().mkpath(QFileInfo(path).absolutePath())) error=tr("无法创建书架数据目录");
        QSaveFile file(path);
        if(error.isEmpty() && (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()))
            error=tr("书架保存失败：%1").arg(file.errorString());
        if(!error.isEmpty()) QMetaObject::invokeMethod(this,[this,error] { error_=error; emit changed(); },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
QVariantMap WorkspaceState::snapshot() const {
    return {{"version",1},{"books",books_},{"words",words_},{"bookmarks",bookmarks_},{"layout",layout_},{"toolbar",toolbar_},{"paper",paper_},{"nightDim",nightDim_},{"english",english_},{"lookupLanguage",lookupLanguage_},{"frenchVoice",frenchVoice_},{"textSize",textSize_},{"inkWidth",inkWidth_},
        {"textColor",textColor_},{"textBold",textBold_},{"textItalic",textItalic_},{"checkMark",checkMark_},
        {"inputSwitching",inputSwitching_},{"inputChoices",inputChoices_},{"inputNoticesOff",inputNoticesOff_}};
}
void WorkspaceState::scheduleSave() {
    const auto candidate=snapshot();
    if(QJsonDocument::fromVariant(candidate).toJson(QJsonDocument::Compact).size()>8*1024*1024) {
        books_=accepted_.value("books").toList();words_=accepted_.value("words").toList();bookmarks_=accepted_.value("bookmarks").toMap();
        layout_=accepted_.value("layout").toInt();toolbar_=accepted_.value("toolbar").toInt();paper_=accepted_.value("paper").toBool();nightDim_=accepted_.value("nightDim",true).toBool();
        english_=accepted_.value("english").toBool();lookupLanguage_=accepted_.value("lookupLanguage",QStringLiteral("fr")).toString();
        frenchVoice_=accepted_.value("frenchVoice",QStringLiteral("male")).toString();
        textSize_=textSizeValue(accepted_.value("textSize"));inkWidth_=inkWidthValue(accepted_.value("inkWidth"));
        textColor_=textColorValue(accepted_.value("textColor"));textBold_=accepted_.value("textBold").toBool();
        textItalic_=accepted_.value("textItalic").toBool();checkMark_=qBound(0,accepted_.value("checkMark").toInt(),2);
        inputSwitching_=accepted_.value("inputSwitching",true).toBool();inputChoices_=inputChoicesValue(accepted_.value("inputChoices"));
        inputNoticesOff_=inputNoticesValue(accepted_.value("inputNoticesOff"));
        error_=tr("书架数据已达到 8 MiB 上限，本次修改未保存。请删除不再需要的生词或笔记。");emit changed();emit preferencesChanged();return;
    }
    accepted_=candidate;if(!loadBlocked_)error_.clear();timer_.start();
}
void WorkspaceState::updateProgress(const QUrl& file,int pages,int page) {
    for(const auto& value:books_) {const auto book=value.toMap();if(book.value("url").toString()==file.toString()){remember(file,book.value("title").toString(),pages,page);return;}}
}
void WorkspaceState::setDocument(QString path) { if(document_==path)return; document_=std::move(path); emit changed(); }
void WorkspaceState::setLayout(int v) { v=qBound(0,v,2); if(layout_==v)return;layout_=v;emit preferencesChanged();scheduleSave(); }
void WorkspaceState::setToolbar(int v) { v=qBound(0,v,2);if(toolbar_==v)return;toolbar_=v;emit preferencesChanged();scheduleSave(); }
void WorkspaceState::setPaper(bool v) { if(paper_==v)return;paper_=v;emit preferencesChanged();scheduleSave(); }
void WorkspaceState::setNightDim(bool v) { if(nightDim_==v)return;nightDim_=v;emit preferencesChanged();scheduleSave(); }
void WorkspaceState::setEnglish(bool v) { if(english_==v)return;english_=v;emit preferencesChanged();scheduleSave(); }
void WorkspaceState::setFrenchVoice(QString v) { v=v==QLatin1String("female")?QStringLiteral("female"):QStringLiteral("male");if(frenchVoice_==v)return;frenchVoice_=v;emit preferencesChanged();scheduleSave(); }
void WorkspaceState::setLookupLanguage(QString v) { v=v==QLatin1String("en")?QStringLiteral("en"):QStringLiteral("fr");if(lookupLanguage_==v)return;lookupLanguage_=v;emit preferencesChanged();scheduleSave(); }
void WorkspaceState::remember(const QUrl& file,const QString& title,int pages,int page) {
    if(!file.isLocalFile() || pages<1)return;
    const auto url=file.toString();
    int old=-1;
    for(int i=0;i<books_.size();++i) if(books_[i].toMap().value("url").toString()==url) {old=i;break;}
    auto book=old>=0?books_[old].toMap():QVariantMap{};
    if(old==0 && book.value("page").toInt()==page && book.value("pages").toInt()==pages)return;
    if(old>=0)books_.removeAt(old);
    book.insert("url",url);book.insert("title",title);book.insert("pages",pages);book.insert("page",page);
    books_.prepend(book);while(books_.size()>100)books_.removeLast();
    emit changed();scheduleSave();
}
void WorkspaceState::forget(const QUrl& file) {
    for(int i=0;i<books_.size();++i) if(books_[i].toMap().value("url").toString()==file.toString()) {books_.removeAt(i);emit changed();scheduleSave();return;}
}
QVariantList WorkspaceState::bookmarks() const { return bookmarks_.value(document_).toList(); }
bool WorkspaceState::bookmarked(int page) const { return bookmarks().contains(page); }
void WorkspaceState::toggleBookmark(int page) {
    if(document_.isEmpty() || page<1)return;
    auto list=bookmarks();if(list.contains(page))list.removeAll(page);else list.append(page);
    std::sort(list.begin(),list.end(),[](const QVariant& a,const QVariant& b){return a.toInt()<b.toInt();});
    bookmarks_[document_]=list;emit changed();scheduleSave();
}
int WorkspaceState::dueCount() const {
    int n=0;const auto now=QDateTime::currentSecsSinceEpoch();
    for(const auto& value:words_) if(value.toMap().value("due").toLongLong()<=now)++n;
    return n;
}
void WorkspaceState::addWord(QString word,QString meaning,QString context,QString source,int page) {
    word=word.trimmed().left(200);if(word.isEmpty() || words_.size()>=5000)return;
    for(const auto& value:words_) if(value.toMap().value("word").toString().compare(word,Qt::CaseInsensitive)==0)return;
    words_.prepend(QVariantMap{{"word",word},{"meaning",meaning.left(4000)},{"context",context.left(4000)},
        {"source",source},{"page",page},{"url",document_},{"grade",0},{"due",0},{"interval",0}});
    emit changed();scheduleSave();
}
void WorkspaceState::editMeaning(int index,QString meaning) { if(index<0||index>=words_.size())return;auto word=words_[index].toMap();word["meaning"]=meaning.left(4000);words_[index]=word;emit changed();scheduleSave(); }
void WorkspaceState::grade(int index,int grade) {
    if(index<0||index>=words_.size()||grade<1||grade>3)return;
    auto word=words_[index].toMap();const int old=word.value("interval").toInt();
    const int days=grade==1?0:grade==2?1:qMin(365,qMax(3,old*2));
    word["grade"]=grade;word["interval"]=days;word["due"]=QDateTime::currentSecsSinceEpoch()+(days?days*86400:600);
    words_[index]=word;emit changed();scheduleSave();
}
void WorkspaceState::removeWord(int index) { if(index<0||index>=words_.size())return;words_.removeAt(index);emit changed();scheduleSave(); }
void WorkspaceState::setTextSize(double v) { if(!std::isfinite(v))return;v=textSizeValue(v);if(textSize_==v)return;textSize_=v;emit preferencesChanged();scheduleSave(); }
void WorkspaceState::setTextColor(QString v) { v=textColorValue(v);if(textColor_==v)return;textColor_=v;emit preferencesChanged();scheduleSave(); }
void WorkspaceState::setTextBold(bool v) { if(textBold_==v)return;textBold_=v;emit preferencesChanged();scheduleSave(); }
void WorkspaceState::setTextItalic(bool v) { if(textItalic_==v)return;textItalic_=v;emit preferencesChanged();scheduleSave(); }
void WorkspaceState::setCheckMark(int v) { v=qBound(0,v,2);if(checkMark_==v)return;checkMark_=v;emit preferencesChanged();scheduleSave(); }
void WorkspaceState::setInkWidth(double v) { if(!std::isfinite(v))return;v=inkWidthValue(v);if(inkWidth_==v)return;inkWidth_=v;emit preferencesChanged();scheduleSave(); }
void WorkspaceState::setInputSwitching(bool v) { if(inputSwitching_==v)return;inputSwitching_=v;emit preferencesChanged();scheduleSave(); }
void WorkspaceState::setInputChoice(const QString& language,const QString& id) {
    if(language.isEmpty() || language.size()>8 || id.size()>256) return;
    if(id.isEmpty() ? !inputChoices_.contains(language) : inputChoices_.value(language).toString()==id) return;
    if(id.isEmpty()) inputChoices_.remove(language); else inputChoices_.insert(language,id);
    emit preferencesChanged();scheduleSave();
}
void WorkspaceState::silenceInputNotice(const QString& language) {
    if(language.isEmpty() || language.size()>8 || inputNoticesOff_.contains(language)) return;
    inputNoticesOff_.append(language);emit preferencesChanged();scheduleSave();
}
