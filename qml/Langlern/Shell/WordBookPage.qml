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
    Rectangle {
        Layout.fillWidth: true; Layout.preferredHeight: 56; color: page.theme.surface
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 28; anchors.rightMargin: 28
            Label { text: qsTr("生词本"); font.pixelSize: 20; font.bold: true; color: page.theme.text }
            Label { text: String(page.workspace.words.length); color: page.theme.faint }
            Item { Layout.fillWidth: true }
            Repeater {
                model: [qsTr("全部"), qsTr("待复习"), qsTr("新加入")]
                DesignButton { required property int index; required property string modelData; theme: page.theme; text: modelData; selected: page.filter === index; onClicked: page.filter = index }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: page.theme.line }
    }
    RowLayout {
        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
        ListView {
            id: words
            Layout.preferredWidth: Math.min(380, page.width * 0.38); Layout.fillHeight: true
            clip: true; reuseItems: true; model: page.workspace.words; spacing: 3
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool included: page.filter === 0 || (page.filter === 1 ? modelData.due <= Date.now() / 1000 : modelData.grade === 0)
                width: words.width; height: included ? 88 : 0; visible: included
                Rectangle {
                    anchors.fill: parent; anchors.margins: 6; radius: 9; color: page.selected === row.index ? page.theme.tint : "transparent"
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 10; spacing: 3
                        Label { text: row.modelData.word; font.pixelSize: 15; font.weight: Font.DemiBold; color: page.theme.text; Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText }
                        Label { text: row.modelData.meaning.length ? row.modelData.meaning : qsTr("尚未填写释义"); font.pixelSize: 12; color: page.theme.muted; Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText }
                        Label { text: row.modelData.source + " · p." + row.modelData.page; font.pixelSize: 11; color: page.theme.faint; Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText }
                    }
                    TapHandler { onTapped: page.selectCard(row.index) }
                    Accessible.role: Accessible.Button; Accessible.name: row.modelData.word
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; color: page.theme.canvas
            ColumnLayout {
                anchors.centerIn: parent; width: Math.min(540, parent.width - 48); spacing: 18
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("今日复习"); color: page.theme.muted }
                    Item { Layout.fillWidth: true }
                    Label { text: qsTr("待复习 %1").arg(page.workspace.dueCount); color: page.theme.muted }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: details.implicitHeight + 60; radius: 20; color: page.theme.card; border.color: page.theme.line
                    ColumnLayout {
                        id: details; anchors.fill: parent; anchors.margins: 30; spacing: 22
                        Label { text: page.card ? page.card.word : page.workspace.words.length ? qsTr("今天的复习完成了") : qsTr("从阅读中积累生词"); font.pixelSize: page.width > 900 ? 36 : 25; font.bold: true; color: page.theme.text; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                        DesignButton { theme: page.theme; visible: page.card !== null; text: qsTr("▶  离线发音"); selected: true; onClicked: page.services.speak(page.card.word) }
                        Label { visible: !page.card; text: qsTr("在阅读页选择原文，点击“查词 / 摘录”加入生词本。"); color: page.theme.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Label { visible: page.card !== null; text: qsTr("原文"); color: page.theme.faint; font.pixelSize: 12 }
                        Label {
                            visible: page.card !== null; Layout.fillWidth: true; padding: 16
                            text: page.card ? page.card.context || page.card.word : ""; wrapMode: Text.Wrap; color: "#2b2823"; font.pixelSize: 19; font.family: "Times New Roman"; textFormat: Text.PlainText
                            background: Rectangle { color: "#f2ede0"; radius: 10 }
                        }
                        DesignButton { visible: page.card !== null; theme: page.theme; text: page.card ? page.card.source + " · p." + page.card.page : ""; Layout.fillWidth: true; onClicked: page.openSource({url: page.card.url, page: page.card.page}) }
                        DesignButton { visible: page.card !== null && !page.revealed; theme: page.theme; text: qsTr("显示释义    Space"); selected: true; Layout.fillWidth: true; implicitHeight: 46; onClicked: page.revealed = true }
                        TextArea {
                            id: meaning; visible: page.card !== null && page.revealed
                            objectName: "wordMeaningEditor"
                            textFormat: TextEdit.PlainText
                            Layout.fillWidth: true; text: page.card ? page.card.meaning : ""; placeholderText: qsTr("填写释义"); wrapMode: TextEdit.Wrap; color: page.theme.text; font.pixelSize: 19
                            onActiveFocusChanged: { if (activeFocus) page.editingIndex = page.selected; else page.commitMeaning() }
                            background: Rectangle { color: page.theme.fill; radius: 8 }
                        }
                        RowLayout {
                            visible: page.card !== null && page.revealed; Layout.fillWidth: true
                            DesignButton { theme: page.theme; text: qsTr("忘了  1"); Layout.fillWidth: true; onClicked: page.grade(1) }
                            DesignButton { theme: page.theme; text: qsTr("模糊  2"); Layout.fillWidth: true; onClicked: page.grade(2) }
                            DesignButton { theme: page.theme; text: qsTr("记住了  3"); selected: true; Layout.fillWidth: true; onClicked: page.grade(3) }
                        }
                        DesignButton { visible: page.card !== null; theme: page.theme; text: qsTr("删除生词"); onClicked: { page.commitMeaning(); page.workspace.removeWord(page.selected); page.nextDue() } }
                    }
                }
            }
        }
    }
}
