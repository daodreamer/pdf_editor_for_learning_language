import QtQuick

// "静流" palette: one quiet ground, raised rounded islands, a single pine accent.
// Light = 晨雾, paper = 暖纸, dark = 夜读. Layering is by lightness, not by lines.
QtObject {
    required property bool dark
    property bool paper: false
    // Page ground behind every island.
    readonly property color canvas: dark ? "#121716" : paper ? "#e8e1d4" : "#e9eeeb"
    // Large content panels (library, word book, settings sheets).
    readonly property color surface: dark ? "#1a211f" : paper ? "#f7f3eb" : "#f6f8f6"
    readonly property color sidebar: dark ? "#1a211f" : paper ? "#f7f3eb" : "#f6f8f6"
    // Floating capsules: toolbars, navigation, bottom controls.
    readonly property color bar: dark ? "#1e2624" : paper ? "#f7f3eb" : "#f6f8f6"
    readonly property color card: dark ? "#222b28" : paper ? "#fffdf8" : "#ffffff"
    readonly property color text: dark ? "#e6ede9" : paper ? "#2b2620" : "#1e2a26"
    readonly property color muted: dark ? "#a3b0aa" : paper ? "#6c6254" : "#55645e"
    readonly property color faint: dark ? "#6f7c76" : paper ? "#a0968a" : "#8a9690"
    readonly property color line: dark ? "#17e6ede9" : paper ? "#1a3a2a14" : "#1a1e2a26"
    readonly property color accent: dark ? "#7ccfb7" : paper ? "#a85a30" : "#2c7a68"
    // Accent used as text on the soft tint (darker in light themes for contrast).
    readonly property color accentText: dark ? "#7ccfb7" : paper ? "#8a4320" : "#1f5c4e"
    readonly property color accentInk: dark ? "#0e1f1a" : "#ffffff"
    readonly property color tint: dark ? "#297ccfb7" : paper ? "#f0dccb" : "#dcebe5"
    // Neutral wells: hover, segmented-control track, input fields.
    readonly property color fill: dark ? "#2a3431" : paper ? "#ede6da" : "#e6ece8"
    readonly property color shadow: dark ? "#40000000" : "#0f1e2a26"
    readonly property color danger: dark ? "#ff8a80" : "#b83a30"
}
