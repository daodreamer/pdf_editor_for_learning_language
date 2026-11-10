#pragma once
#include "annotationdata.h"
#include <QVariantList>
#include <QTextDocument>

QVariantList encodeTextStyles(const QList<TextStyle>& styles);
bool decodeTextStyles(const QVariantList& values,int textLength,QList<TextStyle>& styles);
void applyTextStyles(QTextDocument& document,const QList<TextStyle>& styles);
// Fixed line pitch for ruled multi-line answers (editor, layout and export alike).
void applyLineSpacing(QTextDocument& document,double lineSpacing);
