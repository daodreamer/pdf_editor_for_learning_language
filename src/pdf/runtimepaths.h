#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

// Directory holding bundled runtime data: fonts/, tessdata/, dictionary/, voices/.
// Windows and development builds keep it beside the executable. A macOS .app keeps it in
// Contents/Resources; an .app still inside the build tree uses the build root beside the bundle.
inline QString runtimeDataDir() {
    const QString app=QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    if(app.endsWith(QStringLiteral(".app/Contents/MacOS"))) {
        const QDir resources(app+QStringLiteral("/../Resources"));
        if(QFileInfo::exists(resources.filePath(QStringLiteral("fonts")))) return resources.canonicalPath();
        const QDir buildRoot(app+QStringLiteral("/../../.."));
        if(QFileInfo::exists(buildRoot.filePath(QStringLiteral("fonts")))) return buildRoot.canonicalPath();
    }
#endif
    return app;
}
