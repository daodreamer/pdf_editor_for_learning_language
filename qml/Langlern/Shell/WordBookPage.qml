pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: page
    objectName: "wordBookPage"
    required property Theme theme
    required property WorkspaceState workspace
    required property OfflineServices services
    signal openSource(var book)
    property int filter: 0
    property int selected: -1
    property bool revealed: false
    property int editingIndex: -1
    readonly property var card: selected >= 0 && selected < workspace.words.length ? workspace.words[selected] : null
    spacing: 0
    // Words and source sentences use the system serif, so the language itself stands out from the interface.
    readonly property string serif: "Georgia"
    function commitMeaning(): void {
        if (editingIndex < 0) return
        const index = editingIndex
        editingIndex = -1
        workspace.editMeaning(index, meaning.text)
    }
    function selectCard(index: int): void { commitMeaning(); selected = index; revealed = false }
    Component.onDestruction: commitMeaning()
    function nextDue(): void {
        commitMeaning()
        revealed = false
        selected = -1
        const now = Date.now() / 1000
        for (let i = 0; i < workspace.words.length; ++i) {
            if (workspace.words[i].due <= now) { selected = i; return }
        }
    }
    function grade(value: int): void { if (card) { commitMeaning(); workspace.grade(selected, value); nextDue() } }
    Component.onCompleted: nextDue()
    Shortcut { sequence: "Space"; enabled: page.visible && !meaning.activeFocus; onActivated: page.revealed = true }
    Shortcut { sequence: "1"; enabled: page.visible && page.revealed && !meaning.activeFocus; onActivated: page.grade(1) }
    Shortcut { sequence: "2"; enabled: page.visible && page.revealed && !meaning.activeFocus; onActivated: page.grade(2) }
    Shortcut { sequence: "3"; enabled: page.visible && page.revealed && !meaning.activeFocus; onActivated: page.grade(3) }
    RowLayout {
        Layout.fillWidth: true; Layout.preferredHeight: 76; Layout.leftMargin: 32; Layout.rightMargin: 28; spacing: 10
        Label { text: qsTr("生词本"); font.pixelSize: 24; font.weight: Font.Bold; color: page.theme.text }
        Label { text: String(page.workspace.words.length); color: page.theme.muted; font.pixelSize: 15; Layout.alignment: Qt.AlignBaseline }
        Item { Layout.fillWidth: true }
        // Segmented filter: one rounded track, the active option lifts to the card colour.
        Rectangle {
            implicitWidth: filters.implicitWidth + 6; implicitHeight: 38; radius: height / 2; color: page.theme.fill
            Row {
                id: filters
                anchors.centerIn: parent
                Repeater {
                    model: [qsTr("全部"), qsTr("待复习"), qsTr("新加入")]
                    AbstractButton {
                        id: option
                        required property int index
                        required property string modelData
                        width: optionLabel.implicitWidth + 32; height: 32; text: modelData
                        contentItem: Text { id: optionLabel; text: option.text; font.pixelSize: 13; font.weight: page.filter === option.index ? Font.DemiBold : Font.Normal; color: page.filter === option.index ? page.theme.accentText : page.theme.muted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: height / 2; color: page.filter === option.index ? page.theme.card : "transparent" }
                        onClicked: page.filter = index
                        Accessible.name: text
                    }
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
        ListView {
            id: words
            Layout.preferredWidth: Math.min(360, page.width * 0.38); Layout.fillHeight: true
            Layout.leftMargin: 18; Layout.bottomMargin: 12
            clip: true; reuseItems: true; model: page.workspace.words; spacing: 3
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool included: page.filter === 0 || (page.filter === 1 ? modelData.due <= Date.now() / 1000 : modelData.grade === 0)
                width: words.width; height: included ? 88 : 0; visible: included
                Rectangle {
                    anchors.fill: parent; anchors.margins: 4; radius: 18
                    color: page.selected === row.index ? page.theme.tint : rowHover.hovered ? page.theme.fill : "transparent"
                    HoverHandler { id: rowHover }
                    ColumnLayout {
                        anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; anchors.topMargin: 10; anchors.bottomMargin: 10; spacing: 2
                        Label { text: row.modelData.word; font.pixelSize: 17; font.family: page.serif; font.weight: Font.DemiBold; color: page.theme.text; Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText }
                        Label { text: row.modelData.meaning.length ? row.modelData.meaning : qsTr("尚未填写释义"); font.pixelSize: 12; color: page.theme.muted; Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText }
                        Label { text: row.modelData.source + " · p." + row.modelData.page; font.pixelSize: 11; color: page.theme.faint; Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText }
                    }
                    TapHandler { onTapped: page.selectCard(row.index) }
                    Accessible.role: Accessible.Button; Accessible.name: row.modelData.word
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 12; Layout.topMargin: 0
            radius: 22; color: page.theme.canvas
            ColumnLayout {
                anchors.centerIn: parent; width: Math.min(540, parent.width - 48); spacing: 18
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("今日复习"); color: page.theme.muted }
                    Item { Layout.fillWidth: true }
                    Label { text: qsTr("待复习 %1").arg(page.workspace.dueCount); color: page.theme.muted }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: details.implicitHeight + 64; radius: 32; color: page.theme.card
                    Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: -4; radius: parent.radius; color: page.theme.shadow }
                    ColumnLayout {
                        id: details; anchors.fill: parent; anchors.margins: 32; spacing: 20
                        RowLayout {
                            Layout.fillWidth: true; spacing: 14
                            Label { text: page.card ? page.card.word : page.workspace.words.length ? qsTr("今天的复习完成了") : qsTr("从阅读中积累生词"); font.pixelSize: page.width > 900 ? 40 : 26; font.family: page.card ? page.serif : ""; font.weight: Font.DemiBold; color: page.theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                            DesignButton { theme: page.theme; visible: page.card !== null; text: qsTr("离线发音"); symbol: "speak"; iconOnly: true; selected: true; implicitHeight: 46; onClicked: page.services.speak(page.card.word) }
                        }
                        Label { visible: !page.card; text: qsTr("在阅读页选择原文，点击“查词”加入生词本。"); color: page.theme.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Label { visible: page.card !== null; text: qsTr("原文"); color: page.theme.faint; font.pixelSize: 12 }
                        Label {
                            visible: page.card !== null; Layout.fillWidth: true; padding: 16
                            text: page.card ? page.card.context || page.card.word : ""; wrapMode: Text.Wrap; color: page.theme.text; font.pixelSize: 18; font.family: page.serif; lineHeight: 1.25; textFormat: Text.PlainText
                            background: Rectangle { color: page.theme.dark ? page.theme.fill : Qt.lighter(page.theme.fill, 1.03); radius: 18 }
                        }
                        DesignButton { visible: page.card !== null; theme: page.theme; text: page.card ? page.card.source + " · p." + page.card.page : ""; Layout.fillWidth: true; onClicked: page.openSource({url: page.card.url, page: page.card.page}) }
                        DesignButton { visible: page.card !== null && !page.revealed; theme: page.theme; text: qsTr("显示释义    Space"); selected: true; Layout.fillWidth: true; implicitHeight: 48; onClicked: page.revealed = true }
                        TextArea {
                            id: meaning; visible: page.card !== null && page.revealed
                            objectName: "wordMeaningEditor"
                            textFormat: TextEdit.PlainText
                            Layout.fillWidth: true; text: page.card ? page.card.meaning : ""; placeholderText: qsTr("填写释义"); wrapMode: TextEdit.Wrap; color: page.theme.text; font.pixelSize: 19
                            onActiveFocusChanged: { if (activeFocus) page.editingIndex = page.selected; else page.commitMeaning() }
                            leftPadding: 16; rightPadding: 16; topPadding: 12; bottomPadding: 12
                            background: Rectangle { color: page.theme.fill; radius: 18; border.width: meaning.activeFocus ? 2 : 0; border.color: page.theme.accent }
                        }
                        RowLayout {
                            visible: page.card !== null && page.revealed; Layout.fillWidth: true; spacing: 10
                            DesignButton { theme: page.theme; text: qsTr("忘了  1"); Layout.fillWidth: true; implicitHeight: 46; onClicked: page.grade(1); background: Rectangle { radius: height / 2; color: page.theme.fill } }
                            DesignButton { theme: page.theme; text: qsTr("模糊  2"); Layout.fillWidth: true; implicitHeight: 46; onClicked: page.grade(2); background: Rectangle { radius: height / 2; color: page.theme.fill } }
                            DesignButton { theme: page.theme; text: qsTr("记住了  3"); primary: true; Layout.fillWidth: true; implicitHeight: 46; onClicked: page.grade(3) }
                        }
                        DesignButton { visible: page.card !== null; theme: page.theme; text: qsTr("删除生词"); font.pixelSize: 12; Layout.alignment: Qt.AlignHCenter; onClicked: { page.commitMeaning(); page.workspace.removeWord(page.selected); page.nextDue() } }
                    }
                }
            }
        }
    }
}
