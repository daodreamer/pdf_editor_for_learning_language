#include "storageworker.h"
#include "textstyles.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <QSet>
#include <cmath>
#include <stdexcept>

namespace {
constexpr qint64 maxSnapshotBytes=64*1024*1024;
QJsonArray point(QPointF p) { return {p.x(),p.y()}; }
QPointF point(const QJsonValue& v) {
    const auto a=v.toArray();
    if(a.size()!=2 || !a[0].isDouble() || !a[1].isDouble()) throw std::runtime_error("point");
    const QPointF p(a[0].toDouble(),a[1].toDouble());
    if(!std::isfinite(p.x()) || !std::isfinite(p.y()) || std::abs(p.x())>1e9 || std::abs(p.y())>1e9) throw std::runtime_error("point range");
    return p;
}
double number(const QJsonObject& o,const char* key,double lo,double hi) {
    const auto v=o[key]; const double d=v.toDouble(std::nan(""));
    if(!v.isDouble() || !std::isfinite(d) || d<lo || d>hi) throw std::runtime_error("number");
    return d;
}
QByteArray read(const QString& path) {
    QFile f(path); if(!f.open(QIODevice::ReadOnly) || f.size()>maxSnapshotBytes) return {};
    return f.readAll();
}
bool write(const QString& path,const QByteArray& data) {
    QSaveFile f(path); f.setDirectWriteFallback(false);
    return f.open(QIODevice::WriteOnly) && f.write(data)==data.size() && f.commit();
}
}
QByteArray StorageWorker::digest(const QString& path) {
    QFile f(path); if(!f.open(QIODevice::ReadOnly)) return {};
    const auto size=f.size(); const auto modified=QFileInfo(f).lastModified();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&f) || size!=f.size() || modified!=QFileInfo(f).lastModified()) return {};
    return hash.result().toHex();
}
QByteArray StorageWorker::encode(const AnnotationSnapshot& s,const QByteArray& identity) {
    QJsonArray notes,ink;
    for(const auto& n:s.notes) notes.append(QJsonObject{{"id",n.id},{"page",n.page},{"anchor",point(n.anchor)},
        {"right",point(n.right)},{"down",point(n.down)},{"width",n.width},{"height",n.height},{"size",n.fontSize},
        {"text",n.text},{"family",n.family},{"color",n.color.name(QColor::HexArgb)},{"order",QString::number(n.order)},
        {"widthLocked",n.widthLocked},{"minimumHeight",n.minimumHeight},{"maximumWidth",n.maximumWidth},{"styles",QJsonArray::fromVariantList(encodeTextStyles(n.styles))}});
    for(const auto& i:s.ink) {
        QJsonArray points; for(auto p:i.points) points.append(point(p));
        ink.append(QJsonObject{{"id",i.id},{"page",i.page},{"points",points},{"width",i.width},
            {"color",i.color.name(QColor::HexArgb)},{"order",QString::number(i.order)}});
    }
    const auto payload=QJsonDocument(QJsonObject{{"notes",notes},{"ink",ink}}).toJson(QJsonDocument::Compact);
    return QJsonDocument(QJsonObject{{"schema",1},{"identity",QString::fromLatin1(identity)},
        {"revision",QString::number(s.revision)},{"payload",QString::fromUtf8(payload)},
        {"sha256",QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex())}}).toJson(QJsonDocument::Compact);
}
bool StorageWorker::decode(const QByteArray& bytes,const QByteArray& identity,AnnotationSnapshot& output,QString& error) {
    error.clear();
    try {
        if(bytes.isEmpty() || bytes.size()>maxSnapshotBytes) throw std::runtime_error("size");
        const auto doc=QJsonDocument::fromJson(bytes); const auto o=doc.object();
        if(o["schema"].toDouble(-1)!=1) { error=QStringLiteral("不支持的快照版本；保留文件并禁止覆盖。"); return false; }
        if(o["identity"].toString().toLatin1()!=identity) throw std::runtime_error("identity");
        const auto payload=o["payload"].toString().toUtf8();
        if(o["sha256"].toString().toLatin1()!=QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex()) throw std::runtime_error("checksum");
        const auto data=QJsonDocument::fromJson(payload).object();
        if(!data["notes"].isArray() || !data["ink"].isArray()) throw std::runtime_error("arrays");
        AnnotationSnapshot s; bool ok=false;
        s.revision=o["revision"].toString().toULongLong(&ok); if(!ok || s.revision>=9007199254740991ULL) throw std::runtime_error("revision");
        QSet<QString> ids; QSet<quint64> orders; qsizetype totalPoints=0;
        auto common=[&](const QJsonObject& v,QString& id,int& page,QColor& color) {
            id=v["id"].toString(); color=QColor(v["color"].toString());
            if(QUuid(id).isNull() || ids.contains(id) || !color.isValid()) throw std::runtime_error("id/color");
            ids.insert(id); const double p=number(v,"page",0,1000000);
            if(p!=std::floor(p)) throw std::runtime_error("page"); page=int(p);
        };
        if(data["notes"].toArray().size()+data["ink"].toArray().size()>100000) throw std::runtime_error("count");
        QSet<quint64> textOrders;
        for(const auto& value:data["notes"].toArray()) {
            const auto v=value.toObject(); TextAnnotation n; common(v,n.id,n.page,n.color);
            n.anchor=point(v["anchor"]); n.right=point(v["right"]); n.down=point(v["down"]);
            if(std::abs(QPointF::dotProduct(n.right,n.right)-1)>1e-6 || std::abs(QPointF::dotProduct(n.down,n.down)-1)>1e-6
                || std::abs(QPointF::dotProduct(n.right,n.down))>1e-6) throw std::runtime_error("axes");
            n.width=number(v,"width",1e-6,1e9); n.height=number(v,"height",1e-6,1e9); n.fontSize=number(v,"size",1,256);
            if(v.contains("widthLocked") && !v["widthLocked"].isBool()) throw std::runtime_error("widthLocked");
            n.widthLocked=v["widthLocked"].toBool(true);
            if(v.contains("minimumHeight")) n.minimumHeight=number(v,"minimumHeight",1e-6,1e9);
            if(v.contains("maximumWidth")) n.maximumWidth=number(v,"maximumWidth",0,1e9);
            if(!v["text"].isString()) throw std::runtime_error("text");
            n.text=v["text"].toString(); n.family=v["family"].toString();
            if(v.contains("styles") && (!v["styles"].isArray() || !decodeTextStyles(v["styles"].toArray().toVariantList(),int(n.text.size()),n.styles)))
                throw std::runtime_error("styles");
            n.order=v["order"].toString().toULongLong(&ok);
            if(!ok || n.order>=9007199254740991ULL || textOrders.contains(n.order)) throw std::runtime_error("text order");
            textOrders.insert(n.order);
            if(n.family!=QStringLiteral("Noto Sans SC") || n.text.size()>1000000) throw std::runtime_error("text/font");
            s.notes.append(std::move(n));
        }
        for(const auto& value:data["ink"].toArray()) {
            const auto v=value.toObject(); InkStroke i; common(v,i.id,i.page,i.color);
            i.width=number(v,"width",.25,32); i.order=v["order"].toString().toULongLong(&ok);
            if(!ok || i.order>=9007199254740991ULL || orders.contains(i.order)) throw std::runtime_error("order");
            orders.insert(i.order);
            if(!v["points"].isArray()) throw std::runtime_error("points");
            const auto points=v["points"].toArray(); totalPoints+=points.size();
            if(points.isEmpty() || points.size()>65536 || totalPoints>2000000) throw std::runtime_error("points count");
            for(const auto& p:points) i.points.append(point(p));
            s.ink.append(std::move(i));
        }
        output=std::move(s); return true;
    } catch(const std::exception&) { error=QStringLiteral("快照损坏、身份不符或数据超限；原文件已保留。"); return false; }
}
void StorageWorker::close() { writable_=false; lock_.reset(); session_=0; }
void StorageWorker::open(quint64 session,const QString& path) {
    close(); session_=session; path_=path;
    auto fail=[&](const QString& message) { emit loaded(session,{},identity_,message,false); };
    identity_=digest(path);
    if(identity_.isEmpty()) { fail(QStringLiteral("无法校验 PDF；练习工作区未启用。")); return; }
    const QFileInfo info(path); sourceSize_=info.size(); sourceModified_=info.lastModified().toMSecsSinceEpoch();
    if(!QDir().mkpath(root_)) { fail(QStringLiteral("无法创建本地练习目录。")); return; }
    file_=root_+'/'+QString::fromLatin1(identity_)+".json";
    lock_=std::make_unique<QLockFile>(file_+".lock"); lock_->setStaleLockTime(0);
    if(!lock_->tryLock(0)) { fail(QStringLiteral("另一进程正在使用此练习；请关闭另一窗口后重新打开。")); return; }
    AnnotationSnapshot snapshot; QString error,message;
    if(QFileInfo::exists(file_)) {
        const auto bytes=read(file_);
        const auto schema=QJsonDocument::fromJson(bytes).object().value("schema");
        if(schema.isDouble() && schema.toInt()!=1) { fail(QStringLiteral("快照版本不受支持；保留文件并禁止覆盖。")); return; }
        if(!decode(bytes,identity_,snapshot,error)) {
            if(!decode(read(file_+".bak"),identity_,snapshot,error)) { fail(error); return; }
            const auto evidence=file_+".corrupt-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
            if(!QFile::rename(file_,evidence)) { fail(QStringLiteral("无法保留损坏快照，已禁止覆盖。")); return; }
            message=QStringLiteral("主快照损坏，已从备份恢复；损坏文件已保留。");
        }
    } else if(QFileInfo::exists(file_+".bak")) {
        if(!decode(read(file_+".bak"),identity_,snapshot,error)) { fail(error); return; }
        message=QStringLiteral("已从备份恢复练习。");
    }
    writable_=true;
    const auto pathKey=QCryptographicHash::hash(QFileInfo(path).absoluteFilePath().toUtf8(),QCryptographicHash::Sha256).toHex();
    const auto marker=root_+"/path-"+QString::fromLatin1(pathKey);
    const auto oldIdentity=read(marker);
    if(!oldIdentity.isEmpty() && oldIdentity!=identity_) message+=QStringLiteral("原路径的 PDF 内容已改变；旧练习已保留，当前使用独立工作区。");
    if(!write(marker,identity_)) message+=QStringLiteral("无法记录路径提示，但内容身份与练习保存不受影响。");
    emit loaded(session,snapshot,identity_,message,true);
}
void StorageWorker::save(quint64 session,AnnotationSnapshot snapshot) {
    QString error;
    const QFileInfo source(path_);
    if(session!=session_ || !writable_) error=QStringLiteral("工作区未就绪，未保存。");
    else if(source.size()!=sourceSize_ || source.lastModified().toMSecsSinceEpoch()!=sourceModified_)
        error=QStringLiteral("原 PDF 已改变；请保留当前窗口并恢复原文件后重试。");
    else {
        const auto bytes=encode(snapshot,identity_); AnnotationSnapshot check;
        if(!decode(bytes,identity_,check,error)) {}
        else {
            const auto previous=QFileInfo::exists(file_) ? read(file_) : QByteArray();
            AnnotationSnapshot prior;
            if(QFileInfo::exists(file_) && !decode(previous,identity_,prior,error)) {}
            else if(!previous.isEmpty() && !write(file_+".bak",previous)) error=QStringLiteral("备份写入失败；未替换快照。");
            else if(!write(file_,bytes)) error=QStringLiteral("自动保存失败；请检查磁盘空间或目录权限，然后重试保存。");
        }
    }
    emit saved(session,snapshot.revision,error);
}
