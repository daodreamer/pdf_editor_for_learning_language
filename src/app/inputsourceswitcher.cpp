#include "inputsourceswitcher.h"
#include <QDesktopServices>
#include <QGuiApplication>
#include <QUrl>

namespace {
// LANGLERN_NO_INPUT_SWITCH keeps automated GUI runs from changing the developer's keyboard.
bool systemDisabled() { return qEnvironmentVariableIsSet("LANGLERN_NO_INPUT_SWITCH"); }
InputSourceSwitcher::Backend systemBackend() {
    if(systemDisabled()) return {[] { return QList<InputSources::Source>{}; },[] { return QString{}; },[](const QString&) { return false; }};
    return {&InputSources::enabled,&InputSources::current,&InputSources::select};
}
}

InputSourceSwitcher::InputSourceSwitcher(QObject* parent)
    : InputSourceSwitcher(systemBackend(),QGuiApplication::applicationState()==Qt::ApplicationActive,parent) {
    supported_=!systemDisabled() && !InputSources::settingsUrl().isEmpty();
    connect(qGuiApp,&QGuiApplication::applicationStateChanged,this,[this](Qt::ApplicationState state) {
        setApplicationActive(state==Qt::ApplicationActive);
    });
}

InputSourceSwitcher::InputSourceSwitcher(Backend backend,bool active,QObject* parent)
    : QObject(parent),backend_(std::move(backend)),active_(active) {
    sources_=backend_.enabled();
}

InputSourceSwitcher::~InputSourceSwitcher() { restore(); }

void InputSourceSwitcher::setLanguage(const QString& value) { if(value!=language_) { language_=value; update(); } }
void InputSourceSwitcher::setDocumentOpen(bool value) { if(value!=documentOpen_) { documentOpen_=value; update(); } }
void InputSourceSwitcher::setSwitching(bool value) { if(value!=switching_) { switching_=value; update(); } }
void InputSourceSwitcher::setChoices(const QVariantMap& value) { if(value!=choices_) { choices_=value; update(); } }

void InputSourceSwitcher::setApplicationActive(bool active) {
    if(active==active_) return;
    active_=active;
    // Keyboards added in System Settings while the user was away count on return.
    if(active_) refresh(); else update();
}

void InputSourceSwitcher::refresh() {
    auto sources=backend_.enabled();
    const bool same=sources.size()==sources_.size() && std::equal(sources.begin(),sources.end(),sources_.begin(),
        [](const auto& a,const auto& b) { return a.id==b.id && a.name==b.name && a.language==b.language; });
    if(!same) { sources_=std::move(sources); emit sourcesChanged(); }
    update();
}

QVariantList InputSourceSwitcher::sources() const {
    QVariantList list;
    for(const auto& source:sources_) list.append(QVariantMap{{"id",source.id},{"name",source.name},{"language",source.language}});
    return list;
}

QString InputSourceSwitcher::targetName() const {
    for(const auto& source:sources_) if(source.id==target_) return source.name;
    return {};
}

QString InputSourceSwitcher::resolve() const {
    // The user's pick for this language, while it is still installed.
    const auto chosen=choices_.value(language_).toString();
    if(!chosen.isEmpty()) for(const auto& source:sources_) if(source.id==chosen) return chosen;
    // Automatic: what is already selected when it fits (or what this object selected), else the
    // first source for the language in system order.
    const auto current=engaged_ ? applied_ : backend_.current();
    for(const auto& source:sources_) if(source.id==current && source.language==language_) return current;
    for(const auto& source:sources_) if(source.language==language_) return source.id;
    return {};
}

void InputSourceSwitcher::update() {
    const bool wanted=switching_ && documentOpen_ && !language_.isEmpty();
    const auto target=wanted ? resolve() : QString{};
    const bool missing=wanted && target.isEmpty() && supported_;
    if(!target.isEmpty() && active_) {
        if(!engaged_) { previous_=backend_.current(); engaged_=true; applied_.clear(); }
        // Select only when the wanted source changes: a source the user picks by hand while
        // reading stays until they leave the app.
        if(target!=applied_) { if(backend_.current()!=target) backend_.select(target); applied_=target; }
    } else restore();
    if(target!=target_ || missing!=missing_) { target_=target; missing_=missing; }
    emit changed();
}

void InputSourceSwitcher::restore() {
    if(!engaged_) return;
    engaged_=false;
    if(!previous_.isEmpty() && backend_.current()!=previous_) backend_.select(previous_);
    previous_.clear(); applied_.clear();
}

void InputSourceSwitcher::openSystemSettings() const {
    if(const auto url=InputSources::settingsUrl(); !url.isEmpty()) QDesktopServices::openUrl(QUrl(url));
}
