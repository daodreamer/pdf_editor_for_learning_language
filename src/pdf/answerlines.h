#pragma once
#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QList>

namespace AnswerLines {
inline constexpr int maxWidth=1024, maxHeight=2048, maxLines=128;
inline constexpr int dottedThreshold=205, minMarks=6;
inline constexpr double maxDashGapPoints=4, maxDashMarkPoints=6, dottedBlankAbove=10, dottedFieldHeight=14;
QList<QRectF> detect(const QImage& image,QSizeF pageUnits);
}
