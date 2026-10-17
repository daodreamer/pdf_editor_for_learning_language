pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: view
    required property ShellState shell
    required property Theme theme
    required property OfflineServices services
    property int inputMode: 0 // 0 automatic, 1 original text selection, 2 answer input, 3 pen, 4 eraser, 5 move ink
    function switchMode(mode: int): void {
        shell.annotations.cancelGesture()
        shell.finishTextEditing()
        shell.textSelection.clear()
        inputMode = mode
        selectionMouse.forceActiveFocus()
    }
    Shortcut { sequence: (Qt.platform.os === "osx" ? "Meta" : "Ctrl") + "+Shift+A"; enabled: view.visible; onActivated: view.switchMode(0) }
    Shortcut { sequence: (Qt.platform.os === "osx" ? "Meta" : "Ctrl") + "+Shift+S"; enabled: view.visible; onActivated: view.switchMode(1) }
    Shortcut { sequence: (Qt.platform.os === "osx" ? "Meta" : "Ctrl") + "+Shift+T"; enabled: view.visible; onActivated: view.switchMode(2) }
    Shortcut { sequence: (Qt.platform.os === "osx" ? "Meta" : "Ctrl") + "+Shift+P"; enabled: view.visible; onActivated: view.switchMode(3) }
    Shortcut { sequence: (Qt.platform.os === "osx" ? "Meta" : "Ctrl") + "+Shift+E"; enabled: view.visible; onActivated: view.switchMode(4) }
    Shortcut { sequence: (Qt.platform.os === "osx" ? "Meta" : "Ctrl") + "+Shift+M"; enabled: view.visible; onActivated: view.switchMode(5) }
    property color inkColor: inkColors.colors[inkColors.currentIndex]
    property real inkWidth: inkSize.value
    property bool marking: false
    property bool reading: false
    property rect answerHint: Qt.rect(0, 0, 0, 0)
    readonly property bool answerHintsEnabled: !marking && !reading && (inputMode === 0 || inputMode === 2) && shell.storageReady
    onAnswerHintsEnabledChanged: updateAnswerHint()
    function updateAnswerHint(): void {
        if (!answerHintsEnabled || !answerHover.hovered || viewport.moving || shell.annotations.gestureActive()) { answerHint = Qt.rect(0, 0, 0, 0); return }
        const p = viewport.contentItem.mapFromItem(answerHover.parent, answerHover.point.position.x, answerHover.point.position.y)
        answerHint = shell.answerBlankAt(p.x, p.y)
    }
    Connections { target: view.shell; function onAnswerLinesChanged(): void { view.updateAnswerHint() } function onSelectionChanged(): void { view.updateAnswerHint() } }
    Connections { target: view.shell.annotations; function onContentChanged(): void { view.answerHint = Qt.rect(0, 0, 0, 0) } function onSelectionChanged(): void { view.answerHint = Qt.rect(0, 0, 0, 0) } }
    signal wordRequested(string text, string context)
    spacing: 0
    ComboBox {
        id: inkColors; objectName: "inkColors"; visible: false
        readonly property var colors: ["#235fbd", "#ce3446", "#188450", "#242424"]
        model: colors
    }
    Item { id: inkSize; objectName: "inkSize"; property real value: 2 }
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
        onContentYChanged: { view.answerHint = Qt.rect(0, 0, 0, 0); if (!viewportTimer.running) viewportTimer.start() }
        onContentXChanged: view.answerHint = Qt.rect(0, 0, 0, 0)
        onWidthChanged: viewportTimer.restart()
        onHeightChanged: viewportTimer.restart()
        onVisibleChanged: if (visible) viewportTimer.restart()
        Component.onCompleted: Qt.callLater(viewport.report)
        Connections {
            target: view.shell
            function onLayoutChanged(): void {
                view.answerHint = Qt.rect(0, 0, 0, 0)
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
                required property real pageLeft
                required property var pixels
                x: sheet.pageLeft
                y: sheet.pageTop
                width: sheet.pageWidth
                height: sheet.pageHeight
                color: view.theme.dark ? "black" : "white"
                Rectangle { x: 3; y: 5; width: parent.width; height: parent.height; z: -1; color: "#18000000" }
                PageItem {
                    objectName: "pdfPage"
                    anchors.fill: parent
                    image: sheet.pixels
                    opacity: view.theme.dark ? 0.88 : 1
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("PDF 第 %1 页").arg(sheet.pageNumber)
                }
                InkItem {
                    anchors.fill: parent
                    model: view.shell.annotations
                    page: sheet.pageNumber - 1
                    clip: true
                    Accessible.ignored: true
                }
                SelectionItem {
                    anchors.fill: parent
                    rectangles: {
                        const version = view.shell.textSelection.version
                        return view.shell.textSelection.rectangles(sheet.pageNumber - 1)
                    }
                    Accessible.ignored: true
                }
                Repeater {
                    model: (view.reading || (view.inputMode === 0 && !view.marking)) && view.services.document === String(view.shell.selectedUrl) && view.services.page === sheet.pageNumber ? view.services.words : []
                    delegate: Rectangle {
                        id: ocrWord
                        required property var modelData
                        x: modelData.x * sheet.width; y: modelData.y * sheet.height
                        width: modelData.w * sheet.width; height: modelData.h * sheet.height
                        color: "#10007aff"; border.color: "#30007aff"; radius: 2
                        TapHandler { onTapped: view.wordRequested(ocrWord.modelData.word, ocrWord.modelData.context) }
                        Accessible.role: Accessible.Button; Accessible.name: modelData.word
                    }
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
            cursorShape: view.inputMode === 2 ? Qt.IBeamCursor : Qt.ArrowCursor
            property bool dragging: false
            property bool selecting: false
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
                view.shell.finishTextEditing()
                selecting = view.inputMode !== 2 && view.shell.beginTextSelection(p.x, p.y)
                // Keep the press until the drag threshold; Flickable can steal blank drags.
                preventStealing = selecting
                if (!selecting) mouse.accepted = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                if (Math.abs(mouse.x - origin.x) + Math.abs(mouse.y - origin.y) > 3) dragging = true
                if (dragging && selecting) extend()
            }
            onReleased: {
                if (dragging && selecting) extend()
                if (selecting && !dragging) view.shell.textSelection.expandWord()
                if (selecting && view.shell.textSelection.ready && (view.reading || !dragging)) view.wordRequested(view.shell.textSelection.text, view.shell.textSelection.context)
                dragging = false
            }
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
                running: selectionMouse.pressed && selectionMouse.dragging && selectionMouse.selecting
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
        TapHandler {
            acceptedButtons: Qt.LeftButton
            gesturePolicy: TapHandler.DragThreshold
            onTapped: function(eventPoint) {
                if (view.marking || view.reading || view.inputMode === 0 || view.inputMode >= 3 || selectionMouse.selecting || view.inputMode === 1 || view.shell.annotations.selectedId.length > 0) return
                const p = viewport.contentItem.mapFromItem(null, eventPoint.scenePosition.x, eventPoint.scenePosition.y)
                if (view.shell.containsPage(p.x, p.y)) view.shell.createTextBox(p.x, p.y)
            }
        }
        Repeater {
            model: view.shell.annotations
            delegate: TextNote {
                onEditingFinished: selectionMouse.forceActiveFocus()
                shell: view.shell
                enabled: view.inputMode < 3 && view.shell.storageReady
                marking: view.marking
                pageWidth: view.shell.displayPageWidth
                pageHeight: pageWidth * notePageUnits.height / notePageUnits.width
                pageTop: view.shell.contentHeight >= 0 ? view.shell.pageTop(notePage) : 0
                pageLeft: view.shell.displayPageWidth > 0 ? view.shell.pageLeft(notePage) : 0
                contentWidth: viewport.contentWidth
                viewportX: viewport.contentX
                viewportY: viewport.contentY
            }
        }
        InkInput {
            z: 3
            id: inkInput
            objectName: "inkInput"
            x: viewport.contentX
            y: viewport.contentY
            width: viewport.width
            height: viewport.height
            enabled: view.shell.storageReady
            drawingEnabled: !view.marking && !view.reading
            visible: enabled
            shell: view.shell
            mode: view.inputMode
            passivePage: view.services.document === String(view.shell.selectedUrl) ? view.services.page - 1 : -1
            passiveRects: view.services.words.map(word => Qt.rect(word.x, word.y, word.w, word.h))
            onGestureStarting: viewport.cancelFlick()
            contentX: viewport.contentX
            contentY: viewport.contentY
            inkColor: view.inkColor
            inkWidth: view.inkWidth
        }
        HoverHandler {
            id: answerHover
            objectName: "answerHover"
            enabled: view.answerHintsEnabled
            parent: viewport.contentItem
            target: null
            blocking: false
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onPointChanged: view.updateAnswerHint()
            onHoveredChanged: view.updateAnswerHint()
        }
        Rectangle {
            objectName: "answerLineHint"
            z: 4
            x: view.answerHint.x; y: view.answerHint.y
            width: view.answerHint.width; height: view.answerHint.height
            visible: view.answerHintsEnabled && width > 0 && height > 0
            color: "#16007aff"; border.color: "#70007aff"; radius: 3
            readonly property bool blocksInkInput: true
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.IBeamCursor
                // Hover feedback may disappear on press; focus the existing editor immediately.
                onPressed: function(mouse) {
                    const p = mapToItem(viewport.contentItem, mouse.x, mouse.y)
                    view.shell.createAnswerAt(p.x, p.y)
                }
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("点击填写横线答案")
            }
        }
        Shortcut {
            sequences: [StandardKey.Undo]
            enabled: (selectionMouse.activeFocus || inkInput.activeFocus) && view.shell.annotations.canUndo
            onActivated: view.shell.annotations.undo()
        }
        Shortcut {
            sequences: [StandardKey.Redo]
            enabled: (selectionMouse.activeFocus || inkInput.activeFocus) && view.shell.annotations.canRedo
            onActivated: view.shell.annotations.redo()
        }
    }
}
