#pragma once
#include <QObject>
#include <QVariantList>
#include <QUrl>
#include <QThread>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class WorkspaceState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList books READ books NOTIFY changed)
    Q_PROPERTY(QVariantList words READ words NOTIFY changed)
    Q_PROPERTY(QVariantList bookmarks READ bookmarks NOTIFY changed)
    Q_PROPERTY(QString document READ document WRITE setDocument NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int dueCount READ dueCount NOTIFY changed)
    Q_PROPERTY(int layout READ layout WRITE setLayout NOTIFY preferencesChanged)
    Q_PROPERTY(int toolbar READ toolbar WRITE setToolbar NOTIFY preferencesChanged)
    Q_PROPERTY(bool paper READ paper WRITE setPaper NOTIFY preferencesChanged)
    Q_PROPERTY(bool english READ english WRITE setEnglish NOTIFY preferencesChanged)
public:
    explicit WorkspaceState(QObject* parent=nullptr);
    ~WorkspaceState() override;
    QVariantList books() const { return books_; }
    QVariantList words() const { return words_; }
    QVariantList bookmarks() const;
    QString document() const { return document_; }
    QString error() const { return error_; }
    int dueCount() const;
    int layout() const { return layout_; }
    int toolbar() const { return toolbar_; }
    bool paper() const { return paper_; }
    bool english() const { return english_; }
    void setDocument(QString path);
    void setLayout(int value);
    void setToolbar(int value);
    void setPaper(bool value);
    void setEnglish(bool value);
    Q_INVOKABLE void remember(const QUrl& file, const QString& title, int pages, int page);
    Q_INVOKABLE void updateProgress(const QUrl& file,int pages,int page);
    Q_INVOKABLE void forget(const QUrl& file);
    Q_INVOKABLE bool bookmarked(int page) const;
    Q_INVOKABLE void toggleBookmark(int page);
    Q_INVOKABLE void addWord(QString word, QString meaning, QString context, QString source, int page);
    Q_INVOKABLE void editMeaning(int index, QString meaning);
    Q_INVOKABLE void grade(int index, int grade);
    Q_INVOKABLE void removeWord(int index);
signals:
    void changed();
    void preferencesChanged();
private:
    void save();
    void scheduleSave();
    QVariantMap snapshot() const;
    QVariantList books_, words_;
    QVariantMap bookmarks_;
    QString document_, error_, path_;
    int layout_=0, toolbar_=0;
    bool paper_=false;
    bool english_=false;
    QThread writerThread_;
    QObject* writer_=nullptr;
    QTimer timer_;
    QVariantMap accepted_;
    bool loadBlocked_=false;
};
