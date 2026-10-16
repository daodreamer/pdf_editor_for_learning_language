pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: page
    required property Theme theme
    required property WorkspaceState workspace
    property bool recentOnly: false
    signal openBook(var book)
    signal importRequested()
    signal reviewRequested()
    readonly property var latest: workspace.books.length ? workspace.books[0] : null
    spacing: 0
    RowLayout {
        Layout.fillWidth: true; Layout.preferredHeight: 56; Layout.leftMargin: 28; Layout.rightMargin: 28; spacing: 12
        Label { text: qsTr("练习册"); font.pixelSize: 20; font.bold: true; color: page.theme.text; Layout.fillWidth: true }
        TextField { id: search; Layout.preferredWidth: Math.min(220, page.width * 0.25); placeholderText: qsTr("搜索练习册"); font.pixelSize: 13; background: Rectangle { radius: 8; color: page.theme.fill } }
        DesignButton { theme: page.theme; text: qsTr("导入 PDF"); glyph: "＋"; primary: true; onClicked: page.importRequested() }
    }
    ScrollView {
        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width; spacing: 28
            Rectangle {
                Layout.fillWidth: true; Layout.margins: 28; Layout.bottomMargin: 0
                implicitHeight: 220; radius: 16; color: page.theme.card; border.color: page.theme.line
                RowLayout {
                    anchors.fill: parent; anchors.margins: 22; spacing: 28
                    BookCover { Layout.preferredWidth: 132; Layout.preferredHeight: 176; title: page.latest ? page.latest.title : "LinguaPDF" }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 8
                        Label { text: page.latest ? qsTr("继续学习") : qsTr("你的练习，从这里开始"); color: page.theme.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                        Label { text: page.latest ? page.latest.title : qsTr("阅读、书写，留住每一点进步"); Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 24; font.bold: true; color: page.theme.text; textFormat: Text.PlainText }
                        Label { text: page.latest ? qsTr("第 %1 页 · 共 %2 页").arg(page.latest.page).arg(page.latest.pages) : qsTr("打开本地 PDF，答案与笔迹自动保存"); color: page.theme.muted }
                        StudyProgress { theme: page.theme; visible: page.latest !== null; Layout.fillWidth: true; Layout.maximumWidth: 420; value: page.latest ? page.latest.page / page.latest.pages : 0 }
                        DesignButton { theme: page.theme; primary: true; text: page.latest ? qsTr("继续阅读 · p.%1").arg(page.latest.page) : qsTr("导入第一本练习册"); onClicked: page.latest ? page.openBook(page.latest) : page.importRequested() }
                    }
                    Rectangle { visible: page.width > 1000; Layout.preferredWidth: 1; Layout.fillHeight: true; color: page.theme.line }
                    ColumnLayout {
                        visible: page.width > 1000; Layout.preferredWidth: 180; spacing: 6
                        Label { text: qsTr("今日待复习"); color: page.theme.muted }
                        Label { text: qsTr("%1 个生词").arg(page.workspace.dueCount); font.pixelSize: 30; font.bold: true; color: page.theme.text }
                        DesignButton { theme: page.theme; text: qsTr("开始复习"); selected: true; onClicked: page.reviewRequested() }
                    }
                }
            }
            Label { Layout.leftMargin: 28; text: page.recentOnly ? qsTr("最近阅读") : qsTr("全部练习册"); font.pixelSize: 15; font.weight: Font.DemiBold; color: page.theme.text }
            GridLayout {
                Layout.fillWidth: true; Layout.leftMargin: 28; Layout.rightMargin: 28
                columns: Math.min(6, Math.max(2, Math.floor((page.width - 56) / 170))); columnSpacing: 26; rowSpacing: 24
                Repeater {
                    model: page.workspace.books
                    ColumnLayout {
                        id: book
                        required property var modelData
                        required property int index
                        visible: (!page.recentOnly || index < 6) && modelData.title.toLowerCase().includes(search.text.toLowerCase())
                        Layout.preferredWidth: 140; Layout.maximumWidth: 180; Layout.fillWidth: true; spacing: 8
                        BookCover {
                            Layout.fillWidth: true; Layout.preferredHeight: width * 4 / 3
                            title: book.modelData.title; variant: book.index
                            TapHandler { onTapped: page.openBook(book.modelData) }
                            Accessible.role: Accessible.Button; Accessible.name: title
                        }
                        Label { text: book.modelData.title; Layout.fillWidth: true; elide: Text.ElideRight; font.weight: Font.DemiBold; color: page.theme.text; textFormat: Text.PlainText }
                        RowLayout {
                            Layout.fillWidth: true
                            StudyProgress { theme: page.theme; Layout.fillWidth: true; value: book.modelData.page / book.modelData.pages }
                            Label { text: book.modelData.page + "/" + book.modelData.pages; color: page.theme.muted; font.pixelSize: 11 }
                        }
                        DesignButton { theme: page.theme; text: qsTr("从书架移除"); font.pixelSize: 11; onClicked: page.workspace.forget(book.modelData.url) }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.margins: 28; implicitHeight: 90; radius: 14; color: "transparent"; border.color: page.theme.line
                RowLayout {
                    anchors.fill: parent; anchors.margins: 22; spacing: 16
                    DesignButton { theme: page.theme; text: "＋"; selected: true; onClicked: page.importRequested() }
                    ColumnLayout {
                        Label { text: qsTr("拖入扫描 PDF，或点击选择文件"); font.pixelSize: 14; color: page.theme.text }
                        Label { text: qsTr("本地阅读、填写答案与手写标记"); color: page.theme.muted; font.pixelSize: 12 }
                    }
                    Item { Layout.fillWidth: true }
                }
                TapHandler { onTapped: page.importRequested() }
            }
        }
    }
}
