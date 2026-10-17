#pragma once
#include <QColor>
#include <QPointF>
#include <QList>
#include <QString>
#include <QMetaType>

struct TextStyle {
    int start = 0, length = 0;
    double size = 14;
    QColor color = QColor(30,70,140);
    bool bold = false, italic = false;
    bool operator==(const TextStyle&) const = default;
};

struct TextAnnotation {
    QString id;
    int page = 0;
    QPointF anchor, right, down;
    double width = 200, height = 24, fontSize = 14;
    QString text;
    QString family = QStringLiteral("Noto Sans SC");
    QColor color = QColor(30,70,140);
    quint64 order = 0;
    bool widthLocked = true;
    double minimumHeight = 24;
    QList<TextStyle> styles;
    double maximumWidth = 0; // Optional automatic width cap, in PDF units; 0 uses the page edge.
    bool operator==(const TextAnnotation&) const = default;
};
struct InkStroke {
    QString id;
    int page = -1;
    QList<QPointF> points;
    QColor color;
    double width = 2;
    quint64 order = 0;
    bool operator==(const InkStroke&) const = default;
};
struct AnnotationSnapshot {
    QList<TextAnnotation> notes;
    QList<InkStroke> ink;
    quint64 revision = 0;
};
Q_DECLARE_METATYPE(AnnotationSnapshot)
