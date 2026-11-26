#pragma once
// System side of the "ask an AI" hand-off: which chat apps are installed, which app is in front,
// and a synthetic paste keystroke. UI thread only. No network access: the chat apps (or the
// default browser) do all of that themselves, and nothing is ever sent from here.
#include <QString>
#include <QUrl>

namespace AiPlatform {

struct App {
    QString id;   // compared with frontmost(): macOS bundle id, Windows executable file name (lower case)
    QUrl launch;  // opens the app (bundle / executable / URL scheme root); empty = not installed
};

// Installed desktop chat app for "claude" or "chatgpt".
App desktopApp(const QString& target);
// Application in front, in the same form as App::id; empty when unknown.
QString frontmost();
// This process, in the same form.
QString self();
// The app that opens https links (the default browser); empty when unknown.
QString browser();
// Whether synthetic key events reach other apps. macOS: Accessibility permission; prompt asks the
// system to show its permission dialog. Windows: always.
bool pasteAllowed(bool prompt);
// Posts Command/Ctrl+V to the frontmost app.
bool paste();
// System page where the user grants the permission (empty: nothing to grant).
QString permissionSettingsUrl();

}
