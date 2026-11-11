#pragma once
#include <QString>
#include <QStringList>
#include <QVariantList>
class QSqlDatabase;

// Offline French lookup over the database built by tools/bootstrap-french-dictionary.py.
// All functions are reentrant; the caller owns the (thread-affine) SQLite connection.
namespace FrenchDictionary {
// Lower-cased lookup keys for a selected word, best first: strips punctuation, elision
// (l', qu'...), inversion hyphens (va-t-il) and a leading reflexive pronoun (me lève).
QStringList candidates(const QString& selection);
// Ranked lemma cards. Each map: lemma, pos, posLabel, gender, ipa, aux, glosses (QStringList),
// analyses (QStringList, how the selected form relates to the lemma; empty when it is the lemma),
// conjugation (QVariantList of {title, rows:[{person, form}]}, verbs only).
QVariantList lookup(QSqlDatabase& database, const QString& selection, int limit = 5);
// Chinese description of a Wiktionary tag set, e.g. "直陈式 未完成过去时 · nous".
QString describe(const QString& tags, const QString& pos);
QVariantList conjugation(QSqlDatabase& database, const QString& lemma);
}
