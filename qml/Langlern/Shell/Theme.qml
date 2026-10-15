import QtQuick

QtObject {
    required property bool dark
    readonly property color canvas: dark ? "#14181f" : "#f3f5f8"
    readonly property color surface: dark ? "#1d232d" : "#ffffff"
    readonly property color text: dark ? "#edf1f7" : "#202b3b"
    readonly property color muted: dark ? "#a6b2c4" : "#637186"
    readonly property color line: dark ? "#364152" : "#dce2eb"
    readonly property color accent: dark ? "#92b9ff" : "#285db0"
}
