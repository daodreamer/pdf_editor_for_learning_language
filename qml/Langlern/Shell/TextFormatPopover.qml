pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Popup {
    id: popover
    required property TextEdit editor
    required property Item textBox
    required property AnnotationText formatter
    required property OfflineServices services
    required property bool dark
    required property bool active
    property real positionRevision: 0
    property int formatRevision: 0
    readonly property var current: {
        if (formatRevision < 0) return ({})
        return formatter.selectionStyle(editor.selectionStart, editor.selectionEnd)
    }
    readonly property color foreground: dark ? "#f5f5f7" : "#1d1d1f"
    readonly property color tint: dark ? "#303d52" : "#e7f0ff"
    readonly property color selectedColor: current.color || "#1e468c"
    property point selectionTop: Qt.point(0, 0)
    property point selectionBottom: Qt.point(0, 0)
    function updateAnchor(): void {
        if (!editor || !parent) return
        const first = editor.positionToRectangle(editor.selectionStart)
        const last = editor.positionToRectangle(editor.selectionEnd)
        selectionTop = editor.mapToItem(parent, first.x, first.y)
        selectionBottom = editor.mapToItem(parent, last.x, last.y + last.height)
        selectionTop.y = Math.min(selectionTop.y, textBox.mapToItem(parent, 0, -12).y)
        selectionBottom.y = Math.max(selectionBottom.y, textBox.mapToItem(parent, 0, textBox.height).y)
    }
    onPositionRevisionChanged: updateAnchor()
    onVisibleChanged: if (visible) updateAnchor()
    onParentChanged: updateAnchor()
    Connections {
        target: popover.editor
        function onSelectionStartChanged(): void { popover.updateAnchor() }
        function onSelectionEndChanged(): void { popover.updateAnchor() }
        function onContentHeightChanged(): void { popover.updateAnchor() }
        function onWidthChanged(): void { popover.updateAnchor() }
    }
    parent: Overlay.overlay
    objectName: "textFormatPopover"
    visible: active && editor.activeFocus && editor.selectionEnd > editor.selectionStart && editor.preeditText.length === 0
    focus: false
    modal: false
    closePolicy: Popup.NoAutoClose
    padding: 8
    width: Math.min(568, parent ? parent.width - 16 : 568)
    height: tools.childrenRect.height + 16
    x: Math.max(8, Math.min((parent ? parent.width : 400) - width - 8, (selectionTop.x + selectionBottom.x) / 2 - width / 2))
    y: Math.max(8, Math.min((parent ? parent.height : 900) - height - 8,
        selectionTop.y - height - 12 >= 8 ? selectionTop.y - height - 12 : selectionBottom.y + 12))
    function apply(property: string, value: var): void {
        const start = editor.selectionStart, end = editor.selectionEnd
        formatter.format(start, end, property, value)
        editor.select(start, end)
        ++formatRevision
        editor.forceActiveFocus()
    }
    Connections { target: popover.formatter; function onChanged(): void { ++popover.formatRevision } }
    background: Item {
        readonly property bool blocksInkInput: true
        Rectangle { anchors.fill: parent; anchors.topMargin: 3; anchors.bottomMargin: -3; radius: 16; color: popover.dark ? "#40000000" : "#18000000" }
        Rectangle { anchors.fill: parent; radius: 16; color: popover.dark ? "#f02c2c2e" : "#fcfafafa"; border.color: popover.dark ? "#35ffffff" : "#16000000" }
    }
    contentItem: Flow {
        id: tools
        spacing: 4
        ToolButton {
            objectName: "copyEditorSelection"
            width: 54; height: 32; text: qsTr("复制"); focusPolicy: Qt.NoFocus
            onClicked: popover.editor.copy()
            Accessible.name: qsTr("复制文本框选中文字")
        }
        ToolButton {
            objectName: "speakEditorSelection"
            width: 54; height: 32; text: qsTr("朗读"); focusPolicy: Qt.NoFocus
            onClicked: popover.services.speak(popover.editor.selectedText)
            Accessible.name: qsTr("朗读文本框选中文字")
        }
        Item { width: 1; height: 32; Rectangle { anchors.centerIn: parent; width: 1; height: 22; color: popover.dark ? "#35ffffff" : "#1c000000" } }
        ToolButton {
            id: boldButton
            objectName: "formatBold"
            width: 32; height: 32; text: "B"; font.bold: true; font.pixelSize: 16
            focusPolicy: Qt.NoFocus
            background: Rectangle { radius: 9; color: popover.current.bold ? popover.tint : boldButton.hovered ? (popover.dark ? "#3a3a3c" : "#eeeeef") : "transparent" }
            contentItem: Text { text: boldButton.text; font: boldButton.font; color: popover.current.bold ? "#007aff" : popover.foreground; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            onClicked: popover.apply("bold", !popover.current.bold)
            Accessible.name: qsTr("加粗选中文字")
        }
        ToolButton {
            id: italicButton
            objectName: "formatItalic"
            width: 32; height: 32; text: "I"; font.italic: true; font.pixelSize: 17
            focusPolicy: Qt.NoFocus
            background: Rectangle { radius: 9; color: popover.current.italic ? popover.tint : italicButton.hovered ? (popover.dark ? "#3a3a3c" : "#eeeeef") : "transparent" }
            contentItem: Text { text: italicButton.text; font: italicButton.font; color: popover.current.italic ? "#007aff" : popover.foreground; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            onClicked: popover.apply("italic", !popover.current.italic)
            Accessible.name: qsTr("倾斜选中文字")
        }
        Item { width: 1; height: 32; Rectangle { anchors.centerIn: parent; width: 1; height: 22; color: popover.dark ? "#35ffffff" : "#1c000000" } }
        ToolButton {
            id: smallerButton
            objectName: "formatSmaller"
            width: 30; height: 32; text: "A−"; font.pixelSize: 13; focusPolicy: Qt.NoFocus
            onClicked: popover.apply("size", Math.max(8, (popover.current.size || 14) - 1))
            contentItem: Text { text: smallerButton.text; font: smallerButton.font; color: popover.foreground; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { radius: 9; color: smallerButton.hovered ? popover.tint : "transparent" }
            Accessible.name: qsTr("减小字号")
        }
        Text { width: 26; height: 32; text: popover.current.size || 14; font.pixelSize: 12; color: popover.foreground; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        ToolButton {
            id: largerButton
            objectName: "formatLarger"
            width: 30; height: 32; text: "A+"; font.pixelSize: 15; focusPolicy: Qt.NoFocus
            onClicked: popover.apply("size", Math.min(72, (popover.current.size || 14) + 1))
            contentItem: Text { text: largerButton.text; font: largerButton.font; color: popover.foreground; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { radius: 9; color: largerButton.hovered ? popover.tint : "transparent" }
            Accessible.name: qsTr("增大字号")
        }
        Item { width: 1; height: 32; Rectangle { anchors.centerIn: parent; width: 1; height: 22; color: popover.dark ? "#35ffffff" : "#1c000000" } }
        Repeater {
            model: ["#1e468c", "#1d1d1f", "#e5484d", "#e69819", "#30a46c", "#8e4ec6"]
            delegate: ToolButton {
                id: swatch
                required property string modelData
                required property int index
                readonly property color swatchColor: modelData
                objectName: "formatColor" + index
                width: 26; height: 32; focusPolicy: Qt.NoFocus
                background: Rectangle { radius: 9; color: swatch.hovered ? popover.tint : "transparent" }
                contentItem: Item {
                    Rectangle { anchors.centerIn: parent; width: 20; height: 20; radius: 10; color: "transparent"; border.width: popover.selectedColor === swatch.swatchColor ? 1.5 : 0; border.color: swatch.modelData }
                    Rectangle { anchors.centerIn: parent; width: 14; height: 14; radius: 7; color: swatch.modelData }
                }
                onClicked: popover.apply("color", modelData)
                Accessible.name: qsTr("文字颜色 %1").arg(modelData)
            }
        }
    }
}
