#pragma once

#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class ShellState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY selectionChanged)
    Q_PROPERTY(int themeMode READ themeMode NOTIFY themeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY themeChanged)
public:
    explicit ShellState(QObject* parent = nullptr);
    QString selectedName() const { return selectedName_; }
    QString statusText() const { return statusText_; }
    int themeMode() const { return themeMode_; }
    bool dark() const;
    Q_INVOKABLE void selectFile(const QUrl& url);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void cycleTheme();
    Q_INVOKABLE void acknowledgeProbe() { emit startupAcknowledged(); }
signals:
    void startupAcknowledged();
    void selectionChanged();
    void themeChanged();
private:
    QString selectedName_;
    QString statusText_;
    int themeMode_ = 0; // system, light, dark
};
