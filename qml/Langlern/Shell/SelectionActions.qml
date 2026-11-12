pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Popup {
    id: actions
    required property Theme theme
    required property OfflineServices services
    required property TextSelection selection
    required property bool active
    property point anchorPoint: Qt.point(0, 0)
    signal lookupRequested()
    parent: Overlay.overlay
    objectName: "pageSelectionActions"
    visible: active && selection.ready
    focus: false
    modal: false
    closePolicy: Popup.NoAutoClose
    padding: 4
    width: buttons.implicitWidth + 10
    height: 40
    x: Math.max(8, Math.min((parent ? parent.width : 800) - width - 8, anchorPoint.x - width / 2))
    y: Math.max(8, Math.min((parent ? parent.height : 600) - height - 8, anchorPoint.y + 16))
    background: Rectangle {
        readonly property bool blocksInkInput: true; radius: height / 2; color: actions.theme.card; border.color: actions.theme.line
        Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: 3; anchors.bottomMargin: -3; radius: parent.radius; color: actions.theme.shadow }
    }
    component IconAction: ToolButton {
        id: action
        required property string symbol
        required property string tip
        implicitWidth: 32; implicitHeight: 32; padding: 6
        focusPolicy: Qt.NoFocus
        contentItem: LineIcon { symbol: action.symbol; size: 18; ink: actions.theme.text }
        background: Rectangle { radius: height / 2; color: action.hovered || action.down ? actions.theme.fill : "transparent" }
        Accessible.name: tip
        Loader {
            active: action.hovered && (action.tip).length > 0
            sourceComponent: HoverTip { parent: action; theme: actions.theme; text: action.tip; delay: 400; visible: true }
        }
    }
    contentItem: Row {
        id: buttons
        spacing: 2
        // Lookup first: it is the most frequent action on a single selected word.
        IconAction {
            objectName: "lookupPageSelection"
            symbol: "book"; tip: qsTr("查词")
            onClicked: actions.lookupRequested()
        }
        IconAction {
            objectName: "speakPageSelection"
            symbol: "speak"; tip: qsTr("朗读选中文字")
            onClicked: actions.services.speak(actions.selection.text)
        }
        IconAction {
            objectName: "copyPageSelection"
            symbol: "copy"; tip: qsTr("复制选中文字")
            onClicked: actions.selection.copy()
        }
    }
}
