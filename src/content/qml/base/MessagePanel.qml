import QtQuick
import QtQuick.Controls

Rectangle {
    id: messagePanel
    width: flow.implicitWidth + 20 + 48
    height: 100
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 96
    color: "#4DFFFFFF"
    radius: 10
    visible: false

    property string successIcon: ""
    property string errorIcon: ""

    function showMessage(success, content) {
        messageIcon.source = success?successIcon:errorIcon
        messageContent.text = content
        messagePanel.visible = true
        timer.restart()
    }

    Flow {
        id: flow
        height: parent.height
        anchors.left: parent.left
        anchors.leftMargin: 20
        spacing: 20

        Item {
            width: messageIcon.width
            height: parent.height

            Image {
                id: messageIcon
                width: 44
                height: 44
                anchors.verticalCenter: parent.verticalCenter
                fillMode: Image.PreserveAspectFit
            }
        }

        Text {
            id: messageContent
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            font.weight: 400
            font.pixelSize: 28
            color: "white"
        }
    }

    Timer {
        id: timer
        interval: 3000
        repeat: false
        onTriggered: {
            messagePanel.visible = false
        }
    }
}
