pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: page
    objectName: "importPage"
    required property Theme theme
    required property ShellState shell
    required property OfflineServices services
    property bool deshadow: true
    property bool straighten: true
    property bool clarity: true
    property bool crop: true
    readonly property var options: ({deshadow: deshadow, straighten: straighten, clarity: clarity, crop: crop})
    signal readRequested()
    function preview(): void { if (shell.pageCount > 0) services.previewScan(shell.pageImage, options) }
    onOptionsChanged: previewTimer.restart()
    Component.onCompleted: previewTimer.start()
    Connections { target: page.shell; function onImageChanged(): void { previewTimer.restart() } }
    Timer { id: previewTimer; interval: 200; onTriggered: page.preview() }
    FileDialog {
        id: optimizedFile
        title: qsTr("保存优化后的新 PDF")
        fileMode: FileDialog.SaveFile; defaultSuffix: "pdf"; nameFilters: [qsTr("PDF (*.pdf)")]
        onAccepted: page.shell.optimizeScan(selectedFile, page.options)
    }
    signal chooseRequested()
    signal cancelRequested()
    spacing: 0
    Rectangle {
        Layout.fillWidth: true; Layout.preferredHeight: 56; color: page.theme.bar
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 24; anchors.rightMargin: 24
            Label { text: qsTr("扫描优化"); font.pixelSize: 20; font.bold: true; color: page.theme.text; Layout.fillWidth: true }
            DesignButton { theme: page.theme; text: qsTr("取消"); onClicked: page.cancelRequested() }
            DesignButton { objectName: "openButton"; theme: page.theme; text: page.shell.pageCount > 0 ? qsTr("返回阅读") : qsTr("选择 PDF"); primary: true; enabled: !page.shell.scanning; onClicked: page.shell.pageCount > 0 ? page.readRequested() : page.chooseRequested() }
        }
    }
    RowLayout {
        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; color: page.theme.canvas
            Item {
                visible: page.shell.pageCount > 0
                anchors.fill: parent; anchors.margins: 32
                PageItem { readonly property real ratio: compareOriginal.pressed ? page.shell.pageRatio : page.services.previewRatio; anchors.centerIn: parent; width: Math.min(parent.width, parent.height / ratio); height: width * ratio; image: compareOriginal.pressed ? page.shell.pageImage : page.services.preview }
                DesignButton { id: compareOriginal; anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; theme: page.theme; text: qsTr("按住查看原图") }
            }
            ColumnLayout {
                visible: page.shell.pageCount === 0
                anchors.centerIn: parent; width: Math.min(440, parent.width - 48); spacing: 20
                BookCover { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 210; Layout.preferredHeight: 280; title: "PDF\nWorkbook" }
                Label { text: qsTr("把教材变成你的练习空间"); font.pixelSize: 24; font.bold: true; color: page.theme.text; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
                Label { text: qsTr("拖入本地 PDF，或点击右上角选择文件。\n原始文件始终保留，练习进度独立保存。"); color: page.theme.muted; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; lineHeight: 1.6 }
                Label { visible: page.shell.busy; text: page.shell.statusText; color: page.theme.accent; Layout.alignment: Qt.AlignHCenter }
            }
        }
        Rectangle {
            Layout.preferredWidth: page.width > 900 ? 320 : 250; Layout.fillHeight: true; color: page.theme.surface
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 24; spacing: 18
                Label { text: qsTr("扫描优化"); font.pixelSize: 16; font.bold: true; color: page.theme.text }
                Label { text: qsTr("仅在拍摄或扫描的 PDF 需要去阴影、拉直或增强时使用。普通打开直接阅读原版，优化需手动另存为新 PDF。"); color: page.theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 12 }
                DesignSwitch { theme: page.theme; text: qsTr("去除阴影"); checked: page.deshadow; onToggled: page.deshadow = checked }
                DesignSwitch { theme: page.theme; text: qsTr("自动拉直（±3°）"); checked: page.straighten; onToggled: page.straighten = checked }
                DesignSwitch { theme: page.theme; text: qsTr("增强清晰度"); checked: page.clarity; onToggled: page.clarity = checked }
                DesignSwitch { theme: page.theme; text: qsTr("裁掉黑边"); checked: page.crop; onToggled: page.crop = checked }
                Label { text: qsTr("预览显示当前页。优化将逐页生成新的图像 PDF（最高 150 DPI），文字层与练习标注不会带入新文件；原始 PDF 与练习数据保留。拉直不包含曲面展开。"); color: page.theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 12 }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: page.theme.line }
                Label { text: qsTr("文字识别"); font.pixelSize: 16; font.bold: true; color: page.theme.text }
                ComboBox { id: languages; Layout.fillWidth: true; model: page.services.languages; Accessible.name: qsTr("识别语言") }
                DesignButton { theme: page.theme; text: qsTr("识别当前预览页"); enabled: page.shell.pageCount > 0 && !page.services.busy && languages.count > 0; onClicked: page.services.recognize(page.shell.pageImage, String(page.shell.selectedUrl), page.shell.currentPage, languages.currentText) }
                Label { text: page.services.status; wrapMode: Text.Wrap; color: page.theme.muted; Layout.fillWidth: true; textFormat: Text.PlainText }
                Label { text: page.shell.exportStatus; wrapMode: Text.Wrap; color: page.theme.muted; Layout.fillWidth: true; textFormat: Text.PlainText }
                DesignButton { theme: page.theme; text: page.shell.scanning ? qsTr("取消优化") : qsTr("优化全部页并另存 PDF…"); selected: true; enabled: page.shell.storageReady; Layout.fillWidth: true; onClicked: page.shell.scanning ? page.shell.cancelScan() : optimizedFile.open() }
                DesignButton { theme: page.theme; text: qsTr("重新选择 PDF…"); enabled: !page.shell.scanning; Layout.fillWidth: true; onClicked: page.chooseRequested() }
                Item { Layout.fillHeight: true }
                Label { text: qsTr("完全离线 · 无需账号"); color: page.theme.faint; font.pixelSize: 12 }
            }
        }
    }
}
