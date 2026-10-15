pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: view
    required property ShellState shell
    required property Theme theme
    spacing: 8
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 12
        Button { text: qsTr("上一页"); enabled: view.shell.currentPage > 1; onClicked: view.shell.goToPage(view.shell.currentPage - 1) }
        SpinBox {
            objectName: "pageNumber"
            from: 1
            to: Math.max(1, view.shell.pageCount)
            value: view.shell.currentPage
            editable: true
            enabled: view.shell.pageCount > 0
            onValueModified: view.shell.goToPage(value)
            Accessible.name: qsTr("页码")
        }
        Label { text: qsTr("/ %1 页").arg(view.shell.pageCount); color: view.theme.text }
        Button { text: qsTr("下一页"); enabled: view.shell.currentPage < view.shell.pageCount; onClicked: view.shell.goToPage(view.shell.currentPage + 1) }
        Item { Layout.fillWidth: true }
        Label { text: qsTr("适应页宽"); color: view.theme.muted }
        Button { text: qsTr("关闭文档"); onClicked: view.shell.clearSelection() }
    }
    Flickable {
        id: viewport
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: width
        contentHeight: page.height + 32
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        readonly property int targetWidth: Math.max(64, Math.ceil((width - 40) * Screen.devicePixelRatio))
        onTargetWidthChanged: resizeTimer.restart()
        Component.onCompleted: resizeTimer.restart()
        Connections {
            target: view.shell
            property int previousPage: 0
            function onSelectionChanged(): void {
                if (previousPage !== view.shell.currentPage) {
                    viewport.contentY = 0
                    previousPage = view.shell.currentPage
                }
            }
        }
        Timer {
            id: resizeTimer
            interval: 120
            onTriggered: view.shell.setRenderWidth(viewport.targetWidth)
        }
        PageItem {
            id: page
            objectName: "pdfPage"
            x: 20
            y: 16
            width: Math.max(1, viewport.width - 40)
            height: width * view.shell.pageRatio
            image: view.shell.pageImage
            Accessible.role: Accessible.Graphic
            Accessible.name: qsTr("PDF 第 %1 页").arg(view.shell.currentPage)
        }
    }
}
