#pragma once
#include <QImage>
#include <QVariantMap>

// Import only: output becomes a new raster PDF, never a display-only coordinate transform.
QImage cleanScan(QImage source,const QVariantMap& options);
