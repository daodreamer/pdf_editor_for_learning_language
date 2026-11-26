#pragma once
// The learning prompt for "Ask AI", written out in each language it can be given in. One prompt
// asks for everything at once: translation, explanation, grammar, common mistakes, easily
// confused points, register / frequency of use and a strict check of the learner's answers.
// Patterns borrowed from published tutor prompts: strict correction with reasons, examples
// with translations, register, focus over volume, and "say so instead of guessing" for unclear
// screenshots.
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace AiPrompt {

// Languages the prompt exists in: "zh", "zh-Hant", "en", "fr", "de", "es", "it", "ja", "ko".
QStringList languages();
// {code, name}: each language under its own name, for the picker.
QVariantList languageList();
bool supported(const QString& code);
// Prompt language from the system UI languages (QLocale::uiLanguages order); empty when none of
// them is supported.
QString fromSystem(const QStringList& uiLanguages);
// The user's choice: "app" (default: the interface language), "system", or a language code.
// Falls back to the interface language when the choice cannot be met.
QString resolve(const QString& choice,bool englishInterface,const QStringList& uiLanguages);
// Complete prompt. target: ISO 639-1 language of the book ("fr", …; empty = unknown).
// title: the book's file name (a trailing .pdf is dropped); page: 1-based.
QString build(const QString& language,const QString& target,const QString& title,int page);

}
