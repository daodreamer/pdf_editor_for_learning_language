import QtQuick
import QtQuick.Controls.Basic

ProgressBar {
    id: control
    required property Theme theme
    implicitHeight: 6
    padding: 0
    background: Rectangle { color: control.theme.fill; radius: height / 2; implicitHeight: 6 }
    contentItem: Item {
        Rectangle {
            // Keep a visible round nub even at the first page.
            width: control.value > 0 ? Math.max(height, control.visualPosition * parent.width) : 0
            height: parent.height; radius: height / 2; color: control.theme.accent
        }
    }
}
