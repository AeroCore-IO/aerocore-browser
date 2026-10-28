import QtQuick
import "base"

PageBase {
    id: openPage
    pageName: "open"
    firstFocusItem: websiteTextField

    Rectangle {
        id: openPageRectangle
        width: 1000
        height: 100
        anchors.centerIn: parent
        color: "#1AFFFFFF"
        radius: 10

        Image {
            id: earthImage
            width: 58
            height: 58
            anchors.left: parent.left
            anchors.leftMargin: 40
            anchors.verticalCenter: parent.verticalCenter
            source: AppUtils.img("earth.svg")
            fillMode: Image.PreserveAspectFit
        }

        TextFieldBase {
            id: websiteTextField
            latinOnly: true
            anchors.left: earthImage.right
            anchors.leftMargin: 30
            anchors.right: seperator.left
            anchors.rightMargin: 30
            height: parent.height
            color: "transparent"
            leftPadding: 3

            placeholderText: qsTr("Enter a URL")
            placeholderColor: "#99FFFFFF"
            placeholderFont.pixelSize: 28
            placeholderFont.weight: 400
            font.pixelSize: 28
            font.weight: 400
            fontColor: "#FFFFFF"

            focusInterface.opName: "openPageWebsiteEdit"

            onNavigationRightRequested: function() {
                goButton.forceActiveFocus()
            }
        }

        Rectangle {
            id: seperator
            width: 2
            height: 50
            anchors.right: parent.right
            anchors.rightMargin: 104
            anchors.verticalCenter: parent.verticalCenter
            color: "#1AFFFFFF"
        }

        ImageButton {
            id: goButton
            width: 60
            height: 60
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            normalImage: AppUtils.img("go.svg")
            navigateLeft: websiteTextField

            property alias focusInterface: focusInterfaceGo
            FocusInterface {
                id: focusInterfaceGo
                opName: "openPageWebsiteEdit"

                clickHandler: function() {
                    goButton.clicked()
                }
            }

            onClicked: {
                var url = websiteTextField.text.trim()

                if (url.length === 0) {
                    AppUtils.showMessage(false, qsTr("URL cannot be empty"))
                    return
                }

                if (!isValidUrl(url)) {
                    url = autoCompleteUrl(url)
                }

                if (!isValidUrl(url)) {
                    AppUtils.showMessage(false, qsTr("Enter a valid URL"))
                    return
                }

                AppUtils.openUrl(url, 1)
                websiteTextField.text = ""
            }
        }
    }

    function isValidUrl(url) {
        var urlPattern = /^(https?:\/\/)?([\da-z\.-]+)\.([a-z\.]{2,6})([\/\w \.-]*)*\/?$/
        return urlPattern.test(url)
    }

    function autoCompleteUrl(url) {
        if (!url.startsWith('http://') && !url.startsWith('https://')) {
            url = 'https://' + url
        }

        if (!url.includes('.') && !url.startsWith('http')) {
            url = 'www.' + url + '.com'
        }

        return url
    }
}
