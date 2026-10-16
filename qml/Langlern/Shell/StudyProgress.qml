import QtQuick
import QtQuick.Controls.Basic

ProgressBar {
    id: control
    required property Theme theme
    implicitHeight: 5
    padding: 0
    background: Rectangle { color: control.theme.fill; radius: 3; implicitHeight: 5 }
    contentItem: Item {
        Rectangle { width: control.visualPosition * parent.width; height: parent.height; radius: 3; color: control.theme.accent }
    }
}
