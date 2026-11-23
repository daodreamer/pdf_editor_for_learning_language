#pragma once
#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QList>

// Printed tick boxes ("□ le sport"): small hollow squares found on the rendered page.
// Heuristic, image-only (scanned textbooks have no text layer); see docs/CHECK_BOXES.md.
namespace CheckBoxes {
// Detection scale (render pixels per PDF point) and its size cap. PdfWorker renders each
// page once at this scale for detection, so zoom never changes which boxes are found.
inline constexpr double pixelsPerPoint=2.7;
inline constexpr int maxWidth=2400, maxHeight=3400, maxBoxes=256;
// Box side in PDF points and the accepted width/height ratio.
inline constexpr double minSidePoints=5.5, maxSidePoints=22, maxAspect=1.15;
// A pixel is ink when its darkest channel is below this (coloured strokes count).
inline constexpr int inkLevel=165;
// Each side must be covered along this fraction of its length; the inside must stay paper.
inline constexpr double sideCoverage=.86, maxInsideInk=.03, maxFill=.62;
// Clear paper on both sides, as a fraction of the box side.
inline constexpr double sideClearance=.2;
// CJK 口 rejection: a neighbour closer than closeGap (box widths), looked for across
// neighbourWidth box widths, with ink nearMargin..nearReach box heights both above and
// below the square and at least neighbourDensity ink on the square's rows.
inline constexpr double closeGap=.8, nearMargin=.07, nearReach=.25, neighbourWidth=1, neighbourDensity=.16;
QList<QRectF> detect(const QImage& image,QSizeF pageUnits);
}
