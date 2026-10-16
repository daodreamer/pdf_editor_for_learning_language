#include "workspacestate.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <QElapsedTimer>

WorkspaceState::WorkspaceState(QObject* parent):QObject(parent) {
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
                english_=data.value("english").toBool();
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
    return {{"version",1},{"books",books_},{"words",words_},{"bookmarks",bookmarks_},{"layout",layout_},{"toolbar",toolbar_},{"paper",paper_},{"english",english_}};
}
void WorkspaceState::scheduleSave() {
    const auto candidate=snapshot();
    if(QJsonDocument::fromVariant(candidate).toJson(QJsonDocument::Compact).size()>8*1024*1024) {
        books_=accepted_.value("books").toList();words_=accepted_.value("words").toList();bookmarks_=accepted_.value("bookmarks").toMap();
        layout_=accepted_.value("layout").toInt();toolbar_=accepted_.value("toolbar").toInt();paper_=accepted_.value("paper").toBool();
        english_=accepted_.value("english").toBool();
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
void WorkspaceState::setEnglish(bool v) { if(english_==v)return;english_=v;emit preferencesChanged();scheduleSave(); }
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
