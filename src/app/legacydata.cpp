#include "legacydata.h"
#include <QDir>
#include <QFileInfo>

LegacyDataResult migrateLegacyData(const QString& legacyRoot,const QString& newRoot) {
    LegacyDataResult result;
    const QFileInfo legacy(legacyRoot),target(newRoot);
    if(!legacy.isDir()) return result;
    if(target.exists()) {
        result.detail=QStringLiteral("legacy data kept beside existing %1").arg(newRoot);
        return result;
    }
    if(!QDir().mkpath(target.absolutePath()) || !QDir().rename(legacy.absoluteFilePath(),target.absoluteFilePath())) {
        result.useLegacy=true;
        result.detail=QStringLiteral("could not move %1 to %2; using legacy directory").arg(legacyRoot,newRoot);
        return result;
    }
    result.migrated=true;
    result.detail=QStringLiteral("moved %1 to %2").arg(legacyRoot,newRoot);
    QDir parent=legacy.absoluteDir();  // Remove the now empty "Langlern" organization folder.
    if(parent.isEmpty()) parent.rmdir(parent.absolutePath());
    return result;
}
