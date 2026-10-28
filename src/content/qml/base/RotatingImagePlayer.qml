import QtQuick

Item {
    id: root

    property string backgroundSource: ""
    property string foregroundSource: ""
    property int imageSize: 100
    property int rotationDuration: 2000
    property bool running: visible

    width: imageSize
    height: imageSize

    Image {
        anchors.fill: parent
        source: root.backgroundSource
        fillMode: Image.PreserveAspectFit
    }

    Image {
        anchors.fill: parent
        source: root.foregroundSource
        fillMode: Image.PreserveAspectFit

        RotationAnimation on rotation {
            from: 0
            to: 360
            duration: root.rotationDuration
            loops: Animation.Infinite
            running: root.running
        }
    }

    function start() { running = true }
    function stop() { running = false }
}