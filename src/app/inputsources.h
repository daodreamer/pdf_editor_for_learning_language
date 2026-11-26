#pragma once
// System keyboard input sources (macOS Text Input Sources, Windows keyboard layouts / input
// languages). UI thread only: macOS requires the main thread for TIS calls, and on Windows the
// active layout belongs to the calling (UI) thread.
#include <QList>
#include <QString>

namespace InputSources {

struct Source {
    QString id;        // macOS: input source ID; Windows: HKL in hex
    QString name;      // localized, as the system shows it
    QString language;  // primary ISO 639-1 language, e.g. "fr", "zh"
};

// Sources the user has enabled and that can be selected, in system order.
QList<Source> enabled();
QString current();
bool select(const QString& id);
// System page where the user adds keyboards / input methods.
QString settingsUrl();

}
