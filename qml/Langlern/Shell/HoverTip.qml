import QtQuick
import QtQuick.Controls.Basic

// Hover hint in the shell's rounded card style. Controls create it through a Loader
// only while hovered, so idle buttons carry no extra popup objects.
ToolTip {
    id: tip
    required property Theme theme
    delay: 600
    margins: 6
    y: -implicitHeight - 6
    topPadding: 5; bottomPadding: 5; leftPadding: 12; rightPadding: 12
    contentItem: Text {
        text: tip.text; font.pixelSize: 12; color: tip.theme.text
        wrapMode: Text.Wrap; textFormat: Text.PlainText
    }
    background: Rectangle {
        implicitHeight: 28
        radius: Math.min(height / 2, 14); color: tip.theme.card; border.color: tip.theme.line
        Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: 2; anchors.bottomMargin: -2; radius: parent.radius; color: tip.theme.shadow }
    }
}
