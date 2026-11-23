#pragma once
#include "annotationdata.h"
#include <fpdfview.h>
#include <fpdf_edit.h>
#include <QByteArray>

// One glyph of an answer: `font` indexes ExportText::fonts. A bold run drawn from a
// regular face (Noto fallback, older answers) is thickened with a stroke instead.
struct ExportGlyph { int font = 0; uint32_t cid; QPointF baseline; TextStyle style; int character = 0; bool syntheticBold = false; };
struct ExportNote { TextAnnotation note; QList<ExportGlyph> glyphs; };
// A bundled face actually used by the answers, embedded as its own CID font.
struct ExportFont { QByteArray data, cmap, cidToGid; };
struct ExportText {
    QList<ExportFont> fonts;
    QList<ExportNote> notes;
};
ExportText prepareExportText(const QList<TextAnnotation>& notes);
void insertExportText(FPDF_DOCUMENT document,FPDF_PAGE page,const QList<FPDF_FONT>& fonts,const ExportNote& note);
