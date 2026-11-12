pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: side
    required property Theme theme
    required property ShellState shell
    required property WorkspaceState workspace
    property bool showBookmarks: false
    color: theme.sidebar
    radius: 22
    ColumnLayout {
        anchors.fill: parent; anchors.topMargin: 10; anchors.bottomMargin: 10; spacing: 10
        // Segmented control: one rounded track, the active half lifts to the card colour.
        Rectangle {
            Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10; Layout.preferredHeight: 36
            radius: height / 2; color: side.theme.fill
            Row {
                anchors.fill: parent; anchors.margins: 3
                Repeater {
                    model: [qsTr("页面"), qsTr("书签")]
                    AbstractButton {
                        id: segment
                        required property int index
                        required property string modelData
                        readonly property bool active: side.showBookmarks === (index === 1)
                        width: parent.width / 2; height: parent.height
                        text: modelData
                        contentItem: Text { text: segment.text; font.pixelSize: 13; font.weight: segment.active ? Font.DemiBold : Font.Normal; color: segment.active ? side.theme.accentText : side.theme.muted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: height / 2; color: segment.active ? side.theme.card : "transparent" }
                        onClicked: side.showBookmarks = index === 1
                        Accessible.name: text
                    }
                }
            }
        }
        ListView {
            id: pages
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; reuseItems: true
            model: side.showBookmarks ? side.workspace.bookmarks : side.shell.pageCount
            spacing: 14; cacheBuffer: 0
            ScrollBar.vertical: ScrollBar {}
            currentIndex: side.shell.currentPage - 1
            onCurrentIndexChanged: if (!side.showBookmarks) positionViewAtIndex(currentIndex, ListView.Contain)
            delegate: Item {
                id: thumb
                required property int index
                required property var modelData
                readonly property int number: side.showBookmarks ? Number(modelData) : index + 1
                width: pages.width; height: side.showBookmarks ? 42 : 174
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter; y: 3
                    width: side.showBookmarks ? parent.width - 20 : 108; height: side.showBookmarks ? 36 : 147
                    radius: side.showBookmarks ? height / 2 : 8
                    color: side.showBookmarks ? (thumb.number === side.shell.currentPage ? side.theme.tint : "transparent") : "white"
                    border.color: side.showBookmarks ? "transparent" : thumb.number === side.shell.currentPage ? side.theme.accent : side.theme.line
                    border.width: thumb.number === side.shell.currentPage ? 2.5 : 1
                    PageItem {
                        anchors.fill: parent; anchors.margins: 4; visible: !side.showBookmarks
                        image: { const revision = side.shell.pageImage; return side.shell.cachedPage(thumb.number - 1) }
                    }
                    Label { anchors.centerIn: parent; text: side.showBookmarks ? qsTr("⚑  第 %1 页").arg(thumb.number) : String(thumb.number); color: side.theme.muted; visible: { const revision = side.shell.pageImage; return side.showBookmarks || !side.shell.hasCachedPage(thumb.number - 1) } font.pixelSize: 12 }
                    TapHandler { onTapped: side.shell.goToPage(thumb.number) }
                    Accessible.role: Accessible.Button; Accessible.name: qsTr("跳转到第 %1 页").arg(thumb.number)
                }
                Label { visible: !side.showBookmarks; anchors.horizontalCenter: parent.horizontalCenter; y: 156; text: String(thumb.number); color: thumb.number === side.shell.currentPage ? side.theme.accentText : side.theme.muted; font.pixelSize: 11; font.weight: thumb.number === side.shell.currentPage ? Font.DemiBold : Font.Normal }
            }
            Label { anchors.centerIn: parent; visible: side.showBookmarks && side.workspace.bookmarks.length === 0; text: qsTr("还没有书签"); color: side.theme.faint }
        }
    }
}
