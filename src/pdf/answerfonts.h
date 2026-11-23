#pragma once
#include <QFont>
#include <QList>
#include <QString>
#include <QStringList>

// Bundled answer fonts. notes.family names a font set, not a single face:
//  - "Langlern Sans + Kai" (new answers): Langlern Sans (Andika by SIL) for letters,
//    Langlern Kai (a GB2312 subset of LXGW WenKai) for Chinese, Noto Sans SC for any
//    character outside both. Bold uses real bold faces (Kai: the WenKai Medium design). Both were renamed and
//    given Noto's vertical metrics (tools/make-answer-fonts.py), so line heights and
//    baselines match the legacy face.
//  - "Noto Sans SC": answers written before the switch keep their look.
// Display, size calculation and PDF export all build the font through font(), so the
// glyphs Qt lays out are exactly the faces export embeds.
namespace AnswerFonts {
inline const QString legacy = QStringLiteral("Noto Sans SC");
inline const QString learning = QStringLiteral("Langlern Sans + Kai");
bool supported(const QString& family);
QStringList families(const QString& family);
QFont font(const QString& family, double pixelSize);
// Every bundled face file (fonts/ under the runtime data directory); Noto Sans SC first.
QStringList files();
// Registers the bundled faces with Qt once; false when any file is missing.
bool registerAll();
}
