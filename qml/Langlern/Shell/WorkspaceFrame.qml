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
    property InputSourceSwitcher inputSources: null
    function languageName(code: string): string {
        return ({fr: qsTr("法语"), en: qsTr("英语"), de: qsTr("德语"), es: qsTr("西班牙语"), it: qsTr("意大利语"), zh: qsTr("中文")})[code] || code
    }
    signal openRequested()
    signal exportRequested()
    property string screen: "library"
    property string observed: ""
    property real observedSession: -1
    property int resumePage: 1
    property bool recentOnly: false
    property bool readerLoaded: shell.pageCount > 0
    property bool initialZoomSet: false
    color: theme.canvas
    function openBook(book: var): void {
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
                frame.screen = "reader"
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
                frame.shell.selectFile(drop.urls[0])
                drop.acceptProposedAction()
            }
        }
    }
    Timer {
        interval: 1200; repeat: true; running: frame.shell.pageCount > 0
        onTriggered: frame.workspace.updateProgress(frame.shell.selectedUrl, frame.shell.pageCount, frame.shell.currentPage)
    }
    component NavItem: AbstractButton {
        id: nav
        property string symbol
        property string count: ""
        property bool active: false
        Layout.fillWidth: true
        implicitHeight: 42
        leftPadding: 14; rightPadding: 14
        font.pixelSize: 14
        contentItem: RowLayout {
            spacing: 10
            LineIcon { visible: nav.symbol.length > 0; symbol: nav.symbol; size: 18; ink: nav.active ? frame.theme.accentText : frame.theme.text }
            Text { text: nav.text; font.pixelSize: nav.font.pixelSize; font.weight: nav.active ? Font.DemiBold : Font.Normal; color: nav.active ? frame.theme.accentText : nav.symbol.length ? frame.theme.text : frame.theme.muted; Layout.fillWidth: true; elide: Text.ElideRight }
            Text { visible: nav.count.length > 0; text: nav.count; font.pixelSize: 12; color: frame.theme.muted }
        }
        background: Rectangle { radius: height / 2; color: nav.active ? frame.theme.card : nav.hovered ? frame.theme.fill : "transparent" }
        Accessible.name: text
    }
    RowLayout {
        anchors.fill: parent
        spacing: 0
        ColumnLayout {
            Layout.preferredWidth: frame.width < 850 ? 176 : 224
            Layout.maximumWidth: Layout.preferredWidth
            Layout.fillWidth: false
            Layout.fillHeight: true
            Layout.margins: 12; Layout.topMargin: 18; Layout.rightMargin: 4
            visible: frame.screen !== "reader" && frame.screen !== "import"
            spacing: 4
            RowLayout {
                Layout.leftMargin: 10; Layout.bottomMargin: 18; spacing: 10
                Rectangle {
                    implicitWidth: 30; implicitHeight: 30; radius: 10; color: frame.theme.accent
                    LineIcon { anchors.centerIn: parent; size: 20; symbol: "wave"; ink: frame.theme.accentInk }
                }
                Label { text: "LinguaPDF"; font.pixelSize: 18; font.weight: Font.Bold; color: frame.theme.text }
            }
            NavItem { text: qsTr("全部练习册"); symbol: "library"; count: String(frame.workspace.books.length); active: frame.screen === "library" && !frame.recentOnly; onClicked: { frame.recentOnly = false; frame.screen = "library" } }
            NavItem { text: qsTr("最近阅读"); symbol: "recent"; active: frame.screen === "library" && frame.recentOnly; onClicked: { frame.recentOnly = true; frame.screen = "library" } }
            NavItem { text: qsTr("生词本"); symbol: "book"; count: String(frame.workspace.words.length); active: frame.screen === "words"; onClicked: frame.screen = "words" }
            Item { Layout.fillHeight: true }
            NavItem { text: qsTr("外观与布局"); implicitHeight: 38; font.pixelSize: 13; onClicked: settings.open() }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            // Non-reading screens sit on one large rounded sheet above the ground.
            Layout.margins: frame.screen === "reader" ? 0 : 12
            Rectangle { anchors.fill: parent; visible: frame.screen !== "reader"; radius: 26; color: frame.theme.surface }
            LibraryPage {
                anchors.fill: parent
                visible: frame.screen === "library"
                theme: frame.theme; workspace: frame.workspace; recentOnly: frame.recentOnly
                onOpenBook: function(book) { frame.openBook(book) }
                onImportRequested: frame.openRequested()
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
                function onOptimizeRequested(): void { frame.shell.finishTextEditing(); frame.screen = "import" }
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
                function onChooseRequested(): void { frame.openRequested() }
                function onCancelRequested(): void { frame.screen = frame.shell.pageCount > 0 ? "reader" : "library" }
                function onReadRequested(): void { frame.screen = "reader" }
            }
        }
    }
    Label {
        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 32, 700); padding: 8
        visible: text.length > 0
        text: frame.workspace.error.length ? frame.workspace.error : frame.shell.errorCode.length ? frame.shell.statusText : ""
        anchors.bottomMargin: 18; leftPadding: 18; rightPadding: 18; topPadding: 10; bottomPadding: 10
        wrapMode: Text.WordWrap; color: frame.theme.danger; textFormat: Text.PlainText
        background: Rectangle { radius: Math.min(height / 2, 20); color: frame.theme.card; border.color: frame.theme.line }
    }
    // ⌘, on macOS (system Settings convention), Ctrl+, on Windows.
    Shortcut { sequence: "Ctrl+,"; onActivated: settings.open() }
    Popup {
        id: settings
        objectName: "settingsPopup"
        parent: Overlay.overlay; anchors.centerIn: parent
        modal: true; padding: 26; width: Math.min(400, parent ? parent.width - 32 : 400)
        height: Math.min(settingsContent.implicitHeight + 52, parent ? parent.height - 32 : 720)
        background: Rectangle {
            radius: 26; color: frame.theme.card; border.color: frame.theme.line
            Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: -4; radius: parent.radius; color: frame.theme.shadow }
        }
        contentItem: ScrollView {
          id: settingsScroll
          clip: true
          contentWidth: availableWidth
          ColumnLayout {
            id: settingsContent
            width: settingsScroll.availableWidth
            spacing: 14
            Label { text: qsTr("外观与布局"); font.pixelSize: 20; font.weight: Font.Bold; color: frame.theme.text }
            Label { text: qsTr("主题"); color: frame.theme.muted }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 38; radius: height / 2; color: frame.theme.fill
                Row {
                    anchors.fill: parent; anchors.margins: 3
                    Repeater {
                        model: [qsTr("跟随系统"), qsTr("浅色"), qsTr("深色")]
                        AbstractButton {
                            id: themeOption
                            required property int index
                            required property string modelData
                            readonly property bool active: frame.shell.themeMode === index
                            width: parent.width / 3; height: parent.height; text: modelData
                            contentItem: Text { text: themeOption.text; font.pixelSize: 13; font.weight: themeOption.active ? Font.DemiBold : Font.Normal; color: themeOption.active ? frame.theme.accentText : frame.theme.muted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { radius: height / 2; color: themeOption.active ? frame.theme.card : "transparent" }
                            // ShellState only exposes cycling; at most two steps reach any mode.
                            onClicked: { for (let i = 0; i < 3 && frame.shell.themeMode !== index; ++i) frame.shell.cycleTheme() }
                            Accessible.name: text
                        }
                    }
                }
            }
            DesignSwitch { theme: frame.theme; text: qsTr("暖纸色（浅色模式）"); checked: frame.workspace.paper; onToggled: frame.workspace.paper = checked }
            DesignSwitch { theme: frame.theme; text: qsTr("夜读柔光（深色模式柔化页面亮度）"); checked: frame.workspace.nightDim; onToggled: frame.workspace.nightDim = checked }
            Label { text: qsTr("阅读界面"); color: frame.theme.muted; Layout.topMargin: 4 }
            DesignCombo { theme: frame.theme; Layout.fillWidth: true; model: [qsTr("侧栏"), qsTr("专注"), qsTr("练习面板")]; currentIndex: frame.workspace.layout; onActivated: frame.workspace.layout = currentIndex }
            Label { text: qsTr("工具栏"); color: frame.theme.muted }
            DesignCombo { theme: frame.theme; Layout.fillWidth: true; model: [qsTr("顶部"), qsTr("悬浮"), qsTr("侧边")]; currentIndex: frame.workspace.toolbar; onActivated: frame.workspace.toolbar = currentIndex }
            DesignSwitch { theme: frame.theme; text: "English / 中文"; checked: frame.services.english; onToggled: { frame.workspace.english = checked; frame.services.english = checked } }
            Label { visible: inputSection.visible; text: qsTr("输入法"); color: frame.theme.muted; Layout.topMargin: 4 }
            ColumnLayout {
                id: inputSection
                objectName: "inputSourceSettings"
                readonly property var switcher: frame.inputSources
                readonly property string language: switcher ? switcher.language : ""
                visible: switcher !== null && switcher.supported
                Layout.fillWidth: true; spacing: 10
                DesignSwitch {
                    objectName: "inputSwitchingToggle"
                    theme: frame.theme; Layout.fillWidth: true
                    text: qsTr("按文档语言切换系统输入法")
                    checked: frame.workspace.inputSwitching; onToggled: frame.workspace.inputSwitching = checked
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12; color: frame.theme.muted
                    text: inputSection.language.length && frame.shell.pageCount > 0
                        ? qsTr("当前文档以%1为主。使用 LinguaPDF 时切换，切到其他窗口时恢复原输入法。").arg(frame.languageName(inputSection.language))
                        : qsTr("打开文档后自动识别语言。使用 LinguaPDF 时切换，切到其他窗口时恢复原输入法。")
                }
                DesignCombo {
                    id: inputChoice
                    objectName: "inputSourceChoice"
                    theme: frame.theme; Layout.fillWidth: true
                    visible: frame.workspace.inputSwitching && inputSection.language.length > 0
                    // Automatic first, then the sources made for this language, then all others.
                    model: {
                        const all = inputSection.switcher ? inputSection.switcher.sources : []
                        const own = all.filter(s => s.language === inputSection.language)
                        return [{id: "", name: qsTr("自动选择%1输入法").arg(frame.languageName(inputSection.language))}]
                            .concat(own, all.filter(s => s.language !== inputSection.language))
                    }
                    textRole: "name"; valueRole: "id"
                    currentIndex: Math.max(0, model.findIndex(s => s.id === (frame.workspace.inputChoices[inputSection.language] || "")))
                    onActivated: frame.workspace.setInputChoice(inputSection.language, currentValue)
                    Accessible.name: qsTr("文档语言使用的输入法")
                }
                RowLayout {
                    visible: frame.workspace.inputSwitching && inputSection.switcher !== null && inputSection.switcher.missing
                    Layout.fillWidth: true; spacing: 10
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12; color: frame.theme.danger; text: qsTr("系统尚未安装%1输入法。").arg(frame.languageName(inputSection.language)) }
                    DesignButton { theme: frame.theme; text: qsTr("打开系统设置"); onClicked: inputSection.switcher.openSystemSettings() }
                }
            }
            DesignButton { theme: frame.theme; text: qsTr("完成"); primary: true; Layout.alignment: Qt.AlignRight; onClicked: settings.close() }
          }
        }
    }
    // Once per language and run: the document language has no keyboard on this system.
    property var inputNoticesShown: ({})
    readonly property bool inputNoticeWanted: inputSources !== null && inputSources.missing && screen === "reader"
        && !workspace.inputNoticesOff.includes(inputSources.language)
    onInputNoticeWantedChanged: if (inputNoticeWanted && !inputNoticesShown[inputSources.language]) inputNoticeTimer.restart()
    // Let the first page appear before the notice covers it.
    Timer {
        id: inputNoticeTimer
        interval: 800
        onTriggered: {
            if (!frame.inputNoticeWanted || frame.inputNoticesShown[frame.inputSources.language]) return
            frame.inputNoticesShown[frame.inputSources.language] = true
            inputNotice.language = frame.inputSources.language
            inputNoticeSilence.checked = false
            inputNotice.open()
        }
    }
    Popup {
        id: inputNotice
        objectName: "inputSourceNotice"
        property string language: ""
        parent: Overlay.overlay; anchors.centerIn: parent
        modal: true; padding: 24; width: Math.min(400, parent ? parent.width - 32 : 400)
        background: Rectangle {
            readonly property bool blocksInkInput: true
            radius: 24; color: frame.theme.card; border.color: frame.theme.line
            Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: -4; radius: parent.radius; color: frame.theme.shadow }
        }
        onClosed: if (inputNoticeSilence.checked) frame.workspace.silenceInputNotice(language)
        contentItem: ColumnLayout {
            spacing: 14
            Label { text: qsTr("未找到%1输入法").arg(frame.languageName(inputNotice.language)); font.pixelSize: 18; font.weight: Font.Bold; color: frame.theme.text }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap; color: frame.theme.muted
                text: qsTr("这份文档以%1为主，但系统中还没有%1键盘或输入法。在系统设置中添加后回到 LinguaPDF，输入法会自动切换。").arg(frame.languageName(inputNotice.language))
            }
            DesignSwitch { id: inputNoticeSilence; objectName: "inputNoticeSilence"; theme: frame.theme; text: qsTr("不再提示"); Layout.fillWidth: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight; spacing: 10
                DesignButton { theme: frame.theme; text: qsTr("打开系统设置"); onClicked: { frame.inputSources.openSystemSettings(); inputNotice.close() } }
                DesignButton { objectName: "inputNoticeClose"; theme: frame.theme; text: qsTr("知道了"); primary: true; onClicked: inputNotice.close() }
            }
        }
    }
}
