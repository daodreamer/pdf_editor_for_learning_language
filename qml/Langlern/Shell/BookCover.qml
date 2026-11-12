import QtQuick
import QtQuick.Shapes

Rectangle {
    id: cover
    property string title: ""
    property int variant: 0
    // Muted, similar-lightness hues so a shelf reads calm rather than colourful.
    color: ["#3f6b7a", "#8a6a4f", "#5d6b8a", "#4e7a62", "#7a5a6e", "#6b7a4e"][variant % 6]
    radius: Math.min(18, width * 0.11)
    implicitWidth: 132; implicitHeight: 176
    // One soft flowing line, the shelf's only ornament.
    Shape {
        x: 0; y: parent.height * 0.14; width: parent.width; height: parent.height * 0.24
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "#59ffffff"; strokeWidth: 2; fillColor: "transparent"; capStyle: ShapePath.RoundCap
            startX: -4; startY: cover.height * 0.12
            PathCubic { x: cover.width * 0.5; y: cover.height * 0.12; control1X: cover.width * 0.16; control1Y: -cover.height * 0.02; control2X: cover.width * 0.34; control2Y: -cover.height * 0.02 }
            PathCubic { x: cover.width + 4; y: cover.height * 0.12; control1X: cover.width * 0.66; control1Y: cover.height * 0.26; control2X: cover.width * 0.84; control2Y: cover.height * 0.26 }
        }
    }
    Text {
        x: Math.round(parent.width * 0.12); width: parent.width - 2 * x
        anchors.bottom: parent.bottom; anchors.bottomMargin: Math.round(parent.height * 0.1)
        text: cover.title; color: "white"; font.pixelSize: Math.max(13, Math.min(17, cover.width / 8)); font.weight: Font.DemiBold
        lineHeight: 1.15; wrapMode: Text.Wrap; maximumLineCount: 4; elide: Text.ElideRight; textFormat: Text.PlainText
    }
    Accessible.ignored: true
}
