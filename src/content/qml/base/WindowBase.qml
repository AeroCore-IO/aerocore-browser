import QtQuick
import QtQuick.Controls
import QtQuick.Window

Window {
    id: root

    readonly property int baseWidth: 1920
    readonly property int baseHeight: 1080

    property real scaleFactor: Math.min(width / baseWidth, height / baseHeight)

    visibility: Window.FullScreen
    width: Screen.width
    height: Screen.height
    color: "#000000"

    default property alias content: container.data

    Item {
        id: container
        width: root.width / root.scaleFactor
        height: root.height / root.scaleFactor

        transform: Scale {
            origin.x: 0
            origin.y: 0
            xScale: root.scaleFactor
            yScale: root.scaleFactor
        }

        clip: true
    }
}
