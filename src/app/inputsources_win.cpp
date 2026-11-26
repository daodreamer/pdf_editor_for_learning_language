#include "inputsources.h"
#include <QHash>
#include <windows.h>

namespace InputSources {
namespace {
QString idOf(HKL layout) { return QString::number(quintptr(layout),16); }
QString localeName(HKL layout) {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if(!LCIDToLocaleName(MAKELCID(LOWORD(quintptr(layout)),SORT_DEFAULT),name,LOCALE_NAME_MAX_LENGTH,0)) return {};
    return QString::fromWCharArray(name);
}
QString displayName(const QString& locale) {
    wchar_t name[256]{};
    const auto wide=locale.toStdWString();
    if(!GetLocaleInfoEx(wide.c_str(),LOCALE_SLOCALIZEDDISPLAYNAME,name,256)) return locale;
    return QString::fromWCharArray(name);
}
}

QList<Source> enabled() {
    QList<Source> result;
    const int count=GetKeyboardLayoutList(0,nullptr);
    if(count<=0) return result;
    QList<HKL> layouts(count);
    const int filled=GetKeyboardLayoutList(count,layouts.data());
    QHash<QString,int> seen;
    for(int i=0;i<filled;++i) {
        const auto locale=localeName(layouts[i]);
        if(locale.isEmpty()) continue;
        const auto name=displayName(locale);
        result.append({idOf(layouts[i]),name,locale.section(u'-',0,0).toLower()});
        ++seen[name];
    }
    // Two layouts of one language (e.g. French AZERTY and a US keyboard for fr-FR) share a name.
    for(auto& source:result) if(seen.value(source.name)>1) source.name+=QStringLiteral(" (%1)").arg(source.id);
    return result;
}

QString current() { return idOf(GetKeyboardLayout(0)); }

bool select(const QString& id) {
    bool ok=false;
    const auto value=id.toULongLong(&ok,16);
    if(!ok) return false;
    return ActivateKeyboardLayout(HKL(quintptr(value)),KLF_SETFORPROCESS)!=nullptr;
}

QString settingsUrl() { return QStringLiteral("ms-settings:regionlanguage"); }

}
