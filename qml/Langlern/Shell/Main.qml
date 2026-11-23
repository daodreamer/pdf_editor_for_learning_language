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
    // Native title bar takes the ground colour, so the caption reads as part of the shell.
    WindowChrome {
        objectName: "windowChrome"
        window: window
        caption: shellTheme.canvas; text: shellTheme.text; border: shellTheme.canvas
        dark: shellTheme.dark
    }
    WorkspaceState { id: workspace }
    // The last text size the user picked is the size of every new answer, also after restart.
    Component.onCompleted: window.shell.annotations.defaultFontSize = workspace.textSize
    Connections {
        target: window.shell.annotations
        function onDefaultFontSizeChanged(): void { workspace.textSize = window.shell.annotations.defaultFontSize }
    }
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
    // Platform window conventions: ⌃⌘F / F11 full screen, ⌘M minimise and ⌘? help on macOS,
    // Ctrl+F4 closes the document on Windows (Ctrl+W comes from closeAction).
    readonly property bool mac: Qt.platform.os === "osx"
    Shortcut {
        sequences: [StandardKey.FullScreen]
        onActivated: window.visibility = window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }
    Shortcut { sequence: "Ctrl+M"; enabled: window.mac; onActivated: window.showMinimized() }
    Shortcut { sequences: [StandardKey.HelpContents]; enabled: window.mac; onActivated: helpLoader.active = true }
    Shortcut { sequence: "Ctrl+F4"; enabled: !window.mac && closeAction.enabled; onActivated: closeAction.trigger() }
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
                padding: 24
                background: Rectangle { radius: 24; color: shellTheme.card; border.color: shellTheme.line }
                contentItem: ScrollView {
                    implicitHeight: Math.min(400, window.height - 180)
                    clip: true
                    Label {
                        width: parent.width
                        // Qt maps "Ctrl" shortcuts and Qt.ControlModifier to Command on macOS.
                        readonly property bool mac: Qt.platform.os === "osx"
                        readonly property string mod: mac ? "⌘" : "Ctrl+"
                        text: (mac
                            ? qsTr("文件\n⌘O  打开 PDF    ⌘W  关闭文档    ⌘S  保存\n⌘,  外观与布局    ⌘Q  退出\n\n阅读与缩放\n滚动 / 空格 / ⇧空格 / PageUp·PageDown  翻屏\n↑ ↓  滚动    ← →  上一页 / 下一页（放大时左右平移）\n⌘↑ / ⌘↓  首页 / 末页    ⌘⌥G  跳到页码\n⌘+滚动 / 触控板双指捏合  以指针为中心缩放\n⌘+ / ⌘−  放大 / 缩小    ⌘0  适应页宽\n⌃⌘F  全屏    ⌘M  最小化\n\n编辑\n⌘Z / ⇧⌘Z  撤销 / 重做（编辑时为文字历史）\n⌘C  复制选中原文\n⇧⌘A  自动：空白输入 / 原文拖选\n⇧⌘S / ⇧⌘T  原文选择 / 输入答案\n⇧⌘P / ⇧⌘E / ⇧⌘M  画笔 / 整笔擦除 / 移动笔迹\n⇧⌘L  切换主题\nEsc / ⌘↩  完成文字编辑\n⌘⌦（fn+⌘+Delete）  删除选中文字框\nDelete  删除选中笔迹\n\n答案自动保存在本机。导出时使用新的 PDF 文件名。\n⌘?  快捷键    Tab / ⇧Tab  切换焦点")
                            : qsTr("文件\nCtrl+O  打开 PDF    Ctrl+W / Ctrl+F4  关闭文档    Ctrl+S  保存\nCtrl+,  外观与布局    Ctrl+Q  退出\n\n阅读与缩放\n滚轮 / 空格 / Shift+空格 / PageUp·PageDown  翻屏\n↑ ↓  滚动    ← →  上一页 / 下一页（放大时左右平移）\nHome / End（Ctrl+Home / Ctrl+End）  首页 / 末页    Ctrl+G  跳到页码\nCtrl+滚轮 / 触控板双指捏合  以指针为中心缩放\nCtrl++ / Ctrl+−  放大 / 缩小    Ctrl+0  适应页宽\nF11  全屏\n\n编辑\nCtrl+Z / Ctrl+Y  撤销 / 重做（编辑时为文字历史）\nCtrl+C  复制选中原文\nCtrl+Shift+A  自动：空白输入 / 原文拖选\nCtrl+Shift+S / T  原文选择 / 输入答案\nCtrl+Shift+P / E / M  画笔 / 整笔擦除 / 移动笔迹\nCtrl+Shift+L  切换主题\nEsc / Ctrl+Enter  完成文字编辑\nCtrl+Delete  删除选中文字框\nDelete  删除选中笔迹\n\n答案自动保存在本机。导出时使用新的 PDF 文件名。\nF1  快捷键    Tab / Shift+Tab  切换焦点"))
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
    }
}
