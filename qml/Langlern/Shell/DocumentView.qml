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
        Button { objectName: "copyTextButton"; text: qsTr("复制选中文字"); enabled: view.shell.textSelection.ready; onClicked: view.shell.textSelection.copy() }
        Item { Layout.fillWidth: true }
    }
    Label {
        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 12
        visible: text.length > 0
        text: view.shell.textSelection.status
        color: view.theme.muted
        wrapMode: Text.WordWrap
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
                SelectionItem {
                    anchors.fill: parent
                    rectangles: {
                        const version = view.shell.textSelection.version
                        return view.shell.textSelection.rectangles(sheet.pageNumber - 1)
                    }
                    Accessible.ignored: true
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
        MouseArea {
            id: selectionMouse
            objectName: "textSelectionMouse"
            x: viewport.contentX
            y: viewport.contentY
            width: viewport.width
            height: viewport.height
            acceptedButtons: Qt.LeftButton
            preventStealing: true
            cursorShape: Qt.IBeamCursor
            property bool dragging: false
            property point origin
            function extend(): void {
                const p = mapToItem(viewport.contentItem, mouseX, mouseY)
                view.shell.extendTextSelection(p.x, p.y)
            }
            onPressed: function(mouse) {
                viewport.cancelFlick()
                forceActiveFocus()
                dragging = false
                origin = Qt.point(mouse.x, mouse.y)
                const p = mapToItem(viewport.contentItem, mouse.x, mouse.y)
                if (!view.shell.beginTextSelection(p.x, p.y)) mouse.accepted = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                if (Math.abs(mouse.x - origin.x) + Math.abs(mouse.y - origin.y) > 3) dragging = true
                if (dragging) extend()
            }
            onReleased: { if (dragging) extend(); dragging = false }
            onCanceled: dragging = false
            Keys.onEscapePressed: view.shell.textSelection.clear()
            Shortcut {
                sequences: [StandardKey.Copy]
                enabled: selectionMouse.activeFocus && view.shell.textSelection.ready
                onActivated: view.shell.textSelection.copy()
            }
            Timer {
                interval: 16
                repeat: true
                running: selectionMouse.pressed && selectionMouse.dragging
                onTriggered: {
                    const y = selectionMouse.mouseY
                    const speed = y < 36 ? -18 : y > viewport.height - 36 ? 18 : 0
                    if (speed !== 0) {
                        viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height, viewport.contentY + speed))
                        viewport.report()
                        selectionMouse.extend()
                    }
                }
            }
        }
    }
}
