#pragma once
#include <QTransform>
#include <QSizeF>
#include <cmath>

// Value-only snapshot. No PDFium handles cross the worker boundary.
struct PageGeometry {
    QTransform displayToPdf; // normalized cropped/rotated display -> original PDF units
    QTransform pdfToDisplay;
    QSizeF units; // lengths of the display axes in original page units
    bool valid = false;
    QPointF right() const { return (displayToPdf.map(QPointF(1,0))-displayToPdf.map(QPointF()))/units.width(); }
    QPointF down() const { return (displayToPdf.map(QPointF(0,1))-displayToPdf.map(QPointF()))/units.height(); }
};
