import QtQuick
import QtQuick.Shapes

// Line icons from Lucide v0.460.0 (ISC, see third_party/notices/Lucide-ISC.txt),
// converted to path data so they stay vector and recolourable per theme. The curve
// renderer antialiases strokes analytically; the default geometry renderer does not.
Shape {
    id: icon
    required property string symbol
    required property color ink
    property real size: 20
    // Fixed box: Shape would otherwise report each path's own bounds as its
    // implicit size, so icons of different shapes would not line up.
    width: size
    height: size
    implicitWidth: size
    implicitHeight: size
    preferredRendererType: Shape.CurveRenderer
    readonly property var paths: ({
        "library": "M4 3H9A1 1 0 0 1 10 4V9A1 1 0 0 1 9 10H4A1 1 0 0 1 3 9V4A1 1 0 0 1 4 3Z M15 3H20A1 1 0 0 1 21 4V9A1 1 0 0 1 20 10H15A1 1 0 0 1 14 9V4A1 1 0 0 1 15 3Z M15 14H20A1 1 0 0 1 21 15V20A1 1 0 0 1 20 21H15A1 1 0 0 1 14 20V15A1 1 0 0 1 15 14Z M4 14H9A1 1 0 0 1 10 15V20A1 1 0 0 1 9 21H4A1 1 0 0 1 3 20V15A1 1 0 0 1 4 14Z", // layout-grid
        "recent": "M2 12a10 10 0 1 0 20 0a10 10 0 1 0 -20 0 M12 6 L12 12 L16 14", // clock
        "plus": "M5 12h14 M12 5v14", // plus
        "minus": "M5 12h14", // minus
        "bookmark": "M19 21l-7-4-7 4V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2v16z", // bookmark
        "audio": "M9 18V5l12-2v13 M3 18a3 3 0 1 0 6 0a3 3 0 1 0 -6 0 M15 16a3 3 0 1 0 6 0a3 3 0 1 0 -6 0", // music
        "more": "M11 12a1 1 0 1 0 2 0a1 1 0 1 0 -2 0 M18 12a1 1 0 1 0 2 0a1 1 0 1 0 -2 0 M4 12a1 1 0 1 0 2 0a1 1 0 1 0 -2 0", // ellipsis
        "auto": "M9.937 15.5A2 2 0 0 0 8.5 14.063l-6.135-1.582a.5.5 0 0 1 0-.962L8.5 9.936A2 2 0 0 0 9.937 8.5l1.582-6.135a.5.5 0 0 1 .963 0L14.063 8.5A2 2 0 0 0 15.5 9.937l6.135 1.581a.5.5 0 0 1 0 .964L15.5 14.063a2 2 0 0 0-1.437 1.437l-1.582 6.135a.5.5 0 0 1-.963 0z M20 3v4 M22 5h-4 M4 17v2 M5 18H3", // sparkles
        "pen": "M12 20h9 M16.376 3.622a1 1 0 0 1 3.002 3.002L7.368 18.635a2 2 0 0 1-.855.506l-2.872.838a.5.5 0 0 1-.62-.62l.838-2.872a2 2 0 0 1 .506-.854z", // pen-line
        "type": "M4 7 L4 4 L20 4 L20 7 M9 20L15 20 M12 4L12 20", // type
        "mark": "M20 6 9 17l-5-5", // check
        "read": "M2 10v3 M6 6v11 M10 3v18 M14 8v7 M18 5v13 M22 10v3", // audio-lines
        "erase": "M7 21l-4.3-4.3c-1-1-1-2.5 0-3.4l9.6-9.6c1-1 2.5-1 3.4 0l5.6 5.6c1 1 1 2.5 0 3.4L13 21 M22 21H7 M5 11l9 9", // eraser
        "close": "M18 6 6 18 M6 6l12 12", // x
        "copy": "M10 8H20A2 2 0 0 1 22 10V20A2 2 0 0 1 20 22H10A2 2 0 0 1 8 20V10A2 2 0 0 1 10 8Z M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2", // copy
        "delete": "M3 6h18 M19 6v14c0 1-1 2-2 2H7c-1 0-2-1-2-2V6 M8 6V4c0-1 1-2 2-2h4c1 0 2 1 2 2v2 M10 11L10 17 M14 11L14 17", // trash-2
        "export": "M4 12v8a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-8 M16 6 L12 2 L8 6 M12 2L12 15", // share
        "help": "M2 12a10 10 0 1 0 20 0a10 10 0 1 0 -20 0 M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3 M12 17h.01", // circle-help
        "next": "M9 18l6-6-6-6", // chevron-right
        "open": "M6 14l1.5-2.9A2 2 0 0 1 9.24 10H20a2 2 0 0 1 1.94 2.5l-1.54 6a2 2 0 0 1-1.95 1.5H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 2 0 0 0 1.67.9H18a2 2 0 0 1 2 2v2", // folder-open
        "previous": "M15 18l-6-6 6-6", // chevron-left
        "redo": "M15 14l5-5-5-5 M20 9H9.5A5.5 5.5 0 0 0 4 14.5A5.5 5.5 0 0 0 9.5 20H13", // redo-2
        "save": "M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7 M7 3v4a1 1 0 0 0 1 1h7", // save
        "theme": "M12 3a6 6 0 0 0 9 9 9 9 0 1 1-9-9Z", // moon
        "undo": "M9 14 4 9l5-5 M4 9h10.5a5.5 5.5 0 0 1 5.5 5.5a5.5 5.5 0 0 1-5.5 5.5H11", // undo-2
        "book": "M12 7v14 M3 18a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h5a4 4 0 0 1 4 4a4 4 0 0 1 4-4h5a1 1 0 0 1 1 1v13a1 1 0 0 1-1 1h-6a3 3 0 0 0-3 3a3 3 0 0 0-3-3z", // book-open
        "external": "M15 3h6v6 M10 14 21 3 M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6", // external-link
        "speak": "M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 11 19.298z M16 9a5 5 0 0 1 0 6 M19.364 18.364a9 9 0 0 0 0-12.728", // volume-2
        "star": "M11.525 2.295a.53.53 0 0 1 .95 0l2.31 4.679a2.123 2.123 0 0 0 1.595 1.16l5.166.756a.53.53 0 0 1 .294.904l-3.736 3.638a2.123 2.123 0 0 0-.611 1.878l.882 5.14a.53.53 0 0 1-.771.56l-4.618-2.428a2.122 2.122 0 0 0-1.973 0L6.396 21.01a.53.53 0 0 1-.77-.56l.881-5.139a2.122 2.122 0 0 0-.611-1.879L2.16 9.795a.53.53 0 0 1 .294-.906l5.165-.755a2.122 2.122 0 0 0 1.597-1.16z", // star
    })
    ShapePath {
        scale: Qt.size(icon.size / 24, icon.size / 24)
        strokeColor: icon.ink
        strokeWidth: 1.6 // device-independent px; Lucide is ~1.67 at 20 px, slightly lighter reads closer to SF Symbols
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        PathSvg { path: icon.paths[icon.symbol] || "" }
    }
}
