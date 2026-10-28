import QtQuick 2.15
import QtQuick.Window 2.15

Item {
    id: joystickCursor

    property string cursorImage: ""

    property int cursorSize: 32

    property bool visible_: false

    property int autoHideDelay: 0

    property real cursorX: 0
    property real cursorY: 0

    property real uiScale: 1.0

    readonly property bool svgReady: cursorImageDisplay.status === 1 /* Image.Ready */
    Image {
        id: cursorImageDisplay
        width: joystickCursor.cursorSize
        height: joystickCursor.cursorSize
        x: joystickCursor.cursorX
        y: joystickCursor.cursorY
        source: joystickCursor.cursorImage
        sourceSize: Qt.size(60, 60)
        fillMode: Image.PreserveAspectFit
        asynchronous: false
        cache: true
        visible: joystickCursor.visible_ && joystickCursor.svgReady
        opacity: visible ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation {
                duration: 200
                easing.type: Easing.InOutQuad
            }
        }
    }

    Canvas {
        id: fallbackCursor
        width: joystickCursor.cursorSize
        height: joystickCursor.cursorSize
        x: joystickCursor.cursorX
        y: joystickCursor.cursorY
        visible: joystickCursor.visible_ && !joystickCursor.svgReady
        opacity: visible ? 1.0 : 0.0
        renderTarget: Canvas.FramebufferObject
        renderStrategy: Canvas.Cooperative

        onPaint: {
            var ctx = getContext("2d")
            var s = width / 60.0
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            ctx.scale(s, s)
            ctx.beginPath()
            ctx.moveTo(6.94922, 1.20117)
            ctx.bezierCurveTo(8.38545, 0.694341, 9.83325, 1.19708, 11.3496, 2.09375)
            ctx.bezierCurveTo(12.8797, 2.99857, 14.7561, 4.47602, 17.1504, 6.36035)
            ctx.lineTo(17.1494, 6.36035)
            ctx.lineTo(26.4893, 13.6963)
            ctx.bezierCurveTo(29.5614, 16.1089, 31.9449, 17.9793, 33.4912, 19.54)
            ctx.bezierCurveTo(34.9915, 21.0544, 36.0216, 22.5822, 35.6875, 24.2832)
            ctx.bezierCurveTo(35.6122, 24.6673, 35.4902, 25.0411, 35.3252, 25.3955)
            ctx.lineTo(35.3252, 25.3965)
            ctx.bezierCurveTo(34.5907, 26.9662, 32.8574, 27.5894, 30.7539, 27.9248)
            ctx.bezierCurveTo(29.1279, 28.184, 27.0191, 28.3063, 24.4199, 28.4072)
            ctx.lineTo(21.6572, 28.5059)
            ctx.bezierCurveTo(21.3177, 28.5172, 20.9833, 28.5976, 20.6758, 28.7422)
            ctx.bezierCurveTo(20.4452, 28.8506, 20.2329, 28.9932, 20.0459, 29.165)
            ctx.lineTo(19.8672, 29.3467)
            ctx.lineTo(18.2578, 31.1631)
            ctx.lineTo(18.2568, 31.1631)
            ctx.bezierCurveTo(15.7626, 33.9715, 13.8268, 36.1531, 12.2295, 37.5498)
            ctx.bezierCurveTo(10.6753, 38.9088, 9.13195, 39.8097, 7.4834, 39.4014)
            ctx.bezierCurveTo(7.10615, 39.3085, 6.741, 39.1714, 6.39648, 38.9912)
            ctx.lineTo(6.39453, 38.9902)
            ctx.bezierCurveTo(4.89242, 38.2005, 4.33521, 36.5015, 4.07129, 34.4521)
            ctx.bezierCurveTo(3.80016, 32.3467, 3.79369, 29.4301, 3.78418, 25.6729)
            ctx.lineTo(3.75195, 12.8994)
            ctx.lineTo(3.75195, 12.8984)
            ctx.bezierCurveTo(3.74563, 9.84909, 3.73851, 7.45814, 3.96875, 5.69434)
            ctx.bezierCurveTo(4.19683, 3.94723, 4.6925, 2.49485, 5.97852, 1.67773)
            ctx.lineTo(5.97852, 1.67871)
            ctx.bezierCurveTo(6.28144, 1.48509, 6.60513, 1.32494, 6.94336, 1.20312)
            ctx.lineTo(6.94922, 1.20117)
            ctx.closePath()
            ctx.fillStyle = "#FFFFFF"
            ctx.strokeStyle = "#333333"
            ctx.lineWidth = 2
            ctx.fill()
            ctx.stroke()
        }

        Component.onCompleted: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()

        Behavior on opacity {
            NumberAnimation {
                duration: 200
                easing.type: Easing.InOutQuad
            }
        }
    }

    Timer {
        id: hideTimer
        interval: Math.max(0, joystickCursor.autoHideDelay)
        repeat: false
        onTriggered: hideCursor()
    }

    function showCursor() {
        if (!visible_) {
            visible_ = true
        }
        if (autoHideDelay > 0) {
            hideTimer.restart()
        } else {
            hideTimer.stop()
        }
    }

    function hideCursor() {
        if (visible_) {
            visible_ = false
        }
        hideTimer.stop()
    }

    function changeCursorPos(globalX, globalY) {
        var win = Window.window
        var designX = globalX
        var designY = globalY
        if (win && win.contentItem) {
            var windowPos = win.contentItem.mapFromGlobal(globalX, globalY)
            designX = windowPos.x / Math.max(joystickCursor.uiScale, 0.01)
            designY = windowPos.y / Math.max(joystickCursor.uiScale, 0.01)
        } else {
            var localPos = joystickCursor.mapFromGlobal(globalX, globalY)
            designX = localPos.x
            designY = localPos.y
        }

        joystickCursor.cursorX = designX
        joystickCursor.cursorY = designY
        showCursor()
    }
}
