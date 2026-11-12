pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Compact dictionary card shown next to the selected word: lemma, how the selected form
// relates to it (tense/person/gender), English glosses and, for verbs, the conjugation table.
Popup {
    id: card
    required property Theme theme
    required property OfflineServices services
    property point anchorPoint: Qt.point(width / 2, 0)
    property string context: ""
    property int entryIndex: 0
    property bool showConjugation: false
    property int tenseIndex: 0
    signal addRequested(string word, string meaning, string context)

    readonly property var entries: services.lookupEntries
    readonly property var entry: entries.length > entryIndex ? entries[entryIndex] : null
    readonly property bool french: services.lookupLanguage === "fr"
    readonly property string lemma: entry ? entry.lemma : services.lookupWord
    readonly property string selectedKey: entry && entry.key ? entry.key : services.lookupWord.toLowerCase()
    readonly property var conjugation: entry && entry.conjugation ? entry.conjugation : []
    readonly property url externalUrl: services.externalDictionaryUrl(lemma)

    function show(word: string, sentence: string, point: point): void {
        context = sentence
        anchorPoint = point
        entryIndex = 0
        showConjugation = false
        note.clear()
        services.lookup(word)
        open()
    }
    function pickTense(): void {
        // Open the conjugation on the tense that contains the selected form. Read from entry
        // directly: this runs from onEntryChanged, before the derived bindings refresh.
        tenseIndex = 0
        const groups = entry && entry.conjugation ? entry.conjugation : []
        const key = entry && entry.key ? entry.key : ""
        for (let g = 0; g < groups.length; ++g)
            for (const row of groups[g].rows)
                if (row.form.split(" / ").includes(key)) { tenseIndex = g; return }
    }
    onEntryChanged: pickTense()

    objectName: "lookupCard"
    parent: Overlay.overlay
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 18
    width: Math.min(360, (parent ? parent.width : 400) - 16)
    readonly property real gap: 18
    x: Math.max(8, Math.min((parent ? parent.width : 800) - width - 8, anchorPoint.x - width / 2))
    y: {
        const limit = (parent ? parent.height : 600) - height - 8
        const below = anchorPoint.y + gap
        return Math.max(8, below <= limit ? below : anchorPoint.y - gap - height)
    }
    background: Rectangle {
        readonly property bool blocksInkInput: true
        radius: 22; color: card.theme.card; border.color: card.theme.line
        Rectangle { z: -1; anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: -4; radius: parent.radius; color: card.theme.shadow }
    }

    component IconButton: ToolButton {
        id: iconButton
        required property string symbol
        property string tip
        implicitWidth: 32; implicitHeight: 32; padding: 7
        focusPolicy: Qt.NoFocus
        contentItem: LineIcon { symbol: iconButton.symbol; size: 18; ink: iconButton.enabled ? card.theme.muted : card.theme.faint }
        background: Rectangle { radius: height / 2; color: iconButton.hovered || iconButton.down ? card.theme.fill : "transparent" }
        Accessible.name: tip
        Loader {
            active: iconButton.hovered && (iconButton.tip).length > 0
            sourceComponent: HoverTip { parent: iconButton; theme: card.theme; text: iconButton.tip; delay: 500; visible: true }
        }
    }
    component Chip: AbstractButton {
        id: chip
        property bool active: false
        property string tip: ""
        Loader {
            active: chip.hovered && (chip.tip).length > 0
            sourceComponent: HoverTip { parent: chip; theme: card.theme; text: chip.tip; delay: 500; visible: true }
        }
        implicitHeight: 26
        implicitWidth: chipLabel.implicitWidth + 20
        focusPolicy: Qt.NoFocus
        contentItem: Text {
            id: chipLabel
            text: chip.text; font.pixelSize: 11; color: chip.active ? card.theme.accentText : card.theme.muted
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; textFormat: Text.PlainText
        }
        background: Rectangle { radius: height / 2; color: chip.active ? card.theme.tint : chip.hovered ? card.theme.fill : "transparent"; border.color: chip.active ? "transparent" : card.theme.line }
    }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            spacing: 4
            ColumnLayout {
                Layout.fillWidth: true; spacing: 2
                Row {
                    spacing: 8
                    Text {
                        id: lemmaText
                        objectName: "lookupLemma"
                        text: card.lemma; color: card.theme.text; font.pixelSize: 20; font.weight: Font.DemiBold; textFormat: Text.PlainText
                        width: Math.min(implicitWidth, 200); elide: Text.ElideRight
                    }
                    Text {
                        anchors.baseline: lemmaText.baseline
                        text: card.entry && card.entry.ipa ? card.entry.ipa : card.services.phonetic
                        visible: text.length > 0; color: card.theme.muted; font.pixelSize: 13; textFormat: Text.PlainText
                    }
                }
                Text {
                    text: card.entry ? [card.entry.posLabel, card.entry.aux ? qsTr("助动词 %1").arg(card.entry.aux) : ""].filter(s => s).join(" · ") : ""
                    visible: text.length > 0; color: card.theme.muted; font.pixelSize: 11; textFormat: Text.PlainText
                }
            }
            IconButton { symbol: "speak"; tip: qsTr("朗读"); Layout.alignment: Qt.AlignTop; onClicked: card.services.speak(card.lemma, card.french ? "fr-FR" : "en-US") }
            IconButton {
                objectName: "lookupExternal"
                symbol: "external"; tip: qsTr("在法语助手中查看「%1」").arg(card.lemma)
                visible: card.externalUrl.toString().length > 0; Layout.alignment: Qt.AlignTop
                onClicked: Qt.openUrlExternally(card.externalUrl)
            }
            IconButton { symbol: "close"; tip: qsTr("关闭"); Layout.alignment: Qt.AlignTop; onClicked: card.close() }
        }

        // How the selected form maps to the lemma, e.g. "allions → 直陈式 未完成过去时 · nous".
        Rectangle {
            Layout.fillWidth: true
            visible: card.entry !== null && card.entry.analyses.length > 0
            implicitHeight: analysis.implicitHeight + 14; radius: 12; color: card.theme.tint
            Text {
                id: analysis
                objectName: "lookupAnalysis"
                anchors.fill: parent; anchors.margins: 6; anchors.leftMargin: 10
                text: card.entry ? card.services.lookupWord + "  →  " + card.entry.analyses.join("；") : ""
                wrapMode: Text.Wrap; color: card.theme.text; font.pixelSize: 12; textFormat: Text.PlainText
            }
        }

        Flow {
            Layout.fillWidth: true; spacing: 6
            visible: card.entries.length > 1
            Repeater {
                model: card.entries
                Chip {
                    required property var modelData
                    required property int index
                    text: modelData.lemma + " · " + modelData.posLabel.split(" · ")[0]
                    active: index === card.entryIndex
                    onClicked: { card.entryIndex = index; card.showConjugation = false }
                }
            }
        }

        Row {
            spacing: 16
            visible: card.conjugation.length > 0
            Repeater {
                model: [qsTr("释义"), qsTr("变位")]
                AbstractButton {
                    id: tab
                    required property string modelData
                    required property int index
                    readonly property bool current: card.showConjugation === (index === 1)
                    objectName: index === 1 ? "lookupConjugationTab" : "lookupMeaningTab"
                    implicitWidth: tabLabel.implicitWidth; implicitHeight: 24
                    focusPolicy: Qt.NoFocus
                    onClicked: card.showConjugation = index === 1
                    contentItem: Text { id: tabLabel; text: tab.modelData; font.pixelSize: 13; font.weight: tab.current ? Font.DemiBold : Font.Normal; color: tab.current ? card.theme.text : card.theme.muted }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; radius: 1; color: card.theme.accent; visible: tab.current }
                }
            }
        }

        ScrollView {
            id: body
            Layout.fillWidth: true
            // Tall enough for the tense chips plus all six persons; shrinks with small windows (header+footer ≈ 330 px).
            Layout.preferredHeight: Math.min(contentHeight, Math.max(140, Math.min(340, (card.parent ? card.parent.height : 700) - 330)))
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: body.availableWidth
                spacing: 6
                Text {
                    visible: card.services.lookupBusy || card.entries.length === 0
                    text: card.services.lookupBusy ? qsTr("查询中…") : card.services.meaning
                    Layout.fillWidth: true; wrapMode: Text.Wrap; color: card.theme.muted; font.pixelSize: 13; textFormat: Text.PlainText
                }
                Repeater {
                    model: !card.showConjugation && card.entry ? card.entry.glosses : []
                    Text {
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        text: (card.entry.glosses.length > 1 ? (index + 1) + ".  " : "") + modelData
                        wrapMode: Text.Wrap; color: card.theme.text; font.pixelSize: 13; lineHeight: 1.15; textFormat: Text.PlainText
                    }
                }
                Flow {
                    Layout.fillWidth: true; spacing: 6
                    visible: card.showConjugation
                    Repeater {
                        model: card.showConjugation ? card.conjugation : []
                        Chip {
                            required property var modelData
                            required property int index
                            text: modelData.title
                            active: index === card.tenseIndex
                            onClicked: card.tenseIndex = index
                            tip: modelData.name
                        }
                    }
                }
                // French name of the selected tense, as textbooks print it.
                Text {
                    objectName: "lookupTenseName"
                    visible: card.showConjugation && card.conjugation.length > card.tenseIndex
                    Layout.topMargin: 4
                    text: visible ? card.conjugation[card.tenseIndex].name : ""
                    color: card.theme.muted; font.pixelSize: 12; font.italic: true; textFormat: Text.PlainText
                }
                GridLayout {
                    objectName: "lookupConjugationGrid"
                    visible: card.showConjugation
                    Layout.topMargin: 4
                    columns: 2; columnSpacing: 14; rowSpacing: 4
                    Repeater {
                        model: card.showConjugation && card.conjugation.length > card.tenseIndex ? card.conjugation[card.tenseIndex].rows : []
                        delegate: Item {
                            id: cell
                            required property var modelData
                            readonly property bool hit: modelData.form.split(" / ").includes(card.selectedKey)
                            Layout.columnSpan: 2; Layout.fillWidth: true
                            implicitHeight: 22
                            Text { id: who; width: 86; text: cell.modelData.person; color: card.theme.muted; font.pixelSize: 13; textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter }
                            Text {
                                x: who.width; anchors.verticalCenter: parent.verticalCenter
                                text: cell.modelData.form; textFormat: Text.PlainText; font.pixelSize: 13
                                font.weight: cell.hit ? Font.DemiBold : Font.Normal
                                color: cell.hit ? card.theme.accent : card.theme.text
                            }
                        }
                    }
                }
            }
        }

        TextField {
            id: note
            Layout.fillWidth: true
            placeholderText: qsTr("笔记（可选，替代默认释义）")
            font.pixelSize: 12; color: card.theme.text
            background: Rectangle { radius: 12; color: card.theme.fill }
        }

        RowLayout {
            spacing: 8
            Row {
                objectName: "lookupLanguage"
                Repeater {
                    model: [{ code: "fr", label: "FR" }, { code: "en", label: "EN" }]
                    Chip {
                        required property var modelData
                        text: modelData.label
                        active: card.services.lookupLanguage === modelData.code
                        onClicked: card.services.lookupLanguage = modelData.code
                        Accessible.name: modelData.code === "fr" ? qsTr("查法语词典") : qsTr("查英语词典")
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: card.french ? "Wiktionary · CC BY-SA" : "ECDICT"
                color: card.theme.faint; font.pixelSize: 10; elide: Text.ElideRight
            }
            DesignButton {
                objectName: "lookupAddWord"
                theme: card.theme; primary: true; text: qsTr("加入生词本"); implicitHeight: 30
                enabled: card.lemma.length > 0
                onClicked: {
                    const glosses = card.entry ? card.entry.glosses.slice(0, 4).join("\n") : ""
                    const origin = card.lemma.toLowerCase() !== card.services.lookupWord.toLowerCase() ? "\n（原文：" + card.services.lookupWord + "）" : ""
                    card.addRequested(card.lemma, (note.text.length ? note.text : glosses) + origin, card.context)
                    card.close()
                }
            }
        }
    }
}
