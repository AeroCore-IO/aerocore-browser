import QtQuick
import QtQuick.Controls

Rectangle {
    id: menuPanel
    anchors.fill: parent
    color: "#9903040C"
    visible: false

    property var menuItems: []

    property bool focusOnCancel: false

    property var userData: null

    property var _fullMenuItems: []

    property var lastFocusedItem: null

    onMenuItemsChanged: {
        var items = []
        for (var i = 0; i < menuItems.length; i++) {
            items.push(menuItems[i])
        }
        items.push({ id: "cancel", name: qsTr("Cancel") })
        _fullMenuItems = items
    }

    onVisibleChanged: {
        if (visible) {
            Qt.callLater(function() {
                if (focusOnCancel) {
                    var last = menuRepeater.itemAt(menuRepeater.count - 1)
                    if (last) {
                        lastFocusedItem = last
                        last.forceActiveFocus()
                    }
                } else {
                    var first = menuRepeater.itemAt(0)
                    if (first) {
                        lastFocusedItem = first
                        first.forceActiveFocus()
                    }
                }
            })
        } else {
            lastFocusedItem = null
        }
    }

    Text {
        anchors.bottom: flow.top
        anchors.bottomMargin: 28
        anchors.horizontalCenter: flow.horizontalCenter
        text: qsTr("Choose an action")
        font.weight: 500
        font.pixelSize: 40
        color: "white"
    }

    Flow {
        id: flow
        width: 600
        anchors.centerIn: parent
        spacing: 0

        Repeater {
            id: menuRepeater
            model: menuPanel._fullMenuItems
            delegate: TextButton {
                id: menuItem
                width: flow.width
                height: 100
                normalBgColor: "#06646A"
                focusBgColor: "#00969A"
                text: modelData.name
                textColor: "white"
                textFont.pixelSize: 34
                textFont.weight: 400
                radius: 10

                onNavigationRequested: function(direction) {
                    if (direction === Qt.UpArrow) {
                        if (index > 0) {
                            lastFocusedItem = menuRepeater.itemAt(index - 1)
                            lastFocusedItem.forceActiveFocus()
                        }
                    } else if (direction === Qt.DownArrow) {
                        if (index < menuRepeater.count - 1) {
                            lastFocusedItem = menuRepeater.itemAt(index + 1)
                            lastFocusedItem.forceActiveFocus()
                        }
                    }
                }

                onClicked: {
                    if (modelData.id === "cancel") {
                        menuPanel.visible = false
                    } else {
                        menuPanel.visible = false
                        Qt.callLater(function() {
                            AppUtils.emitEventWith("menuClick", {"id": modelData.id, "userData": userData})
                        })
                    }
                }

                Rectangle {
                    visible: index > 0
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: menuItem.radius
                    color: menuItem.color
                }

                Rectangle {
                    visible: index < menuRepeater.count - 1
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: menuItem.radius
                    color: menuItem.color
                }

                Rectangle {
                    visible: index < menuRepeater.count - 1
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: "#1AFFFFFF"
                }
            }
        }
    }

    Component.onCompleted: {
        var win = Window.window
        if (win) {
            win.activeFocusItemChanged.connect(function() {
                if (!menuPanel.visible || !lastFocusedItem) return
                if (!isMenuChild(win.activeFocusItem)) {
                    console.log("menu focus loss, grab it")
                    lastFocusedItem.forceActiveFocus()
                }
            })
        }
    }

    function isMenuChild(item) {
        var current = item
        while (current) {
            if (current === menuPanel) return true
            current = current.parent
        }
        return false
    }
}
