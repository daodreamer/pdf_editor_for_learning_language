pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Floating tool capsule. Only the current tool carries a label; the others are
// quiet icons with the name and shortcut in the tooltip.
Rectangle {
    id: tools
    required property Theme theme
    property int current: 0
    property bool vertical: false
    signal picked(int mode)
    implicitWidth: vertical ? 52 : row.implicitWidth + 12
    implicitHeight: vertical ? row.implicitHeight + 12 : 48
    radius: vertical ? width / 2 : height / 2
    color: theme.bar
    border.color: theme.line
    Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: 3; anchors.bottomMargin: -3; radius: parent.radius; color: tools.theme.shadow }
    Grid {
        id: row
        anchors.centerIn: parent
        columns: tools.vertical ? 1 : 6
        spacing: 2
        Repeater {
            model: [ {label: qsTr("自动"), key: "A"}, {label: qsTr("书写"), key: "P"},
                {label: qsTr("打字"), key: "T"}, {label: qsTr("批改"), key: "C"},
                {label: qsTr("点读"), key: "R"}, {label: qsTr("橡皮"), key: "E"} ]
            ToolButton {
                id: button
                required property var modelData
                required property int index
                readonly property bool active: tools.current === index
                readonly property bool labelled: active && !tools.vertical
                width: labelled ? caption.implicitWidth + 50 : (tools.vertical ? 40 : 42)
                height: 40
                padding: 0
                contentItem: Item {
                    LineIcon {
                        x: button.labelled ? 14 : Math.round((parent.width - width) / 2)
                        y: Math.round((parent.height - height) / 2)
                        size: 19
                        symbol: ["auto", "pen", "type", "mark", "read", "erase"][button.index]
                        ink: button.active ? tools.theme.accentInk : tools.theme.text
                    }
                    Text {
                        id: caption
                        visible: button.labelled
                        x: 39; anchors.verticalCenter: parent.verticalCenter
                        text: button.modelData.label
                        color: tools.theme.accentInk
                        font.pixelSize: 13; font.weight: Font.DemiBold
                    }
                }
                background: Rectangle {
                    radius: height / 2
                    color: button.active ? tools.theme.accent : button.hovered ? tools.theme.fill : "transparent"
                }
                onClicked: tools.picked(index)
                Accessible.name: modelData.label
                Loader {
                    active: button.hovered && (button.modelData.label + "  " + button.modelData.key).length > 0
                    sourceComponent: HoverTip { parent: button; theme: tools.theme; text: button.modelData.label + "  " + button.modelData.key; delay: 400; visible: true }
                }
            }
        }
    }
}
