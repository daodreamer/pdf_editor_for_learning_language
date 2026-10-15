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
        Button { text: qsTr("关闭文档"); onClicked: view.shell.clearSelection() }
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 12
        Button { text: "−"; Accessible.name: qsTr("缩小"); onClicked: view.shell.setZoom(view.shell.zoom / 1.2) }
        Label { text: qsTr("%1%").arg(Math.round(view.shell.zoom * 100)); color: view.theme.text }
        Button { text: "+"; Accessible.name: qsTr("放大"); onClicked: view.shell.setZoom(view.shell.zoom * 1.2) }
        Button { text: qsTr("适应页宽"); onClicked: view.shell.setZoom(1) }
        Item { Layout.fillWidth: true }
    }
    Flickable {
        id: viewport
        objectName: "pdfViewport"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: Math.max(width, view.shell.contentWidth)
        contentHeight: view.shell.contentHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}
        readonly property real renderDpr: Screen.devicePixelRatio
        onRenderDprChanged: viewportTimer.restart()
        function report(): void {
            view.shell.updateViewport(contentY, height, width, renderDpr)
        }
        onContentYChanged: if (!viewportTimer.running) viewportTimer.start()
        onWidthChanged: viewportTimer.restart()
        onHeightChanged: viewportTimer.restart()
        onVisibleChanged: if (visible) viewportTimer.restart()
        Component.onCompleted: viewport.report()
        Connections {
            target: view.shell
            function onLayoutChanged(): void {
                viewport.contentY = view.shell.scrollOffset
                viewportTimer.restart()
            }
        }
        Timer { id: viewportTimer; interval: 16; onTriggered: viewport.report() }
        Repeater {
            model: view.shell.readingModel
            delegate: Rectangle {
                id: sheet
                required property int pageNumber
                required property real pageTop
                required property real pageWidth
                required property real pageHeight
                required property var pixels
                x: (viewport.contentWidth - width) / 2
                y: sheet.pageTop
                width: sheet.pageWidth
                height: sheet.pageHeight
                color: "white"
                PageItem {
                    objectName: "pdfPage"
                    anchors.fill: parent
                    image: sheet.pixels
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("PDF 第 %1 页").arg(sheet.pageNumber)
                }
            }
        }
        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: function(event) {
                const delta = event.angleDelta.y || event.pixelDelta.y
                if (delta !== 0)
                    view.shell.setZoom(view.shell.zoom * (delta > 0 ? 1.1 : 1 / 1.1))
                event.accepted = true
            }
        }
    }
}
