#include "aiprompt.h"
#include <QRegularExpression>
#include <array>

namespace AiPrompt {
namespace {

constexpr int sectionCount=7; // translation first: dropped when the book is in the prompt language

struct Text {
    const char* code;
    const char* name;
    const char* intro;
    const char* target;  // %1 language name
    const char* book;    // %1 title, %2 page
    const char* read;
    std::array<const char*,sectionCount> titles;
    std::array<const char*,sectionCount> bodies;
    const char* rules;
    const char* unknown;
    // Names of the book languages languagedetect reports: fr, de, es, it, en, zh.
    std::array<const char*,6> names;
};

const std::array<const char*,6> bookLanguages{{"fr","de","es","it","en","zh"}};

const std::array<Text,9>& texts() {
    static const std::array<Text,9> table{{
        {"zh","简体中文",
         "你是一位经验丰富、要求严格的语言老师。附件截图来自我正在学习的教材，可能包含练习题和我填写的答案（打字或手写）。",
         "目标语言：%1","教材：《%1》第 %2 页",
         "请先完整识别截图中的文字；看不清或无法确定的地方请直接指出，不要猜测。然后用中文按以下标题回答，例句用目标语言并附中文翻译：",
         {{"翻译","讲解","语法点","易错点","易混淆点","使用场景与频率","检查答案"}},
         {{"逐句翻译截图中的目标语言内容。",
           "这部分内容（或练习）在练习什么；关键词汇和表达的意思与用法。",
           "涉及的语法规则，每条配 1–2 个例句。",
           "学习者在这里最常犯的错误，以及如何避免。",
           "容易混淆的词、形式或结构，对比说明区别。",
           "重要词汇和表达分别用于日常、学术、工作，还是很少使用；并说明正式程度。",
           "逐题核对我的答案：正确的标 ✓；错误的标 ✗，给出正确答案，并解释错在哪里、为什么错。未作答的题请给出参考答案；有多种可接受答案时一并列出。截图中没有练习或答案时，说明即可。"}},
         "批改从严：拼写、变音符号、性数配合、动词变位等细节错误都要指出；突出重点，每部分简明扼要，不要堆砌。",
         "（请根据截图判断）",
         {{"法语","德语","西班牙语","意大利语","英语","中文"}}},
        {"zh-Hant","繁體中文",
         "你是一位經驗豐富、要求嚴格的語言老師。附件截圖來自我正在學習的教材，可能包含練習題和我填寫的答案（打字或手寫）。",
         "目標語言：%1","教材：《%1》第 %2 頁",
         "請先完整辨識截圖中的文字；看不清或無法確定的地方請直接指出，不要猜測。然後用繁體中文按以下標題回答，例句用目標語言並附中文翻譯：",
         {{"翻譯","講解","語法重點","易錯點","易混淆點","使用場合與頻率","檢查答案"}},
         {{"逐句翻譯截圖中的目標語言內容。",
           "這部分內容（或練習）在練習什麼；關鍵詞彙和表達的意思與用法。",
           "涉及的語法規則，每條附 1–2 個例句。",
           "學習者在這裡最常犯的錯誤，以及如何避免。",
           "容易混淆的詞、形式或結構，對比說明差別。",
           "重要詞彙和表達分別用於日常、學術、工作，還是很少使用；並說明正式程度。",
           "逐題核對我的答案：正確的標 ✓；錯誤的標 ✗，給出正確答案，並解釋錯在哪裡、為什麼錯。未作答的題請給出參考答案；有多種可接受答案時一併列出。截圖中沒有練習或答案時，說明即可。"}},
         "批改從嚴：拼寫、變音符號、性數配合、動詞變位等細節錯誤都要指出；突出重點，每部分簡明扼要，不要堆砌。",
         "（請根據截圖判斷）",
         {{"法語","德語","西班牙語","義大利語","英語","中文"}}},
        {"en","English",
         "You are an experienced, strict language teacher. The attached screenshot is from a textbook I am studying. It may contain exercises and my answers (typed or handwritten).",
         "Target language: %1","Textbook: “%1”, page %2",
         "First read all the text in the screenshot carefully. If anything is unclear or illegible, say so instead of guessing. Then answer in English under the headings below; give example sentences in the target language with an English translation.",
         {{"Translation","Explanation","Grammar points","Common mistakes","Easily confused","Usage and frequency","Answer check"}},
         {{"Translate the target-language text in the screenshot sentence by sentence.",
           "What this part (or exercise) practises; the meaning and use of the key words and expressions.",
           "The grammar rules involved, each with 1–2 example sentences.",
           "The mistakes learners most often make here, and how to avoid them.",
           "Words, forms or structures that are easily confused; compare them and explain the difference.",
           "For the important words and expressions: are they used in everyday life, in academic or work contexts, or rarely? Also say how formal they are.",
           "Check my answers one by one. Mark correct ones ✓. Mark wrong ones ✗, give the correct answer, and explain what is wrong and why. For unanswered questions, give a model answer; if several answers are acceptable, list them. If the screenshot has no exercise or answers, just say so."}},
         "Be strict: point out every error in spelling, accents, gender and number agreement, verb conjugation and similar details. Focus on what matters most and keep each section concise.",
         "(please identify it from the screenshot)",
         {{"French","German","Spanish","Italian","English","Chinese"}}},
        {"fr","Français",
         "Tu es un professeur de langue expérimenté et exigeant. La capture d’écran jointe provient d’un manuel que j’étudie. Elle peut contenir des exercices et mes réponses (tapées ou manuscrites).",
         "Langue étudiée : %1","Manuel : « %1 », page %2",
         "Lis d’abord attentivement tout le texte de la capture. Si un passage est flou ou illisible, signale-le au lieu de deviner. Réponds ensuite en français sous les titres suivants ; donne les exemples dans la langue étudiée avec leur traduction en français.",
         {{"Traduction","Explications","Points de grammaire","Erreurs fréquentes","Confusions possibles","Usage et fréquence","Correction"}},
         {{"Traduis phrase par phrase le texte de la capture écrit dans la langue étudiée.",
           "Ce que cette partie (ou cet exercice) fait travailler ; le sens et l’emploi des mots et expressions clés.",
           "Les règles de grammaire en jeu, chacune avec 1 ou 2 exemples.",
           "Les erreurs que les apprenants font le plus souvent ici, et comment les éviter.",
           "Les mots, formes ou structures faciles à confondre ; compare-les et explique la différence.",
           "Pour les mots et expressions importants : s’emploient-ils dans la vie quotidienne, en contexte universitaire, au travail, ou rarement ? Indique aussi leur registre.",
           "Vérifie mes réponses une par une. Marque les bonnes réponses ✓. Marque les mauvaises ✗, donne la bonne réponse et explique l’erreur et sa cause. Pour les questions sans réponse, propose une réponse modèle ; si plusieurs réponses sont acceptables, cite-les. S’il n’y a ni exercice ni réponse dans la capture, dis-le simplement."}},
         "Sois exigeant : signale toutes les erreurs d’orthographe, d’accents, d’accord en genre et en nombre, de conjugaison, etc. Va à l’essentiel et reste concis dans chaque partie.",
         "(à déterminer d’après la capture)",
         {{"français","allemand","espagnol","italien","anglais","chinois"}}},
        {"de","Deutsch",
         "Du bist eine erfahrene, strenge Sprachlehrkraft. Der angehängte Screenshot stammt aus einem Lehrbuch, mit dem ich lerne. Er kann Übungen und meine Antworten (getippt oder handschriftlich) enthalten.",
         "Zielsprache: %1","Lehrbuch: „%1“, Seite %2",
         "Lies zuerst den gesamten Text im Screenshot sorgfältig. Wenn etwas unscharf oder unleserlich ist, sag das, statt zu raten. Antworte dann auf Deutsch unter den folgenden Überschriften; gib Beispielsätze in der Zielsprache mit deutscher Übersetzung an.",
         {{"Übersetzung","Erklärung","Grammatik","Typische Fehler","Verwechslungsgefahr","Gebrauch und Häufigkeit","Antworten prüfen"}},
         {{"Übersetze den Text in der Zielsprache Satz für Satz.",
           "Was dieser Teil (oder diese Übung) trainiert; Bedeutung und Gebrauch der wichtigsten Wörter und Ausdrücke.",
           "Die beteiligten Grammatikregeln, jeweils mit 1–2 Beispielsätzen.",
           "Die Fehler, die Lernende hier am häufigsten machen, und wie man sie vermeidet.",
           "Wörter, Formen oder Strukturen, die leicht verwechselt werden; vergleiche sie und erkläre den Unterschied.",
           "Für die wichtigen Wörter und Ausdrücke: Werden sie im Alltag, im akademischen Kontext, im Beruf oder selten verwendet? Gib auch das Register an.",
           "Prüfe meine Antworten einzeln. Markiere richtige mit ✓. Markiere falsche mit ✗, gib die richtige Antwort an und erkläre, was falsch ist und warum. Für unbeantwortete Aufgaben gib eine Musterlösung an; wenn mehrere Antworten möglich sind, nenne sie. Wenn der Screenshot keine Übung oder Antworten enthält, sag das einfach."}},
         "Sei streng: Weise auf jeden Fehler bei Rechtschreibung, Akzenten, Genus- und Numerus-Kongruenz, Konjugation usw. hin. Konzentriere dich auf das Wichtigste und fasse dich in jedem Abschnitt kurz.",
         "(bitte anhand des Screenshots bestimmen)",
         {{"Französisch","Deutsch","Spanisch","Italienisch","Englisch","Chinesisch"}}},
        {"es","Español",
         "Eres un profesor de idiomas experimentado y exigente. La captura adjunta procede de un libro de texto con el que estudio. Puede contener ejercicios y mis respuestas (escritas a máquina o a mano).",
         "Idioma de estudio: %1","Libro: «%1», página %2",
         "Primero lee con atención todo el texto de la captura. Si algo está borroso o es ilegible, dilo en lugar de adivinar. Después responde en español con los siguientes apartados; da los ejemplos en el idioma de estudio con su traducción al español.",
         {{"Traducción","Explicación","Gramática","Errores frecuentes","Confusiones habituales","Uso y frecuencia","Corrección"}},
         {{"Traduce frase por frase el texto escrito en el idioma de estudio.",
           "Qué practica esta parte (o este ejercicio); el significado y el uso de las palabras y expresiones clave.",
           "Las reglas gramaticales que intervienen, cada una con 1 o 2 ejemplos.",
           "Los errores que más cometen los estudiantes aquí y cómo evitarlos.",
           "Palabras, formas o estructuras que se confunden fácilmente; compáralas y explica la diferencia.",
           "Para las palabras y expresiones importantes: ¿se usan en la vida cotidiana, en contextos académicos, en el trabajo o rara vez? Indica también su registro.",
           "Revisa mis respuestas una por una. Marca las correctas con ✓. Marca las incorrectas con ✗, da la respuesta correcta y explica en qué consiste el error y por qué. Para las preguntas sin responder, da una respuesta modelo; si hay varias respuestas aceptables, menciónalas. Si la captura no contiene ejercicios ni respuestas, simplemente dilo."}},
         "Sé exigente: señala todos los errores de ortografía, acentos, concordancia de género y número, conjugación, etc. Céntrate en lo importante y sé conciso en cada apartado.",
         "(identifícalo a partir de la captura)",
         {{"francés","alemán","español","italiano","inglés","chino"}}},
        {"it","Italiano",
         "Sei un insegnante di lingue esperto ed esigente. Lo screenshot allegato viene da un libro di testo su cui sto studiando. Può contenere esercizi e le mie risposte (digitate o scritte a mano).",
         "Lingua di studio: %1","Libro: «%1», pagina %2",
         "Prima leggi con attenzione tutto il testo dello screenshot. Se qualcosa è sfocato o illeggibile, dillo invece di indovinare. Poi rispondi in italiano con i seguenti titoli; fornisci gli esempi nella lingua di studio con la traduzione in italiano.",
         {{"Traduzione","Spiegazione","Grammatica","Errori frequenti","Possibili confusioni","Uso e frequenza","Correzione"}},
         {{"Traduci frase per frase il testo scritto nella lingua di studio.",
           "Che cosa esercita questa parte (o questo esercizio); significato e uso delle parole e delle espressioni chiave.",
           "Le regole grammaticali coinvolte, ciascuna con 1–2 esempi.",
           "Gli errori che gli studenti commettono più spesso qui e come evitarli.",
           "Parole, forme o strutture che si confondono facilmente; confrontale e spiega la differenza.",
           "Per le parole e le espressioni importanti: si usano nella vita quotidiana, in ambito accademico, al lavoro o raramente? Indica anche il registro.",
           "Controlla le mie risposte una per una. Segna quelle corrette con ✓. Segna quelle sbagliate con ✗, dai la risposta corretta e spiega che cosa è sbagliato e perché. Per le domande senza risposta, dai una risposta modello; se sono accettabili più risposte, elencale. Se nello screenshot non ci sono esercizi né risposte, dillo semplicemente."}},
         "Sii esigente: segnala ogni errore di ortografia, accenti, accordo di genere e numero, coniugazione e simili. Concentrati sull’essenziale e sii conciso in ogni sezione.",
         "(da individuare dallo screenshot)",
         {{"francese","tedesco","spagnolo","italiano","inglese","cinese"}}},
        {"ja","日本語",
         "あなたは経験豊富で厳しい語学教師です。添付のスクリーンショットは私が学習中の教科書のもので、練習問題と私の解答（入力または手書き）が含まれている場合があります。",
         "学習言語：%1","教科書：『%1』%2ページ",
         "まずスクリーンショットの文字をすべて丁寧に読み取ってください。不鮮明・判読できない箇所は推測せず、その旨を指摘してください。そのうえで、以下の見出しに沿って日本語で回答してください。例文は学習言語で示し、日本語訳を付けてください。",
         {{"翻訳","解説","文法ポイント","間違えやすい点","混同しやすい点","使用場面と頻度","解答チェック"}},
         {{"スクリーンショット内の学習言語の文を一文ずつ翻訳してください。",
           "この部分（または練習問題）で何を練習しているか、重要な単語・表現の意味と使い方。",
           "関係する文法規則。それぞれ例文を1〜2つ。",
           "学習者がここでよく犯す誤りと、その避け方。",
           "混同しやすい単語・形・構文を比較し、違いを説明してください。",
           "重要な単語・表現が、日常・学術・仕事のどの場面で使われるか、あるいはほとんど使われないか。フォーマル度も示してください。",
           "私の解答を一問ずつ確認してください。正解には ✓、不正解には ✗ を付け、正しい答えと、どこがなぜ間違っているかを説明してください。未解答の問題には模範解答を示し、複数の答えが可能な場合はすべて挙げてください。練習問題や解答がない場合は、その旨だけで構いません。"}},
         "採点は厳しく、綴り、アクセント記号、性・数の一致、動詞の活用などの細かな誤りもすべて指摘してください。要点を絞り、各項目は簡潔にまとめてください。",
         "（スクリーンショットから判断してください）",
         {{"フランス語","ドイツ語","スペイン語","イタリア語","英語","中国語"}}},
        {"ko","한국어",
         "당신은 경험이 풍부하고 엄격한 언어 교사입니다. 첨부한 스크린샷은 제가 공부하고 있는 교재의 일부이며, 연습 문제와 제 답(입력 또는 손글씨)이 포함되어 있을 수 있습니다.",
         "학습 언어: %1","교재: 《%1》 %2쪽",
         "먼저 스크린샷의 글을 모두 꼼꼼히 읽어 주세요. 흐리거나 알아볼 수 없는 부분은 추측하지 말고 그렇다고 알려 주세요. 그런 다음 아래 제목에 따라 한국어로 답해 주세요. 예문은 학습 언어로 쓰고 한국어 번역을 붙여 주세요.",
         {{"번역","설명","문법 포인트","자주 하는 실수","헷갈리기 쉬운 점","사용 상황과 빈도","정답 확인"}},
         {{"스크린샷 속 학습 언어 문장을 한 문장씩 번역해 주세요.",
           "이 부분(또는 연습 문제)이 무엇을 연습하는지, 핵심 단어와 표현의 뜻과 쓰임.",
           "관련 문법 규칙을 각각 예문 1~2개와 함께.",
           "학습자가 여기서 가장 자주 하는 실수와 피하는 방법.",
           "헷갈리기 쉬운 단어, 형태, 구조를 비교하고 차이를 설명해 주세요.",
           "중요한 단어와 표현이 일상, 학술, 업무 중 어디에서 쓰이는지, 아니면 거의 쓰이지 않는지 알려 주세요. 격식 정도도 표시해 주세요.",
           "제 답을 한 문제씩 확인해 주세요. 맞은 답은 ✓, 틀린 답은 ✗로 표시하고 정답과 함께 무엇이 왜 틀렸는지 설명해 주세요. 답하지 않은 문제는 모범 답안을 주고, 가능한 답이 여러 개면 모두 알려 주세요. 스크린샷에 연습 문제나 답이 없으면 그렇다고만 말해 주세요."}},
         "엄격하게 채점해 주세요. 철자, 강세 부호, 성·수 일치, 동사 변화 등 세부 오류도 모두 지적해 주세요. 핵심 위주로, 각 항목은 간결하게 써 주세요.",
         "(스크린샷을 보고 판단해 주세요)",
         {{"프랑스어","독일어","스페인어","이탈리아어","영어","중국어"}}},
    }};
    return table;
}

const Text* find(const QString& code) {
    for(const auto& text:texts()) if(code==QLatin1String(text.code)) return &text;
    return nullptr;
}

}

QStringList languages() {
    QStringList result;
    for(const auto& text:texts()) result.append(QString::fromLatin1(text.code));
    return result;
}

QVariantList languageList() {
    QVariantList result;
    for(const auto& text:texts()) result.append(QVariantMap{{"code",QString::fromLatin1(text.code)},{"name",QString::fromUtf8(text.name)}});
    return result;
}

bool supported(const QString& code) { return find(code)!=nullptr; }

QString fromSystem(const QStringList& uiLanguages) {
    for(QString tag:uiLanguages) {
        tag=tag.replace(u'_',u'-').toLower();
        const QString primary=tag.section(u'-',0,0);
        if(primary==QLatin1String("zh")) {
            // zh-Hant, zh-TW, zh-HK, zh-MO: traditional characters; everything else simplified.
            const bool traditional=tag.contains(QLatin1String("hant")) || tag.endsWith(QLatin1String("-tw"))
                || tag.endsWith(QLatin1String("-hk")) || tag.endsWith(QLatin1String("-mo"));
            return traditional ? QStringLiteral("zh-Hant") : QStringLiteral("zh");
        }
        if(supported(primary)) return primary;
    }
    return {};
}

QString resolve(const QString& choice,bool englishInterface,const QStringList& uiLanguages) {
    const QString interface=englishInterface ? QStringLiteral("en") : QStringLiteral("zh");
    if(choice==QLatin1String("system")) { const auto system=fromSystem(uiLanguages); return system.isEmpty() ? interface : system; }
    return supported(choice) ? choice : interface;
}

QString build(const QString& language,const QString& target,const QString& title,int page) {
    const Text& text=*(find(language) ? find(language) : find(QStringLiteral("zh")));
    QString name=QString::fromUtf8(text.unknown);
    for(size_t i=0;i<bookLanguages.size();++i) if(target==QLatin1String(bookLanguages[i])) name=QString::fromUtf8(text.names[i]);
    QString book=title;
    book.remove(QRegularExpression(QStringLiteral("\\.pdf$"),QRegularExpression::CaseInsensitiveOption));
    QStringList lines{QString::fromUtf8(text.intro),QString(),
        QString::fromUtf8(text.target).arg(name),QString::fromUtf8(text.book).arg(book.trimmed()).arg(page),QString(),
        QString::fromUtf8(text.read),QString()};
    // A book in the prompt language needs no translation section.
    const bool same=!target.isEmpty() && QString::fromLatin1(text.code).section(u'-',0,0)==target;
    int number=0;
    for(int i=same ? 1 : 0;i<sectionCount;++i) {
        lines.append(QStringLiteral("## %1. %2").arg(++number).arg(QString::fromUtf8(text.titles[size_t(i)])));
        lines.append(QString::fromUtf8(text.bodies[size_t(i)]));
        lines.append(QString());
    }
    lines.append(QString::fromUtf8(text.rules));
    return lines.join(u'\n');
}

}
