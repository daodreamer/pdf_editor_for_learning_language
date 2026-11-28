#pragma once
#include <QColor>
#include <QDateTime>
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
    // Ruled multi-line answer: fixed baseline pitch in PDF units matching the printed
    // rules, so each wrapped or Enter-started line lands on the next rule. 0 = font default.
    double lineSpacing = 0;
    bool operator==(const TextAnnotation&) const = default;
};
struct InkStroke {
    QString id;
    int page = -1;
    QList<QPointF> points;
    QColor color;
    double width = 2;
    quint64 order = 0;
    // Optional per-point width factor (pen pressure or speed), same length as points.
    // Empty means a uniform width, which is how strokes saved before it was added load.
    QList<float> pressure;
    bool operator==(const InkStroke&) const = default;
};
// Whole-workspace limits (current round plus every archived round), checked on save/restore.
namespace SnapshotLimits {
inline constexpr qsizetype maxAnnotations = 100000;
inline constexpr qsizetype maxTotalPoints = 2000000;
}
// One archived practice round of one page (docs/STORAGE_FORMAT.md, schema 2). A book-wide
// "new round" archives every answered page as its own round sharing one batch id.
struct PracticeRound {
    QString id;
    int page = 0;
    QString batch; // empty = archived from this page alone
    QDateTime archived; // UTC
    QList<TextAnnotation> notes;
    QList<InkStroke> ink;
    bool operator==(const PracticeRound&) const = default;
};
struct AnnotationSnapshot {
    QList<TextAnnotation> notes; // the current round
    QList<InkStroke> ink;
    quint64 revision = 0;
    QList<PracticeRound> rounds; // archived rounds, oldest first; empty for schema 1 files
};
Q_DECLARE_METATYPE(AnnotationSnapshot)
