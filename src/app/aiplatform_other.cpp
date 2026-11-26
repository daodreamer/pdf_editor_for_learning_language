#include "aiplatform.h"

// Other platforms: no desktop apps are detected and nothing is pasted; the web page still opens and
// the screenshot stays on the clipboard.
namespace AiPlatform {
App desktopApp(const QString&) { return {}; }
QString frontmost() { return {}; }
QString self() { return {}; }
QString browser() { return {}; }
bool pasteAllowed(bool) { return false; }
bool paste() { return false; }
QString permissionSettingsUrl() { return {}; }
}
