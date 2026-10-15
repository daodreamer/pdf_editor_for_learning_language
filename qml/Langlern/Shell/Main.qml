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
    title: shell.selectedName.length > 0 ? qsTr("%1 — Langlern").arg(shell.selectedName) : "Langlern"
    color: shellTheme.canvas
    onClosing: function(close) {
        if (!window.shell.shutdownComplete) {
            close.accepted = false
            window.shell.beginShutdown()
        }
    }
    Connections {
        target: window.shell
        function onShutdownChanged(): void { if (window.shell.shutdownComplete) window.close() }
    }
    font.pixelSize: 14
    palette.window: shellTheme.surface
    palette.windowText: shellTheme.text
    palette.base: shellTheme.surface
    palette.text: shellTheme.text
    palette.button: shellTheme.surface
    palette.buttonText: shellTheme.text
    palette.highlight: shellTheme.accent
    palette.highlightedText: shellTheme.surface

    Theme { id: shellTheme; dark: window.shell.dark }

    Action {
        id: openAction
        text: qsTr("选择 PDF…")
        shortcut: StandardKey.Open
        onTriggered: window.chooseFile()
    }
    Action {
        id: closeAction
        text: qsTr("关闭文档")
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
        id: exportLoader
        active: false
        sourceComponent: Component {
            FileDialog {
                title: qsTr("导出为新 PDF（请选择未使用的文件名）")
                fileMode: FileDialog.SaveFile
                nameFilters: [qsTr("PDF 文件 (*.pdf)")]
                defaultSuffix: "pdf"
                Component.onCompleted: open()
                onAccepted: { window.shell.exportPdf(selectedFile); exportLoader.active = false }
                onRejected: exportLoader.active = false
            }
        }
    }
    Shortcut { sequences: [StandardKey.Save]; onActivated: window.shell.saveNow() }
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
                        + (Qt.platform.os === "osx" ? "⌘" : "Ctrl") + qsTr("+W  关闭文档\n")
                        + (Qt.platform.os === "osx" ? "⌘" : "Ctrl") + qsTr("+Shift+L  切换主题\nF1  快捷键\nTab / Shift+Tab  切换焦点\nEsc  关闭对话框")
                    lineHeight: 1.6
                    wrapMode: Text.WordWrap
                    color: shellTheme.text
                }
            }
        }
    }

    header: ToolBar {
        padding: 16
        background: Rectangle { color: shellTheme.surface }
        contentItem: RowLayout {
            spacing: 16
            Label { text: "Langlern"; font.pixelSize: 20; font.weight: Font.DemiBold; color: shellTheme.text }
            Label { text: qsTr("阅读 · 练习"); color: shellTheme.muted; visible: window.width > 800 }
            Item { Layout.fillWidth: true }
            ToolButton { objectName: "openButton"; action: openAction; activeFocusOnTab: true }
            ToolButton { text: qsTr("保存"); enabled: window.shell.storageReady && !window.shell.transitioning; onClicked: window.shell.saveNow() }
            ToolButton { objectName: "exportButton"; text: qsTr("导出 PDF…"); enabled: window.shell.storageReady && window.shell.pageCount > 0 && !window.shell.exporting && !window.shell.transitioning; onClicked: exportLoader.active = true }
            ToolButton { action: themeAction; activeFocusOnTab: true; Accessible.name: qsTr("切换主题：") + window.themeLabel }
            ToolButton { action: helpAction; activeFocusOnTab: true }
        }
    }
    DocumentView {
        anchors.fill: parent
        enabled: !window.shell.transitioning
        shell: window.shell
        theme: shellTheme
        visible: window.shell.pageCount > 0
    }
    ColumnLayout {
        visible: window.shell.pageCount === 0
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
            color: shellTheme.text
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("本地 PDF 阅读与语言练习工具")
            horizontalAlignment: Text.AlignHCenter
            color: shellTheme.muted
        }
        Button {
            id: chooseButton
            Layout.alignment: Qt.AlignHCenter
            action: openAction
            highlighted: true
            padding: 16
            activeFocusOnTab: true
            palette.buttonText: shellTheme.surface
            palette.highlightedText: shellTheme.surface
            background: Rectangle {
                radius: 6
                color: chooseButton.down ? Qt.darker(shellTheme.accent, 1.12) : shellTheme.accent
                border.width: chooseButton.visualFocus ? 2 : 0
                border.color: shellTheme.text
            }
        }
        Label {
            Layout.fillWidth: true
            text: window.shell.selectedName
            visible: text.length > 0
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
            color: shellTheme.text
            textFormat: Text.PlainText
        }
        Label {
            Layout.fillWidth: true
            text: window.shell.statusText.length > 0 ? window.shell.statusText
                  : qsTr("打开本地 PDF，开始阅读。")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            color: shellTheme.muted
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
        background: Rectangle { color: shellTheme.surface }
        contentItem: RowLayout {
            Label { text: qsTr("离线 · 本地文件"); color: shellTheme.muted }
            Item { Layout.fillWidth: true }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: window.shell.exportStatus.length > 0 ? window.shell.exportStatus : window.shell.storageStatus.length > 0 ? window.shell.storageStatus : window.shell.statusText
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                color: shellTheme.muted
            }
        }
    }
}
