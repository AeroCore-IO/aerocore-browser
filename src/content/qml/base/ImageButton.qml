import QtQuick
import QtQuick.Controls

Item {
    id: imageButton

    width: 48
    height: 48

    property bool checked: false

    property bool hovered: false

    property string normalImage: ""

    property string checkedImage: ""

    property string hoverImage: ""

    property string focusImage: ""

    property real normalOpacity: 0.3

    property real notNormalOpacity: 1.0

    property var navigateUp: null
    property var navigateDown: null
    property var navigateLeft: null
    property var navigateRight: null

    signal clicked()

    // direction： Qt.UpArrow Qt.DownArrow Qt.LeftArrow Qt.RightArrow
    signal navigationRequested(int direction)

    function getCurrentImage() {
        if (checked) {
            if (checkedImage != "") {
                return checkedImage
            } else {
                return normalImage
            }
        }

        if (activeFocus) {
            if (focusImage != "") {
                return focusImage
            } else if (checkedImage != "") {
                return checkedImage
            } else {
                return normalImage
            }
        }

        if (hovered) {
            if (hoverImage != "") {
                return hoverImage
            } else if (checkedImage != "") {
                return checkedImage
            } else {
                return normalImage
            }
        }

        return normalImage
    }

    function getCurrentOpacity() {
        if (checked || activeFocus || hovered) {
            if (getCurrentImage() !== normalImage) {
                return 1
            } else {
                return notNormalOpacity
            }
        }

        return normalOpacity
    }

    function navigateTo(target, direction) {
        if (target) {
            target.forceActiveFocus()
        } else {
            navigationRequested(direction)
        }
    }

    Image {
        id: buttonImage
        anchors.fill: parent
        source: imageButton.getCurrentImage()
        fillMode: Image.PreserveAspectFit
        opacity: imageButton.getCurrentOpacity()
    }


    PointHandler {
        enabled: imageButton.enabled
        acceptedDevices: PointerDevice.AllDevices
        acceptedButtons: Qt.LeftButton
        grabPermissions: PointerHandler.TakeOverForbidden

        onPointChanged: function(event) {
            if (active) {
                imageButton.forceActiveFocus()
                imageButton.clicked()
            }
        }
    }    

    Keys.onUpPressed: function(event) {
        navigateTo(navigateUp, Qt.UpArrow)
        event.accepted = true
    }
    Keys.onDownPressed: function(event) {
        navigateTo(navigateDown, Qt.DownArrow)
        event.accepted = true
    }
    Keys.onLeftPressed: function(event) {
        navigateTo(navigateLeft, Qt.LeftArrow)
        event.accepted = true
    }
    Keys.onRightPressed: function(event) {
        navigateTo(navigateRight, Qt.RightArrow)
        event.accepted = true
    }

    Keys.onReturnPressed: function(event) {
        imageButton.clicked()
    }
    Keys.onEnterPressed: function(event) {
        imageButton.clicked()
    }
    Keys.onSpacePressed: function(event) {
        imageButton.clicked()
    }
}