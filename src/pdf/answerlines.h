#pragma once
#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QList>

namespace AnswerLines {
inline constexpr int maxWidth=1024, maxHeight=2048, maxLines=128;
QList<QRectF> detect(const QImage& image,QSizeF pageUnits);
}
