#pragma once
// Switches the system keyboard input source to the open document's language while LinguaPDF is
// the active application, and puts the previous source back when the user leaves the app,
// closes the document or turns the feature off. UI thread only (see inputsources.h).
#include "inputsources.h"
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <QtQml/qqmlregistration.h>

class InputSourceSwitcher : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // Inputs, bound from QML: detected document language, whether a document is open, the saved
    // on/off preference and the per-language choice (language → source id; missing = automatic).
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed)
    Q_PROPERTY(bool documentOpen READ documentOpen WRITE setDocumentOpen NOTIFY changed)
    Q_PROPERTY(bool switching READ switching WRITE setSwitching NOTIFY changed)
    Q_PROPERTY(QVariantMap choices READ choices WRITE setChoices NOTIFY changed)
    // Every selectable source: {id, name, language}.
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
    // Source used for the document language (empty when none is installed or nothing applies).
    Q_PROPERTY(QString target READ target NOTIFY changed)
    Q_PROPERTY(QString targetName READ targetName NOTIFY changed)
    // Switching is wanted but the system has no source for the document language.
    Q_PROPERTY(bool missing READ missing NOTIFY changed)
    Q_PROPERTY(bool engaged READ engaged NOTIFY changed)
    Q_PROPERTY(bool supported READ supported CONSTANT)
public:
    struct Backend {
        std::function<QList<InputSources::Source>()> enabled;
        std::function<QString()> current;
        std::function<bool(const QString&)> select;
    };
    explicit InputSourceSwitcher(QObject* parent=nullptr);
    // Tests drive a fake system; the application state is then set with setApplicationActive.
    InputSourceSwitcher(Backend backend,bool active,QObject* parent=nullptr);
    ~InputSourceSwitcher() override;
    QString language() const { return language_; }
    void setLanguage(const QString& value);
    bool documentOpen() const { return documentOpen_; }
    void setDocumentOpen(bool value);
    bool switching() const { return switching_; }
    void setSwitching(bool value);
    QVariantMap choices() const { return choices_; }
    void setChoices(const QVariantMap& value);
    QVariantList sources() const;
    QString target() const { return target_; }
    QString targetName() const;
    bool missing() const { return missing_; }
    bool engaged() const { return engaged_; }
    bool supported() const { return supported_; }
    void setApplicationActive(bool active);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openSystemSettings() const;
signals:
    void changed();
    void sourcesChanged();
private:
    QString resolve() const;
    void update();
    void restore();
    Backend backend_;
    QList<InputSources::Source> sources_;
    QString language_;
    QVariantMap choices_;
    bool documentOpen_=false, switching_=false, active_=false, supported_=true;
    bool engaged_=false, missing_=false;
    // While engaged: previous_ is the source to put back, applied_ the one this object selected.
    QString previous_, applied_, target_;
};
