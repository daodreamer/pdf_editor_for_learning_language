#include "frenchdictionary.h"
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariantMap>
#include <algorithm>

namespace FrenchDictionary {
namespace {
struct Card {
    QString lemma, pos, gender, ipa, aux;
    QStringList glosses, analyses;
    int senses = 0;
    bool direct = false;
};
const QStringList moodOrder{QStringLiteral("indicative"), QStringLiteral("conditional"), QStringLiteral("subjunctive"), QStringLiteral("imperative")};

QSet<QString> tagSet(const QString& tags) {
    const auto parts = tags.split(u' ', Qt::SkipEmptyParts);
    return {parts.begin(), parts.end()};
}
QString moodOf(const QSet<QString>& tags) {
    for(const auto& mood : moodOrder) if(tags.contains(mood)) return mood;
    if(tags.contains(QStringLiteral("participle"))) return QStringLiteral("participle");
    if(tags.contains(QStringLiteral("infinitive"))) return QStringLiteral("infinitive");
    for(const auto* tense : {"present", "imperfect", "historic", "future"})
        if(tags.contains(QLatin1String(tense))) return QStringLiteral("indicative");
    return {};
}
QString tenseOf(const QSet<QString>& tags, const QString& mood) {
    if(mood == QLatin1String("conditional") || mood == QLatin1String("imperative")) return QStringLiteral("present");
    // Only past participles agree in gender/number; Wiktionary often omits "past" on those rows.
    if(mood == QLatin1String("participle")) return tags.contains(QStringLiteral("present")) ? QStringLiteral("present") : QStringLiteral("past");
    if(tags.contains(QStringLiteral("historic"))) return QStringLiteral("historic");
    for(const auto* tense : {"imperfect", "future", "present", "past"})
        if(tags.contains(QLatin1String(tense))) return QLatin1String(tense);
    return {};
}
QString tenseTitle(const QString& mood, const QString& tense) {
    static const QHash<QString, QString> titles{
        {QStringLiteral("indicative/present"), QStringLiteral("直陈式 现在时")},
        {QStringLiteral("indicative/imperfect"), QStringLiteral("直陈式 未完成过去时")},
        {QStringLiteral("indicative/historic"), QStringLiteral("简单过去时（书面语）")},
        {QStringLiteral("indicative/future"), QStringLiteral("直陈式 简单将来时")},
        {QStringLiteral("conditional/present"), QStringLiteral("条件式 现在时")},
        {QStringLiteral("subjunctive/present"), QStringLiteral("虚拟式 现在时")},
        {QStringLiteral("subjunctive/imperfect"), QStringLiteral("虚拟式 未完成过去时（书面语）")},
        {QStringLiteral("imperative/present"), QStringLiteral("命令式")},
        {QStringLiteral("participle/present"), QStringLiteral("现在分词")},
        {QStringLiteral("participle/past"), QStringLiteral("过去分词")},
    };
    if(mood == QLatin1String("infinitive")) return QStringLiteral("不定式");
    return titles.value(mood + u'/' + tense);
}
QString pronoun(int person, bool plural, const QString& mood, const QString& form) {
    static const char* const subjects[2][3]{{"je", "tu", "il/elle"}, {"nous", "vous", "ils/elles"}};
    if(mood == QLatin1String("imperative")) return QStringLiteral("(%1)").arg(QLatin1String(subjects[plural][person - 1]));
    QString subject = QLatin1String(subjects[plural][person - 1]);
    static const QRegularExpression vowel(QStringLiteral("^[aeiouyhâàäéèêëîïôöûùüœæ]"));
    if(person == 1 && !plural && vowel.match(form).hasMatch()) subject = QStringLiteral("j'");
    if(mood == QLatin1String("subjunctive")) subject = (person == 3 ? QStringLiteral("qu'") : QStringLiteral("que ")) + subject;
    return subject;
}
QList<std::pair<int, bool>> persons(const QSet<QString>& tags) {
    QList<int> people;
    for(int i = 1; i <= 3; ++i)
        if(tags.contains(QStringList{QStringLiteral("first-person"), QStringLiteral("second-person"), QStringLiteral("third-person")}[i - 1])) people << i;
    QList<bool> numbers;
    if(tags.contains(QStringLiteral("singular"))) numbers << false;
    if(tags.contains(QStringLiteral("plural"))) numbers << true;
    QList<std::pair<int, bool>> result;
    for(int person : people) for(bool plural : numbers) result.append({person, plural});
    return result;
}
QString genderNumber(const QSet<QString>& tags) {
    QStringList parts;
    if(tags.contains(QStringLiteral("masculine"))) parts << QStringLiteral("阳性");
    if(tags.contains(QStringLiteral("feminine"))) parts << QStringLiteral("阴性");
    if(tags.contains(QStringLiteral("singular"))) parts << QStringLiteral("单数");
    if(tags.contains(QStringLiteral("plural"))) parts << QStringLiteral("复数");
    return parts.join(u' ');
}
QString posLabel(const QString& pos) {
    static const QHash<QString, QString> labels{
        {QStringLiteral("verb"), QStringLiteral("动词")}, {QStringLiteral("noun"), QStringLiteral("名词")},
        {QStringLiteral("adj"), QStringLiteral("形容词")}, {QStringLiteral("adv"), QStringLiteral("副词")},
        {QStringLiteral("pron"), QStringLiteral("代词")}, {QStringLiteral("prep"), QStringLiteral("介词")},
        {QStringLiteral("conj"), QStringLiteral("连词")}, {QStringLiteral("det"), QStringLiteral("限定词")},
        {QStringLiteral("article"), QStringLiteral("冠词")}, {QStringLiteral("intj"), QStringLiteral("感叹词")},
        {QStringLiteral("num"), QStringLiteral("数词")}, {QStringLiteral("name"), QStringLiteral("专有名词")},
        {QStringLiteral("phrase"), QStringLiteral("短语")}, {QStringLiteral("prep_phrase"), QStringLiteral("介词短语")},
        {QStringLiteral("particle"), QStringLiteral("小品词")}, {QStringLiteral("contraction"), QStringLiteral("缩合形式")},
    };
    return labels.value(pos, pos);
}
void addEntry(Card& card, const QSqlQuery& query, bool samePos) {
    // Columns: word, pos, gender, ipa, aux, senses, glosses.
    if(!samePos && !card.glosses.isEmpty()) return;
    if(card.ipa.isEmpty()) card.ipa = query.value(3).toString();
    if(card.gender.isEmpty()) card.gender = query.value(2).toString();
    if(card.aux.isEmpty()) card.aux = query.value(4).toString();
    card.senses += query.value(5).toInt();
    for(const auto& line : query.value(6).toString().split(u'\n', Qt::SkipEmptyParts))
        if(!card.glosses.contains(line)) card.glosses << line;
}
QVariantMap toMap(const Card& card) {
    QString label = posLabel(card.pos);
    if(card.pos == QLatin1String("noun") && !card.gender.isEmpty())
        label += QStringLiteral(" · ") + (card.gender.contains(u' ') ? QStringLiteral("阴阳同形") : genderNumber(tagSet(card.gender)));
    return {{QStringLiteral("lemma"), card.lemma}, {QStringLiteral("pos"), card.pos}, {QStringLiteral("posLabel"), label},
            {QStringLiteral("gender"), card.gender}, {QStringLiteral("ipa"), card.ipa}, {QStringLiteral("aux"), card.aux},
            {QStringLiteral("glosses"), card.glosses}, {QStringLiteral("analyses"), card.analyses}};
}
}

QStringList candidates(const QString& selection) {
    QString text = selection.trimmed().left(120).toLower();
    text.replace(QChar(0x2019), u'\'').replace(QChar(0x02BC), u'\'').replace(QChar(0x2018), u'\'');
    static const QRegularExpression edges(QStringLiteral("^[^\\p{L}]+|[^\\p{L}]+$"));
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    text.remove(edges);
    text.replace(spaces, QStringLiteral(" "));
    QStringList result;
    const auto add = [&result](const QString& value) { if(!value.isEmpty() && !result.contains(value)) result << value; };
    if(text.isEmpty()) return result;
    add(text);
    static const QRegularExpression elision(QStringLiteral("^(?:l|d|j|m|t|s|n|c|qu|jusqu|lorsqu|puisqu|quoiqu)'(.+)$"));
    if(const auto match = elision.match(text); match.hasMatch()) { text = match.captured(1); add(text); }
    static const QRegularExpression reflexive(QStringLiteral("^(?:me|te|se|nous|vous|m'|t'|s')\\s*(\\p{L}.*)$"));
    if(const auto match = reflexive.match(text); match.hasMatch()) add(match.captured(1));
    if(text.contains(u'-')) {
        // va-t-il, allons-y, dis-moi: the verb is the first component.
        add(text.section(u'-', 0, 0));
    }
    if(text.contains(u' ')) add(text.section(u' ', 0, 0));
    return result;
}

QString describe(const QString& tags, const QString& pos) {
    const auto set = tagSet(tags);
    if(pos == QLatin1String("verb")) {
        const auto mood = moodOf(set);
        const auto title = tenseTitle(mood, tenseOf(set, mood));
        if(mood == QLatin1String("participle")) {
            const auto extra = genderNumber(set);
            return extra.isEmpty() ? title : title + QStringLiteral(" · ") + extra;
        }
        QStringList who;
        for(const auto& [person, plural] : persons(set)) who << pronoun(person, plural, mood == QLatin1String("subjunctive") ? QString() : mood, QString());
        who.removeDuplicates();
        if(title.isEmpty()) return set.contains(QStringLiteral("alternative")) ? QStringLiteral("异体拼写") : QStringLiteral("变位形式");
        return who.isEmpty() ? title : title + QStringLiteral(" · ") + who.join(QStringLiteral(" / "));
    }
    QStringList parts;
    if(const auto extra = genderNumber(set); !extra.isEmpty()) parts << extra;
    if(set.contains(QStringLiteral("comparative"))) parts << QStringLiteral("比较级");
    if(set.contains(QStringLiteral("superlative"))) parts << QStringLiteral("最高级");
    if(set.contains(QStringLiteral("alternative"))) parts << QStringLiteral("异体拼写");
    return parts.isEmpty() ? QStringLiteral("变化形式") : parts.join(u' ');
}

namespace {
// Auxiliary paradigms for compound tenses: [tense][person 0..5] = je, tu, il, nous, vous, ils.
const char* const avoirForms[5][6]{
    {"ai", "as", "a", "avons", "avez", "ont"}, {"avais", "avais", "avait", "avions", "aviez", "avaient"},
    {"aurai", "auras", "aura", "aurons", "aurez", "auront"}, {"aurais", "aurais", "aurait", "aurions", "auriez", "auraient"},
    {"aie", "aies", "ait", "ayons", "ayez", "aient"}};
const char* const etreForms[5][6]{
    {"suis", "es", "est", "sommes", "êtes", "sont"}, {"étais", "étais", "était", "étions", "étiez", "étaient"},
    {"serai", "seras", "sera", "serons", "serez", "seront"}, {"serais", "serais", "serait", "serions", "seriez", "seraient"},
    {"sois", "sois", "soit", "soyons", "soyez", "soient"}};
const char* const allerPresent[6]{"vais", "vas", "va", "allons", "allez", "vont"};
const char* const venirPresent[6]{"viens", "viens", "vient", "venons", "venez", "viennent"};
enum AuxTense { AuxPresent, AuxImperfect, AuxFuture, AuxConditional, AuxSubjunctive };
bool startsWithVowel(const QString& word) {
    static const QRegularExpression vowel(QStringLiteral("^[aeiouyhâàäéèêëîïôöûùüœæ]"));
    return vowel.match(word).hasMatch();
}
}

QVariantList conjugation(QSqlDatabase& database, const QString& lemma) {
    // Only tenses in everyday, university and workplace use (CEFR A1–B2), in the order textbooks teach
    // them. Literary tenses (passé simple, passé antérieur, imparfait/plus-que-parfait du subjonctif)
    // are left out of the table; describe() still names such a form when one is looked up.
    QHash<QString, QStringList> cells;  // "mood/tense/person/plural" -> forms
    QStringList present, past;
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT f.key,t.tags FROM forms f JOIN tagsets t ON t.id=f.tagset WHERE f.lemma=? AND f.pos='verb' ORDER BY f.rowid"));
    query.addBindValue(lemma);
    if(!query.exec()) return {};
    while(query.next()) {
        const auto form = query.value(0).toString();
        const auto set = tagSet(query.value(1).toString());
        if(set.contains(QStringLiteral("alternative"))) continue;
        const auto mood = moodOf(set);
        if(mood == QLatin1String("participle")) {
            const bool feminineOrPlural = set.contains(QStringLiteral("feminine")) || set.contains(QStringLiteral("plural"));
            auto& list = tenseOf(set, mood) == QLatin1String("past") ? past : present;
            if(!feminineOrPlural && !list.contains(form) && list.size() < 2) list << form;
            continue;
        }
        const auto who = persons(set);
        if(who.size() != 1) continue;  // rows naming several persons are ambiguous summaries
        const auto keyText = mood + u'/' + tenseOf(set, mood) + u'/' + QString::number(who[0].first) + (who[0].second ? u'p' : u's');
        auto& list = cells[keyText];
        if(!list.contains(form) && list.size() < 2) list << form;
    }
    QStringList auxiliaries;
    QString participle;
    QSqlQuery auxQuery(database);
    auxQuery.prepare(QStringLiteral("SELECT aux,participle FROM entries WHERE key=? AND pos='verb'"));
    auxQuery.addBindValue(lemma.toLower());
    if(auxQuery.exec()) while(auxQuery.next()) {
        for(const auto& aux : auxQuery.value(0).toString().split(u'/', Qt::SkipEmptyParts)) if(!auxiliaries.contains(aux)) auxiliaries << aux;
        if(participle.isEmpty()) participle = auxQuery.value(1).toString();
    }
    std::sort(auxiliaries.begin(), auxiliaries.end());  // "avoir" before "être"
    // The verb's own table wins over entries that merely claim to be its participle (été under aller).
    if(!participle.isEmpty()) past = QStringList{participle};
    else participle = past.value(0);

    const auto cell = [&cells](const char* mood, const char* tense, int index) {
        return cells.value(QLatin1String(mood) + u'/' + QLatin1String(tense) + u'/' + QString::number(index % 3 + 1) + (index >= 3 ? u'p' : u's'));
    };
    // Finite persons the verb really has (impersonal verbs such as falloir only have il).
    QList<int> people;
    for(int index = 0; index < 6; ++index) if(!cell("indicative", "present", index).isEmpty()) people << index;
    const auto row = [](int index, const QString& mood, const QString& form) {
        return QVariantMap{{QStringLiteral("person"), pronoun(index % 3 + 1, index >= 3, mood, form)}, {QStringLiteral("form"), form}};
    };
    QVariantList groups;
    const auto addGroup = [&groups](const QString& title, const QString& name, const QVariantList& rows) {
        if(!rows.isEmpty()) groups << QVariantMap{{QStringLiteral("title"), title}, {QStringLiteral("name"), name}, {QStringLiteral("rows"), rows}};
    };
    const auto simple = [&](const char* mood, const char* tense, const QString& title, const QString& name) {
        const bool imperative = QLatin1String(mood) == QLatin1String("imperative");
        QVariantList rows;
        for(int index = 0; index < 6; ++index) {
            if(imperative && index != 1 && index != 3 && index != 4) continue;
            const auto forms = cell(mood, tense, index);
            if(!forms.isEmpty()) rows << row(index, QLatin1String(mood), forms.join(QStringLiteral(" / ")));
        }
        addGroup(title, name, rows);
    };
    const auto compound = [&](AuxTense tense, const QString& mood, const QString& title, const QString& name) {
        if(participle.isEmpty() || auxiliaries.isEmpty()) return;
        QVariantList rows;
        for(int index : people) {
            QStringList variants;
            for(const auto& aux : auxiliaries) {
                const bool etre = aux == QStringLiteral("être");
                QString agreed = participle;
                // With être the participle agrees with the subject: allé(e), allé(e)s.
                if(etre) agreed += index >= 3 ? (participle.endsWith(u's') ? QStringLiteral("(e)") : QStringLiteral("(e)s")) : QStringLiteral("(e)");
                variants << QString::fromUtf8((etre ? etreForms : avoirForms)[tense][index]) + u' ' + agreed;
            }
            // "j'ai rentré / je suis rentré(e)": each variant may elide the subject differently.
            QStringList subjects;
            for(const auto& variant : variants) {
                const auto subject = pronoun(index % 3 + 1, index >= 3, mood, variant);
                if(!subjects.contains(subject)) subjects << subject;
            }
            QString person = subjects.value(0);
            if(subjects.size() == 2) {  // share the "que " prefix: "que j'/je"
                const int split = subjects[0].lastIndexOf(u' ') + 1;
                person = subjects[0] + u'/' + subjects[1].mid(split);
            }
            rows << QVariantMap{{QStringLiteral("person"), person}, {QStringLiteral("form"), variants.join(QStringLiteral(" / "))}};
        }
        addGroup(title, name, rows);
    };
    const auto periphrastic = [&](const char* const helper[6], bool de, const QString& title, const QString& name) {
        QVariantList rows;
        const QString tail = de ? (startsWithVowel(lemma) ? QStringLiteral("d'") : QStringLiteral("de ")) + lemma : lemma;
        for(int index : people) rows << row(index, QStringLiteral("indicative"), QString::fromUtf8(helper[index]) + u' ' + tail);
        addGroup(title, name, rows);
    };
    const QString indicative = QStringLiteral("indicative"), conditional = QStringLiteral("conditional"), subjunctive = QStringLiteral("subjunctive");
    simple("indicative", "present", QStringLiteral("现在时"), QStringLiteral("présent"));
    compound(AuxPresent, indicative, QStringLiteral("复合过去时"), QStringLiteral("passé composé"));
    simple("indicative", "imperfect", QStringLiteral("未完成过去时"), QStringLiteral("imparfait"));
    compound(AuxImperfect, indicative, QStringLiteral("愈过去时"), QStringLiteral("plus-que-parfait"));
    periphrastic(allerPresent, false, QStringLiteral("最近将来时"), QStringLiteral("futur proche"));
    simple("indicative", "future", QStringLiteral("简单将来时"), QStringLiteral("futur simple"));
    compound(AuxFuture, indicative, QStringLiteral("先将来时"), QStringLiteral("futur antérieur"));
    periphrastic(venirPresent, true, QStringLiteral("最近过去时"), QStringLiteral("passé récent"));
    simple("conditional", "present", QStringLiteral("条件式现在时"), QStringLiteral("conditionnel présent"));
    compound(AuxConditional, conditional, QStringLiteral("条件式过去时"), QStringLiteral("conditionnel passé"));
    simple("subjunctive", "present", QStringLiteral("虚拟式现在时"), QStringLiteral("subjonctif présent"));
    compound(AuxSubjunctive, subjunctive, QStringLiteral("虚拟式过去时"), QStringLiteral("subjonctif passé"));
    simple("imperative", "present", QStringLiteral("命令式"), QStringLiteral("impératif"));
    if(groups.isEmpty()) return {};
    QVariantList nonFinite{QVariantMap{{QStringLiteral("person"), QStringLiteral("不定式")}, {QStringLiteral("form"), lemma}}};
    if(!present.isEmpty()) nonFinite << QVariantMap{{QStringLiteral("person"), QStringLiteral("现在分词")}, {QStringLiteral("form"), present.join(QStringLiteral(" / "))}};
    if(!past.isEmpty()) nonFinite << QVariantMap{{QStringLiteral("person"), QStringLiteral("过去分词")}, {QStringLiteral("form"), past.join(QStringLiteral(" / "))}};
    addGroup(QStringLiteral("分词"), QStringLiteral("infinitif · participes"), nonFinite);
    return groups;
}

QVariantList lookup(QSqlDatabase& database, const QString& selection, int limit) {
    QSqlQuery entries(database), forms(database), lemmas(database);
    entries.prepare(QStringLiteral("SELECT word,pos,gender,ipa,aux,senses,glosses FROM entries WHERE key=?"));
    forms.prepare(QStringLiteral("SELECT f.lemma,f.pos,t.tags FROM forms f JOIN tagsets t ON t.id=f.tagset WHERE f.key=?"));
    lemmas.prepare(QStringLiteral("SELECT word,pos,gender,ipa,aux,senses,glosses FROM entries WHERE key=?"));
    for(const auto& key : candidates(selection)) {
        QList<Card> cards;
        const auto find = [&cards](const QString& lemma, const QString& pos) -> Card* {
            for(auto& card : cards) if(card.pos == pos && card.lemma.compare(lemma, Qt::CaseInsensitive) == 0) return &card;
            return nullptr;
        };
        entries.addBindValue(key);
        if(!entries.exec()) return {};
        while(entries.next()) {
            const auto word = entries.value(0).toString(), pos = entries.value(1).toString();
            Card* card = find(word, pos);
            if(!card) { cards.append(Card{word, pos, {}, {}, {}, {}, {}, 0, true}); card = &cards.last(); }
            addEntry(*card, entries, true);
        }
        struct Row { QString lemma, pos, tags; };
        QList<Row> rows;
        forms.addBindValue(key);
        if(!forms.exec()) return {};
        while(forms.next()) rows.append({forms.value(0).toString(), forms.value(1).toString(), forms.value(2).toString()});
        for(auto& row : rows) {
            // One more hop when the target is itself only a form without a headword:
            // organisés -> organisé (participle form) -> organiser.
            lemmas.addBindValue(row.lemma.toLower());
            if(lemmas.exec() && !lemmas.next()) {
                forms.addBindValue(row.lemma.toLower());
                if(forms.exec()) while(forms.next())
                    if(forms.value(1).toString() == row.pos && forms.value(0).toString().compare(row.lemma, Qt::CaseInsensitive) != 0) { row.lemma = forms.value(0).toString(); break; }
            }
        }
        for(const auto& row : rows) {
            const auto& lemma = row.lemma;
            const auto& pos = row.pos;
            if(lemma.toLower() == key) continue;  // the lemma's own infinitive row
            Card* card = find(lemma, pos);
            if(!card) {
                cards.append(Card{lemma, pos, {}, {}, {}, {}, {}, 0, false});
                card = &cards.last();
                lemmas.addBindValue(lemma.toLower());
                // Prefer the headword with the same part of speech; otherwise borrow the first one
                // (e.g. a participle filed as an adjective form).
                if(lemmas.exec()) {
                    bool samePos = false;
                    while(lemmas.next()) if(lemmas.value(1).toString() == pos) { addEntry(*card, lemmas, true); samePos = true; }
                    if(!samePos && lemmas.first()) addEntry(*card, lemmas, false);
                }
            }
            const auto analysis = describe(row.tags, pos);
            if(!card->analyses.contains(analysis)) card->analyses << analysis;
        }
        if(cards.isEmpty()) continue;
        // Rank by how much Wiktionary says about the lemma; a direct headword gets a head start so
        // "livre" stays a noun, while "est" (east) still yields to the far richer "être". Sense counts
        // are only a proxy for frequency, so the two auxiliaries win ties such as "été" (être vs aller).
        const auto score = [](const Card& card) {
            const bool auxiliary = card.pos == QLatin1String("verb") && (card.lemma == QStringLiteral("être") || card.lemma == QLatin1String("avoir"));
            return card.senses + (card.direct ? 3 : 0) + (auxiliary ? 10 : 0) + (card.glosses.isEmpty() ? -100 : 0);
        };
        std::stable_sort(cards.begin(), cards.end(), [&score](const Card& a, const Card& b) { return score(a) > score(b); });
        QVariantList result;
        for(const auto& card : cards) {
            if(result.size() >= limit) break;
            auto map = toMap(card);
            if(card.pos == QLatin1String("verb")) map.insert(QStringLiteral("conjugation"), conjugation(database, card.lemma));
            map.insert(QStringLiteral("key"), key);
            result << map;
        }
        return result;
    }
    return {};
}
}
