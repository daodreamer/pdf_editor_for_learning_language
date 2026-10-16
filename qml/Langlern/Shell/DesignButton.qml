import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control
    required property Theme theme
    property bool primary: false
    property bool selected: false
    property string glyph: ""
    readonly property var textIcons: ({"＋": "plus", "+": "plus", "−": "minus", "‹": "previous", "›": "next", "↶": "undo", "↷": "redo", "⚑": "bookmark", "♪": "audio", "···": "more"})
    readonly property string iconSymbol: textIcons[text] || ({"▦": "library", "◷": "recent", "✎": "pen", "‹": "previous", "＋": "plus"})[glyph] || ""
    readonly property string labelText: textIcons[text] ? "" : text
    readonly property color foreground: !enabled ? theme.faint : primary ? "white" : selected ? theme.accent : theme.text
    implicitHeight: 32
    implicitWidth: Math.max(32, label.implicitWidth + (iconSymbol.length ? 26 : 0) + 24)
    padding: 8
    font.pixelSize: 13
    activeFocusOnTab: true
    contentItem: Row {
        spacing: control.labelText.length && control.iconSymbol.length ? 6 : 0
        LineIcon { visible: control.iconSymbol.length > 0; symbol: control.iconSymbol; ink: control.foreground; anchors.verticalCenter: parent.verticalCenter }
        Text {
            id: label
            text: control.labelText
            width: Math.max(0, control.availableWidth - (control.iconSymbol.length ? 26 : 0))
            font: control.font; color: control.foreground
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: control.glyph.length ? Text.AlignLeft : Text.AlignHCenter
            elide: Text.ElideRight
        }
    }
    background: Rectangle {
        radius: 8
        color: control.primary ? control.theme.accent : control.selected ? control.theme.tint : control.hovered || control.down ? control.theme.fill : "transparent"
        border.width: control.visualFocus ? 2 : 0
        border.color: control.theme.accent
    }
    Accessible.name: text
    ToolTip.visible: hovered && text.length > 0
    ToolTip.delay: 700
    ToolTip.text: text
}
