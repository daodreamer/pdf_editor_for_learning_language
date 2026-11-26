#include "languagedetect.h"
#include <QSet>
#include <algorithm>

namespace {
const QSet<QString>& wordsOf(int index) {
    static const std::array<QSet<QString>, LanguageEvidence::latinCount> sets{{
        {"le","la","les","des","du","est","et","une","un","vous","je","nous","il","elle","ce","que","qui","pour","pas","sur","au","aux","avec","dans","mon","votre"},
        {"der","die","das","und","ist","nicht","ich","sie","mit","ein","eine","zu","den","auf","ihr","wir","sind"},
        {"el","los","las","y","es","por","con","una","del","usted","yo","está","son"},
        {"il","gli","che","è","per","non","con","una","della","sono","io","lei"},
        {"the","and","is","are","you","of","to","in","it","that","with","for","this","what","your","i"},
    }};
    return sets[size_t(index)];
}
constexpr int english = 4;
// Letters used by one of the languages only (é, à and the like are shared and say little).
int letterLanguage(char32_t u) {
    switch(u) {
    case U'è': case U'ê': case U'ë': case U'â': case U'ç': case U'î': case U'ï': case U'ô': case U'û': case U'œ': return 0;
    case U'ä': case U'ö': case U'ü': case U'ß': return 1;
    case U'ñ': case U'á': case U'í': case U'ó': case U'ú': case U'¿': case U'¡': return 2;
    case U'ò': case U'ì': return 3;
    default: return -1;
    }
}
// Letters that suggest a box-crossing language (kept identical to the tick-box rule's history).
bool crossLetter(char32_t u) {
    static const QString letters=QStringLiteral("éèêëàâçîïôûùœäöüßñ");
    return u<=0xffff && letters.contains(QChar(char16_t(u)));
}

template<typename Next>
LanguageEvidence scan(Next next) {
    LanguageEvidence result; QString word;
    auto flush=[&] {
        if(word.isEmpty()) return;
        bool crossing=false;
        for(int i=0;i<LanguageEvidence::latinCount;++i) {
            if(!wordsOf(i).contains(word)) continue;
            ++result.words[size_t(i)];
            if(i==english) ++result.tick; else crossing=true;
        }
        if(crossing) ++result.cross;
        word.clear();
    };
    char32_t u=0;
    while(next(u)) {
        if(u>=0x4e00 && u<=0x9fff) { ++result.han; flush(); continue; }
        if(QChar::isLetter(u)) {
            const char32_t lower=QChar::toLower(u);
            if(crossLetter(lower)) ++result.cross;
            if(const int language=letterLanguage(lower); language>=0) ++result.letters[size_t(language)];
            word+=QString::fromUcs4(&lower,1);
        } else if(u==U'¿' || u==U'¡') {
            ++result.letters[2]; flush();
        } else if(u!=U'\'' && u!=U'’') flush();
        else word.clear(); // l'école, j'ai: the elided article is not a word here
    }
    flush();
    return result;
}
}

LanguageEvidence& LanguageEvidence::operator+=(const LanguageEvidence& other) {
    for(int i=0;i<latinCount;++i) { words[size_t(i)]+=other.words[size_t(i)]; letters[size_t(i)]+=other.letters[size_t(i)]; }
    cross+=other.cross; tick+=other.tick; han+=other.han;
    return *this;
}

QString LanguageEvidence::dominant() const {
    int best=-1, bestScore=0, second=0;
    for(int i=0;i<latinCount;++i) {
        const int score=latinScore(i);
        if(score>bestScore) { second=bestScore; bestScore=score; best=i; }
        else if(score>second) second=score;
    }
    // Ten function words (or the equivalent in distinctive letters) and a clear lead.
    if(best>=0 && bestScore>=20 && bestScore>=second*3/2) return QString::fromLatin1(languageCodes()[size_t(best)]);
    if(han>=40 && bestScore<20) return QStringLiteral("zh");
    return {};
}

const std::array<const char*, LanguageEvidence::latinCount>& languageCodes() {
    static const std::array<const char*, LanguageEvidence::latinCount> codes{{"fr","de","es","it","en"}};
    return codes;
}

LanguageEvidence languageEvidence(const TextPage& page) {
    qsizetype i=0;
    return scan([&](char32_t& u) { if(i>=page.characters.size()) return false; u=page.characters[i++].unicode; return true; });
}

LanguageEvidence languageEvidence(const QString& text) {
    const auto ucs4=text.toUcs4(); qsizetype i=0;
    return scan([&](char32_t& u) { if(i>=ucs4.size()) return false; u=ucs4[i++]; return true; });
}

QString languageFromOcr(const QString& ocrLanguage) {
    const auto first=ocrLanguage.section(u'+',0,0);
    if(first.startsWith(QLatin1String("fra"))) return QStringLiteral("fr");
    if(first.startsWith(QLatin1String("eng"))) return QStringLiteral("en");
    if(first.startsWith(QLatin1String("chi"))) return QStringLiteral("zh");
    if(first.startsWith(QLatin1String("deu"))) return QStringLiteral("de");
    if(first.startsWith(QLatin1String("spa"))) return QStringLiteral("es");
    if(first.startsWith(QLatin1String("ita"))) return QStringLiteral("it");
    return {};
}

QString languageFromName(const QString& name) {
    static const std::array<std::pair<const char*,QStringList>,5> names{{
        {"fr",{QStringLiteral("法语"),QStringLiteral("法文"),QStringLiteral("french"),QStringLiteral("français"),QStringLiteral("francais")}},
        {"de",{QStringLiteral("德语"),QStringLiteral("德文"),QStringLiteral("german"),QStringLiteral("deutsch")}},
        {"es",{QStringLiteral("西班牙语"),QStringLiteral("西语"),QStringLiteral("spanish"),QStringLiteral("español"),QStringLiteral("espanol")}},
        {"it",{QStringLiteral("意大利语"),QStringLiteral("意语"),QStringLiteral("italian"),QStringLiteral("italiano")}},
        {"en",{QStringLiteral("英语"),QStringLiteral("英文"),QStringLiteral("english")}},
    }};
    const auto lower=name.toLower();
    QString found;
    for(const auto& [code,words]:names) {
        if(std::none_of(words.begin(),words.end(),[&](const QString& word) { return lower.contains(word); })) continue;
        if(!found.isEmpty()) return {}; // "法英词典": ambiguous
        found=QString::fromLatin1(code);
    }
    return found;
}
