#include "aihandoff.h"
#include <QFile>
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QLocale>
#include <QQuickItem>
#include <QQuickWindow>

namespace {
AiHandoff::Backend systemBackend() {
    AiHandoff::Backend backend;
    backend.desktopApp=&AiPlatform::desktopApp;
    backend.frontmost=&AiPlatform::frontmost;
    backend.self=&AiPlatform::self;
    backend.browser=&AiPlatform::browser;
    backend.pasteAllowed=&AiPlatform::pasteAllowed;
    backend.paste=&AiPlatform::paste;
    backend.open=[](const QUrl& url) { return QDesktopServices::openUrl(url); };
    backend.setImage=[](const QImage& image) { QGuiApplication::clipboard()->setImage(image); };
    backend.setText=[](const QString& text) { QGuiApplication::clipboard()->setText(text); };
    backend.permissionUrl=AiPlatform::permissionSettingsUrl();
    return backend;
}
// Percent-encodes everything outside the unreserved set, so '+', '&' and '#' in the prompt stay
// text for the page's URLSearchParams.
QUrl withPrompt(const QString& base,const QString& key,const QString& prompt) {
    return QUrl(base+key+u'='+QString::fromLatin1(QUrl::toPercentEncoding(prompt)),QUrl::StrictMode);
}
}

AiHandoff::AiHandoff(QObject* parent) : AiHandoff(systemBackend(),Timing{},parent) {}

AiHandoff::AiHandoff(Backend backend,Timing timing,QObject* parent)
    : QObject(parent),backend_(std::move(backend)),timing_(timing) {
    timer_.setInterval(timing_.poll);
    connect(&timer_,&QTimer::timeout,this,&AiHandoff::step);
    refreshTargets();
}

AiHandoff::~AiHandoff()=default;

QString AiHandoff::systemLanguage() const { return AiPrompt::fromSystem(QLocale::system().uiLanguages()); }

QString AiHandoff::promptLanguage(const QString& choice,bool englishInterface) const {
    return AiPrompt::resolve(choice,englishInterface,QLocale::system().uiLanguages());
}

QString AiHandoff::prompt(const QString& language,const QString& target,const QString& title,int page) const {
    return AiPrompt::build(language,target,title,page);
}

bool AiHandoff::pasteAllowed() const { return backend_.pasteAllowed && backend_.pasteAllowed(false); }

bool AiHandoff::capture(QQuickItem* item,const QRectF& rect) {
    if(!item || !item->window() || !rect.isValid()) return false;
    QQuickWindow* window=item->window();
    const QRectF scene=item->mapRectToScene(rect.normalized());
    const QImage frame=window->grabWindow();
    if(frame.isNull() || window->width()<=0) return false;
    // Device pixels per scene unit, from the grab itself (Retina / Windows scaling).
    const qreal scale=qreal(frame.width())/window->width();
    const QRect pixels=QRectF(scene.x()*scale,scene.y()*scale,scene.width()*scale,scene.height()*scale)
        .toAlignedRect().intersected(frame.rect());
    if(pixels.width()<8 || pixels.height()<8) return false;
    return setCapture(frame.copy(pixels));
}

bool AiHandoff::setCapture(QImage image) {
    if(image.isNull()) return false;
    image.setDevicePixelRatio(1);
    capture_=image.convertToFormat(QImage::Format_RGB32);
    // A file per capture: Image caches by URL, and Qt Quick does not decode data: URLs.
    removePreview();
    if(!previewDir_) previewDir_=std::make_unique<QTemporaryDir>();
    const QString path=previewDir_->isValid() ? previewDir_->filePath(QStringLiteral("capture-%1.png").arg(++previewCount_)) : QString{};
    if(!path.isEmpty() && capture_.scaled(QSize(1040,640).boundedTo(capture_.size()),Qt::KeepAspectRatio,Qt::SmoothTransformation).save(path,"PNG"))
        preview_=QUrl::fromLocalFile(path).toString();
    emit captureChanged();
    return true;
}

void AiHandoff::clearCapture() {
    if(capture_.isNull()) return;
    capture_={}; removePreview();
    emit captureChanged();
}

void AiHandoff::removePreview() {
    if(!preview_.isEmpty()) QFile::remove(QUrl(preview_).toLocalFile());
    preview_.clear();
}

void AiHandoff::refreshTargets() {
    QVariantList list;
    for(const auto& [id,name]:{std::pair{QStringLiteral("claude"),QStringLiteral("Claude")},std::pair{QStringLiteral("chatgpt"),QStringLiteral("ChatGPT")}}) {
        const bool desktop=backend_.desktopApp && backend_.desktopApp(id).launch.isValid();
        list.append(QVariantMap{{"id",id},{"name",name},{"desktop",desktop}});
    }
    // The permission can change in System Settings while LinguaPDF runs.
    emit stateChanged();
    if(list==targets_) return;
    targets_=list;
    emit targetsChanged();
}

