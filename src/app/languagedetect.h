#pragma once
// Heuristic language evidence from a page's text (PDF text layer or OCR). Counts common function
// words and letters that only some languages use; cheap enough to run on every text page that
// arrives. Not a general language identifier: it only separates the languages the app serves.
#include "textpage.h"
#include <QString>
#include <array>

struct LanguageEvidence {
    // Order of words/letters: French, German, Spanish, Italian, English (see languageCodes()).
    static constexpr int latinCount = 5;
    std::array<int, latinCount> words{};
    std::array<int, latinCount> letters{};
    // Tick-box habits: function words of languages that cross boxes (fr/de/es/it) or tick them
    // (en). A word shared by several crossing languages counts once.
    int cross = 0, tick = 0;
    int han = 0;
    LanguageEvidence& operator+=(const LanguageEvidence& other);
    // ISO 639-1 primary language ("fr", "de", "es", "it", "en", "zh") or empty when the evidence
    // is too thin. A Latin-script language with clear evidence wins over Chinese: textbooks
    // published for Chinese learners explain in Chinese but are practised in the target language.
    QString dominant() const;
    int latinScore(int index) const { return words[size_t(index)] * 2 + letters[size_t(index)]; }
};

const std::array<const char*, LanguageEvidence::latinCount>& languageCodes();
LanguageEvidence languageEvidence(const TextPage& page);
LanguageEvidence languageEvidence(const QString& text);
// Language named in a file name or title ("你好法语3", "English Grammar in Use", "Deutsch A1"),
// for scanned books without a text layer. Empty when none or several are named.
QString languageFromName(const QString& name);
// Maps the OCR language setting ("fra", "eng", "chi_sim", "fra+eng") to an ISO 639-1 code.
QString languageFromOcr(const QString& ocrLanguage);
