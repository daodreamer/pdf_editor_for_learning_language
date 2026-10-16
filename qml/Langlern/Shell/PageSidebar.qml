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
    ColumnLayout {
        anchors.fill: parent; spacing: 8
        RowLayout {
            Layout.fillWidth: true; Layout.margins: 12; spacing: 2
            DesignButton { theme: side.theme; text: qsTr("页面"); selected: !side.showBookmarks; Layout.fillWidth: true; onClicked: side.showBookmarks = false }
            DesignButton { theme: side.theme; text: qsTr("书签"); selected: side.showBookmarks; Layout.fillWidth: true; onClicked: side.showBookmarks = true }
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
                    radius: side.showBookmarks ? 7 : 3
                    color: side.showBookmarks ? (thumb.number === side.shell.currentPage ? side.theme.tint : "transparent") : "white"
                    border.color: thumb.number === side.shell.currentPage ? side.theme.accent : side.theme.line
                    border.width: thumb.number === side.shell.currentPage ? 3 : 1
                    PageItem {
                        anchors.fill: parent; anchors.margins: 3; visible: !side.showBookmarks
                        image: { const revision = side.shell.pageImage; return side.shell.cachedPage(thumb.number - 1) }
                    }
                    Label { anchors.centerIn: parent; text: side.showBookmarks ? qsTr("⚑  第 %1 页").arg(thumb.number) : String(thumb.number); color: side.theme.muted; visible: { const revision = side.shell.pageImage; return side.showBookmarks || !side.shell.hasCachedPage(thumb.number - 1) } font.pixelSize: 12 }
                    TapHandler { onTapped: side.shell.goToPage(thumb.number) }
                    Accessible.role: Accessible.Button; Accessible.name: qsTr("跳转到第 %1 页").arg(thumb.number)
                }
                Label { visible: !side.showBookmarks; anchors.horizontalCenter: parent.horizontalCenter; y: 156; text: String(thumb.number); color: thumb.number === side.shell.currentPage ? side.theme.accent : side.theme.muted; font.pixelSize: 11 }
            }
            Label { anchors.centerIn: parent; visible: side.showBookmarks && side.workspace.bookmarks.length === 0; text: qsTr("还没有书签"); color: side.theme.faint }
        }
    }
    Rectangle { anchors.right: parent.right; height: parent.height; width: 1; color: side.theme.line }
}
