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
    signal optimizeRequested()
    property int tool: 0
    property bool penOptions: false
    property bool textOptions: false
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
    // Go to page: ⌘⌥G on macOS (Preview), Ctrl+G on Windows.
    Shortcut {
        sequence: Qt.platform.os === "osx" ? "Ctrl+Alt+G" : "Ctrl+G"
        enabled: pane.visible && pane.shell.pageCount > 0
        onActivated: { pageNumber.contentItem.forceActiveFocus(); pageNumber.contentItem.selectAll() }
    }
    ToolTip {
        id: feedback; parent: pane; timeout: 6000
        // A quiet rounded toast above the bottom capsules instead of the stock tooltip box.
        x: Math.round((pane.width - width) / 2); y: pane.height - height - 68
        padding: 10; leftPadding: 18; rightPadding: 18
        contentItem: Text { text: feedback.text; font.pixelSize: 13; color: pane.theme.text; wrapMode: Text.Wrap; textFormat: Text.PlainText }
        background: Rectangle {
            radius: Math.min(height / 2, 20); color: pane.theme.card; border.color: pane.theme.line
            Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: 3; anchors.bottomMargin: -3; radius: parent.radius; color: pane.theme.shadow }
        }
    }
    Connections { target: pane.services; function onChanged(): void { if (pane.services.status.length > 0) { feedback.text = pane.services.status; feedback.open() } } }
    Connections { target: pane.shell; function onOcrChanged(): void { if (!pane.shell.ocrBusy && pane.shell.ocrStatus.length > 0) { feedback.text = pane.shell.ocrStatus; feedback.open() } } }
    readonly property var hints: [qsTr("点空白处打字 · 按住拖动书写 · 双击后按住拖动选文"), qsTr("直接在页面书写 · Ctrl / Command + Z 撤销"), qsTr("点空白处输入答案 · Esc 完成编辑"), qsTr("点击文字答案：✓ → × → 清除 · 用打字工具补充订正"), qsTr("拖选文字以复制或朗读 · 扫描页自动离线识别"), qsTr("点击或划过笔迹擦除 · 文字答案可选中后删除")]
    function pick(index: int): void {
        penOptions = index === 1 && (tool !== 1 || !penOptions)
        textOptions = index === 2 && (tool !== 2 || !textOptions)
        tool = index
        document.switchMode([0, 3, 2, 0, 1, 4][index])
    }
    // One quiet ground; every control sits on a rounded island above it.
    Rectangle { anchors.fill: parent; color: pane.theme.canvas }
    component Island: Rectangle {
        radius: height / 2
        color: pane.theme.bar
        border.color: pane.theme.line
        Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: 2; anchors.bottomMargin: -2; radius: parent.radius; color: pane.theme.shadow }
    }
    component Divider: Rectangle { width: 1; height: 20; color: pane.theme.line }
    // Round on/off button of the typing settings (bold, italic, tick-box mark).
    component StyleToggle: ToolButton {
        id: toggle
        property bool on: false
        property bool boldFace: false
        property bool italicFace: false
        implicitWidth: 32; implicitHeight: 32; focusPolicy: Qt.NoFocus
        contentItem: Text { text: toggle.text; font.pixelSize: 15; font.bold: toggle.boldFace; font.italic: toggle.italicFace; color: toggle.on ? pane.theme.accentText : pane.theme.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: height / 2; color: toggle.on ? pane.theme.tint : toggle.hovered ? pane.theme.fill : "transparent" }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10
    Item {
        id: header
        readonly property bool stacked: pane.width < 1280 && pane.workspace.toolbar === 0
        Layout.fillWidth: true; Layout.preferredHeight: stacked ? 104 : 48
        Island {
            id: titleIsland
            readonly property real room: header.stacked || !topTools.visible ? header.width - actionsIsland.width - 10 : (header.width - topTools.width) / 2 - 12
            width: Math.max(48, Math.min(titleRow.implicitWidth + 22, room)); height: 48
            RowLayout {
                id: titleRow
                anchors.fill: parent; anchors.leftMargin: 7; anchors.rightMargin: 15; spacing: 10
                DesignButton {
                    id: backButton
                    theme: pane.theme; text: qsTr("资料库"); symbol: "previous"; iconOnly: true; Layout.preferredWidth: 34
                    onClicked: { pane.shell.finishTextEditing(); pane.libraryRequested() }
                    Accessible.name: qsTr("返回资料库")
                    background: Rectangle { radius: height / 2; color: backButton.hovered ? Qt.darker(pane.theme.fill, pane.theme.dark ? 0.85 : 1.05) : pane.theme.fill }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.maximumWidth: 320; spacing: 0
                    Label { text: pane.shell.selectedName; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 13; font.weight: Font.DemiBold; color: pane.theme.text; textFormat: Text.PlainText }
                    Label { text: qsTr("第 %1 页 · 本地自动保存").arg(pane.shell.currentPage); Layout.fillWidth: true; elide: Text.ElideRight; color: pane.theme.muted; font.pixelSize: 11 }
                }
            }
        }
        Island {
            id: actionsIsland
            anchors.right: parent.right
            width: actionsRow.implicitWidth + 12; height: 48
            Row {
                id: actionsRow
                anchors.centerIn: parent; spacing: 2
                Row {
                    id: navigation; objectName: "navigationTools"; spacing: 2
                    anchors.verticalCenter: parent.verticalCenter
                    DesignButton { objectName: "previousPageButton"; theme: pane.theme; text: "‹"; anchors.verticalCenter: parent.verticalCenter; enabled: pane.shell.doublePage ? pane.spreadFirst > 1 : pane.shell.currentPage > 1; onClicked: pane.shell.goToPage(pane.shell.doublePage ? pane.spreadFirst - 2 : pane.shell.currentPage - 1); Accessible.name: pane.shell.doublePage ? qsTr("上一组") : qsTr("上一页") }
                    SpinBox {
                        id: pageNumber
                        objectName: "pageNumber"; width: 52; height: 30
                        anchors.verticalCenter: parent.verticalCenter
                        from: 1; to: pane.shell.pageCount; value: pane.shell.currentPage; editable: true
                        up.indicator: Item {}
                        down.indicator: Item {}
                        font.pixelSize: 13; font.weight: Font.DemiBold
                        onValueModified: pane.shell.goToPage(value)
                        Accessible.name: qsTr("页码"); background: Rectangle { radius: height / 2; color: pane.theme.fill }
                    }
                    Label { text: " / " + pane.shell.pageCount; height: 34; verticalAlignment: Text.AlignVCenter; color: pane.theme.muted; font.pixelSize: 12 }
                    DesignButton { objectName: "nextPageButton"; theme: pane.theme; text: "›"; anchors.verticalCenter: parent.verticalCenter; enabled: pane.shell.doublePage ? pane.spreadFirst + 2 <= pane.shell.pageCount : pane.shell.currentPage < pane.shell.pageCount; onClicked: pane.shell.goToPage(pane.shell.doublePage ? pane.spreadFirst + 2 : pane.shell.currentPage + 1); Accessible.name: pane.shell.doublePage ? qsTr("下一组") : qsTr("下一页") }
                }
                Divider { anchors.verticalCenter: parent.verticalCenter }
                DesignButton { theme: pane.theme; text: "⚑"; anchors.verticalCenter: parent.verticalCenter; selected: { const pages = pane.workspace.bookmarks; return pages.includes(pane.shell.currentPage) } onClicked: pane.workspace.toggleBookmark(pane.shell.currentPage); Accessible.name: qsTr("切换书签") }
                DesignButton { theme: pane.theme; text: qsTr("生词本 %1").arg(pane.workspace.words.length); anchors.verticalCenter: parent.verticalCenter; visible: pane.width > 700; onClicked: pane.wordsRequested() }
                DesignButton { theme: pane.theme; text: "♪"; anchors.verticalCenter: parent.verticalCenter; selected: pane.audioVisible; onClicked: pane.audioVisible = !pane.audioVisible; Accessible.name: qsTr("本地音频") }
                DesignButton { objectName: "askAiButton"; theme: pane.theme; text: qsTr("问 AI"); anchors.verticalCenter: parent.verticalCenter; selected: document.aiCapturing || aiPopup.visible; onClicked: document.startAiCapture(); Accessible.name: qsTr("截图问 AI") }
                DesignButton { id: moreButton; objectName: "documentMenuButton"; theme: pane.theme; text: "···"; anchors.verticalCenter: parent.verticalCenter; selected: more.visible; onClicked: more.visible ? more.close() : more.open(); Accessible.name: qsTr("文档菜单") }
            }
        }
        StudyTools {
            id: topTools; objectName: "editingTools"
            anchors.horizontalCenter: parent.horizontalCenter
            y: header.stacked ? 56 : 0
            visible: pane.workspace.toolbar === 0
            theme: pane.theme; current: pane.tool; onPicked: function(index) { pane.pick(index) }
        }
    }
    RowLayout {
        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 10
        PageSidebar { Layout.preferredWidth: 184; Layout.fillHeight: true; visible: pane.workspace.layout === 0 && pane.width > 800; theme: pane.theme; shell: pane.shell; workspace: pane.workspace }
        Item {
            id: stage
            Layout.fillWidth: true; Layout.fillHeight: true
            DocumentView {
                id: document
                anchors.fill: parent
                anchors.topMargin: 44
                shell: pane.shell; theme: pane.theme
                nightDim: pane.workspace.nightDim
                marking: pane.tool === 3; reading: pane.tool === 4
                services: pane.services
                ai: aiHandoff
                onAiCaptured: function(ok) { if (ok) { aiHandoff.refreshTargets(); aiPopup.open() } }
                // The last pen width the user picked stays for new strokes and next launches.
                property bool penRestored: false
                Component.onCompleted: { penWidth = pane.workspace.inkWidth; penRestored = true }
                onInkWidthChanged: if (penRestored) pane.workspace.inkWidth = inkWidth
                onInputModeChanged: {
                    const map = {0: 0, 2: 2, 3: 1, 4: 5}
                    if (inputMode in map && !(pane.tool === 3 && inputMode === 0)) pane.tool = map[inputMode]
                }
                onWordRequested: function(text, context) { lookup.show(text, context, lookupPoint) }
            }
            Rectangle {
                anchors.top: parent.top; anchors.topMargin: 4; anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width - ((pane.tool <= 2) ? 270 : 28), hint.implicitWidth + 36); height: 32; radius: 16
                color: pane.theme.bar; border.color: pane.theme.line
                Label { id: hint; anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 18; text: document.aiCapturing ? qsTr("拖动框选要问 AI 的区域 · Esc 取消") : pane.hints[pane.tool]; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter; color: pane.theme.muted; font.pixelSize: 12 }
            }
            AbstractButton {
                id: penButton
                objectName: "penSettingsButton"
                visible: pane.tool === 0 || pane.tool === 1
                anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 2
                implicitHeight: 36; implicitWidth: penRow.implicitWidth + 26
                hoverEnabled: true
                contentItem: Item {
                    Row {
                        id: penRow
                        anchors.centerIn: parent; spacing: 8
                        Rectangle { width: 14; height: 14; radius: 7; color: document.inkColor; border.color: pane.theme.line; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: qsTr("画笔 · %1 pt").arg(document.inkWidth.toFixed(1)); font.pixelSize: 12; color: pane.penOptions ? pane.theme.accentText : pane.theme.text; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
                background: Rectangle { radius: height / 2; color: pane.penOptions ? pane.theme.tint : penButton.hovered ? pane.theme.fill : pane.theme.bar; border.color: pane.penOptions ? "transparent" : pane.theme.line }
                onClicked: pane.penOptions = !pane.penOptions
                Accessible.name: qsTr("画笔颜色与粗细设置")
            }
            AbstractButton {
                id: textButton
                objectName: "textSettingsButton"
                visible: pane.tool === 2
                anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 2
                implicitHeight: 36; implicitWidth: textRow.implicitWidth + 26
                hoverEnabled: true
                contentItem: Item {
                    Row {
                        id: textRow
                        anchors.centerIn: parent; spacing: 8
                        Text {
                            text: "A"; anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: 15; font.bold: pane.shell.annotations.defaultBold; font.italic: pane.shell.annotations.defaultItalic
                            color: pane.shell.annotations.defaultTextColor
                        }
                        Text { text: qsTr("文字 · %1 px").arg(pane.shell.annotations.defaultFontSize); font.pixelSize: 12; color: pane.textOptions ? pane.theme.accentText : pane.theme.text; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
                background: Rectangle { radius: height / 2; color: pane.textOptions ? pane.theme.tint : textButton.hovered ? pane.theme.fill : pane.theme.bar; border.color: pane.textOptions ? "transparent" : pane.theme.line }
                onClicked: pane.textOptions = !pane.textOptions
                Accessible.name: qsTr("文字颜色、字号与样式设置")
            }
            AudioStrip {
                visible: pane.audioVisible
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 12; anchors.bottomMargin: 60
                theme: pane.theme; services: pane.services
            }
            // Bottom controls float in the corners instead of a full-width status bar.
            Island {
                id: historyIsland
                objectName: "readerStatusIsland"
                anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 12
                height: 40; width: historyRow.implicitWidth + 16
                Row {
                    id: historyRow
                    anchors.verticalCenter: parent.verticalCenter; x: 6; spacing: 2
                    DesignButton { theme: pane.theme; text: "↶"; anchors.verticalCenter: parent.verticalCenter; implicitHeight: 30; enabled: pane.shell.annotations.canUndo; focusPolicy: Qt.NoFocus; onClicked: { pane.shell.finishTextEditing(); pane.shell.annotations.undo() } Accessible.name: qsTr("撤销") }
                    DesignButton { theme: pane.theme; text: "↷"; anchors.verticalCenter: parent.verticalCenter; implicitHeight: 30; enabled: pane.shell.annotations.canRedo; focusPolicy: Qt.NoFocus; onClicked: pane.shell.annotations.redo(); Accessible.name: qsTr("重做") }
                    Label {
                        objectName: "storageStatusLabel"
                        text: pane.shell.textSelection.status.length > 0 ? pane.shell.textSelection.status : pane.shell.ocrBusy ? pane.shell.ocrStatus : pane.shell.storageStatus
                        width: Math.min(implicitWidth, Math.max(80, stage.width * 0.3)); leftPadding: 6; rightPadding: 4
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight; color: pane.theme.muted; font.pixelSize: 11; textFormat: Text.PlainText
                    }
                    Label { text: pane.shell.exportStatus; visible: text.length > 0 && pane.width > 1000; width: Math.min(implicitWidth, 240); anchors.verticalCenter: parent.verticalCenter; rightPadding: 4; elide: Text.ElideRight; color: pane.theme.muted; font.pixelSize: 11; textFormat: Text.PlainText }
                }
            }
            Island {
                id: zoomIsland
                anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 12
                height: 40; width: zoomRow.implicitWidth + 12
                Row {
                    id: zoomRow
                    anchors.centerIn: parent; spacing: 2
                    DesignButton { objectName: "doublePageButton"; theme: pane.theme; text: qsTr("双页"); anchors.verticalCenter: parent.verticalCenter; selected: pane.shell.doublePage; visible: pane.width > 750; implicitHeight: 30; onClicked: pane.shell.doublePage = !pane.shell.doublePage; Accessible.name: qsTr("横向双页布局") }
                    DesignButton { theme: pane.theme; text: "−"; anchors.verticalCenter: parent.verticalCenter; implicitHeight: 30; onClicked: pane.shell.setZoom(pane.shell.zoom / 1.2); Accessible.name: qsTr("缩小") }
                    DesignButton { theme: pane.theme; text: Math.round(pane.shell.zoom * 100) + "%"; anchors.verticalCenter: parent.verticalCenter; implicitHeight: 30; font.weight: Font.DemiBold; onClicked: pane.shell.setZoom(1); Accessible.name: qsTr("适应页宽") }
                    DesignButton { theme: pane.theme; text: "+"; anchors.verticalCenter: parent.verticalCenter; implicitHeight: 30; onClicked: pane.shell.setZoom(pane.shell.zoom * 1.2); Accessible.name: qsTr("放大") }
                }
            }
            StudyTools {
                // Sits between the corner islands when there is room, otherwise just above them.
                readonly property bool fits: stage.width - historyIsland.width - zoomIsland.width - 48 >= width
                visible: pane.workspace.toolbar === 1; anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom; anchors.bottomMargin: (pane.audioVisible ? 138 : fits ? 8 : 60)
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
                anchors.top: parent.top; anchors.topMargin: 46; anchors.right: parent.right
                width: Math.min(330, parent.width - 24); height: 140; radius: 22; color: pane.theme.card; border.color: pane.theme.line
                Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: -4; radius: parent.radius; color: pane.theme.shadow }
                MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: function(wheel) { wheel.accepted = true } }
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
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
                            readonly property bool chosen: Qt.colorEqual(document.inkColor, colorButton.modelData)
                            background: Item {
                                Rectangle { anchors.fill: parent; radius: width / 2; color: "transparent"; border.width: colorButton.chosen ? 2 : 0; border.color: colorButton.modelData }
                                Rectangle { anchors.centerIn: parent; width: colorButton.chosen ? 18 : 22; height: width; radius: width / 2; color: colorButton.modelData }
                            }
                            onClicked: document.inkColor = modelData
                            Accessible.name: qsTr("笔色 %1").arg(modelData)
                        }
                    }
                    Item { Layout.fillWidth: true }
                    DesignButton { objectName: "customPenColor"; theme: pane.theme; text: qsTr("自选"); implicitHeight: 28; onClicked: { penColorDialog.selectedColor = document.inkColor; penColorDialog.open() } Accessible.name: qsTr("自选画笔颜色") }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("粗细"); color: pane.theme.muted }
                        Slider { objectName: "penWidthSlider"; Layout.fillWidth: true; from: 0.5; to: 16; stepSize: 0.5; value: document.inkWidth; onMoved: document.penWidth = value; Accessible.name: qsTr("画笔粗细，单位 PDF 点") }
                        Label { text: qsTr("%1 pt").arg(document.inkWidth.toFixed(1)); color: pane.theme.text; Layout.preferredWidth: 48 }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 24; radius: height / 2; color: pane.theme.fill
                        Rectangle { anchors.centerIn: parent; width: parent.width - 32; height: document.inkWidth; radius: height / 2; color: document.inkColor }
                    }
                }
            }
            ColorDialog { id: penColorDialog; title: qsTr("选择画笔颜色"); onAccepted: document.inkColor = selectedColor }
            Rectangle {
                id: textPanel
                objectName: "textSettingsPanel"
                readonly property bool blocksInkInput: true
                readonly property AnnotationModel notes: pane.shell.annotations
                visible: pane.textOptions && pane.tool === 2
                anchors.top: parent.top; anchors.topMargin: 46; anchors.right: parent.right
                width: Math.min(330, parent.width - 24); height: textColumn.implicitHeight + 28; radius: 22; color: pane.theme.card; border.color: pane.theme.line
                Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: -4; radius: parent.radius; color: pane.theme.shadow }
                MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: function(wheel) { wheel.accepted = true } }
                ColumnLayout {
                    id: textColumn
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14; spacing: 10
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        Repeater {
                            model: ["#1e468c", "#1d1d1f", "#e5484d", "#e69819", "#30a46c", "#8e4ec6"]
                            ToolButton {
                                id: textSwatch
                                required property string modelData
                                required property int index
                                objectName: "textColor" + index
                                implicitWidth: 28; implicitHeight: 28; focusPolicy: Qt.NoFocus
                                readonly property bool chosen: Qt.colorEqual(textPanel.notes.defaultTextColor, textSwatch.modelData)
                                background: Item {
                                    Rectangle { anchors.fill: parent; radius: width / 2; color: "transparent"; border.width: textSwatch.chosen ? 2 : 0; border.color: textSwatch.modelData }
                                    Rectangle { anchors.centerIn: parent; width: textSwatch.chosen ? 18 : 22; height: width; radius: width / 2; color: textSwatch.modelData }
                                }
                                onClicked: textPanel.notes.defaultTextColor = modelData
                                Accessible.name: qsTr("文字颜色 %1").arg(modelData)
                            }
                        }
                        Item { Layout.fillWidth: true }
                        DesignButton { objectName: "customTextColor"; theme: pane.theme; text: qsTr("自选"); implicitHeight: 28; onClicked: { textColorDialog.selectedColor = textPanel.notes.defaultTextColor; textColorDialog.open() } Accessible.name: qsTr("自选文字颜色") }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("字号"); color: pane.theme.muted }
                        Slider { objectName: "textSizeSlider"; Layout.fillWidth: true; from: 8; to: 36; stepSize: 1; value: textPanel.notes.defaultFontSize; onMoved: textPanel.notes.defaultFontSize = value; Accessible.name: qsTr("新文字答案的字号") }
                        Label { text: qsTr("%1 px").arg(textPanel.notes.defaultFontSize); color: pane.theme.text; Layout.preferredWidth: 42 }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 4
                        Label { text: qsTr("样式"); color: pane.theme.muted; Layout.rightMargin: 6 }
                        StyleToggle { objectName: "textDefaultBold"; text: "B"; boldFace: true; on: textPanel.notes.defaultBold; onClicked: textPanel.notes.defaultBold = !textPanel.notes.defaultBold; Accessible.name: qsTr("新答案加粗") }
                        StyleToggle { objectName: "textDefaultItalic"; text: "I"; italicFace: true; on: textPanel.notes.defaultItalic; onClicked: textPanel.notes.defaultItalic = !textPanel.notes.defaultItalic; Accessible.name: qsTr("新答案倾斜") }
                        Item { Layout.fillWidth: true }
                        // The bundled answer fonts, embedded on export so the PDF matches the page.
                        Label { text: qsTr("字体 基于 Andika · 霞鹜文楷"); color: pane.theme.muted; font.pixelSize: 11 }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: Math.max(34, preview.implicitHeight + 12); radius: 12; color: pane.theme.fill
                        Text {
                            id: preview
                            anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: 14; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Réponse · 答案"); elide: Text.ElideRight
                            font: {
                                const f = textPanel.notes.answerFont("Langlern Sans + Kai", textPanel.notes.defaultFontSize)
                                f.bold = textPanel.notes.defaultBold
                                f.italic = textPanel.notes.defaultItalic
                                return f
                            }
                            color: textPanel.notes.defaultTextColor
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: pane.theme.line }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 4
                        Label { text: qsTr("选择框"); color: pane.theme.muted; Layout.rightMargin: 6 }
                        Repeater {
                            model: [qsTr("按语言"), "✓", "✗"]
                            StyleToggle {
                                required property string modelData
                                required property int index
                                objectName: "checkMarkStyle" + index
                                implicitWidth: index === 0 ? 64 : 36
                                text: modelData; on: pane.shell.checkMarkStyle === index
                                onClicked: pane.shell.checkMarkStyle = index
                                Accessible.name: [qsTr("选择框标记跟随页面语言"), qsTr("选择框标记为对号"), qsTr("选择框标记为叉号")][index]
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: pane.theme.muted; font.pixelSize: 11
                        text: {
                            const page = pane.shell.currentPage - 1
                            const style = pane.shell.checkMarkStyle
                            // Re-evaluate when OCR language or page text changes.
                            const cross = pane.shell.ocrLanguage.length >= 0 && pane.shell.textSelection.version >= 0 && pane.shell.crossMarks(page)
                            return style === 0 ? qsTr("点击页面上的方框即可标记；法语、德语等用 ✗，英语、中文用 ✓。本页：%1").arg(cross ? "✗" : "✓")
                                               : qsTr("点击页面上的方框即可标记，再点一次取消。")
                        }
                    }
                }
            }
            ColorDialog { id: textColorDialog; title: qsTr("选择文字颜色"); onAccepted: textPanel.notes.defaultTextColor = selectedColor }
        }
        Rectangle {
            visible: pane.workspace.layout === 2 && pane.width > 900
            Layout.preferredWidth: 240; Layout.fillHeight: true; color: pane.theme.sidebar; radius: 22
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 22; spacing: 14
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
    }
    component DocumentMenuItem: MenuItem {
        id: entry
        property string symbol: ""
        property string keyHint: ""
        readonly property color ink: !enabled ? pane.theme.faint : highlighted ? pane.theme.accentText : pane.theme.text
        implicitHeight: 36
        implicitWidth: 252
        leftPadding: 12; rightPadding: 12
        font.pixelSize: 13
        hoverEnabled: true
        indicator: null
        arrow: null
        background: Rectangle { radius: height / 2; color: entry.highlighted && entry.enabled ? pane.theme.tint : "transparent" }
        contentItem: RowLayout {
            spacing: 10
            LineIcon { symbol: entry.checkable && entry.checked ? "mark" : entry.symbol; ink: entry.ink; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
            Text { text: entry.text; font: entry.font; color: entry.ink; Layout.fillWidth: true; elide: Text.ElideRight }
            Text { text: entry.keyHint; font.pixelSize: 12; color: pane.theme.muted; visible: text.length > 0 }
        }
    }
    component DocumentMenuSeparator: MenuSeparator {
        topPadding: 5; bottomPadding: 5; leftPadding: 10; rightPadding: 10
        contentItem: Rectangle { implicitHeight: 1; radius: 1; color: pane.theme.line }
    }
    Menu {
        id: more
        objectName: "documentMenu"
        parent: moreButton
        popupType: Popup.Item
        x: moreButton.width - width
        y: moreButton.height + 8
        width: 264
        padding: 8
        margins: 8
        overlap: 0
        dim: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Item {
            Rectangle { anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: -4; radius: 20; color: pane.theme.shadow }
            Rectangle { anchors.fill: parent; anchors.topMargin: 2; anchors.bottomMargin: -2; radius: 20; color: pane.theme.shadow }
            Rectangle { anchors.fill: parent; radius: 20; color: pane.theme.card; border.color: pane.theme.line }
        }
        DocumentMenuItem { symbol: "open"; text: qsTr("打开 PDF…"); keyHint: Qt.platform.os === "osx" ? "⌘O" : "Ctrl+O"; onTriggered: pane.openRequested() }
        DocumentMenuItem { symbol: "save"; text: qsTr("保存"); keyHint: Qt.platform.os === "osx" ? "⌘S" : "Ctrl+S"; enabled: pane.shell.storageReady; onTriggered: pane.shell.saveNow() }
        DocumentMenuItem { symbol: "export"; objectName: "exportButton"; text: qsTr("导出新 PDF…"); enabled: pane.shell.storageReady && !pane.shell.exporting; onTriggered: pane.exportRequested() }
        DocumentMenuSeparator {}
        DocumentMenuItem { symbol: "auto"; objectName: "optimizeScanAction"; text: qsTr("扫描优化…"); enabled: pane.shell.storageReady && !pane.shell.scanning && !pane.shell.exporting; onTriggered: pane.optimizeRequested() }
        DocumentMenuItem { symbol: "read"; text: qsTr("OCR 与朗读设置…"); onTriggered: ocr.open() }
        DocumentMenuItem { symbol: "audio"; text: qsTr("停止朗读"); onTriggered: pane.services.stopSpeaking() }
        DocumentMenuSeparator {}
        DocumentMenuItem { symbol: "library"; objectName: "menuDoublePage"; text: qsTr("横向双页布局"); checkable: true; checked: pane.shell.doublePage; onTriggered: pane.shell.doublePage = !pane.shell.doublePage }
        DocumentMenuItem { symbol: "theme"; text: qsTr("外观与布局"); onTriggered: pane.settingsRequested() }
        DocumentMenuSeparator {}
        DocumentMenuItem { symbol: "close"; text: qsTr("关闭文档"); keyHint: Qt.platform.os === "osx" ? "⌘W" : "Ctrl+W"; onTriggered: pane.shell.clearSelection() }
    }
    Popup {
        id: ocr
        objectName: "ocrSettingsPopup"
        parent: Overlay.overlay; anchors.centerIn: parent; width: Math.min(420, parent ? parent.width - 32 : 420); padding: 22; modal: true
        height: Math.min(ocrContent.implicitHeight + 44, parent ? parent.height - 32 : 640)
        background: Rectangle { readonly property bool blocksInkInput: true; radius: 24; color: pane.theme.card; border.color: pane.theme.line }
        contentItem: ScrollView {
            id: ocrScroll
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
            id: ocrContent
            width: ocrScroll.availableWidth
            spacing: 14
            Label { text: qsTr("离线文字识别"); font.pixelSize: 20; font.bold: true; color: pane.theme.text }
            Label { text: qsTr("免费离线识别。扫描页按需处理；混合页面可以手动重新识别当前页。"); wrapMode: Text.Wrap; Layout.fillWidth: true; color: pane.theme.muted }
            DesignCombo {
                id: ocrLanguage; theme: pane.theme
                objectName: "ocrLanguage"
                model: [qsTr("法语"), qsTr("英语"), qsTr("简体中文"), qsTr("法语与英语")]
                readonly property var codes: ["fra", "eng", "chi_sim", "fra+eng"]
                currentIndex: codes.indexOf(pane.shell.ocrLanguage)
                onActivated: pane.shell.ocrLanguage = codes[currentIndex]
                Layout.fillWidth: true; Accessible.name: qsTr("识别语言")
            }
            Label { text: pane.shell.ocrStatus; Layout.fillWidth: true; wrapMode: Text.Wrap; color: pane.theme.muted; textFormat: Text.PlainText }
            DesignButton { theme: pane.theme; text: qsTr("重新识别当前页"); primary: true; onClicked: { pane.shell.recognizeCurrentPage(); pane.pick(4); ocr.close() } }
            DesignButton { theme: pane.theme; text: qsTr("框选区域识别"); onClicked: { document.startRegionRecognition(); pane.tool = 4; ocr.close() } }
            Label { text: qsTr("朗读语言与声音"); color: pane.theme.text }
            DesignCombo {
                theme: pane.theme
                model: [qsTr("法语"), qsTr("英语（美国）"), qsTr("英语（英国）"), qsTr("中文")]
                readonly property var codes: ["fr-FR", "en-US", "en-GB", "zh-CN"]
                currentIndex: codes.indexOf(pane.services.speechLocale)
                onActivated: pane.services.speechLocale = codes[currentIndex]
                Layout.fillWidth: true; Accessible.name: qsTr("朗读语言")
            }
            DesignCombo {
                objectName: "speechVoice"; theme: pane.theme
                model: pane.services.speechVoices.filter(voice => voice.locale === pane.services.speechLocale)
                // Without an explicit pick, French uses the saved male/female preference.
                readonly property string effectiveVoice: pane.services.speechVoice !== "" ? pane.services.speechVoice
                    : pane.services.speechLocale === "fr-FR" ? "natural:fr:" + pane.services.frenchVoice : ""
                currentIndex: Math.max(0, model.findIndex(voice => voice.id === effectiveVoice))
                textRole: "label"; valueRole: "id"
                onActivated: pane.services.speechVoice = currentValue
                displayText: count > 0 ? currentText : qsTr("此语言暂无可用的离线声音")
                enabled: count > 0; Layout.fillWidth: true; Accessible.name: qsTr("离线声音")
            }
            Label { text: qsTr("语速 %1×").arg(pane.services.speechRate.toFixed(1)); color: pane.theme.muted }
            Slider { from: 0.5; to: 2; stepSize: 0.1; value: pane.services.speechRate; onMoved: pane.services.speechRate = value; Layout.fillWidth: true; Accessible.name: qsTr("朗读语速") }
            }
        }
    }
    Rectangle {
        visible: pane.services.speaking
        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 72
        width: speechControls.implicitWidth + 28; height: 44; radius: height / 2
        color: pane.theme.card; border.color: pane.theme.line
        Row {
            id: speechControls
            anchors.centerIn: parent; spacing: 6
            Label { text: pane.services.speechLoading ? qsTr("正在准备声音…") : qsTr("选文朗读"); height: 32; leftPadding: 4; verticalAlignment: Text.AlignVCenter; color: pane.theme.text }
            DesignButton { theme: pane.theme; text: pane.services.speechPaused ? qsTr("继续") : qsTr("暂停"); selected: true; implicitHeight: 32; focusPolicy: Qt.NoFocus; onClicked: pane.services.toggleSpeechPause() }
            DesignButton { theme: pane.theme; text: qsTr("停止"); implicitHeight: 32; focusPolicy: Qt.NoFocus; onClicked: pane.services.stopSpeaking() }
        }
    }
    // Ask AI: the captured area goes to Claude or ChatGPT with a learning prompt; never sent from here.
    AiHandoff {
        id: aiHandoff
        onFinished: aiToast.show()
    }
    Popup {
        id: aiPopup
        objectName: "askAiPopup"
        readonly property var target: aiHandoff.targets.find(entry => entry.id === pane.workspace.aiTarget) || aiHandoff.targets[0]
        readonly property string targetName: target ? target.name + (target.desktop ? qsTr(" 桌面版") : qsTr(" 网页版")) : ""
        // Prompt language: the interface language by default, else the system language or a pick.
        readonly property string promptLanguage: aiHandoff.promptLanguage(pane.workspace.aiPromptLanguage, pane.workspace.english)
        function languageName(code: string): string { const entry = aiHandoff.promptLanguages.find(item => item.code === code); return entry ? entry.name : "" }
        function fillPrompt(): void { aiPrompt.text = aiHandoff.prompt(promptLanguage, pane.shell.documentLanguage, pane.shell.selectedName, pane.shell.currentPage) }
        onPromptLanguageChanged: if (visible) fillPrompt()
        parent: Overlay.overlay; anchors.centerIn: parent; width: Math.min(520, parent ? parent.width - 32 : 520); padding: 22; modal: true
        height: Math.min(aiContent.implicitHeight + 44, parent ? parent.height - 32 : 720)
        onAboutToShow: fillPrompt()
        onClosed: aiHandoff.clearCapture()
        background: Rectangle { readonly property bool blocksInkInput: true; radius: 24; color: pane.theme.card; border.color: pane.theme.line }
        contentItem: ScrollView {
            id: aiScroll
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                id: aiContent
                width: aiScroll.availableWidth
                spacing: 12
                Label { text: qsTr("问 AI"); font.pixelSize: 20; font.bold: true; color: pane.theme.text }
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: Math.min(220, Math.max(80, aiPreview.implicitHeight * width / Math.max(1, aiPreview.implicitWidth)))
                    radius: 12; color: pane.theme.fill; border.color: pane.theme.line; clip: true
                    Image { id: aiPreview; objectName: "askAiPreview"; anchors.fill: parent; anchors.margins: 6; source: aiHandoff.preview; fillMode: Image.PreserveAspectFit; smooth: true; asynchronous: false }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    Label { text: qsTr("提示词语言"); color: pane.theme.text; font.pixelSize: 13 }
                    DesignCombo {
                        objectName: "askAiLanguage"; theme: pane.theme
                        readonly property string systemName: aiPopup.languageName(aiHandoff.systemLanguage)
                        model: [{choice: "app", label: qsTr("跟随应用（%1）").arg(aiPopup.languageName(pane.workspace.english ? "en" : "zh"))},
                            {choice: "system", label: systemName.length > 0 ? qsTr("跟随系统（%1）").arg(systemName) : qsTr("跟随系统（不支持，使用应用语言）")}]
                            .concat(aiHandoff.promptLanguages.map(item => ({choice: item.code, label: item.name})))
                        textRole: "label"; valueRole: "choice"
                        currentIndex: Math.max(0, model.findIndex(item => item.choice === pane.workspace.aiPromptLanguage))
                        onActivated: pane.workspace.aiPromptLanguage = currentValue
                        Layout.fillWidth: true; Accessible.name: qsTr("提示词语言")
                    }
                }
                ScrollView {
                    Layout.fillWidth: true; Layout.preferredHeight: 190
                    TextArea {
                        id: aiPrompt
                        objectName: "askAiPrompt"
                        wrapMode: TextEdit.Wrap; font.pixelSize: 13; color: pane.theme.text; selectByMouse: true
                        leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 10
                        background: Rectangle { radius: 12; color: pane.theme.fill; border.width: aiPrompt.activeFocus ? 2 : 0; border.color: pane.theme.accent }
                        Accessible.name: qsTr("提示词")
                    }
                }
                Flow {
                    Layout.fillWidth: true; spacing: 6
                    Repeater {
                        model: aiHandoff.targets
                        DesignButton {
                            required property var modelData
                            theme: pane.theme
                            text: modelData.name + (modelData.desktop ? qsTr(" · 桌面版") : qsTr(" · 网页版"))
                            selected: aiPopup.target && aiPopup.target.id === modelData.id
                            onClicked: pane.workspace.aiTarget = modelData.id
                        }
                    }
                }
                Label {
                    text: qsTr("提示词要求 AI 翻译、讲解、列出语法点、易错点、易混淆点、使用场景与频率，并逐题批改你的答案；可直接修改。打开后截图和提示词会放进输入框，不会自动发送，请确认后自己发送。")
                    wrapMode: Text.Wrap; Layout.fillWidth: true; color: pane.theme.muted; font.pixelSize: 12
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    visible: aiHandoff.permissionNeeded && !aiHandoff.pasteAllowed
                    Label {
                        text: qsTr("自动粘贴截图需要在系统设置中允许 LinguaPDF 使用“辅助功能”；未允许时请手动粘贴。")
                        wrapMode: Text.Wrap; Layout.fillWidth: true; color: pane.theme.muted; font.pixelSize: 12
                    }
                    DesignButton { theme: pane.theme; text: qsTr("打开设置"); implicitHeight: 30; onClicked: aiHandoff.openPermissionSettings() }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    Item { Layout.fillWidth: true }
                    DesignButton { theme: pane.theme; text: qsTr("取消"); onClicked: aiPopup.close() }
                    DesignButton {
                        objectName: "askAiSend"; theme: pane.theme; primary: true
                        text: qsTr("打开 %1").arg(aiPopup.targetName)
                        enabled: aiHandoff.hasCapture && aiPopup.target !== undefined
                        onClicked: { if (aiHandoff.send(aiPopup.target.id, aiPrompt.text)) aiPopup.close(); else aiToast.show() }
                    }
                }
            }
        }
    }
    Rectangle {
        id: aiToast
        objectName: "askAiStatus"
        property bool shown: false
        function show(): void { shown = true; aiToastTimer.restart() }
        visible: (shown || aiHandoff.busy) && aiHandoff.status.length > 0
        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 72
        width: Math.min(parent.width - 48, aiToastRow.implicitWidth + 28); height: aiToastRow.implicitHeight + 20; radius: 22
        color: pane.theme.card; border.color: pane.theme.line
        RowLayout {
            id: aiToastRow
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 8; spacing: 6
            Label { text: aiHandoff.status; Layout.fillWidth: true; Layout.maximumWidth: 560; wrapMode: Text.Wrap; color: pane.theme.text; font.pixelSize: 12; textFormat: Text.PlainText }
            DesignButton { theme: pane.theme; text: qsTr("打开设置"); implicitHeight: 30; focusPolicy: Qt.NoFocus; visible: aiHandoff.permissionNeeded && !aiHandoff.pasteAllowed && !aiHandoff.busy; onClicked: aiHandoff.openPermissionSettings() }
            DesignButton { theme: pane.theme; text: qsTr("关闭"); implicitHeight: 30; focusPolicy: Qt.NoFocus; onClicked: aiToast.shown = false }
        }
        Timer { id: aiToastTimer; interval: 12000; onTriggered: aiToast.shown = false }
    }
    LookupCard {
        id: lookup
        theme: pane.theme
        services: pane.services
        onAddRequested: function(word, meaning, context) { pane.workspace.addWord(word, meaning, context, pane.shell.selectedName, pane.shell.currentPage) }
    }
    // The lookup language is a saved preference; the service only holds the live value.
    // Likewise the male/female French voice; write back only after the saved value has been applied.
    property bool voicePreferenceApplied: false
    Component.onCompleted: { services.lookupLanguage = workspace.lookupLanguage; services.frenchVoice = workspace.frenchVoice; voicePreferenceApplied = true }
    Connections { target: pane.services; function onLookupLanguageChanged(): void { pane.workspace.lookupLanguage = pane.services.lookupLanguage } }
    Connections { target: pane.services; enabled: pane.voicePreferenceApplied; function onSpeechChanged(): void { pane.workspace.frenchVoice = pane.services.frenchVoice } }
}
