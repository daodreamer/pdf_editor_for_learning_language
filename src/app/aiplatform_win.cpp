#include "aiplatform.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <iterator>
#include <qt_windows.h>
#include <shlwapi.h>

namespace AiPlatform {
namespace {
QString fileName(const QString& path) { return QFileInfo(path).fileName().toLower(); }
// Executable registered for a URL scheme (protocol) or "https"; empty when none.
QString handler(const wchar_t* scheme) {
    wchar_t buffer[MAX_PATH*2]; DWORD size=DWORD(std::size(buffer));
    if(AssocQueryStringW(ASSOCF_IS_PROTOCOL,ASSOCSTR_EXECUTABLE,scheme,L"open",buffer,&size)!=S_OK) return {};
    return QString::fromWCharArray(buffer);
}
}

App desktopApp(const QString& target) {
    // Claude for Windows registers the claude:// scheme. The ChatGPT app has no documented scheme,
    // so it is reached on the web.
    if(target!=QLatin1String("claude")) return {};
    const auto exe=handler(L"claude");
    if(exe.isEmpty() || !QFileInfo::exists(exe)) return {};
    return {fileName(exe),QUrl::fromLocalFile(exe)};
}

QString frontmost() {
    HWND window=GetForegroundWindow();
    if(!window) return {};
    DWORD pid=0; GetWindowThreadProcessId(window,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process) return {};
    wchar_t buffer[MAX_PATH*2]; DWORD size=DWORD(std::size(buffer));
    const bool ok=QueryFullProcessImageNameW(process,0,buffer,&size);
    CloseHandle(process);
    return ok ? fileName(QString::fromWCharArray(buffer,int(size))) : QString{};
}

QString self() { return fileName(QCoreApplication::applicationFilePath()); }

QString browser() { return fileName(handler(L"https")); }

bool pasteAllowed(bool) { return true; }

bool paste() {
    INPUT keys[4]={};
    for(auto& key:keys) key.type=INPUT_KEYBOARD;
    keys[0].ki.wVk=VK_CONTROL;
    keys[1].ki.wVk='V';
    keys[2].ki.wVk='V'; keys[2].ki.dwFlags=KEYEVENTF_KEYUP;
    keys[3].ki.wVk=VK_CONTROL; keys[3].ki.dwFlags=KEYEVENTF_KEYUP;
    return SendInput(4,keys,sizeof(INPUT))==4;
}

QString permissionSettingsUrl() { return {}; }

}
