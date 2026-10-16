import QtQuick

Rectangle {
    id: cover
    property string title: ""
    property int variant: 0
    color: ["#33567d", "#a36643", "#97576c", "#457864", "#467b8b", "#865b8f"][variant % 6]
    radius: 7
    implicitWidth: 132; implicitHeight: 176
    Rectangle { x: 0; y: 0; width: 7; height: parent.height; color: "#30000000"; radius: 3 }
    Text { x: 15; y: 15; text: "PDF · WORKBOOK"; color: "#ddffffff"; font.pixelSize: 10; font.letterSpacing: 1 }
    Text { x: 15; width: parent.width - 28; anchors.bottom: parent.bottom; anchors.bottomMargin: 20; text: cover.title; color: "white"; font.pixelSize: 17; font.weight: Font.Bold; wrapMode: Text.Wrap; maximumLineCount: 5; elide: Text.ElideRight; textFormat: Text.PlainText }
    Accessible.ignored: true
}
