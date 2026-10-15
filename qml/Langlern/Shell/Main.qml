pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    required property ShellState shell
    signal probeAcknowledged()
    onProbeAcknowledged: shell.acknowledgeProbe()
    property bool probeEnabled: false
    readonly property string themeLabel: [qsTr("跟随系统"), qsTr("浅色"), qsTr("深色")][shell.themeMode]
    width: 1120
    height: 760
    minimumWidth: 640
    minimumHeight: 480
    title: "Langlern"
    color: theme.canvas
    font.pixelSize: 14
    palette.window: theme.surface
    palette.windowText: theme.text
    palette.base: theme.surface
    palette.text: theme.text
    palette.button: theme.surface
    palette.buttonText: theme.text
    palette.highlight: theme.accent
    palette.highlightedText: theme.surface

    Theme { id: theme; dark: window.shell.dark }

    Action {
        id: openAction
        text: qsTr("选择 PDF…")
        shortcut: StandardKey.Open
        onTriggered: window.chooseFile()
    }
    Action {
        id: closeAction
        text: qsTr("清除选择")
        shortcut: "Ctrl+W"
        enabled: window.shell.selectedName.length > 0
        onTriggered: window.shell.clearSelection()
    }
    Action {
        id: themeAction
        text: window.themeLabel
        shortcut: "Ctrl+Shift+L"
        onTriggered: window.shell.cycleTheme()
    }
    Action {
        id: helpAction
        text: qsTr("快捷键")
        shortcut: "F1"
        onTriggered: helpLoader.active = true
    }
    Shortcut { sequences: [StandardKey.Quit]; onActivated: window.close() }
    Shortcut { sequence: "F6"; enabled: window.probeEnabled; onActivated: window.probeAcknowledged() }

    function chooseFile(): void {
        if (fileLoader.status === Loader.Ready)
            (fileLoader.item as FileDialog).open()
        else
            fileLoader.active = true
    }

    Loader {
        id: fileLoader
        objectName: "fileLoader"
        active: false
        sourceComponent: Component {
            FileDialog {
                title: qsTr("选择本地 PDF")
                fileMode: FileDialog.OpenFile
                nameFilters: [qsTr("PDF 文件 (*.pdf)"), qsTr("所有文件 (*)")]
                Component.onCompleted: open()
                onAccepted: {
                    window.shell.selectFile(selectedFile)
                }
            }
        }
    }
    Loader {
        id: helpLoader
        objectName: "helpLoader"
        active: false
        sourceComponent: Component {
            Dialog {
                parent: window.contentItem
                anchors.centerIn: parent
                width: Math.min(460, window.width - 48)
                title: qsTr("快捷键")
                modal: true
                standardButtons: Dialog.Ok
                Component.onCompleted: open()
                onClosed: helpLoader.active = false
                contentItem: Label {
                    text: (Qt.platform.os === "osx" ? "⌘" : "Ctrl") + qsTr("+O  选择 PDF\n")
                        + (Qt.platform.os === "osx" ? "⌘" : "Ctrl") + qsTr("+W  清除选择\n")
                        + (Qt.platform.os === "osx" ? "⌘" : "Ctrl") + qsTr("+Shift+L  切换主题\nF1  快捷键\nTab / Shift+Tab  切换焦点\nEsc  关闭对话框")
                    lineHeight: 1.6
                    wrapMode: Text.WordWrap
                    color: theme.text
                }
            }
        }
    }

    header: ToolBar {
        padding: 16
        background: Rectangle { color: theme.surface }
        contentItem: RowLayout {
            spacing: 16
            Label { text: "Langlern"; font.pixelSize: 20; font.weight: Font.DemiBold; color: theme.text }
            Label { text: qsTr("阅读 · 练习"); color: theme.muted; visible: window.width > 800 }
            Item { Layout.fillWidth: true }
            ToolButton { objectName: "openButton"; action: openAction; activeFocusOnTab: true }
            ToolButton { action: themeAction; activeFocusOnTab: true; Accessible.name: qsTr("切换主题：") + window.themeLabel }
            ToolButton { action: helpAction; activeFocusOnTab: true }
        }
    }
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(520, window.width - 80)
        spacing: 18
        Label {
            Layout.fillWidth: true
            text: qsTr("为每一次练习，留一处空间。")
            font.pixelSize: 28
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: theme.text
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("本地 PDF 阅读与语言练习工具")
            horizontalAlignment: Text.AlignHCenter
            color: theme.muted
        }
        Button {
            id: chooseButton
            Layout.alignment: Qt.AlignHCenter
            action: openAction
            highlighted: true
            padding: 16
            activeFocusOnTab: true
            palette.buttonText: theme.surface
            palette.highlightedText: theme.surface
            background: Rectangle {
                radius: 6
                color: chooseButton.down ? Qt.darker(theme.accent, 1.12) : theme.accent
                border.width: chooseButton.visualFocus ? 2 : 0
                border.color: theme.text
            }
        }
        Label {
            Layout.fillWidth: true
            text: window.shell.selectedName
            visible: text.length > 0
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
            color: theme.text
            textFormat: Text.PlainText
        }
        Label {
            Layout.fillWidth: true
            text: window.shell.statusText.length > 0 ? window.shell.statusText
                  : qsTr("当前版本提供基础窗口与文件选择，PDF 阅读功能尚未接入。")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            color: theme.muted
            textFormat: Text.PlainText
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            action: closeAction
            visible: closeAction.enabled
            flat: true
            activeFocusOnTab: true
        }
    }
    footer: ToolBar {
        padding: 12
        background: Rectangle { color: theme.surface }
        contentItem: RowLayout {
            Label { text: qsTr("离线 · 本地文件"); color: theme.muted }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("尚未加载文档"); color: theme.muted }
        }
    }
}
