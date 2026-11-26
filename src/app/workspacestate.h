#pragma once
#include <QObject>
#include <QVariantList>
#include <QStringList>
#include <QUrl>
#include <QThread>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class WorkspaceState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList books READ books NOTIFY changed)
    Q_PROPERTY(QVariantList words READ words NOTIFY changed)
    Q_PROPERTY(QVariantList bookmarks READ bookmarks NOTIFY changed)
    Q_PROPERTY(QString document READ document WRITE setDocument NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int dueCount READ dueCount NOTIFY changed)
    Q_PROPERTY(int layout READ layout WRITE setLayout NOTIFY preferencesChanged)
    Q_PROPERTY(int toolbar READ toolbar WRITE setToolbar NOTIFY preferencesChanged)
    Q_PROPERTY(bool paper READ paper WRITE setPaper NOTIFY preferencesChanged)
    Q_PROPERTY(bool nightDim READ nightDim WRITE setNightDim NOTIFY preferencesChanged)
    Q_PROPERTY(bool english READ english WRITE setEnglish NOTIFY preferencesChanged)
    Q_PROPERTY(QString lookupLanguage READ lookupLanguage WRITE setLookupLanguage NOTIFY preferencesChanged)
    Q_PROPERTY(QString frenchVoice READ frenchVoice WRITE setFrenchVoice NOTIFY preferencesChanged)
    Q_PROPERTY(double textSize READ textSize WRITE setTextSize NOTIFY preferencesChanged)
    Q_PROPERTY(double inkWidth READ inkWidth WRITE setInkWidth NOTIFY preferencesChanged)
    // Typing-tool defaults for new answers and the tick-box mark (0 auto, 1 tick, 2 cross).
    Q_PROPERTY(QString textColor READ textColor WRITE setTextColor NOTIFY preferencesChanged)
    Q_PROPERTY(bool textBold READ textBold WRITE setTextBold NOTIFY preferencesChanged)
    Q_PROPERTY(bool textItalic READ textItalic WRITE setTextItalic NOTIFY preferencesChanged)
    Q_PROPERTY(int checkMark READ checkMark WRITE setCheckMark NOTIFY preferencesChanged)
    // Input source follows the document language; per-language source picks (language → system
    // source id, absent = automatic); languages whose missing-keyboard notice the user turned off.
    Q_PROPERTY(bool inputSwitching READ inputSwitching WRITE setInputSwitching NOTIFY preferencesChanged)
    Q_PROPERTY(QVariantMap inputChoices READ inputChoices NOTIFY preferencesChanged)
    Q_PROPERTY(QStringList inputNoticesOff READ inputNoticesOff NOTIFY preferencesChanged)
    // "Ask AI": last chat service ("claude" / "chatgpt") and the prompt language choice: "app"
    // (interface language, default), "system" or a language code (AiPrompt::languages()).
    Q_PROPERTY(QString aiTarget READ aiTarget WRITE setAiTarget NOTIFY preferencesChanged)
    Q_PROPERTY(QString aiPromptLanguage READ aiPromptLanguage WRITE setAiPromptLanguage NOTIFY preferencesChanged)
public:
    explicit WorkspaceState(QObject* parent=nullptr);
    ~WorkspaceState() override;
    QVariantList books() const { return books_; }
    QVariantList words() const { return words_; }
    QVariantList bookmarks() const;
    QString document() const { return document_; }
    QString error() const { return error_; }
    int dueCount() const;
    int layout() const { return layout_; }
    int toolbar() const { return toolbar_; }
    bool paper() const { return paper_; }
    bool nightDim() const { return nightDim_; }
    bool english() const { return english_; }
    QString lookupLanguage() const { return lookupLanguage_; }
    QString frenchVoice() const { return frenchVoice_; }
    double textSize() const { return textSize_; }
    double inkWidth() const { return inkWidth_; }
    QString textColor() const { return textColor_; }
    bool textBold() const { return textBold_; }
    bool textItalic() const { return textItalic_; }
    int checkMark() const { return checkMark_; }
    bool inputSwitching() const { return inputSwitching_; }
    void setInputSwitching(bool value);
    QVariantMap inputChoices() const { return inputChoices_; }
    // Empty id goes back to automatic.
    Q_INVOKABLE void setInputChoice(const QString& language,const QString& id);
    QStringList inputNoticesOff() const { return inputNoticesOff_; }
    Q_INVOKABLE void silenceInputNotice(const QString& language);
    QString aiTarget() const { return aiTarget_; }
    void setAiTarget(QString value);
    QString aiPromptLanguage() const { return aiPromptLanguage_; }
    void setAiPromptLanguage(QString value);
    void setTextColor(QString value);
    void setTextBold(bool value);
    void setTextItalic(bool value);
    void setCheckMark(int value);
    void setDocument(QString path);
    void setLayout(int value);
    void setToolbar(int value);
    void setPaper(bool value);
    void setNightDim(bool value);
    void setEnglish(bool value);
    void setLookupLanguage(QString value);
    void setFrenchVoice(QString value);
    void setTextSize(double value);
    void setInkWidth(double value);
    Q_INVOKABLE void remember(const QUrl& file, const QString& title, int pages, int page);
    Q_INVOKABLE void updateProgress(const QUrl& file,int pages,int page);
    Q_INVOKABLE void forget(const QUrl& file);
    Q_INVOKABLE bool bookmarked(int page) const;
    Q_INVOKABLE void toggleBookmark(int page);
    Q_INVOKABLE void addWord(QString word, QString meaning, QString context, QString source, int page);
    Q_INVOKABLE void editMeaning(int index, QString meaning);
    Q_INVOKABLE void grade(int index, int grade);
    Q_INVOKABLE void removeWord(int index);
signals:
    void changed();
    void preferencesChanged();
private:
    void save();
    void scheduleSave();
    QVariantMap snapshot() const;
    QVariantList books_, words_;
    QVariantMap bookmarks_;
    QString document_, error_, path_;
    int layout_=0, toolbar_=0;
    bool paper_=false;
    bool nightDim_=true;
    bool english_=false;
    QString lookupLanguage_=QStringLiteral("fr");
    QString frenchVoice_=QStringLiteral("male");
    double textSize_, inkWidth_;
    QString textColor_=QStringLiteral("#1e468c");
    bool textBold_=false, textItalic_=false;
    int checkMark_=0;
    bool inputSwitching_=true;
    QVariantMap inputChoices_;
    QStringList inputNoticesOff_;
    QString aiTarget_=QStringLiteral("claude");
    QString aiPromptLanguage_=QStringLiteral("app");
    QThread writerThread_;
    QObject* writer_=nullptr;
    QTimer timer_;
    QVariantMap accepted_;
    bool loadBlocked_=false;
};
