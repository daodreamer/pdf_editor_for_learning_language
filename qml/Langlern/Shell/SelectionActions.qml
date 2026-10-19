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
    padding: 6
    width: buttons.implicitWidth + 12
    height: 44
    x: Math.max(8, Math.min((parent ? parent.width : 800) - width - 8, anchorPoint.x - width / 2))
    y: Math.max(8, Math.min((parent ? parent.height : 600) - height - 8, anchorPoint.y + 16))
    background: Rectangle { readonly property bool blocksInkInput: true; radius: 10; color: actions.theme.card; border.color: actions.theme.line }
    contentItem: Row {
        id: buttons
        spacing: 4
        ToolButton {
            objectName: "copyPageSelection"
            text: qsTr("复制"); focusPolicy: Qt.NoFocus
            onClicked: actions.selection.copy()
            Accessible.name: qsTr("复制选中文字")
        }
        ToolButton {
            objectName: "speakPageSelection"
            text: qsTr("朗读"); focusPolicy: Qt.NoFocus
            onClicked: actions.services.speak(actions.selection.text)
            Accessible.name: qsTr("朗读选中文字")
        }
        ToolButton {
            text: qsTr("查词"); focusPolicy: Qt.NoFocus
            onClicked: actions.lookupRequested()
        }
    }
}
