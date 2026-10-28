import QtQml

QtObject {
    property string opName: ""

    property var clickHandler: () => {}

    property var backHandler: () => {}

    property var menuHandler: () => {}

    property var buttonClicked: function (buttonName) {
        if (buttonName === "A") {
            clickHandler()
        } else if (buttonName === "Start") {
            menuHandler()
        }
    }
}
