import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PhaseZero

ApplicationWindow {
    id: root
    width: 860
    height: 920
    visible: true
    title: qsTr("阶段 0：PDFium / Scene Graph 验证")
    ColumnLayout {
        anchors.fill: parent
        Label {
            Layout.fillWidth: true
            text: qsTr("技术原型 · 通过命令行指定 PDF · 下方仅用于 IME 实验")
        }
        PageItem {
            objectName: "pageItem"
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        TextField {
            id: input
            objectName: "imeInput"
            Layout.fillWidth: true
            Layout.minimumHeight: 40
            font.pixelSize: 18
            focus: true
            placeholderText: qsTr("在此验证中文预编辑、提交与焦点切换")
            onActiveFocusChanged: console.info("ime_focus", input.activeFocus)
            onPreeditTextChanged: console.info("ime_preedit_length", input.preeditText.length)
            onInputMethodComposingChanged: console.info("ime_composing", input.inputMethodComposing)
            onTextEdited: console.info("ime_committed_length", input.text.length)
        }
        Button {
            objectName: "focusTarget"
            text: qsTr("切换焦点（验收）")
        }
    }
}
