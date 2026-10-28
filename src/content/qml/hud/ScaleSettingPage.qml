import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Effects
import "../base"

Item {
    id: scaleSettingPage
    width: 546
    height: 111
    anchors.top: parent.top
    anchors.topMargin: 20
    anchors.horizontalCenter: parent.horizontalCenter

    property double defaultZoomFactor: 1.2
    property double minZoomFactor: 0.8
    property double maxZoomFactor: 3.0

    property var browserContainer: null

    ShaderEffectSource {
        id: bgSource
        anchors.fill: parent
        sourceItem: browserContainer
        sourceRect: Qt.rect(scaleSettingPage.x, scaleSettingPage.y, scaleSettingPage.width, scaleSettingPage.height)
        live: false
        visible: false
    }

    Rectangle {
        anchors.fill: parent
        radius: 10
        layer.enabled: true
        layer.effect: MultiEffect {
            source: bgSource
            blurEnabled: true
            blurMax: 30
            blur: 1.0
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 10
        color: "#E6666666"
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        font.weight: 600
        font.pixelSize: 24
        color: "#F5FBFE"
        text: qsTr("Zoom level")
    }

    Rectangle {
        width: 120
        height: 70
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 146
        color: "#4D666666"
        radius: 10

        Text {
            anchors.centerIn: parent
            font.weight: 500
            font.pixelSize: 34
            color: "#F5FBFE"
            text: "−"
        }
    }

    Rectangle {
        width: 120
        height: 70
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 150
        color: "#4D666666"
        radius: 10

        Text {
            anchors.centerIn: parent
            font.weight: 500
            font.pixelSize: 34
            color: "#F5FBFE"
            text: Math.round(defaultZoomFactor*100).toString()+"%"
        }
    }

    Rectangle {
        width: 120
        height: 70
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 20
        color: "#4D666666"
        radius: 10

        Text {
            anchors.centerIn: parent
            font.weight: 500
            font.pixelSize: 46
            color: "#F5FBFE"
            text: "+"
        }
    }

    Timer {
        id: bgUpdateTimer
        interval: 500
        repeat: true
        onTriggered: bgSource.scheduleUpdate()
    }

    onVisibleChanged: {
        if (visible) {
            defaultZoomFactor = cppSettings.defaultZoomFactor

            bgSource.scheduleUpdate()
            bgUpdateTimer.start()
        } else {
            bgUpdateTimer.stop()
        }
    }

    function addZoomFactor(value) {
        var newZoomFactor =  defaultZoomFactor + value
        if (newZoomFactor >= minZoomFactor && newZoomFactor <= maxZoomFactor) {
            cppSettings.defaultZoomFactor = newZoomFactor
            if (browserContainer) {
                browserContainer.adjustPageZoom(value)
            }
        }

        newZoomFactor = Math.min(Math.max(newZoomFactor, minZoomFactor), maxZoomFactor)
        defaultZoomFactor = newZoomFactor
    }
}