import QtQuick
import QtQuick.Controls.Basic

ToolButton {
    id: control
    property string symbol
    property bool compact: false
    contentItem: Row {
        spacing: control.compact ? 0 : 6
        LineIcon {
            symbol: control.symbol
            ink: control.enabled ? control.palette.buttonText : control.palette.placeholderText
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: !control.compact
            text: control.text
            font: control.font
            color: control.enabled ? control.palette.buttonText : control.palette.placeholderText
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    activeFocusOnTab: true
    Accessible.name: text
    ToolTip.visible: hovered || visualFocus
    ToolTip.delay: 500
    ToolTip.text: text + (action && action.shortcut ? " (" + action.shortcut + ")" : "")
}
