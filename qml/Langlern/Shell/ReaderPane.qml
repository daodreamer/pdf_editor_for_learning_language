pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: pane
    required property ShellState shell
    required property Theme theme
    required property WorkspaceState workspace
    required property OfflineServices services
    property bool audioVisible: false
    signal libraryRequested()
    signal wordsRequested()
    signal exportRequested()
    signal settingsRequested()
    signal openRequested()
    property int tool: 0
    property bool penOptions: false
    readonly property int spreadFirst: Math.floor((shell.currentPage - 1) / 2) * 2 + 1
    readonly property bool editingField: {
        const item = Window.window ? Window.window.activeFocusItem : null
        return item instanceof TextInput || item instanceof TextEdit
    }
    Shortcut { sequence: "A"; enabled: pane.visible && !pane.editingField; onActivated: pane.pick(0) }
    Shortcut { sequence: "P"; enabled: pane.visible && !pane.editingField; onActivated: pane.pick(1) }
    Shortcut { sequence: "T"; enabled: pane.visible && !pane.editingField; onActivated: pane.pick(2) }
    Shortcut { sequence: "C"; enabled: pane.visible && !pane.editingField; onActivated: pane.pick(3) }
    Shortcut { sequence: "R"; enabled: pane.visible && !pane.editingField; onActivated: pane.pick(4) }
    Shortcut { sequence: "E"; enabled: pane.visible && !pane.editingField; onActivated: pane.pick(5) }
    ToolTip { id: feedback; parent: pane; text: pane.services.status; timeout: 6000 }
    Connections { target: pane.services; function onChanged(): void { if (pane.services.status.length > 0) feedback.open() } }
    readonly property var hints: [qsTr("点空白处打字 · 点原文查词 · 拖动即书写 · Alt 点答案批改"), qsTr("直接在页面书写 · Ctrl / Command + Z 撤销"), qsTr("点空白处输入答案 · Esc 完成编辑"), qsTr("点击文字答案：✓ → × → 清除 · 用打字工具补充订正"), qsTr("选择原文，查看释义并加入生词本"), qsTr("点击或划过笔迹擦除 · 文字答案可选中后删除")]
    function pick(index: int): void {
        penOptions = index === 1 && (tool !== 1 || !penOptions)
        tool = index
        document.switchMode([0, 3, 2, 0, 1, 4][index])
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
    Rectangle {
        Layout.fillWidth: true; Layout.preferredHeight: pane.width < 1280 && pane.workspace.toolbar === 0 ? 104 : 52
        color: pane.theme.bar
        RowLayout {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 52
            anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
            DesignButton { theme: pane.theme; text: qsTr("资料库"); glyph: "‹"; onClicked: { pane.shell.finishTextEditing(); pane.libraryRequested() } }
            ColumnLayout {
                Layout.fillWidth: true; Layout.maximumWidth: pane.width >= 1050 && pane.workspace.toolbar === 0 ? 230 : 500; spacing: 1
                Label { text: pane.shell.selectedName; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 13; font.weight: Font.DemiBold; color: pane.theme.text; textFormat: Text.PlainText }
                Label { text: qsTr("第 %1 页 · 本地自动保存").arg(pane.shell.currentPage); color: pane.theme.muted; font.pixelSize: 11 }
            }
            Item { Layout.fillWidth: true }
            Row {
                id: navigation; objectName: "navigationTools"; spacing: 0
                DesignButton { objectName: "previousPageButton"; theme: pane.theme; text: "‹"; enabled: pane.shell.doublePage ? pane.spreadFirst > 1 : pane.shell.currentPage > 1; onClicked: pane.shell.goToPage(pane.shell.doublePage ? pane.spreadFirst - 2 : pane.shell.currentPage - 1); Accessible.name: pane.shell.doublePage ? qsTr("上一组") : qsTr("上一页") }
                SpinBox {
                    objectName: "pageNumber"; width: 66; height: 30
                    from: 1; to: pane.shell.pageCount; value: pane.shell.currentPage; editable: true
                    up.indicator: Item {}
                    down.indicator: Item {}
                    onValueModified: pane.shell.goToPage(value)
                    Accessible.name: qsTr("页码"); background: Rectangle { radius: 7; color: pane.theme.fill }
                }
                Label { text: "/ " + pane.shell.pageCount; height: 30; verticalAlignment: Text.AlignVCenter; color: pane.theme.muted; font.pixelSize: 12 }
                DesignButton { objectName: "nextPageButton"; theme: pane.theme; text: "›"; enabled: pane.shell.doublePage ? pane.spreadFirst + 2 <= pane.shell.pageCount : pane.shell.currentPage < pane.shell.pageCount; onClicked: pane.shell.goToPage(pane.shell.doublePage ? pane.spreadFirst + 2 : pane.shell.currentPage + 1); Accessible.name: pane.shell.doublePage ? qsTr("下一组") : qsTr("下一页") }
            }
            DesignButton { theme: pane.theme; text: "⚑"; selected: { const pages = pane.workspace.bookmarks; return pages.includes(pane.shell.currentPage) } onClicked: pane.workspace.toggleBookmark(pane.shell.currentPage); Accessible.name: qsTr("切换书签") }
            DesignButton { theme: pane.theme; text: qsTr("生词本 %1").arg(pane.workspace.words.length); visible: pane.width > 700; onClicked: pane.wordsRequested() }
            DesignButton { theme: pane.theme; text: "♪"; selected: pane.audioVisible; onClicked: pane.audioVisible = !pane.audioVisible; Accessible.name: qsTr("本地音频") }
            DesignButton { theme: pane.theme; text: "···"; onClicked: more.open(); Accessible.name: qsTr("文档菜单") }
        }
        StudyTools {
            id: topTools; objectName: "editingTools"
            anchors.horizontalCenter: parent.horizontalCenter
            y: pane.width < 1280 ? 52 : 1
            visible: pane.workspace.toolbar === 0
            theme: pane.theme; current: pane.tool; onPicked: function(index) { pane.pick(index) }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: pane.theme.line }
    }
    RowLayout {
        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
        PageSidebar { Layout.preferredWidth: 196; Layout.fillHeight: true; visible: pane.workspace.layout === 0 && pane.width > 800; theme: pane.theme; shell: pane.shell; workspace: pane.workspace }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; color: pane.theme.canvas
            DocumentView {
                id: document
                anchors.fill: parent
                anchors.topMargin: 50
                shell: pane.shell; theme: pane.theme
                marking: pane.tool === 3; reading: pane.tool === 4
                services: pane.services
                anchors.bottomMargin: pane.audioVisible ? 84 : 0
                onInputModeChanged: {
                    const map = {0: 0, 2: 2, 3: 1, 4: 5}
                    if (inputMode in map && !(pane.tool === 3 && inputMode === 0)) pane.tool = map[inputMode]
                }
                onWordRequested: function(text, context) { pane.services.lookup(text); lookup.draft = pane.services.lookupWord; lookup.context = context; lookup.open() }
            }
            Rectangle {
                anchors.top: parent.top; anchors.topMargin: 12; anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width - ((pane.tool === 0 || pane.tool === 1) ? 270 : 28), hint.implicitWidth + 32); height: 32; radius: 16; color: pane.theme.card; border.color: pane.theme.line
                Label { id: hint; anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; text: pane.hints[pane.tool]; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; color: pane.theme.muted; font.pixelSize: 12 }
            }
            DesignButton {
                objectName: "penSettingsButton"
                visible: pane.tool === 0 || pane.tool === 1
                anchors.right: parent.right; anchors.rightMargin: 12; anchors.top: parent.top; anchors.topMargin: 12
                theme: pane.theme; text: qsTr("画笔 · %1 pt").arg(document.inkWidth.toFixed(1)); selected: pane.penOptions
                onClicked: pane.penOptions = !pane.penOptions
                Accessible.name: qsTr("画笔颜色与粗细设置")
            }
            AudioStrip {
                visible: pane.audioVisible
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 16
                theme: pane.theme; services: pane.services
            }
            StudyTools {
                visible: pane.workspace.toolbar === 1; anchors.bottom: parent.bottom; anchors.bottomMargin: pane.audioVisible ? 100 : 20; anchors.horizontalCenter: parent.horizontalCenter
                theme: pane.theme; current: pane.tool; onPicked: function(index) { pane.pick(index) }
            }
            StudyTools {
                visible: pane.workspace.toolbar === 2; vertical: true; anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
                theme: pane.theme; current: pane.tool; onPicked: function(index) { pane.pick(index) }
            }
            Rectangle {
                objectName: "penSettingsPanel"
                readonly property bool blocksInkInput: true
                visible: pane.penOptions && (pane.tool === 0 || pane.tool === 1)
                anchors.top: parent.top; anchors.topMargin: 50; anchors.right: parent.right; anchors.rightMargin: 12
                width: Math.min(330, parent.width - 24); height: 134; radius: 12; color: pane.theme.card; border.color: pane.theme.line
                MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: function(wheel) { wheel.accepted = true } }
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 8
                    RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    Repeater {
                        model: ["#235fbd", "#ce3446", "#188450", "#242424", "#a855f7", "#e88c21"]
                        ToolButton {
                            id: colorButton
                            required property string modelData
                            implicitWidth: 28; implicitHeight: 28
                            objectName: "penColor" + index
                            required property int index
                            background: Rectangle { radius: 14; color: colorButton.modelData; border.width: Qt.colorEqual(document.inkColor, colorButton.modelData) ? 3 : 1; border.color: Qt.colorEqual(document.inkColor, colorButton.modelData) ? pane.theme.accent : pane.theme.line }
                            contentItem: Label { text: Qt.colorEqual(document.inkColor, colorButton.modelData) ? "✓" : ""; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            onClicked: document.inkColor = modelData
                            Accessible.name: qsTr("笔色 %1").arg(modelData)
                        }
                    }
                    ToolButton { objectName: "customPenColor"; text: qsTr("自选"); onClicked: { penColorDialog.selectedColor = document.inkColor; penColorDialog.open() } Accessible.name: qsTr("自选画笔颜色") }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("粗细"); color: pane.theme.muted }
                        Slider { objectName: "penWidthSlider"; Layout.fillWidth: true; from: 0.5; to: 16; stepSize: 0.5; value: document.inkWidth; onMoved: document.inkWidth = value; Accessible.name: qsTr("画笔粗细，单位 PDF 点") }
                        Label { text: qsTr("%1 pt").arg(document.inkWidth.toFixed(1)); color: pane.theme.text; Layout.preferredWidth: 48 }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 22; radius: 6; color: pane.theme.fill
                        Rectangle { anchors.centerIn: parent; width: parent.width - 32; height: document.inkWidth; radius: height / 2; color: document.inkColor }
                    }
                }
            }
            ColorDialog { id: penColorDialog; title: qsTr("选择画笔颜色"); onAccepted: document.inkColor = selectedColor }
        }
        Rectangle {
            visible: pane.workspace.layout === 2 && pane.width > 900
            Layout.preferredWidth: 240; Layout.fillHeight: true; color: pane.theme.sidebar
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 20; spacing: 14
                Label { text: qsTr("本页练习"); font.pixelSize: 15; font.bold: true; color: pane.theme.text }
                Label { text: qsTr("第 %1 页").arg(pane.shell.currentPage); color: pane.theme.muted }
                Label {
                    readonly property var stats: { const version = pane.shell.annotations.version; return pane.shell.annotations.pageStats(pane.shell.currentPage - 1) }
                    text: qsTr("文字答案 %1\n正确 %2 · 错误 %3\n笔迹 %4").arg(stats.answered).arg(stats.right).arg(stats.wrong).arg(stats.ink)
                    color: pane.theme.text; lineHeight: 1.8
                }
                Label { text: qsTr("输入或手写答案，再用批改工具标记。\n正确与错误标记随答案一同保存、导出。"); Layout.fillWidth: true; wrapMode: Text.WordWrap; color: pane.theme.muted; font.pixelSize: 12 }
                DesignButton { theme: pane.theme; text: qsTr("本页书签"); selected: true; onClicked: pane.workspace.toggleBookmark(pane.shell.currentPage) }
                DesignButton { theme: pane.theme; text: qsTr("生词本"); onClicked: pane.wordsRequested() }
                Item { Layout.fillHeight: true }
            }
        }
    }
    Rectangle {
        Layout.fillWidth: true; Layout.preferredHeight: 32; color: pane.theme.bar
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
            DesignButton { theme: pane.theme; text: "↶"; implicitHeight: 28; enabled: pane.shell.annotations.canUndo; focusPolicy: Qt.NoFocus; onClicked: { pane.shell.finishTextEditing(); pane.shell.annotations.undo() } Accessible.name: qsTr("撤销") }
            DesignButton { theme: pane.theme; text: "↷"; implicitHeight: 28; enabled: pane.shell.annotations.canRedo; focusPolicy: Qt.NoFocus; onClicked: pane.shell.annotations.redo(); Accessible.name: qsTr("重做") }
            DesignButton { theme: pane.theme; text: qsTr("复制"); implicitHeight: 28; enabled: pane.shell.textSelection.ready; onClicked: pane.shell.textSelection.copy() }
            DesignButton { theme: pane.theme; text: qsTr("查词 / 摘录"); implicitHeight: 28; enabled: pane.shell.textSelection.ready; onClicked: { lookup.draft = pane.shell.textSelection.text; pane.services.lookup(lookup.draft); lookup.context = pane.shell.textSelection.context; lookup.open() } }
            Label { objectName: "storageStatusLabel"; text: pane.shell.storageStatus; Layout.fillWidth: true; elide: Text.ElideRight; color: pane.theme.muted; font.pixelSize: 11; textFormat: Text.PlainText }
            Label { text: pane.shell.exportStatus; visible: text.length > 0 && pane.width > 1000; Layout.maximumWidth: 240; elide: Text.ElideRight; color: pane.theme.muted; font.pixelSize: 11; textFormat: Text.PlainText }
            DesignButton { objectName: "doublePageButton"; theme: pane.theme; text: qsTr("双页"); selected: pane.shell.doublePage; visible: pane.width > 750; implicitHeight: 28; onClicked: pane.shell.doublePage = !pane.shell.doublePage; Accessible.name: qsTr("横向双页布局") }
            DesignButton { theme: pane.theme; text: "−"; implicitHeight: 28; onClicked: pane.shell.setZoom(pane.shell.zoom / 1.2); Accessible.name: qsTr("缩小") }
            DesignButton { theme: pane.theme; text: Math.round(pane.shell.zoom * 100) + "%"; implicitHeight: 28; onClicked: pane.shell.setZoom(1); Accessible.name: qsTr("适应页宽") }
            DesignButton { theme: pane.theme; text: "+"; implicitHeight: 28; onClicked: pane.shell.setZoom(pane.shell.zoom * 1.2); Accessible.name: qsTr("放大") }
        }
    }
    }
    Menu {
        id: more
        MenuItem { text: qsTr("打开 PDF…"); onTriggered: pane.openRequested() }
        MenuItem { text: qsTr("保存"); enabled: pane.shell.storageReady; onTriggered: pane.shell.saveNow() }
        MenuItem { objectName: "exportButton"; text: qsTr("导出新 PDF…"); enabled: pane.shell.storageReady && !pane.shell.exporting; onTriggered: pane.exportRequested() }
        MenuSeparator {}
        MenuItem { text: qsTr("横向双页布局"); checkable: true; checked: pane.shell.doublePage; onTriggered: pane.shell.doublePage = !pane.shell.doublePage }
        MenuItem { text: qsTr("离线识别当前页…"); enabled: !pane.services.busy; onTriggered: ocr.open() }
        MenuItem { text: qsTr("停止朗读"); onTriggered: pane.services.stopSpeaking() }
        MenuItem { text: qsTr("外观与布局"); onTriggered: pane.settingsRequested() }
        MenuItem { text: qsTr("关闭文档"); onTriggered: pane.shell.clearSelection() }
    }
    Popup {
        id: ocr
        parent: Overlay.overlay; anchors.centerIn: parent; width: 420; padding: 22; modal: true
        background: Rectangle { radius: 16; color: pane.theme.card; border.color: pane.theme.line }
        contentItem: ColumnLayout {
            spacing: 14
            Label { text: qsTr("离线文字识别"); font.pixelSize: 20; font.bold: true; color: pane.theme.text }
            Label { text: qsTr("识别当前显示页。使用本机已安装的系统语言包，结果可点读、查词与摘录。"); wrapMode: Text.Wrap; Layout.fillWidth: true; color: pane.theme.muted }
            ComboBox { id: ocrLanguage; model: pane.services.languages; Layout.fillWidth: true; Accessible.name: qsTr("识别语言") }
            Label { text: pane.services.status; Layout.fillWidth: true; wrapMode: Text.Wrap; color: pane.theme.muted; textFormat: Text.PlainText }
            DesignButton { theme: pane.theme; text: pane.services.busy ? qsTr("正在识别…") : qsTr("识别当前页"); primary: true; enabled: !pane.services.busy && ocrLanguage.count > 0; onClicked: { pane.services.recognize(pane.shell.pageImage, String(pane.shell.selectedUrl), pane.shell.currentPage, ocrLanguage.currentText); ocr.close() } }
        }
    }
    Popup {
        id: lookup
        property string draft: ""
        property string context: ""
        parent: Overlay.overlay; anchors.centerIn: parent; width: 360; modal: false; padding: 22
        background: Rectangle { radius: 16; color: pane.theme.card; border.color: pane.theme.line }
        contentItem: ColumnLayout {
            spacing: 12
            Label { text: lookup.draft; Layout.fillWidth: true; wrapMode: Text.Wrap; color: pane.theme.text; font.pixelSize: 24; font.bold: true; textFormat: Text.PlainText }
            Label { text: pane.shell.selectedName + " · p." + pane.shell.currentPage; color: pane.theme.muted; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideMiddle; textFormat: Text.PlainText }
            Label { text: pane.services.phonetic; color: pane.theme.muted; visible: text.length > 0 }
            Label { text: pane.services.meaning; Layout.fillWidth: true; wrapMode: Text.Wrap; color: pane.theme.text; textFormat: Text.PlainText }
            RowLayout {
                DesignButton { theme: pane.theme; text: "▶ UK"; selected: true; onClicked: pane.services.speak(lookup.draft, "en-GB") }
                DesignButton { theme: pane.theme; text: "▶ US"; selected: true; onClicked: pane.services.speak(lookup.draft, "en-US") }
                DesignButton { theme: pane.theme; text: qsTr("朗读原文"); onClicked: pane.services.speak(lookup.context || lookup.draft) }
            }
            TextArea { id: meaning; textFormat: TextEdit.PlainText; Layout.fillWidth: true; placeholderText: qsTr("填写释义或学习笔记"); wrapMode: TextEdit.Wrap; color: pane.theme.text; background: Rectangle { color: pane.theme.fill; radius: 8 } }
            DesignButton { theme: pane.theme; text: qsTr("加入生词本"); primary: true; Layout.fillWidth: true; onClicked: { pane.workspace.addWord(lookup.draft, meaning.text.length ? meaning.text : (pane.services.lookupFound ? pane.services.meaning : ""), lookup.context, pane.shell.selectedName, pane.shell.currentPage); meaning.clear(); lookup.close() } }
        }
    }
}
