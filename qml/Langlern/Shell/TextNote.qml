pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: note
    signal editingFinished()
    required property ShellState shell
    required property string noteId
    required property real noteOrder
    z: 1 + noteOrder / (noteOrder + 1)
    required property int notePage
    required property point noteAnchor
    required property real noteWidth
    required property real noteHeight
    required property real noteFontSize
    required property string noteText
    required property string noteFamily
    required property color noteColor
    required property size notePageUnits
    required property real pageWidth
    required property real pageHeight
    required property real pageTop
    required property real contentWidth
    readonly property bool selected: shell.annotations.selectedId === noteId
    readonly property real unitScale: pageWidth / notePageUnits.width
    property point dragOffset: Qt.point(0, 0)
    x: (contentWidth - pageWidth) / 2 + noteAnchor.x * pageWidth + dragOffset.x
    y: pageTop + noteAnchor.y * pageHeight + dragOffset.y
    width: noteWidth * unitScale
    height: Math.min(noteHeight * unitScale, (1 - noteAnchor.y) * pageHeight)
    onSelectedChanged: {
        if (selected) editor.forceActiveFocus()
        else {
            const hadFocus = editor.activeFocus
            editor.focus = false
            if (hadFocus) note.editingFinished()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: "#397bd2"
        border.width: note.selected ? 1 : 0
        Accessible.ignored: true
    }
    Item {
        width: note.noteWidth
        height: note.height / note.unitScale
        scale: note.unitScale
        transformOrigin: Item.TopLeft
        clip: true
        TextEdit {
            id: editor
            objectName: "annotationEditor"
            width: note.noteWidth
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
                    note.shell.textSelection.clear()
                    note.shell.annotations.select(note.noteId)
                } else if (note.selected) {
                    note.shell.commitInput()
                    syncText()
                    note.shell.annotations.finishEditing()
                }
            }
            function syncText(): void {
                if (note.selected) note.shell.annotations.setText(note.noteId, text, preeditText.length > 0 ? note.noteHeight : Math.max(24, contentHeight))
            }
            onTextChanged: syncText()
            onContentHeightChanged: syncText()
            Keys.onEscapePressed: { note.shell.finishTextEditing(); focus = false }
            Keys.onPressed: function(event) {
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
    Rectangle {
        objectName: "annotationMoveHandle"
        visible: note.selected
        width: Math.min(64, note.width)
        height: 16
        y: -height
        color: "#397bd2"
        Text { anchors.centerIn: parent; text: qsTr("移动"); color: "white"; font.pixelSize: 11 }
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.SizeAllCursor
            property point start
            onPressed: function(mouse) {
                note.shell.commitInput()
                start = mapToItem(note.parent, mouse.x, mouse.y)
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                const p = mapToItem(note.parent, mouse.x, mouse.y)
                note.dragOffset = Qt.point(p.x - start.x, p.y - start.y)
            }
            onReleased: {
                const x = note.noteAnchor.x + note.dragOffset.x / note.pageWidth
                const y = note.noteAnchor.y + note.dragOffset.y / note.pageHeight
                note.dragOffset = Qt.point(0, 0)
                note.shell.annotations.move(note.noteId, x, y)
            }
            onCanceled: note.dragOffset = Qt.point(0, 0)
            Accessible.name: qsTr("移动文字答案")
        }
    }
}
