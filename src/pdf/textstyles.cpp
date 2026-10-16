#include "textstyles.h"
#include <QTextCursor>
#include <cmath>

QVariantList encodeTextStyles(const QList<TextStyle>& styles) {
    QVariantList values;
    for(const auto& s:styles) values.append(QVariantMap{{"start",s.start},{"length",s.length},{"size",s.size},
        {"color",s.color.name(QColor::HexArgb)},{"bold",s.bold},{"italic",s.italic}});
    return values;
}
bool decodeTextStyles(const QVariantList& values,int textLength,QList<TextStyle>& styles) {
    if(values.size()>100000) return false;
    QList<TextStyle> result; int end=0;
    for(const auto& value:values) {
        const auto v=value.toMap();
        bool startOk=false,lengthOk=false,sizeOk=false;
        const double start=v.value("start").toDouble(&startOk), length=v.value("length").toDouble(&lengthOk);
        const double size=v.value("size").toDouble(&sizeOk);
        const QColor color(v.value("color").toString());
        if(!startOk || !lengthOk || !sizeOk || !std::isfinite(start) || !std::isfinite(length) || !std::isfinite(size)
            || start<end || start>textLength || start!=std::floor(start) || length<1 || length>textLength-start || length!=std::floor(length)
            || size<8 || size>72 || size!=std::floor(size) || !color.isValid() || v.value("bold").metaType()!=QMetaType::fromType<bool>()
            || v.value("italic").metaType()!=QMetaType::fromType<bool>()) return false;
        result.append({int(start),int(length),size,color,v.value("bold").toBool(),v.value("italic").toBool()});
        end=int(start+length);
    }
    styles=std::move(result); return true;
}
void applyTextStyles(QTextDocument& document,const QList<TextStyle>& styles) {
    for(const auto& s:styles) {
        QTextCursor cursor(&document); cursor.setPosition(s.start); cursor.setPosition(s.start+s.length,QTextCursor::KeepAnchor);
        QTextCharFormat format; format.setProperty(QTextFormat::FontPixelSize,s.size); format.setForeground(s.color);
        format.setFontWeight(s.bold?QFont::Bold:QFont::Normal); format.setFontItalic(s.italic);
        cursor.mergeCharFormat(format);
    }
}
