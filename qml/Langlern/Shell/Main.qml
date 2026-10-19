pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    required property ShellState shell
    signal probeAcknowledged()
    onProbeAcknowledged: shell.acknowledgeProbe()
    property bool probeEnabled: false
    readonly property string themeLabel: [qsTr("跟随系统"), qsTr("浅色"), qsTr("深色")][shell.themeMode]
    width: 1440
    height: 900
    minimumWidth: 640
    minimumHeight: 480
    title: shell.selectedName.length > 0 ? qsTr("%1 — LinguaPDF").arg(shell.selectedName) : "LinguaPDF"
    color: shellTheme.canvas
    onClosing: function(close) {
        offline.stopSpeaking()
        workspace.updateProgress(window.shell.selectedUrl, window.shell.pageCount, window.shell.currentPage)
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

    Theme { id: shellTheme; dark: window.shell.dark; paper: workspace.paper }
    WorkspaceState { id: workspace }
    OfflineServices { id: offline }
    Connections {
        target: window.shell
        property var previousSession: window.shell.sessionId
        function onSelectionChanged(): void {
            if (previousSession !== window.shell.sessionId) {
                previousSession = window.shell.sessionId
                offline.invalidate()
            }
        }
    }

    Action {
        id: openAction
        text: qsTr("选择 PDF…")
        shortcut: StandardKey.Open
        onTriggered: window.chooseFile()
    }
    Action {
        id: closeAction
        text: qsTr("关闭文档")
        shortcut: (Qt.platform.os === "osx" ? "Meta" : "Ctrl") + "+W"
        enabled: window.shell.selectedName.length > 0
        onTriggered: window.shell.clearSelection()
    }
    Action {
        id: themeAction
        text: window.themeLabel
        shortcut: (Qt.platform.os === "osx" ? "Meta" : "Ctrl") + "+Shift+L"
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
                contentItem: ScrollView {
                    implicitHeight: Math.min(400, window.height - 180)
                    clip: true
                    Label {
                        width: parent.width
                        text: (Qt.platform.os === "osx" ? "⌘" : "Ctrl")
                            + qsTr(" 为修饰键：\nO  打开 PDF    W  关闭文档    S  保存\nZ / Y  撤销 / 重做（编辑时为文字历史）\nC  复制选中原文\nShift+A  自动：空白输入 / 原文拖选\nShift+S / T  原文选择 / 输入答案\nShift+P / E / M  画笔 / 整笔擦除 / 移动笔迹\nShift+L  切换主题\n滚轮  阅读    Ctrl+滚轮  缩放\nEsc / Ctrl+Enter  完成文字编辑\nCtrl+Delete  删除选中文字框\nDelete  删除选中笔迹\n\n答案自动保存在本机。导出时使用新的 PDF 文件名。\nF1  快捷键    Tab / Shift+Tab  切换焦点")
                        lineHeight: 1.6
                        wrapMode: Text.WordWrap
                        color: shellTheme.text
                    }
                }
            }
        }
    }

    WorkspaceFrame {
        anchors.fill: parent
        shell: window.shell
        theme: shellTheme
        workspace: workspace
        services: offline
        onOpenRequested: window.chooseFile()
        onExportRequested: exportLoader.active = true
        onHelpRequested: helpLoader.active = true
    }
}