QUrl AiHandoff::chatUrl(const QString& target,bool desktop,const QString& prompt) {
    if(target==QLatin1String("claude"))
        return withPrompt(desktop ? QStringLiteral("claude://claude.ai/new?") : QStringLiteral("https://claude.ai/new?"),QStringLiteral("q"),prompt);
    if(target==QLatin1String("chatgpt") && !desktop)
        return withPrompt(QStringLiteral("https://chatgpt.com/?"),QStringLiteral("prompt"),prompt);
    return {};
}

QString AiHandoff::displayName(const QString& target,bool desktop) const {
    const QString name=target==QLatin1String("chatgpt") ? QStringLiteral("ChatGPT") : QStringLiteral("Claude");
    return desktop ? tr("%1 桌面版").arg(name) : tr("%1 网页版").arg(name);
}

QString AiHandoff::manualHint() const {
#ifdef Q_OS_MACOS
    const QString keys=QStringLiteral("⌘V");
#else
    const QString keys=QStringLiteral("Ctrl+V");
#endif
    QString hint=tr("截图已在剪贴板：在输入框中按 %1 粘贴，确认后再自己发送。").arg(keys);
    if(permissionNeeded() && !pasteAllowed()) hint+=tr("在系统设置中允许 LinguaPDF 使用“辅助功能”后可自动粘贴。");
    return hint;
}

bool AiHandoff::send(const QString& target,const QString& prompt) {
    if(capture_.isNull() || (target!=QLatin1String("claude") && target!=QLatin1String("chatgpt"))) return false;
    cancel();
    // Ask for the permission once per run; the system shows its own dialog.
    const bool allowed=backend_.pasteAllowed && backend_.pasteAllowed(!prompted_ && permissionNeeded());
    prompted_=true;
    AiPlatform::App app=backend_.desktopApp ? backend_.desktopApp(target) : AiPlatform::App{};
    // The ChatGPT app has no link for the prompt; it only gets it by pasting, so without the paste
    // the web page (which takes the prompt in its link) is the better place.
    if(target==QLatin1String("chatgpt") && !allowed) app={};
    const bool desktop=app.launch.isValid();
    const QUrl url=desktop && target==QLatin1String("chatgpt") ? app.launch : chatUrl(target,desktop,prompt);
    pastes_={QVariant(capture_)};
    if(desktop && target==QLatin1String("chatgpt")) pastes_.append(prompt);
    name_=displayName(target,desktop);
    web_=!desktop;
    expected_=desktop ? app.id : backend_.browser ? backend_.browser() : QString{};
    selfId_=backend_.self ? backend_.self() : QString{};
    if(backend_.setImage) backend_.setImage(capture_);
    if(!backend_.open || !backend_.open(url)) {
        finish(false,tr("无法打开 %1。").arg(name_)+manualHint());
        return false;
    }
    if(!allowed) {
        finish(false,tr("已打开 %1，提示词已填入。").arg(name_)+manualHint());
        return true;
    }
    readyAt_=-1; nextAt_=0; pasted_=0;
    status_=tr("正在打开 %1…").arg(name_);
    clock_.start(); timer_.start();
    emit stateChanged();
    return true;
}

void AiHandoff::step() {
    const qint64 now=clock_.elapsed();
    const QString front=backend_.frontmost ? backend_.frontmost() : QString{};
    // Unknown browser: any app other than LinguaPDF counts as the one that opened the link.
    const bool inFront=expected_.isEmpty() ? !front.isEmpty() && front!=selfId_ : front==expected_;
    if(readyAt_<0) {
        if(inFront) { readyAt_=now+(web_ ? timing_.settleWeb : timing_.settleApp); nextAt_=readyAt_; }
        else if(now>=timing_.wait) finish(false,tr("%1 没有切换到前台。").arg(name_)+manualHint());
        return;
    }
    if(now<nextAt_) return;
    if(pastes_.isEmpty()) {
        finish(true,tr("已填入 %1：检查截图和提示词后，请自己点发送。").arg(name_));
        return;
    }
    // Never paste into an app the user switched to in the meantime.
    if(!inFront) { finish(false,tr("%1 已不在前台，未自动粘贴。").arg(name_)+manualHint()); return; }
    const QVariant item=pastes_.takeFirst();
    if(pasted_++>0) {
        if(item.typeId()==QMetaType::QString) { if(backend_.setText) backend_.setText(item.toString()); }
        else if(backend_.setImage) backend_.setImage(item.value<QImage>());
    }
    if(!backend_.paste || !backend_.paste()) { finish(false,tr("无法向 %1 发送粘贴按键。").arg(name_)+manualHint()); return; }
    nextAt_=now+timing_.between;
}

void AiHandoff::finish(bool pasted,QString message) {
    timer_.stop();
    pastes_.clear();
    status_=std::move(message);
    emit stateChanged();
    emit finished(pasted);
}

void AiHandoff::cancel() {
    if(!timer_.isActive()) return;
    timer_.stop(); pastes_.clear();
    status_.clear();
    emit stateChanged();
}

void AiHandoff::openPermissionSettings() const {
    if(!backend_.permissionUrl.isEmpty()) QDesktopServices::openUrl(QUrl(backend_.permissionUrl));
}
