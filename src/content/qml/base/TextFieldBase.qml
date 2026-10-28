import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    property string text: ""
    property string placeholderText: ""
    property alias font: textField.font
    property alias fontColor: textField.color
    property alias placeholderColor: placeholderTextItem.color
    property alias placeholderFont: placeholderTextItem.font
    property alias hasFocus: textField.activeFocus
    property bool echoMode: false

    property bool latinOnly: false

    property alias focusInterface: focusInterface

    property int leftPadding: 0
    property int rightPadding: 0

    property int fontSize: 14
    property string textColor: "#000000"
    property int placeholderFontSize: 14
    property string placeholderTextColor: "#808080"

    signal navigationUpRequested()
    signal navigationDownRequested()
    signal navigationLeftRequested()
    signal navigationRightRequested()

    onTextChanged: {
        if (!latinOnly) {
            return
        }
        var filtered = text.replace(/[^\x20-\x7E]/g, "")
        if (filtered !== text) {
            text = filtered
        }
    }

    function forceActiveFocus() {
        textField.forceActiveFocus()
    }

    TextField {
        id: textField
        height: parent.height
        anchors.left: parent.left
        anchors.leftMargin: root.leftPadding
        anchors.right: parent.right
        anchors.rightMargin: root.rightPadding
        verticalAlignment: Text.AlignVCenter

        text: root.text
        placeholderText: ""
        echoMode: root.echoMode ? TextInput.Password : TextInput.Normal
        inputMethodHints: root.latinOnly
            ? (Qt.ImhLatinOnly | Qt.ImhNoPredictiveText)
            : Qt.ImhNone

        font.pixelSize: root.fontSize
        color: root.textColor

        background: null

        onTextChanged: root.text = text

        Keys.onUpPressed: function(event) {
            root.navigationUpRequested()
            event.accepted = true
        }
        Keys.onDownPressed: function(event) {
            root.navigationDownRequested()
            event.accepted = true
        }
        Keys.onLeftPressed: function(event) {
            if (!cppInputManager.textInputActive || cursorPosition === 0) {
                root.navigationLeftRequested()
                event.accepted = true
            }
        }
        Keys.onRightPressed: function(event) {
            if (!cppInputManager.textInputActive || cursorPosition === text.length) {
                root.navigationRightRequested()
                event.accepted = true
            }
        }
        Keys.onReturnPressed: function(event) {
            event.accepted = true
        }
        Keys.onEnterPressed: function(event) {
            event.accepted = true
        }

        property alias focusInterface: focusInterface
        FocusInterface {
            id: focusInterface

            clickHandler: function() {
                textField.forceActiveFocus()
                AppUtils.showKeyboard(true)
            }
        }
    }

    Text {
        id: placeholderTextItem
        verticalAlignment: Text.AlignVCenter
        height: parent.height
        anchors.left: parent.left
        anchors.leftMargin: root.leftPadding+textField.leftPadding
        anchors.right: parent.right
        anchors.rightMargin: root.rightPadding+textField.rightPadding

        text: root.placeholderText
        font.pixelSize: root.placeholderFontSize
        color: root.placeholderTextColor

        visible: root.text.length === 0 && !textField.activeFocus
        elide: Text.ElideRight
    }
}
