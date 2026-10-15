#include "shellstate.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QStyleHints>

ShellState::ShellState(QObject* parent) : QObject(parent) {
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this] { if (themeMode_ == 0) emit themeChanged(); });
}

bool ShellState::dark() const {
    return themeMode_ == 2 || (themeMode_ == 0 &&
        QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
}

void ShellState::selectFile(const QUrl& url) {
    if (url.isEmpty()) return;
    const QFileInfo file(url.toLocalFile());
    if (!url.isLocalFile() || !file.isFile() || !file.isReadable() ||
        file.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0) {
        statusText_ = QStringLiteral("请选择可读取的本地 PDF 文件。");
    } else {
        selectedName_ = file.fileName();
        statusText_ = QStringLiteral("已选择文件，尚未加载。PDF 阅读将在后续版本提供。");
    }
    emit selectionChanged();
}

void ShellState::clearSelection() {
    selectedName_.clear();
    statusText_.clear();
    emit selectionChanged();
}

void ShellState::cycleTheme() {
    themeMode_ = (themeMode_ + 1) % 3;
    emit themeChanged();
}
