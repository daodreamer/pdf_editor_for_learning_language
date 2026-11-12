pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes

ColumnLayout {
    id: page
    objectName: "libraryPage"
    required property Theme theme
    required property WorkspaceState workspace
    property bool recentOnly: false
    signal openBook(var book)
    signal importRequested()
    signal reviewRequested()
    readonly property var latest: workspace.books.length ? workspace.books[0] : null
    spacing: 0
    RowLayout {
        Layout.fillWidth: true; Layout.preferredHeight: 76; Layout.leftMargin: 32; Layout.rightMargin: 28; spacing: 12
        Label { text: qsTr("练习册"); font.pixelSize: 24; font.weight: Font.Bold; color: page.theme.text; Layout.fillWidth: true }
        TextField {
            id: search
            Layout.preferredWidth: Math.min(240, page.width * 0.28); implicitHeight: 40
            placeholderText: qsTr("搜索练习册"); font.pixelSize: 13
            leftPadding: 40; rightPadding: 16
            color: page.theme.text; placeholderTextColor: page.theme.muted
            background: Rectangle {
                radius: height / 2; color: page.theme.fill
                border.width: search.activeFocus ? 2 : 0; border.color: page.theme.accent
                LineIcon { x: 14; anchors.verticalCenter: parent.verticalCenter; size: 16; symbol: "search"; ink: page.theme.muted }
            }
        }
        DesignButton { theme: page.theme; text: qsTr("导入 PDF"); glyph: "＋"; primary: true; implicitHeight: 40; onClicked: page.importRequested() }
    }
    ScrollView {
        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width; spacing: 26
            // Continue-learning band: the shelf's only tinted surface, with a few faint flowing lines.
            Rectangle {
                id: band
                Layout.fillWidth: true; Layout.leftMargin: 28; Layout.rightMargin: 28; Layout.topMargin: 4
                implicitHeight: 214; radius: 28; color: page.theme.tint
                Shape {
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        strokeColor: Qt.rgba(page.theme.accent.r, page.theme.accent.g, page.theme.accent.b, 0.14); strokeWidth: 2; fillColor: "transparent"
                        startX: 0; startY: page.waveY(0.66)
                        PathCubic { x: page.waveX(0.5); y: page.waveY(0.66); control1X: page.waveX(0.16); control1Y: page.waveY(0.38); control2X: page.waveX(0.3); control2Y: page.waveY(0.94) }
                        PathCubic { x: page.waveX(1); y: page.waveY(0.7); control1X: page.waveX(0.7); control1Y: page.waveY(0.38); control2X: page.waveX(0.86); control2Y: page.waveY(0.94) }
                    }
                    ShapePath {
                        strokeColor: Qt.rgba(page.theme.accent.r, page.theme.accent.g, page.theme.accent.b, 0.09); strokeWidth: 2; fillColor: "transparent"
                        startX: 0; startY: page.waveY(0.8)
                        PathCubic { x: page.waveX(0.5); y: page.waveY(0.8); control1X: page.waveX(0.18); control1Y: page.waveY(0.52); control2X: page.waveX(0.32); control2Y: page.waveY(1.08) }
                        PathCubic { x: page.waveX(1); y: page.waveY(0.84); control1X: page.waveX(0.72); control1Y: page.waveY(0.52); control2X: page.waveX(0.88); control2Y: page.waveY(1.08) }
                    }
                }
                RowLayout {
                    id: hero
                    anchors.fill: parent; anchors.margins: 26; spacing: 30
                    BookCover { Layout.preferredWidth: 122; Layout.preferredHeight: 162; title: page.latest ? page.latest.title : "LinguaPDF" }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 8
                        Label { text: page.latest ? qsTr("继续学习") : qsTr("你的练习，从这里开始"); color: page.theme.accentText; font.pixelSize: 12; font.weight: Font.DemiBold }
                        Label { text: page.latest ? page.latest.title : qsTr("阅读、书写，留住每一点进步"); Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 24; font.weight: Font.Bold; color: page.theme.text; textFormat: Text.PlainText }
                        Label { text: page.latest ? qsTr("第 %1 页 · 共 %2 页").arg(page.latest.page).arg(page.latest.pages) : qsTr("打开本地 PDF，答案与笔迹自动保存"); color: page.theme.muted }
                        RowLayout {
                            Layout.topMargin: 8; spacing: 16
                            DesignButton { theme: page.theme; primary: true; implicitHeight: 42; text: page.latest ? qsTr("继续阅读 · p.%1").arg(page.latest.page) : qsTr("导入第一本练习册"); onClicked: page.latest ? page.openBook(page.latest) : page.importRequested() }
                            StudyProgress { theme: page.theme; visible: page.latest !== null; Layout.preferredWidth: 200; Layout.maximumWidth: 200; value: page.latest ? page.latest.page / page.latest.pages : 0 }
                        }
                    }
                    Rectangle {
                        visible: page.width > 1000
                        Layout.preferredWidth: 210; Layout.preferredHeight: reviewColumn.implicitHeight + 40
                        radius: 22; color: page.theme.surface
                        ColumnLayout {
                            id: reviewColumn
                            anchors.fill: parent; anchors.margins: 20; spacing: 6
                            Label { text: qsTr("今日待复习"); color: page.theme.muted; font.pixelSize: 12 }
                            Label { text: qsTr("%1 个生词").arg(page.workspace.dueCount); font.pixelSize: 26; font.weight: Font.Bold; color: page.theme.text }
                            DesignButton { theme: page.theme; text: qsTr("开始复习"); selected: true; Layout.fillWidth: true; Layout.topMargin: 6; onClicked: page.reviewRequested() }
                        }
                    }
                }
            }
            Label { Layout.leftMargin: 32; text: page.recentOnly ? qsTr("最近阅读") : qsTr("全部练习册"); font.pixelSize: 16; font.weight: Font.DemiBold; color: page.theme.text }
            GridLayout {
                Layout.fillWidth: true; Layout.leftMargin: 32; Layout.rightMargin: 32; Layout.bottomMargin: 32
                columns: Math.min(6, Math.max(2, Math.floor((page.width - 64) / 170))); columnSpacing: 24; rowSpacing: 26
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
                            HoverHandler { id: coverHover; cursorShape: Qt.PointingHandCursor }
                            DesignButton {
                                theme: page.theme; text: qsTr("从书架移除"); symbol: "close"; iconOnly: true; implicitHeight: 30
                                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 8
                                visible: coverHover.hovered || hovered || visualFocus
                                background: Rectangle { radius: height / 2; color: "#e6ffffff" }
                                onClicked: page.workspace.forget(book.modelData.url)
                            }
                            Accessible.role: Accessible.Button; Accessible.name: title
                        }
                        Label { text: book.modelData.title; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 13; font.weight: Font.DemiBold; color: page.theme.text; textFormat: Text.PlainText }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            StudyProgress { theme: page.theme; Layout.fillWidth: true; value: book.modelData.page / book.modelData.pages }
                            Label { text: book.modelData.page + " / " + book.modelData.pages; color: page.theme.muted; font.pixelSize: 11 }
                        }
                    }
                }
                AbstractButton {
                    id: importTile
                    Layout.preferredWidth: 140; Layout.maximumWidth: 180; Layout.fillWidth: true
                    Layout.preferredHeight: width * 4 / 3; Layout.alignment: Qt.AlignTop
                    hoverEnabled: true
                    background: Rectangle { radius: Math.min(18, width * 0.11); color: importTile.hovered ? page.theme.tint : page.theme.fill }
                    contentItem: ColumnLayout {
                        spacing: 10
                        Item { Layout.fillHeight: true }
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter; implicitWidth: 44; implicitHeight: 44; radius: 22; color: page.theme.card
                            LineIcon { anchors.centerIn: parent; symbol: "plus"; ink: page.theme.accentText }
                        }
                        Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("拖入或选择 PDF"); font.pixelSize: 13; font.weight: Font.DemiBold; color: page.theme.accentText }
                        Label { Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: importTile.width - 20; text: qsTr("原文件保持不变"); font.pixelSize: 11; color: page.theme.muted; elide: Text.ElideRight }
                        Item { Layout.fillHeight: true }
                    }
                    onClicked: page.importRequested()
                    Accessible.name: qsTr("拖入或选择 PDF")
                }
            }
        }
    }
    function waveX(t: real): real { return t * band.width }
    function waveY(t: real): real { return t * band.height }
}
