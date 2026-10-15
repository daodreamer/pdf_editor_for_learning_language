#pragma once
#include "annotationdata.h"
#include <QObject>
#include <QLockFile>
#include <memory>

class StorageWorker final : public QObject {
    Q_OBJECT
public:
    explicit StorageWorker(QString root) : root_(std::move(root)) {}
    void open(quint64 session, const QString& path);
    void save(quint64 session, AnnotationSnapshot snapshot);
    void close();
    static QByteArray digest(const QString& path);
    static QByteArray encode(const AnnotationSnapshot&, const QByteArray& identity);
    static bool decode(const QByteArray&, const QByteArray& identity, AnnotationSnapshot&, QString& error);
signals:
    void loaded(quint64 session, AnnotationSnapshot snapshot, QByteArray identity, QString message, bool writable);
    void saved(quint64 session, quint64 revision, QString error);
private:
    QString root_, path_, file_;
    QByteArray identity_;
    qint64 sourceSize_ = 0, sourceModified_ = 0;
    quint64 session_ = 0;
    bool writable_ = false;
    std::unique_ptr<QLockFile> lock_;
};
