import QtQuick
import QtQuick.Shapes

Shape {
    id: icon
    required property string symbol
    required property color ink
    implicitWidth: 20
    implicitHeight: 20
    transform: Scale { xScale: 20 / 24; yScale: 20 / 24 }
    readonly property var paths: {
        "library": "M3 3h7v7H3zM14 3h7v7h-7zM3 14h7v7H3zM14 14h7v7h-7z",
        "recent": "M12 6v6l4 2M21 12a9 9 0 1 0-18 0a9 9 0 1 0 18 0",
        "plus": "M12 5v14M5 12h14",
        "minus": "M5 12h14",
        "bookmark": "M6 3h12v18l-6-4-6 4z",
        "audio": "M9 18V5l11-2v13M9 18a3 2 0 1 1-6 0a3 2 0 1 1 6 0M20 16a3 2 0 1 1-6 0a3 2 0 1 1 6 0",
        "more": "M5 12h.01M12 12h.01M19 12h.01",
        "auto": "M12 2l2.8 7.2L22 12l-7.2 2.8L12 22l-2.8-7.2L2 12l7.2-2.8z",
        "pen": "M14 5l5 5M4 20l5-1L21 7l-5-5L4 14z",
        "mark": "M4 12l5 5L20 5",
        "read": "M7 3l14 9L7 21z",
        "erase": "M8 5h13v14H8L2 12zM11 9l6 6M17 9l-6 6",
        "close": "M6 6l12 12M18 6L6 18",
        "copy": "M16 8V3H3v13h5 M10 8h9q2 0 2 2v9q0 2-2 2h-9q-2 0-2-2v-9q0-2 2-2",
        "delete": "M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M10 10v7M14 10v7",
        "export": "M14 3h7v7M21 3l-10 10M10 3H3v18h18v-7",
        "help": "M9 9a3 3 0 0 1 6 0c0 2-3 2-3 5M12 17h.01 M21 12a9 9 0 1 0-18 0a9 9 0 1 0 18 0",
        "next": "M9 5l7 7-7 7",
        "open": "M3 7h6l2 3h10l-3 10H3z M3 7V4h6l2 3h8v3",
        "previous": "M15 5l-7 7 7 7",
        "redo": "M15 5l6 5-6 5M21 10H11a7 7 0 0 0-7 7v3",
        "save": "M4 3h13l4 4v14H3V3z M7 3v6h9V3M7 21v-8h10v8",
        "theme": "M20 15A9 9 0 0 1 9 4a9 9 0 1 0 11 11z",
        "undo": "M9 5L3 10l6 5M3 10h10a7 7 0 0 1 7 7v3",
    }
    ShapePath {
        strokeColor: icon.ink
        strokeWidth: 1.7
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        PathSvg { path: icon.paths[icon.symbol] || "" }
    }
}
