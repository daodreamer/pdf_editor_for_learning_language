#include "annotationtext.h"
#include <QTextBlock>
#include <QTextFragment>
#include <QTextCursor>
#include <cmath>

void AnnotationText::setDocument(QQuickTextDocument* document) {
    if(document_==document) return;
    if(document_) disconnect(document_->textDocument(),nullptr,this,nullptr);
    document_=document;
    if(document_) connect(document_->textDocument(),&QTextDocument::contentsChanged,this,[this]{if(!restoring_)emit changed();});
    emit documentChanged();
}
QString AnnotationText::plainText() const { return document_?document_->textDocument()->toPlainText():QString(); }
QVariantList AnnotationText::styles() const {
    QList<TextStyle> result;
    if(!document_) return {};
    for(auto block=document_->textDocument()->begin();block.isValid();block=block.next()) for(auto it=block.begin();!it.atEnd();++it) {
        const auto fragment=it.fragment(); if(!fragment.isValid()) continue;
        const auto f=fragment.charFormat();
        const double size=f.hasProperty(QTextFormat::FontPixelSize)?f.doubleProperty(QTextFormat::FontPixelSize):baseSize_;
        const QColor color=f.foreground().style()==Qt::NoBrush?baseColor_:f.foreground().color();
        const bool bold=f.fontWeight()>=QFont::Bold, italic=f.fontItalic();
        if(size!=baseSize_ || color!=baseColor_ || bold || italic)
            result.append({fragment.position(),fragment.length(),size,color,bold,italic});
    }
    return encodeTextStyles(result);
}
QVariantMap AnnotationText::selectionStyle(int start,int end) const {
    if(!document_ || start<0 || end<=start || end>plainText().size()) return {};
    QTextCursor cursor(document_->textDocument()); cursor.setPosition(start); cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
    const auto f=cursor.charFormat();
    return {{"size",f.hasProperty(QTextFormat::FontPixelSize)?f.doubleProperty(QTextFormat::FontPixelSize):baseSize_},
        {"color",f.foreground().style()==Qt::NoBrush?baseColor_:f.foreground().color()},
        {"bold",f.fontWeight()>=QFont::Bold},{"italic",f.fontItalic()}};
}
void AnnotationText::format(int start,int end,const QString& property,const QVariant& value) {
    if(!document_ || start<0 || end<=start || end>plainText().size()) return;
    QTextCharFormat f;
    if(property=="color") { const QColor color(value.toString()); if(!color.isValid())return; f.setForeground(color); }
    else if(property=="size") { bool ok=false;const double size=value.toDouble(&ok);if(!ok||!std::isfinite(size)||size<8||size>72||size!=std::floor(size))return;f.setProperty(QTextFormat::FontPixelSize,size); }
    else if(property=="bold") f.setFontWeight(value.toBool()?QFont::Bold:QFont::Normal);
    else if(property=="italic") f.setFontItalic(value.toBool());
    else return;
    QTextCursor cursor(document_->textDocument()); cursor.setPosition(start);cursor.setPosition(end,QTextCursor::KeepAnchor);
    cursor.beginEditBlock(); cursor.mergeCharFormat(f); cursor.endEditBlock();
}
void AnnotationText::restore(const QVariantList& values) {
    if(!document_) return;
    QList<TextStyle> decoded; if(!decodeTextStyles(values,int(plainText().size()),decoded)) return;
    restoring_=true;
    QTextCursor cursor(document_->textDocument()); cursor.select(QTextCursor::Document); cursor.setCharFormat(QTextCharFormat());
    applyTextStyles(*document_->textDocument(),decoded);
    document_->textDocument()->clearUndoRedoStacks(); restoring_=false;
}
