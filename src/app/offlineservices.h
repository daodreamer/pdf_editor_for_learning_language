#pragma once
#include <QObject>
#include <QImage>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QUrl>
#include <atomic>
#include <memory>
#include <QtQml/qqmlregistration.h>
class QTranslator;

class OfflineServices : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString document READ document NOTIFY changed)
    Q_PROPERTY(int page READ page NOTIFY changed)
    Q_PROPERTY(QVariantList words READ words NOTIFY changed)
    Q_PROPERTY(QStringList languages READ languages NOTIFY changed)
    Q_PROPERTY(QString meaning READ meaning NOTIFY lookupChanged)
    Q_PROPERTY(QString phonetic READ phonetic NOTIFY lookupChanged)
    Q_PROPERTY(QString lookupWord READ lookupWord NOTIFY lookupChanged)
    Q_PROPERTY(bool lookupFound READ lookupFound NOTIFY lookupChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY audioChanged)
    Q_PROPERTY(double position READ position NOTIFY audioChanged)
    Q_PROPERTY(double duration READ duration NOTIFY audioChanged)
    Q_PROPERTY(QString audioName READ audioName NOTIFY audioChanged)
    Q_PROPERTY(QImage preview READ preview NOTIFY previewChanged)
    Q_PROPERTY(double previewRatio READ previewRatio NOTIFY previewChanged)
    Q_PROPERTY(bool english READ english WRITE setEnglish NOTIFY languageChanged)
public:
    explicit OfflineServices(QObject* parent=nullptr);
    ~OfflineServices() override;
    bool busy() const { return busy_; }
    QString status() const { return status_; }
    QString text() const { return text_; }
    QString document() const { return document_; }
    int page() const { return page_; }
    QVariantList words() const { return words_; }
    QStringList languages() const { return languages_; }
    QString meaning() const { return meaning_; }
    QString phonetic() const { return phonetic_; }
    QString lookupWord() const { return lookupWord_; }
    bool lookupFound() const { return lookupFound_; }
    bool playing() const { return playing_; }
    double position() const { return position_; }
    double duration() const { return duration_; }
    QString audioName() const { return audioName_; }
    QImage preview() const { return preview_; }
    double previewRatio() const { return preview_.isNull()?1.414:double(preview_.height())/preview_.width(); }
    bool english() const { return english_; }
    void setEnglish(bool value);
    Q_INVOKABLE void previewScan(QImage image,QVariantMap options);
    Q_INVOKABLE void recognize(QImage image, QString document, int page, QString language);
    Q_INVOKABLE void invalidate();
    Q_INVOKABLE void lookup(QString word);
    Q_INVOKABLE void speak(QString text,QString locale=QString());
    Q_INVOKABLE void stopSpeaking();
    Q_INVOKABLE void openAudio(QUrl file);
    Q_INVOKABLE void toggleAudio();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void setRate(double rate);
    Q_INVOKABLE void setLoop(double start, double end);
signals:
    void changed();
    void lookupChanged();
    void audioChanged();
    void previewChanged();
    void languageChanged();
private:
    void pollAudio();
    struct Native;
    std::shared_ptr<Native> native_;
    QThread thread_;
    QThread speechThread_;
    QObject* worker_=nullptr;
    QObject* speechWorker_=nullptr;
    QTimer audioTimer_;
    std::atomic<quint64> ocrRevision_{0},speechRevision_{0},lookupRevision_{0};
    bool busy_=false,playing_=false;
    bool lookupFound_=false;
    int page_=0;
    QString status_,text_,document_,meaning_,phonetic_,lookupWord_,audioName_;
    QStringList languages_;
    QVariantList words_;
    double position_=0,duration_=0;
    QImage preview_;
    std::atomic<quint64> previewRevision_{0};
    std::atomic<bool> audioPollPending_{false};
    bool english_=false;
    std::unique_ptr<QTranslator> translator_;
};
