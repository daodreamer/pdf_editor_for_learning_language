pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: strip
    required property Theme theme
    required property OfflineServices services
    property real loopA: -1
    property real loopB: -1
    implicitHeight: 68
    radius: 12; color: theme.card; border.color: theme.line
    function stamp(value: real): string { return Math.floor(value / 60) + ":" + String(Math.floor(value % 60)).padStart(2, "0") }
    RowLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 10
        DesignButton { theme: strip.theme; text: strip.services.playing ? "Ⅱ" : "▶"; enabled: strip.services.audioName.length > 0; onClicked: strip.services.toggleAudio(); Accessible.name: qsTr("播放 / 暂停") }
        ColumnLayout {
            Layout.preferredWidth: 180; Layout.maximumWidth: 220; Layout.fillWidth: true
            Label { text: strip.services.audioName.length ? strip.services.audioName : qsTr("本地教材音频"); elide: Text.ElideMiddle; Layout.fillWidth: true; color: strip.theme.text; font.pixelSize: 12; textFormat: Text.PlainText }
            Label { text: strip.stamp(strip.services.position) + " / " + strip.stamp(strip.services.duration); color: strip.theme.muted; font.pixelSize: 11 }
        }
        Slider { Layout.fillWidth: true; from: 0; to: Math.max(1, strip.services.duration); value: strip.services.position; onMoved: strip.services.seek(value); Accessible.name: qsTr("音频进度") }
        ComboBox { model: ["0.75×", "1×", "1.25×", "1.5×", "2×"]; currentIndex: 1; onActivated: strip.services.setRate([.75, 1, 1.25, 1.5, 2][currentIndex]); Accessible.name: qsTr("播放速度") }
        DesignButton {
            theme: strip.theme; text: strip.loopA < 0 ? qsTr("设 A") : strip.loopB < 0 ? qsTr("设 B") : qsTr("清除 A–B"); selected: strip.loopB > strip.loopA && strip.loopA >= 0
            onClicked: {
                if (strip.loopA < 0) strip.loopA = strip.services.position
                else if (strip.loopB < 0) { strip.loopB = strip.services.position; strip.services.setLoop(strip.loopA, strip.loopB) }
                else { strip.loopA = -1; strip.loopB = -1; strip.services.setLoop(-1, -1) }
            }
        }
        DesignButton { theme: strip.theme; text: qsTr("选择音频"); onClicked: files.open() }
    }
    FileDialog { id: files; title: qsTr("选择本地教材音频"); nameFilters: [qsTr("音频 (*.wav *.mp3 *.wma *.mid)"), qsTr("所有文件 (*)")]; onAccepted: { strip.loopA = -1; strip.loopB = -1; strip.services.openAudio(selectedFile) } }
}
