#pragma once
#include <QColor>
#include <QPointF>
#include <QList>
#include <QString>

#include "annotationdata.h"

namespace AnnotationConfig {
inline constexpr qsizetype maxHistoryCommands = 256;
inline constexpr qsizetype historyBytes = 16 * 1024 * 1024;
inline constexpr qsizetype maxStrokePoints = 65536;
inline constexpr int segmentsPerChunk = 128;
inline constexpr double eraserPixels = 8;
}
