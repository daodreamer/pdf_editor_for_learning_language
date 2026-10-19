#pragma once
#include <QRectF>
#include <QString>
#include <QList>
#include <memory>

struct TextCharacter {
    char32_t unicode = 0;
    QRectF box; // normalized display coordinates, after intrinsic rotation and crop
    bool visible = false;
};
struct TextPage {
    QList<TextCharacter> characters; // index is the PDFium character index, not UTF-16
    QString error;
    bool ocr = false;
    bool pending = false;
    QRectF region{0,0,1,1}; // OCR may temporarily cover an explicitly selected region only.
    qint64 bytes() const { return qint64(sizeof(TextPage)) + characters.capacity() * qint64(sizeof(TextCharacter)) + error.size()*2; }
};
using TextSnapshot = std::shared_ptr<const TextPage>;
