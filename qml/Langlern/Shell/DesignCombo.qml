pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Capsule-shaped ComboBox matching the rounded islands; behaviour is the stock Basic one.
ComboBox {
    id: control
    required property Theme theme
    implicitHeight: 38
    font.pixelSize: 13
    leftPadding: 16
    palette.text: theme.text
    palette.buttonText: theme.text
    palette.dark: theme.muted
    background: Rectangle {
        radius: height / 2
        color: control.hovered || control.down ? Qt.darker(control.theme.fill, control.theme.dark ? 0.9 : 1.03) : control.theme.fill
        border.width: control.visualFocus ? 2 : 0
        border.color: control.theme.accent
    }
    contentItem: Text {
        leftPadding: 0; rightPadding: control.indicator.width + 8
        text: control.displayText; font: control.font
        color: control.enabled ? control.theme.text : control.theme.faint
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    indicator: LineIcon {
        x: control.width - width - 14; y: (control.height - height) / 2
        size: 16; symbol: "down"; ink: control.theme.muted
    }
    delegate: ItemDelegate {
        id: entry
        required property int index
        required property var model
        required property var modelData
        width: ListView.view ? ListView.view.width : control.width
        height: 34
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: control.textRole ? (Array.isArray(control.model) ? entry.modelData[control.textRole] : entry.model[control.textRole]) : entry.modelData
            font: control.font; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
            color: entry.highlighted ? control.theme.accentText : control.theme.text
        }
        background: Rectangle { radius: 10; color: entry.highlighted ? control.theme.tint : "transparent" }
    }
    popup.padding: 6
    popup.background: Rectangle {
        radius: 16; color: control.theme.card; border.color: control.theme.line
        Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: -4; radius: parent.radius; color: control.theme.shadow }
    }
}
