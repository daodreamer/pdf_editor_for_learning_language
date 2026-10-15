#pragma once
#include <QColor>
#include <QPointF>
#include <QList>
#include <QString>

struct InkStroke {
    QString id;
    int page = -1;
    QList<QPointF> points; // Original PDF page units, including CropBox/rotation mapping.
    QColor color;
    double width = 2;
    quint64 order = 0;
    bool operator==(const InkStroke&) const = default;
};

namespace AnnotationConfig {
inline constexpr qsizetype maxHistoryCommands = 256;
inline constexpr qsizetype historyBytes = 16 * 1024 * 1024;
inline constexpr qsizetype maxStrokePoints = 65536;
inline constexpr int segmentsPerChunk = 128;
inline constexpr double eraserPixels = 8;
}
