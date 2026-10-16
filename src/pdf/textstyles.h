#pragma once
#include "annotationdata.h"
#include <QVariantList>
#include <QTextDocument>

QVariantList encodeTextStyles(const QList<TextStyle>& styles);
bool decodeTextStyles(const QVariantList& values,int textLength,QList<TextStyle>& styles);
void applyTextStyles(QTextDocument& document,const QList<TextStyle>& styles);
