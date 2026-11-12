pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control
    required property Theme theme
    property bool primary: false
    property bool selected: false
    property string glyph: ""
    // Explicit LineIcon symbol; with iconOnly the text stays as tooltip and accessible name.
    property string symbol: ""
    property bool iconOnly: false
    readonly property var textIcons: ({"＋": "plus", "+": "plus", "−": "minus", "‹": "previous", "›": "next", "↶": "undo", "↷": "redo", "⚑": "bookmark", "♪": "audio", "···": "more"})
    readonly property string iconSymbol: symbol || textIcons[text] || ({"▦": "library", "◷": "recent", "✎": "pen", "‹": "previous", "＋": "plus", "◐": "theme"})[glyph] || ""
    readonly property string labelText: textIcons[text] || iconOnly ? "" : text
    readonly property color foreground: !enabled ? theme.faint : primary ? theme.accentInk : selected ? theme.accentText : theme.text
    implicitHeight: 34
    implicitWidth: labelText.length ? Math.max(34, label.implicitWidth + (iconSymbol.length ? 26 : 0) + 28) : implicitHeight
    padding: labelText.length ? 10 : 7
    font.pixelSize: 13
    font.weight: primary || selected ? Font.DemiBold : Font.Normal
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
        radius: height / 2
        color: control.primary ? (control.down ? Qt.darker(control.theme.accent, 1.12) : control.hovered ? Qt.darker(control.theme.accent, 1.06) : control.theme.accent)
             : control.selected ? control.theme.tint
             : control.hovered || control.down ? control.theme.fill : "transparent"
        opacity: control.primary && !control.enabled ? 0.45 : 1
        border.width: control.visualFocus ? 2 : 0
        border.color: control.theme.accent
    }
    Accessible.name: text
    // Only icon-only or truncated buttons need a tip; it names the action, never the glyph.
    Loader {
        active: control.hovered && (control.Accessible.name).length > 0 && (control.labelText.length === 0 || label.truncated)
        sourceComponent: HoverTip { parent: control; theme: control.theme; text: control.Accessible.name; delay: 600; visible: true }
    }
}
