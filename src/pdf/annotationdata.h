#pragma once
#include <QColor>
#include <QPointF>
#include <QList>
#include <QString>
#include <QMetaType>

struct TextAnnotation {
    QString id;
    int page = 0;
    QPointF anchor, right, down;
    double width = 200, height = 24, fontSize = 14;
    QString text;
    QString family = QStringLiteral("Noto Sans SC");
    QColor color = QColor(30,70,140);
    quint64 order = 0;
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
