#pragma once
#include "annotationdata.h"
#include <fpdfview.h>
#include <fpdf_edit.h>
#include <QByteArray>

struct ExportGlyph { uint32_t cid; QPointF baseline; };
struct ExportNote { TextAnnotation note; QList<ExportGlyph> glyphs; };
struct ExportText {
    QByteArray font, cmap, cidToGid;
    QList<ExportNote> notes;
};
ExportText prepareExportText(const QList<TextAnnotation>& notes);
void insertExportText(FPDF_DOCUMENT document,FPDF_PAGE page,FPDF_FONT font,const ExportNote& note);
