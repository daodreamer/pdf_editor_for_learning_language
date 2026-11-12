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
        regionSelection = false
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
    property bool regionSelection: false
    property bool nightDim: true
    function wash(alpha: real): color { return Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, alpha) }
    function startRegionRecognition(): void {
        shell.finishTextEditing()
        shell.annotations.cancelGesture()
        shell.textSelection.clear()
        regionSelection = true
        inputMode = 1
        selectionMouse.forceActiveFocus()
    }
    onInputModeChanged: shell.ocrSelectionEnabled = inputMode === 1 && !regionSelection
    onRegionSelectionChanged: shell.ocrSelectionEnabled = inputMode === 1 && !regionSelection
    property rect answerHint: Qt.rect(0, 0, 0, 0)
    readonly property bool answerHintsEnabled: !marking && !reading && (inputMode === 0 || inputMode === 2) && shell.storageReady
    onAnswerHintsEnabledChanged: updateAnswerHint()
    function updateAnswerHint(): void {
        if (!answerHintsEnabled || !answerHover.hovered || viewport.moving || wheelGlide.running || shell.annotations.gestureActive() || inkInput.selectingText) { answerHint = Qt.rect(0, 0, 0, 0); return }
        const p = viewport.contentItem.mapFromItem(answerHover.parent, answerHover.point.position.x, answerHover.point.position.y)
        answerHint = shell.answerBlankAt(p.x, p.y)
    }
    Connections { target: view.shell; function onAnswerLinesChanged(): void { view.updateAnswerHint() } function onSelectionChanged(): void { view.updateAnswerHint() } }
    Connections { target: view.shell.annotations; function onContentChanged(): void { view.answerHint = Qt.rect(0, 0, 0, 0) } function onSelectionChanged(): void { view.answerHint = Qt.rect(0, 0, 0, 0) } }
    signal wordRequested(string text, string context)
    // Where the lookup card should attach, in Overlay.overlay coordinates; set before wordRequested.
    property point lookupPoint: Qt.point(width / 2, height / 3)
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
        ScrollBar.vertical: ScrollBar { id: verticalBar; contentItem: Rectangle { implicitWidth: 6; radius: 3; color: view.theme.faint; opacity: verticalBar.active ? 0.75 : 0.0; Behavior on opacity { NumberAnimation { duration: 200 } } } }
        ScrollBar.horizontal: ScrollBar { id: horizontalBar; contentItem: Rectangle { implicitHeight: 6; radius: 3; color: view.theme.faint; opacity: horizontalBar.active ? 0.75 : 0.0; Behavior on opacity { NumberAnimation { duration: 200 } } } }
        readonly property real renderDpr: Screen.devicePixelRatio
        onRenderDprChanged: viewportTimer.restart()
        function report(): void {
            view.shell.updateViewport(contentY, height, width, renderDpr)
        }
        onContentYChanged: {
            view.answerHint = Qt.rect(0, 0, 0, 0)
            if (!viewportTimer.running) viewportTimer.start()
            // Scrollbar drags, page jumps and flicks take over from wheel smoothing.
            if (wheelGlide.running && !wheelGlide.writing) wheelGlide.stop()
        }
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
                // Soft two-step lift instead of one hard offset shadow; plain rectangles, no blur.
                Rectangle { x: -1; y: 1; width: parent.width + 2; height: parent.height + 2; z: -1; radius: 3; color: view.theme.dark ? "#33000000" : "#0c1e2a26" }
                Rectangle { x: 0; y: 5; width: parent.width; height: parent.height; z: -2; radius: 6; color: view.theme.dark ? "#40000000" : "#101e2a26" }
                PageItem {
                    objectName: "pdfPage"
                    anchors.fill: parent
                    image: sheet.pixels
                    // 夜读柔光: in the dark theme the white page is dimmed over the black sheet.
                    opacity: view.theme.dark && view.nightDim ? 0.88 : 1
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
                        // Hovered word uses the same fill as a text selection.
                        color: wordHover.hovered ? view.wash(0.38) : view.wash(0.07)
                        border.color: wordHover.hovered ? "transparent" : view.wash(0.22); radius: 3
                        HoverHandler { id: wordHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: { view.lookupPoint = ocrWord.mapToItem(Overlay.overlay, ocrWord.width / 2, ocrWord.height); view.wordRequested(ocrWord.modelData.word, ocrWord.modelData.context) } }
                        Accessible.role: Accessible.Button; Accessible.name: modelData.word
                    }
                }
            }
        }
        // Wheel scrolling with speed detection: slow notches move a modest step,
        // a fast spin ramps each notch up towards a screen so pages flip quickly.
        // Motion eases towards the target every frame, so it never jumps.
        WheelHandler {
            id: wheelScroll
            objectName: "wheelScroll"
            acceptedModifiers: Qt.NoModifier
            target: null
            property real lastNotch: 0
            property real averageInterval: 260 // ms between notches, smoothed
            property real boost: 1
            property int direction: 0
            readonly property real calmRate: 4 // notches/s at or below which steps stay at base size
            readonly property real baseStep: Math.max(90, Math.min(160, viewport.height * 0.16))
            readonly property real maxStep: Math.max(baseStep, viewport.height * 0.9)
            onWheel: function(event) {
                const angle = event.angleDelta.y
                if (angle === 0 && event.pixelDelta.y === 0) { event.accepted = false; return } // horizontal: Flickable
                const highResolution = event.pixelDelta.y !== 0 || angle % 120 !== 0
                if (highResolution) {
                    // Touchpads and free-spinning wheels already deliver smooth deltas.
                    const delta = event.pixelDelta.y !== 0 ? event.pixelDelta.y : angle / 120 * baseStep
                    wheelGlide.jump(-delta)
                    boost = 1
                    event.accepted = true
                    return
                }
                const now = Date.now()
                const interval = now - lastNotch
                const dir = angle > 0 ? -1 : 1
                lastNotch = now
                // Speed is the smoothed notch rate; a pause or reversal starts calm again.
                averageInterval = dir !== direction || interval > 260 ? 260 : averageInterval * 0.5 + Math.max(8, interval) * 0.5
                boost = Math.max(1, Math.min(maxStep / baseStep, 1 + (1000 / averageInterval - calmRate) * 0.18))
                if (dir !== direction) wheelGlide.settle()
                direction = dir
                wheelGlide.glide(dir * Math.abs(angle / 120) * Math.min(maxStep, baseStep * boost))
                event.accepted = true
            }
        }
        FrameAnimation {
            id: wheelGlide
            property real target: 0
            property bool writing: false
            readonly property real timeConstant: 0.075 // seconds; ~95% of the way in 0.22 s
            function bounded(y: real): real { return Math.max(0, Math.min(viewport.contentHeight - viewport.height, y)) }
            function settle(): void { target = viewport.contentY }
            function glide(delta: real): void {
                if (!running) target = viewport.contentY
                target = bounded(target + delta)
                if (target !== viewport.contentY) { viewport.cancelFlick(); start() }
            }
            function jump(delta: real): void {
                stop()
                viewport.cancelFlick()
                viewport.contentY = bounded(viewport.contentY + delta)
            }
            onTriggered: {
                const remaining = target - viewport.contentY
                const next = Math.abs(remaining) < 0.5 ? target : viewport.contentY + remaining * (1 - Math.exp(-frameTime / timeConstant))
                writing = true
                viewport.contentY = next
                writing = false
                if (next === target) stop()
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
            // Text cursor in select/type modes; otherwise the default arrow (pen,
            // eraser and move-ink cursors come from InkInput).
            cursorShape: view.regionSelection ? Qt.CrossCursor : (view.inputMode === 1 || view.inputMode === 2) ? Qt.IBeamCursor : undefined
            property bool dragging: false
            property bool selecting: false
            property point origin
            property point regionStart
            property point regionEnd
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
                if (view.regionSelection) {
                    regionStart = p; regionEnd = p
                    selecting = false; preventStealing = true
                    return
                }
                view.shell.finishTextEditing()
                selecting = view.inputMode === 1 && view.shell.beginTextSelection(p.x, p.y)
                // Keep the press until the drag threshold; Flickable can steal blank drags.
                preventStealing = selecting
                if (!selecting) mouse.accepted = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                if (Math.abs(mouse.x - origin.x) + Math.abs(mouse.y - origin.y) > 3) dragging = true
                if (view.regionSelection) { regionEnd = mapToItem(viewport.contentItem, mouse.x, mouse.y); return }
                if (dragging && selecting) extend()
            }
            onReleased: {
                if (view.regionSelection) {
                    regionEnd = mapToItem(viewport.contentItem, mouseX, mouseY)
                    view.shell.recognizeRegion(regionStart.x, regionStart.y, regionEnd.x, regionEnd.y)
                    selectionActions.anchorPoint = mapToItem(selectionActions.parent, mouseX, mouseY)
                    view.regionSelection = false
                    dragging = false
                    return
                }
                if (dragging && selecting) extend()
                if (selecting && !dragging) view.shell.textSelection.expandWord()
                if (selecting) selectionActions.anchorPoint = mapToItem(selectionActions.parent, mouseX, mouseY)
                dragging = false
            }
            onCanceled: { dragging = false; selecting = false; view.regionSelection = false }
            Keys.onEscapePressed: { selecting = false; view.regionSelection = false; view.shell.textSelection.clear() }
            Shortcut {
                sequences: [StandardKey.Copy]
                enabled: (selectionMouse.activeFocus || inkInput.activeFocus) && view.shell.textSelection.ready
                onActivated: view.shell.textSelection.copy()
            }
            Timer {
                interval: 16
                repeat: true
                running: (selectionMouse.pressed && selectionMouse.dragging && selectionMouse.selecting) || inkInput.selectingText
                onTriggered: {
                    const y = inkInput.selectingText ? inkInput.selectionPosition.y : selectionMouse.mouseY
                    const speed = y < 36 ? -18 : y > viewport.height - 36 ? 18 : 0
                    if (speed !== 0) {
                        viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height, viewport.contentY + speed))
                        viewport.report()
                        if (inkInput.selectingText) {
                            const p = inkInput.selectionPosition
                            view.shell.extendTextSelection(p.x + viewport.contentX, p.y + viewport.contentY)
                        } else selectionMouse.extend()
                    }
                }
            }
        }
        Rectangle {
            visible: view.regionSelection && selectionMouse.pressed
            x: Math.min(selectionMouse.regionStart.x, selectionMouse.regionEnd.x)
            y: Math.min(selectionMouse.regionStart.y, selectionMouse.regionEnd.y)
            width: Math.abs(selectionMouse.regionEnd.x - selectionMouse.regionStart.x)
            height: Math.abs(selectionMouse.regionEnd.y - selectionMouse.regionStart.y)
            color: view.wash(0.12); border.color: view.theme.accent; radius: 6
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
                services: view.services
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
            onTextGestureChanged: view.updateAnswerHint()
            onTextSelectionFinished: function(position) { selectionActions.anchorPoint = mapToItem(selectionActions.parent, position.x, position.y) }
            onOriginalWordClicked: function(position) {
                if (view.inputMode !== 0) return
                if (view.shell.beginTextSelection(position.x + viewport.contentX, position.y + viewport.contentY)) {
                    view.shell.textSelection.expandWord()
                    if (view.shell.textSelection.ready) { view.lookupPoint = mapToItem(Overlay.overlay, position.x, position.y); view.wordRequested(view.shell.textSelection.text, view.shell.textSelection.context) }
                }
            }
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
            color: view.wash(0.09); border.color: view.wash(0.45); radius: 4
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
    SelectionActions {
        id: selectionActions
        theme: view.theme
        services: view.services
        selection: view.shell.textSelection
        active: view.visible && view.Window.window.active && !inkInput.selectingText && !selectionMouse.pressed
            && (selectionMouse.activeFocus || inkInput.activeFocus) && !viewport.moving
        onLookupRequested: { view.lookupPoint = Qt.point(anchorPoint.x, anchorPoint.y - 16); view.wordRequested(view.shell.textSelection.text, view.shell.textSelection.context) }
    }
}
