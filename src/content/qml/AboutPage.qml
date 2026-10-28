import QtQuick
import "base"

PageBase {
    pageName: "about"
    firstFocusItem: quitButton

    Item {
        anchors.fill: parent

        Column {
            id: actionList
            width: 600
            anchors.centerIn: parent
            spacing: 24

            Column {
                width: actionList.width
                spacing: 12

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Exit application")
                    color: "white"
                    font.pixelSize: 34
                    font.weight: 600
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Exiting will close the current browsing session")
                    color: "#99FFFFFF"
                    font.pixelSize: 22
                    font.weight: 400
                }
            }

            TextButton {
                id: quitButton
                width: actionList.width
                height: 100
                normalBgColor: "#06646A"
                focusBgColor: "#00969A"
                text: qsTr("Confirm")
                textColor: "white"
                textFont.pixelSize: 34
                textFont.weight: 400
                radius: 10

                onClicked: {
                    AppUtils.emitEvent("exitApp")
                }

            }
        }
    }
}
