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
// Automatic mode: a press becomes ink as soon as it moves this far (logical px);
// every buffered point since the press is kept, so small letters stay exact.
inline constexpr double autoInkStartPixels = 1.5;
// A click shortly after and near the previous stroke is a written dot (i, j, ü,
// punctuation), not a request for a text box.
inline constexpr int writingRhythmMs = 1200;
inline constexpr double writingRhythmPixels = 220;
}
