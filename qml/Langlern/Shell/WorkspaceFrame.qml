pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: frame
    objectName: "workspaceFrame"
    required property ShellState shell
    required property Theme theme
    required property WorkspaceState workspace
    required property OfflineServices services
    signal openRequested()
    signal exportRequested()
    signal helpRequested()
    property string screen: "library"
    property string observed: ""
    property real observedSession: -1
    property int resumePage: 1
    property bool recentOnly: false
    property bool readerLoaded: shell.pageCount > 0
    property bool pendingImport: false
    property bool initialZoomSet: false
    color: theme.surface
    function openBook(book: var): void {
        pendingImport = false
        resumePage = book.page || 1
        if (String(shell.selectedUrl) === book.url && shell.pageCount > 0) {
            screen = "reader"
            shell.goToPage(resumePage)
            resumePage = 1
        } else shell.selectFile(book.url)
    }
    Connections {
        target: frame.shell
        function onSelectionChanged(): void {
            if (frame.shell.pageCount > 0 && frame.observedSession !== frame.shell.sessionId) {
                frame.observedSession = frame.shell.sessionId
                frame.readerLoaded = true
                frame.observed = String(frame.shell.selectedUrl)
                frame.services.invalidate()
                frame.workspace.document = frame.observed
                frame.screen = frame.pendingImport ? "import" : "reader"
                frame.pendingImport = false
                if (!frame.initialZoomSet) { frame.initialZoomSet = true; frame.shell.setZoom(0.5) }
                if (frame.resumePage > 1) frame.shell.goToPage(frame.resumePage)
                frame.resumePage = 1
                frame.workspace.remember(frame.shell.selectedUrl, frame.shell.selectedName, frame.shell.pageCount, frame.shell.currentPage)
            }
            if (frame.shell.selectedName.length === 0) { frame.observed = ""; frame.screen = "library" }
        }
    }
    Component.onCompleted: {
        services.english = workspace.english
        if (shell.pageCount > 0) { observed = String(shell.selectedUrl); workspace.document = observed; screen = "reader" }
    }
    DropArea {
        anchors.fill: parent
        onEntered: function(drag) { drag.accepted = drag.hasUrls && drag.urls.length > 0 && !frame.shell.scanning }
        onDropped: function(drop) {
            if (drop.hasUrls && drop.urls.length > 0 && !frame.shell.scanning) {
                frame.pendingImport = true
                frame.screen = "import"
                frame.shell.selectFile(drop.urls[0])
                drop.acceptProposedAction()
            }
        }
    }
    Timer {
        interval: 1200; repeat: true; running: frame.shell.pageCount > 0
        onTriggered: frame.workspace.updateProgress(frame.shell.selectedUrl, frame.shell.pageCount, frame.shell.currentPage)
    }
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.preferredWidth: frame.width < 850 ? 170 : 220
            Layout.fillHeight: true
            visible: frame.screen !== "reader" && frame.screen !== "import"
            color: frame.theme.sidebar
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 4
                Label { text: "LinguaPDF"; font.pixelSize: 21; font.weight: Font.Bold; color: frame.theme.text; Layout.margins: 14 }
                Label { text: qsTr("资料库"); color: frame.theme.faint; font.pixelSize: 11; Layout.leftMargin: 12; Layout.topMargin: 12 }
                DesignButton { theme: frame.theme; text: qsTr("全部练习册 · %1").arg(frame.workspace.books.length); glyph: "▦"; selected: frame.screen === "library" && !frame.recentOnly; Layout.fillWidth: true; onClicked: { frame.recentOnly = false; frame.screen = "library" } }
                DesignButton { theme: frame.theme; text: qsTr("最近阅读"); glyph: "◷"; selected: frame.screen === "library" && frame.recentOnly; Layout.fillWidth: true; onClicked: { frame.recentOnly = true; frame.screen = "library" } }
                DesignButton { theme: frame.theme; text: qsTr("生词本 · %1").arg(frame.workspace.words.length); glyph: "✎"; selected: frame.screen === "words"; Layout.fillWidth: true; onClicked: frame.screen = "words" }
                Item { Layout.fillHeight: true }
                Label { text: qsTr("全部离线 · 保存在本机"); color: frame.theme.faint; font.pixelSize: 11; Layout.leftMargin: 12 }
                DesignButton { theme: frame.theme; text: qsTr("外观与布局"); Layout.fillWidth: true; onClicked: settings.open() }
                DesignButton { theme: frame.theme; text: qsTr("快捷键"); Layout.fillWidth: true; onClicked: frame.helpRequested() }
            }
            Rectangle { width: 1; height: parent.height; anchors.right: parent.right; color: frame.theme.line }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            LibraryPage {
                anchors.fill: parent
                visible: frame.screen === "library"
                theme: frame.theme; workspace: frame.workspace; recentOnly: frame.recentOnly
                onOpenBook: function(book) { frame.openBook(book) }
                onImportRequested: frame.screen = "import"
                onReviewRequested: frame.screen = "words"
            }
            Loader {
                id: readerLoader
                anchors.fill: parent
                active: frame.readerLoaded
                visible: frame.screen === "reader"
                onActiveChanged: if (active && String(source).length === 0) setSource(Qt.resolvedUrl("ReaderPane.qml"), {shell: frame.shell, theme: frame.theme, workspace: frame.workspace, services: frame.services})
                Component.onCompleted: if (active && String(source).length === 0) setSource(Qt.resolvedUrl("ReaderPane.qml"), {shell: frame.shell, theme: frame.theme, workspace: frame.workspace, services: frame.services})
            }
            Connections {
                target: readerLoader.item
                ignoreUnknownSignals: true
                function onLibraryRequested(): void { frame.workspace.updateProgress(frame.shell.selectedUrl, frame.shell.pageCount, frame.shell.currentPage); frame.screen = "library" }
                function onWordsRequested(): void { frame.screen = "words" }
                function onExportRequested(): void { frame.exportRequested() }
                function onSettingsRequested(): void { settings.open() }
                function onOpenRequested(): void { frame.openRequested() }
            }
            Loader {
                id: wordsLoader
                anchors.fill: parent; active: frame.screen === "words"
                onActiveChanged: if (active && String(source).length === 0) setSource(Qt.resolvedUrl("WordBookPage.qml"), {theme: frame.theme, workspace: frame.workspace, services: frame.services})
            }
            Connections { target: wordsLoader.item; ignoreUnknownSignals: true; function onOpenSource(book: var): void { frame.openBook(book) } }
            Loader {
                id: importLoader
                anchors.fill: parent; active: frame.screen === "import"
                onActiveChanged: if (active && String(source).length === 0) setSource(Qt.resolvedUrl("ImportPage.qml"), {theme: frame.theme, shell: frame.shell, services: frame.services})
            }
            Connections {
                target: importLoader.item; ignoreUnknownSignals: true
                function onChooseRequested(): void { frame.pendingImport = true; frame.openRequested() }
                function onCancelRequested(): void { frame.screen = "library" }
                function onReadRequested(): void { frame.screen = "reader" }
            }
        }
    }
    Label {
        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 32, 700); padding: 8
        visible: text.length > 0
        text: frame.workspace.error.length ? frame.workspace.error : frame.shell.errorCode.length ? frame.shell.statusText : ""
        wrapMode: Text.WordWrap; color: "#ff3b30"; textFormat: Text.PlainText
        background: Rectangle { radius: 8; color: frame.theme.card }
    }
    Popup {
        id: settings
        parent: Overlay.overlay; anchors.centerIn: parent
        modal: true; padding: 24; width: 380
        background: Rectangle { radius: 16; color: frame.theme.card; border.color: frame.theme.line }
        contentItem: ColumnLayout {
            spacing: 14
            Label { text: qsTr("外观与布局"); font.pixelSize: 20; font.bold: true; color: frame.theme.text }
            Label { text: qsTr("阅读界面"); color: frame.theme.muted }
            ComboBox { Layout.fillWidth: true; model: [qsTr("侧栏"), qsTr("专注"), qsTr("练习面板")]; currentIndex: frame.workspace.layout; onActivated: frame.workspace.layout = currentIndex }
            Label { text: qsTr("工具栏"); color: frame.theme.muted }
            ComboBox { Layout.fillWidth: true; model: [qsTr("顶部"), qsTr("悬浮"), qsTr("侧边")]; currentIndex: frame.workspace.toolbar; onActivated: frame.workspace.toolbar = currentIndex }
            DesignButton { theme: frame.theme; text: qsTr("切换系统 / 浅色 / 深色"); Layout.fillWidth: true; onClicked: frame.shell.cycleTheme() }
            CheckBox { text: qsTr("纸张色（浅色模式）"); checked: frame.workspace.paper; onToggled: frame.workspace.paper = checked }
            CheckBox { text: "English / 中文"; checked: frame.services.english; onToggled: { frame.workspace.english = checked; frame.services.english = checked } }
            DesignButton { theme: frame.theme; text: qsTr("完成"); primary: true; Layout.alignment: Qt.AlignRight; onClicked: settings.close() }
        }
    }
}
