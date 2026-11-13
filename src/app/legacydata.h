#pragma once
#include <QString>

// One-time move of pre-1.0 data (product name "Langlern") into the LinguaPDF data directory.
// The move is a single directory rename; on failure the caller keeps using the legacy
// directory so existing practice data is never hidden or copied half-way.
struct LegacyDataResult {
    bool migrated=false;   // legacyRoot was renamed to newRoot
    bool useLegacy=false;  // rename failed: keep reading/writing legacyRoot
    QString detail;
};

LegacyDataResult migrateLegacyData(const QString& legacyRoot,const QString& newRoot);
