pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: note
    signal editingFinished()
    property bool marking: false
    required property ShellState shell
    required property OfflineServices services
    required property string noteId
    required property real noteOrder
    z: 1 + noteOrder / (noteOrder + 1)
    required property int notePage
    required property point noteAnchor
    required property real noteWidth
    required property bool noteWidthLocked
    required property real noteHeight
    required property real noteFontSize
    required property string noteText
    required property var noteStyles
    required property string noteFamily
    required property color noteColor
    required property size notePageUnits
    required property real pageWidth
    required property real pageHeight
    required property real pageTop
    required property real pageLeft
    required property real contentWidth
    readonly property bool selected: shell.annotations.selectedId === noteId
    property alias textEditor: editor
    property alias formatPopover: formattingPopover
    property bool applyingModel: false
    onNoteTextChanged: scheduleRestore()
    onNoteStylesChanged: scheduleRestore()
    function scheduleRestore(): void { applyingModel = true; Qt.callLater(restoreStyles) }
    function restoreStyles(): void {
        formatting.restore(noteStyles)
        applyingModel = false
        editor.pendingSync = false
    }
    AnnotationText {
        id: formatting
        document: editor.textDocument
        baseColor: note.noteColor
        baseSize: note.noteFontSize
        onChanged: editor.scheduleSync()
    }
    Component.onCompleted: restoreStyles()
    Connections { target: note.shell; function onTextCommitRequested(): void { editor.syncText() } }
    TextFormatPopover {
        id: formattingPopover
        editor: note.textEditor
        textBox: note
        formatter: formatting
        services: note.services
        dark: note.shell.dark
        active: note.selected && !note.resizing && !note.moving && !note.marking && note.Window.window.active
        positionRevision: note.x + note.y + note.viewportX + note.viewportY + note.unitScale
    }
    readonly property real unitScale: pageWidth / notePageUnits.width
    property point dragOffset: Qt.point(0, 0)
    property bool resizing: false
    property bool moving: false
    property real viewportX: 0
    property real viewportY: 0
    property rect resizePreview
    readonly property bool showFrame: selected || hover.hovered || resizing || moveMouse.pressed
    function cancelAdjustment(): void {
        resizing = false
        moving = false
        dragOffset = Qt.point(0, 0)
    }
    onUnitScaleChanged: cancelAdjustment()
    onPageTopChanged: cancelAdjustment()
    onPageLeftChanged: cancelAdjustment()
    onViewportXChanged: cancelAdjustment()
    onViewportYChanged: cancelAdjustment()
    onEnabledChanged: if (!enabled) cancelAdjustment()
    onMarkingChanged: cancelAdjustment()
    onVisibleChanged: if (!visible) cancelAdjustment()
    Connections {
        target: note.Window.window
        function onActiveChanged(): void { if (!note.Window.window.active) note.cancelAdjustment() }
    }
    HoverHandler { id: hover; margin: 18; blocking: false }
    readonly property real editorWidth: !noteWidthLocked && editor.preeditText.length > 0
        ? Math.min(compositionWidthLimit, Math.max(noteWidth, Math.ceil(compositionMetrics.advanceWidth) + 2)) : noteWidth
    readonly property real compositionWidthLimit: { const version = note.shell.annotations.version; return note.shell.annotations.textWidthLimit(note.noteId) }
    TextMetrics {
        id: compositionMetrics
        font: editor.font
        text: editor.preeditText.length > 0
            ? editor.text.slice(0, editor.cursorPosition) + editor.preeditText + editor.text.slice(editor.cursorPosition) : ""
    }
    x: pageLeft + (resizing ? resizePreview.x : noteAnchor.x * pageWidth) + dragOffset.x
    y: pageTop + (resizing ? resizePreview.y : noteAnchor.y * pageHeight) + dragOffset.y
    width: resizing ? resizePreview.width : editorWidth * unitScale
    height: resizing ? Math.min(pageHeight - resizePreview.y, Math.max(resizePreview.height, editor.contentHeight * unitScale))
                     : Math.min(noteHeight * unitScale, (1 - noteAnchor.y) * pageHeight)
    onSelectedChanged: {
        if (selected) editor.forceActiveFocus()
        else {
            const hadFocus = editor.activeFocus
            editor.focus = false
            if (hadFocus) note.editingFinished()
        }
    }

    Rectangle {
        id: frame
        objectName: "annotationFrame"
        anchors.fill: parent
        color: "transparent"
        border.color: "#397bd2"
        border.width: 1
        opacity: note.showFrame ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 140 } }
        Accessible.ignored: true
    }
    Item {
        width: note.width / note.unitScale
        height: note.height / note.unitScale
        scale: note.unitScale
        transformOrigin: Item.TopLeft
        clip: true
        TextEdit {
            id: editor
            property bool pendingSync: false
            objectName: "annotationEditor"
            width: note.width / note.unitScale
            height: Math.max(24, contentHeight)
            text: note.noteText
            textFormat: TextEdit.PlainText
            padding: 0
            wrapMode: TextEdit.Wrap
            horizontalAlignment: TextEdit.AlignLeft
            color: note.noteColor
            font.family: note.noteFamily
            font.pixelSize: note.noteFontSize
            selectByMouse: true
            persistentSelection: false
            Accessible.name: qsTr("第 %1 页文字答案").arg(note.notePage + 1)
            onActiveFocusChanged: {
                if (activeFocus) {
                    note.shell.focusPage(note.notePage)
                    note.shell.textSelection.clear()
                    note.shell.annotations.select(note.noteId)
                } else if (note.selected) {
                    note.shell.commitInput()
                    syncText()
                    note.shell.annotations.finishEditing()
                }
            }
            function syncText(): void {
                if (!note.selected || !pendingSync || note.applyingModel) return
                pendingSync = false
                note.shell.annotations.setEditorText(note.noteId, formatting.plainText(), preeditText.length > 0 ? note.noteHeight : Math.max(24, contentHeight), formatting.styles())
            }
            function scheduleSync(): void { pendingSync = true; Qt.callLater(syncText) }
            onTextChanged: scheduleSync()
            onContentHeightChanged: scheduleSync()
            Keys.onEscapePressed: { note.shell.finishTextEditing(); focus = false }
            Keys.onPressed: function(event) {
                if (preeditText.length > 0) return
                if (event.key === Qt.Key_Tab) {
                    note.shell.commitInput()
                    syncText()
                    event.accepted = note.shell.annotations.selectNext(note.noteId)
                    return
                }
                if ((event.modifiers & (Qt.platform.os === "osx" ? Qt.MetaModifier : Qt.ControlModifier)) && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                    note.shell.finishTextEditing()
                    focus = false
                    event.accepted = true
                } else if ((event.modifiers & (Qt.platform.os === "osx" ? Qt.MetaModifier : Qt.ControlModifier)) && event.key === Qt.Key_Delete) {
                    note.shell.resetInput()
                    note.shell.annotations.removeSelected()
                    event.accepted = true
                }
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        enabled: note.marking
        cursorShape: Qt.PointingHandCursor
        onClicked: note.shell.annotations.cycleMark(note.noteId)
        Accessible.name: qsTr("批改答案：正确、错误、清除")
    }
    TapHandler {
        acceptedModifiers: Qt.AltModifier
        onTapped: note.shell.annotations.cycleMark(note.noteId)
    }
    Rectangle {
        objectName: "annotationMoveHandle"
        visible: note.showFrame || opacity > 0
        enabled: editor.text.length > 0 && !note.marking
        opacity: frame.opacity
        width: Math.min(64, note.width)
        height: 16
        y: -height
        color: "#397bd2"
        Text { anchors.centerIn: parent; text: qsTr("移动"); color: "white"; font.pixelSize: 11 }
        MouseArea {
            id: moveMouse
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.SizeAllCursor
            property point start
            onActiveFocusChanged: if (!activeFocus && note.moving) note.cancelAdjustment()
            onPressed: function(mouse) {
                note.shell.finishTextEditing()
                forceActiveFocus()
                note.moving = true
                start = mapToItem(note.parent, mouse.x, mouse.y)
            }
            onPositionChanged: function(mouse) {
                if (!pressed || !note.moving) return
                const p = mapToItem(note.parent, mouse.x, mouse.y)
                note.dragOffset = Qt.point(p.x - start.x, p.y - start.y)
            }
            onReleased: {
                if (!note.moving) { if (activeFocus) note.editingFinished(); return }
                const x = note.noteAnchor.x + note.dragOffset.x / note.pageWidth
                const y = note.noteAnchor.y + note.dragOffset.y / note.pageHeight
                note.dragOffset = Qt.point(0, 0)
                note.moving = false
                note.shell.annotations.move(note.noteId, x, y)
                note.editingFinished()
            }
            onCanceled: note.cancelAdjustment()
            Keys.onEscapePressed: { note.cancelAdjustment(); note.editingFinished() }
            Accessible.name: qsTr("移动文字答案")
        }
    }
    Repeater {
        model: 4
        delegate: Rectangle {
            id: corner
            required property int index
            objectName: "annotationResizeHandle" + index
            x: (index % 2 === 0 ? 0 : note.width) - width / 2
            y: (index < 2 ? 0 : note.height) - height / 2
            width: 10
            height: 10
            color: "white"
            border.color: "#397bd2"
            border.width: 1
            radius: 2
            opacity: frame.opacity
            visible: (note.showFrame || opacity > 0) && !note.marking && editor.text.length > 0
            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                preventStealing: true
                cursorShape: corner.index === 0 || corner.index === 3 ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                property point start
                property rect original
                onActiveFocusChanged: if (!activeFocus && note.resizing) note.cancelAdjustment()
                onPressed: function(mouse) {
                    note.shell.finishTextEditing()
                    forceActiveFocus()
                    start = mapToItem(note.parent, mouse.x, mouse.y)
                    original = Qt.rect(note.noteAnchor.x * note.pageWidth, note.noteAnchor.y * note.pageHeight, note.width, note.height)
                    note.resizePreview = original
                    note.resizing = true
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !note.resizing) return
                    const p = mapToItem(note.parent, mouse.x, mouse.y)
                    const dx = p.x - start.x, dy = p.y - start.y
                    const minWidth = Math.min(24 * note.unitScale, note.pageWidth)
                    const minHeight = Math.min(24 * note.unitScale, note.pageHeight)
                    let left = original.x, top = original.y
                    let right = original.x + original.width, bottom = original.y + original.height
                    if (corner.index % 2 === 0) left = Math.max(0, Math.min(right - minWidth, original.x + dx))
                    else right = Math.min(note.pageWidth, Math.max(left + minWidth, right + dx))
                    if (corner.index < 2) top = Math.max(0, Math.min(bottom - minHeight, original.y + dy))
                    else bottom = Math.min(note.pageHeight, Math.max(top + minHeight, bottom + dy))
                    note.resizePreview = Qt.rect(left, top, right - left, bottom - top)
                }
                onReleased: {
                    if (!note.resizing) { if (activeFocus) note.editingFinished(); return }
                    const box = note.resizePreview
                    const changed = box.x !== original.x || box.y !== original.y || box.width !== original.width || box.height !== original.height
                    if (changed)
                        note.shell.annotations.resize(note.noteId, box.x / note.pageWidth, box.y / note.pageHeight,
                        box.width / note.unitScale, box.height / note.unitScale)
                    note.resizing = false
                    if (changed) note.editingFinished()
                    else note.shell.annotations.select(note.noteId)
                }
                onCanceled: note.cancelAdjustment()
                Keys.onEscapePressed: { note.cancelAdjustment(); note.editingFinished() }
                Accessible.name: qsTr("调整文字框大小")
            }
        }
    }
}
