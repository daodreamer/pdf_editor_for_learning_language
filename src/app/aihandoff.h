#pragma once
// "Ask an AI": the user drags over part of the reader; the captured picture (page, typed and
// handwritten answers as shown) goes to the clipboard, the chosen chat app opens with a
// learning prompt already in its message box, and the picture is pasted next to it. The message
// is never sent: the user reviews and sends it in the chat app.
//
// Claude (desktop scheme claude:// and claude.ai) and ChatGPT on the web (chatgpt.com) take the
// prompt in the link (q= / prompt=), which only fills the message box; the ChatGPT desktop app
// takes it as a second paste. The paste waits until the chat app is in front, then posts
// Command/Ctrl+V. When the system does not allow that (macOS Accessibility permission), or the
// app does not come to the front in time, the picture stays on the clipboard for a manual paste.
// UI thread only. LinguaPDF itself sends nothing over the network.
#include "aiplatform.h"
#include "aiprompt.h"
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <memory>
#include <QtQml/qqmlregistration.h>

#include <QQuickItem>

class AiHandoff : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool hasCapture READ hasCapture NOTIFY captureChanged)
    // File URL of a reduced copy of the capture for the confirmation popup.
    Q_PROPERTY(QString preview READ preview NOTIFY captureChanged)
    // {id, name, desktop}: one entry per supported chat service, desktop when its app is installed.
    Q_PROPERTY(QVariantList targets READ targets NOTIFY targetsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    // The platform needs a permission for the automatic paste, and whether it is granted now.
    Q_PROPERTY(bool permissionNeeded READ permissionNeeded CONSTANT)
    Q_PROPERTY(bool pasteAllowed READ pasteAllowed NOTIFY stateChanged)
    // Prompt languages ({code, name}) and the one the system UI language maps to (empty: none).
    Q_PROPERTY(QVariantList promptLanguages READ promptLanguages CONSTANT)
    Q_PROPERTY(QString systemLanguage READ systemLanguage CONSTANT)
public:
    struct Backend {
        std::function<AiPlatform::App(const QString&)> desktopApp;
        std::function<QString()> frontmost;
        std::function<QString()> self;
        std::function<QString()> browser;
        std::function<bool(bool)> pasteAllowed;
        std::function<bool()> paste;
        std::function<bool(const QUrl&)> open;
        std::function<void(const QImage&)> setImage;
        std::function<void(const QString&)> setText;
        QString permissionUrl;
    };
    // Milliseconds. A chat page in the browser needs longer than an app before its message box
    // accepts a paste; a second paste waits until the app has read the clipboard.
    struct Timing { int poll=100; int wait=10000; int settleApp=1000; int settleWeb=3500; int between=700; };
    explicit AiHandoff(QObject* parent=nullptr);
    AiHandoff(Backend backend,Timing timing,QObject* parent=nullptr);
    ~AiHandoff() override;

    bool hasCapture() const { return !capture_.isNull(); }
    QString preview() const { return preview_; }
    QImage captured() const { return capture_; }
    QVariantList targets() const { return targets_; }
    bool busy() const { return timer_.isActive(); }
    QString status() const { return status_; }
    bool permissionNeeded() const { return !backend_.permissionUrl.isEmpty(); }
    bool pasteAllowed() const;
    QVariantList promptLanguages() const { return AiPrompt::languageList(); }
    QString systemLanguage() const;
    // choice: "app", "system" or a language code (see AiPrompt::resolve).
    Q_INVOKABLE QString promptLanguage(const QString& choice,bool englishInterface) const;
    Q_INVOKABLE QString prompt(const QString& language,const QString& target,const QString& title,int page) const;

    // Grabs the window and keeps the part under rect (item coordinates). False when the window
    // cannot be grabbed or the area is too small.
    Q_INVOKABLE bool capture(QQuickItem* item,const QRectF& rect);
    bool setCapture(QImage image);
    Q_INVOKABLE void clearCapture();
    Q_INVOKABLE void refreshTargets();
    // Starts the hand-off of the current capture; false when nothing is captured or the chat
    // cannot be opened. finished() reports whether the picture was pasted automatically.
    Q_INVOKABLE bool send(const QString& target,const QString& prompt);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openPermissionSettings() const;
    // Link that opens a new chat with prompt in the message box (not sent).
    static QUrl chatUrl(const QString& target,bool desktop,const QString& prompt);
signals:
    void captureChanged();
    void targetsChanged();
    void stateChanged();
    void finished(bool pasted);
private:
    void step();
    void finish(bool pasted,QString message);
    QString displayName(const QString& target,bool desktop) const;
    QString manualHint() const;
    void removePreview();
    Backend backend_;
    Timing timing_;
    QImage capture_;
    QString preview_;
    std::unique_ptr<QTemporaryDir> previewDir_;
    int previewCount_=0;
    QVariantList targets_;
    QString status_;
    bool prompted_=false;
    // Hand-off in progress.
    QTimer timer_;
    QElapsedTimer clock_;
    QString expected_, name_, selfId_;
    QList<QVariant> pastes_; // QImage or QString, in order; the first is on the clipboard already
    int pasted_=0;
    qint64 readyAt_=-1;      // when the app was seen in front and the paste may start
    qint64 nextAt_=0;
    bool web_=false;
};
