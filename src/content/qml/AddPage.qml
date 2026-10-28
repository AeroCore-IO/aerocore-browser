import QtQuick
import "base"

PageBase {
    pageName: "add"
    firstFocusItem: nameTextField

    Item {
        id: addPageItem
        anchors.fill: parent        

        Text {
            anchors.left: inputArea.left
            anchors.top: parent.top
            anchors.topMargin: 298
            text: qsTr("Add to library")
            font.pixelSize: 34
            font.weight: 600
            color: "white"
        }

        Text {
            anchors.left: inputArea.left
            anchors.top: parent.top
            anchors.topMargin: 356
            text: qsTr("Add the current page to your library for quick access later")
            font.pixelSize: 22
            font.weight: 400
            color: "#99FFFFFF"
        }

        Item {
            id: inputArea
            width: 1000
            height: 380
            anchors.top: parent.top
            anchors.topMargin: 407
            anchors.horizontalCenter: parent.horizontalCenter

            TextFieldBase {
                id: nameTextField
                width: parent.width
                height: 100
                anchors.top: parent.top
                color: "#1AFFFFFF"
                leftPadding: 40
                radius: 10

                placeholderText: qsTr("Website name")
                placeholderColor: "#99FFFFFF"
                placeholderFont.pixelSize: 28
                placeholderFont.weight: 400
                font.pixelSize: 28
                font.weight: 400
                fontColor: "#FFFFFF"

                focusInterface.opName: "addPageNameEdit"

                onNavigationDownRequested: function() {
                    urlTextField.forceActiveFocus()
                }
            }

            TextFieldBase {
                id: urlTextField
                latinOnly: true
                width: parent.width
                height: 100
                anchors.top: parent.top
                anchors.topMargin: 140
                color: "#1AFFFFFF"
                leftPadding: 40
                radius: 10

                placeholderText: qsTr("Website URL")
                placeholderColor: "#99FFFFFF"
                placeholderFont.pixelSize: 28
                placeholderFont.weight: 400
                font.pixelSize: 28
                font.weight: 400
                fontColor: "#FFFFFF"

                focusInterface.opName: "addPageNameEdit"                

                onNavigationUpRequested: function() {
                    nameTextField.forceActiveFocus()
                }

                onNavigationDownRequested: function() {
                    okButton.forceActiveFocus()
                }
            }            

            TextButton {
                id: okButton
                width: 480
                height: 100
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                normalBgColor: "#4D00969A"
                focusBgColor: "#00969A"
                text: qsTr("Add")
                textColor: "white"
                textFont.pixelSize: 34
                textFont.weight: 600

                navigateRight: cancelButton
                navigateUp: urlTextField

                focusInterface.opName: "addPageNameEdit"

                onClicked: {
                    if (nameTextField.text === "") {
                        AppUtils.showMessage(false, qsTr("Website name cannot be empty"))
                        return
                    }
                    if (urlTextField.text === "") {
                        AppUtils.showMessage(false, qsTr("Website URL cannot be empty"))
                        return
                    }

                    var error = cppMainController.addWebsite(nameTextField.text, urlTextField.text)
                    if (!error) {
                        AppUtils.showMessage(true, qsTr("Website added"))
                        nameTextField.text = ""
                        urlTextField.text = ""
                    } else {
                        AppUtils.showMessage(false, error)
                    }
                }
            }

            TextButton {
                id: cancelButton
                width: 480
                height: 100
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                normalBgColor: "#4D00969A"
                focusBgColor: "#00969A"
                text: qsTr("Reset")
                textColor: "white"
                textFont.pixelSize: 34
                textFont.weight: 600

                navigateUp: urlTextField
                navigateLeft: okButton

                focusInterface.opName: "addPageNameEdit"

                onClicked: {
                    nameTextField.text = ""
                    urlTextField.text = ""
                }
            }
        }
    }
}
