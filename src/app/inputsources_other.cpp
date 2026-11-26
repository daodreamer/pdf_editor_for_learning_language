#include "inputsources.h"

// Platforms without a supported input-source API: the feature reports no sources.
namespace InputSources {
QList<Source> enabled() { return {}; }
QString current() { return {}; }
bool select(const QString&) { return false; }
QString settingsUrl() { return {}; }
}
