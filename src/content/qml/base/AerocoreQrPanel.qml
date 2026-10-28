import QtQuick
import QtQuick.Controls
import AppUtils

Rectangle {
    id: qrPanel
    anchors.fill: parent
    color: "#9903040C"
    visible: false

    property var lastFocusedItem: null

    function close() {
        visible = false
    }

    onVisibleChanged: {
        if (visible) {
            Qt.callLater(function() {
                lastFocusedItem = closeButton
                closeButton.forceActiveFocus()
            })
        } else {
            lastFocusedItem = null
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: qrPanel.close()
    }

    Rectangle {
        id: dialogCard
        width: 680
        height: content.implicitHeight
        radius: 32
        color: "#2C2C2C"
        anchors.centerIn: parent
        clip: true

        MouseArea {
            anchors.fill: parent
            onClicked: {}
        }

        Column {
            id: content
            width: parent.width
            z: 1

            Item { width: 1; height: 56 }

            Rectangle {
                width: 280
                height: 280
                radius: 24
                color: "white"
                anchors.horizontalCenter: parent.horizontalCenter
                clip: true

                Image {
                    anchors.fill: parent
                    anchors.margins: 12
                    source: AppUtils.img("aerocore-wechat-qr.png")
                    fillMode: Image.PreserveAspectFit
                    asynchronous: false
                }
            }

            Item { width: 1; height: 28 }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Scan with WeChat to follow AeroCore")
                font.weight: 600
                font.pixelSize: 32
                color: "white"
            }

            Item { width: 1; height: 20 }

            Text {
                width: parent.width - 80
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: qsTr("From handhelds to TVs, from gaming to entertainment. AeroCore understands players. Follow us for product news, updates, and more.")
                font.weight: 400
                font.pixelSize: 24
                lineHeight: 1.15
                color: "#E8E8E8"
            }

            Item { width: 1; height: 36 }

            Item {
                width: parent.width
                height: 80

                TextButton {
                    id: closeButton
                    width: parent.width - 80
                    height: 80
                    anchors.horizontalCenter: parent.horizontalCenter
                    radius: 16
                    normalBgColor: "#00A3A3"
                    focusBgColor: "#00C4C4"
                    text: qsTr("Close")
                    textColor: "white"
                    textFont.pixelSize: 32
                    textFont.weight: 700
                    focusInterface.opName: "aerocoreQrPanel"

                    onClicked: qrPanel.close()
                }
            }

            Item { width: 1; height: 40 }
        }
    }

    Component.onCompleted: {
        var win = Window.window
        if (win) {
            win.activeFocusItemChanged.connect(function() {
                if (!qrPanel.visible || !lastFocusedItem) return
                if (!isPanelChild(win.activeFocusItem)) {
                    lastFocusedItem.forceActiveFocus()
                }
            })
        }
    }

    function isPanelChild(item) {
        var current = item
        while (current) {
            if (current === qrPanel) return true
            current = current.parent
        }
        return false
    }
}
