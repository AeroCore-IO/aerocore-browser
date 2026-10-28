import QtQuick
import QtQuick.Controls

Rectangle {
    id: textButton

    width: 480
    height: 100

    color: getCurrentBgColor()
    radius: 10

    property bool checked: false

    property bool hovered: false

    property string normalBgColor: "#4D00969A"

    property string checkedBgColor: ""

    property string hoverBgColor: ""

    property string focusBgColor: "#00969A"

    property alias text: buttonText.text

    property alias textFont: buttonText.font

    property alias textColor: buttonText.color

    property var navigateUp: null
    property var navigateDown: null
    property var navigateLeft: null
    property var navigateRight: null

    signal clicked()

    // direction： Qt.UpArrow Qt.DownArrow Qt.LeftArrow Qt.RightArrow
    signal navigationRequested(int direction)

    activeFocusOnTab: true

    function getCurrentBgColor() {
        if (checked) {
            if (checkedBgColor != "") {
                return checkedBgColor
            } else {
                return normalBgColor
            }
        }

        if (activeFocus) {
            if (focusBgColor != "") {
                return focusBgColor
            } else if (checkedBgColor != "") {
                return checkedBgColor
            } else {
                return normalBgColor
            }
        }

        if (hovered) {
            if (hoverBgColor != "") {
                return hoverBgColor
            } else if (focusBgColor != "") {
                return focusBgColor
            } else {
                return normalBgColor
            }
        }

        return normalBgColor
    }

    function navigateTo(target, direction) {
        if (target) {
            target.forceActiveFocus()
        } else {
            navigationRequested(direction)
        }
    }

    Text {
        id: buttonText
        anchors.fill: parent
        anchors.margins: 10
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }


    PointHandler {
        acceptedDevices: PointerDevice.AllDevices
        acceptedButtons: Qt.LeftButton
        grabPermissions: PointerHandler.TakeOverForbidden

        onPointChanged: function(event) {
            if (active) {
                textButton.forceActiveFocus()
                textButton.clicked()
            }
        }
    }

    property alias focusInterface: focusInterface
    FocusInterface {
        id: focusInterface

        clickHandler: function() {
            textButton.clicked()
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
        textButton.clicked()
    }
    Keys.onEnterPressed: function(event) {
        textButton.clicked()
    }
    Keys.onSpacePressed: function(event) {
        textButton.clicked()
    }
}
