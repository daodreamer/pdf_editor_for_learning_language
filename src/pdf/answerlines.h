#pragma once
#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QList>

namespace AnswerLines {
inline constexpr int maxWidth=1024, maxHeight=2048, maxLines=128;
// Dotted/dashed rules are found as chains of thin connected components.
inline constexpr int minMarks=6, maxRuns=800000;
inline constexpr double maxDashGapPoints=4, smallMarkPoints=8, thinMarkPoints=2.8, chainRisePoints=2;
inline constexpr double dottedBlankAbove=7, dottedBlankBelow=4, dottedFieldHeight=14, punctuationStackPoints=4;
// Ink is judged against the brightest paper nearby, so faint or tinted-background
// rules count; isolated marks must also stand out from the paper around them.
inline constexpr double backgroundOffsetPoints=5;
inline constexpr int inkCeiling=232, inkContrast=24, minimumMarkContrast=18;
// Faint rules: fine grey dots printed only ~10-25 levels below the paper, which
// never reach the ink mask. A 1-2 row band this much darker than flat paper
// faintOffsetPoints above and below, covering faintFill of a field-length run.
inline constexpr double faintOffsetPoints=1.6, faintFill=0.45;
inline constexpr int faintContrast=6, faintFlatness=6, faintPaperFloor=140;
// Paper rows above and below a faint rule must be smooth (std. dev. in levels);
// textured fills (banners, halftone) produce random faint hits.
inline constexpr double faintPaperNoise=4;
QList<QRectF> detect(const QImage& image,QSizeF pageUnits);
}
