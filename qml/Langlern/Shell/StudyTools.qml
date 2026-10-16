pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: tools
    required property Theme theme
    property int current: 0
    property bool vertical: false
    signal picked(int mode)
    implicitWidth: vertical ? 64 : row.implicitWidth + 8
    implicitHeight: vertical ? row.implicitHeight + 8 : 50
    radius: 12
    color: theme.bar
    Grid {
        id: row
        anchors.centerIn: parent
        columns: tools.vertical ? 1 : 6
        spacing: 2
        Repeater {
            model: [ {label: qsTr("自动"), glyph: "✦"}, {label: qsTr("书写"), glyph: "✎"},
                {label: qsTr("打字"), glyph: "Aa"}, {label: qsTr("批改"), glyph: "✓"},
                {label: qsTr("点读"), glyph: "▶"}, {label: qsTr("橡皮"), glyph: "⌫"} ]
            ToolButton {
                id: button
                required property var modelData
                required property int index
                width: 52
                height: 42
                contentItem: Column {
                    spacing: 1
                    LineIcon { visible: button.index !== 2; anchors.horizontalCenter: parent.horizontalCenter; symbol: ["auto", "pen", "", "mark", "read", "erase"][button.index]; ink: tools.current === button.index ? tools.theme.accent : tools.theme.text }
                    Text { visible: button.index === 2; anchors.horizontalCenter: parent.horizontalCenter; text: "Aa"; color: tools.current === button.index ? tools.theme.accent : tools.theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: button.modelData.label; color: tools.current === button.index ? tools.theme.accent : tools.theme.text; font.pixelSize: 10 }
                }
                background: Rectangle { radius: 9; color: tools.current === button.index ? tools.theme.tint : button.hovered ? tools.theme.fill : "transparent" }
                onClicked: tools.picked(index)
                Accessible.name: modelData.label
                ToolTip.visible: hovered
                ToolTip.text: modelData.label
            }
        }
    }
}
