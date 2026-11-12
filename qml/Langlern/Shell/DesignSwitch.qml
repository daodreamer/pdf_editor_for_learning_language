import QtQuick
import QtQuick.Controls.Basic

Switch {
    id: control
    required property Theme theme
    spacing: 12
    font.pixelSize: 13
    palette.windowText: theme.text
    indicator: Rectangle {
        implicitWidth: 40; implicitHeight: 24
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: height / 2
        color: control.checked ? control.theme.accent : control.theme.fill
        border.width: control.visualFocus ? 2 : 0
        border.color: control.theme.accentText
        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3; y: 3
            width: 18; height: 18; radius: 9
            color: control.checked ? control.theme.accentInk : control.theme.card
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text; font: control.font; color: control.theme.text
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
}
