import QtQuick

QtObject {
    required property bool dark
    property bool paper: false
    readonly property color canvas: dark ? "#151517" : paper ? "#e4ddd0" : "#e8e8eb"
    readonly property color surface: dark ? "#1f1f21" : paper ? "#fbf9f4" : "#ffffff"
    readonly property color sidebar: dark ? "#28282a" : paper ? "#f1ece2" : "#f2f2f4"
    readonly property color bar: dark ? "#2b2b2d" : paper ? "#f6f2ea" : "#f7f7f8"
    readonly property color card: dark ? "#2c2c2e" : paper ? "#fffdf8" : "#ffffff"
    readonly property color text: dark ? "#f5f5f7" : paper ? "#2b2620" : "#1d1d1f"
    readonly property color muted: dark ? "#a1a1a6" : paper ? "#7b7163" : "#6e6e73"
    readonly property color faint: dark ? "#6e6e73" : paper ? "#aaa192" : "#a1a1a6"
    readonly property color line: dark ? "#1affffff" : paper ? "#24463214" : "#1c000000"
    readonly property color accent: dark ? "#0a84ff" : paper ? "#c4561f" : "#007aff"
    readonly property color tint: dark ? "#380a84ff" : paper ? "#1fc4561f" : "#1f007aff"
    readonly property color fill: dark ? "#3d767680" : paper ? "#1f786446" : "#1f767680"
}
