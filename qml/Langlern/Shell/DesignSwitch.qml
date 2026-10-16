import QtQuick
import QtQuick.Controls.Basic

Switch {
    id: control
    required property Theme theme
    spacing: 10
    font.pixelSize: 13
    palette.windowText: theme.text
    indicator: Rectangle {
        implicitWidth: 36; implicitHeight: 20
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 10
        color: control.checked ? "#34c759" : control.theme.fill
        border.width: control.visualFocus ? 2 : 0
        border.color: control.theme.accent
        Rectangle { x: control.checked ? 18 : 2; y: 2; width: 16; height: 16; radius: 8; color: "white" }
    }
}
