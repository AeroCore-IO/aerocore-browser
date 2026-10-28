import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "base"

PageBase {
    id: homePage
    pageName: "home"

    property var gridItems: []
    property bool websiteListStale: false

    Text {
        anchors.left: scrollView.left
        anchors.bottom: scrollView.top
        anchors.bottomMargin: 20

        text: qsTr("Websites")
        font.weight: 600
        font.pixelSize: 34
        color: "#BDCAD7"
    }

    Image {
        id: lastPageImage
        width: 40
        height: 40
        anchors.bottom: scrollView.top
        anchors.bottomMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        visible: scrollView.canGoLastPage()
        source: AppUtils.img("last-page.svg")
    }

    Image {
        id: nextPageImage
        width: 40
        height: 40
        anchors.top: scrollView.bottom
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        visible: scrollView.canGoNextPage()
        source: AppUtils.img("next-page.svg")
    }

    ScrollView {
        id: scrollView
        width: itemWidth*columnCount+itemSpace*(columnCount-1)
        height: itemHeight*rowCount+itemSpace*(rowCount-1)
        contentWidth: websiteFlow.width
        contentHeight: websiteFlow.implicitHeight
        anchors.centerIn: parent
        clip: true

        property int rowCount: 3
        property int columnCount: 6
        property int pageHeight: itemHeight*rowCount+itemSpace*rowCount

        property int itemWidth: 280
        property int itemHeight: 180
        property int itemSpace: 20

        ScrollBar.vertical.policy: ScrollBar.AlwaysOff
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.interactive: false
        ScrollBar.horizontal.interactive: false

        Component.onCompleted: {
            contentItem.interactive = false
        }

        function lastPage() {
            contentItem.contentY = Math.max(contentItem.contentY-pageHeight, 0)
        }

        function nextPage() {
            var maxY = contentHeight - height
            contentItem.contentY = Math.min(contentItem.contentY+pageHeight, maxY)
        }

        function canGoLastPage() {
            return contentItem.contentY > 0
        }

        function canGoNextPage() {
            return contentItem.contentY+height < contentHeight
        }

        Flow {
            id: websiteFlow
            width: scrollView.width
            spacing: scrollView.itemSpace

            Repeater {
                id: websiteRepeater
                model: gridItems

                onCountChanged: {
                    if (count > 0) {
                        Qt.callLater(function() {
                            var win = Window.window
                            if (win && !win.activeFocusItem) {
                                homePage.focusFirstVisibleItem()
                            }
                        })
                    }
                }

                Component.onCompleted: {
                    AppUtils.onEvent("menuClick", function(menuData) {
                        if (menuData.id === "deleteWebsite") {
                            var websiteId = menuData.userData
                            if (!websiteId) {
                                return
                            }

                            var removedGridIndex = -1
                            for (var j = 0; j < gridItems.length; j++) {
                                if (gridItems[j].id === websiteId) {
                                    removedGridIndex = j
                                    break
                                }
                            }
                            if (removedGridIndex < 0) {
                                return
                            }

                            cppMainController.deleteWebsite(websiteId)

                            Qt.callLater(function() {
                                if (websiteRepeater.count === 0) return

                                var newFocusItem = null
                                if (removedGridIndex >= 0) {
                                    newFocusItem = websiteRepeater.itemAt(
                                        Math.min(removedGridIndex, websiteRepeater.count - 1))
                                }
                                if (!newFocusItem) {
                                    newFocusItem = websiteRepeater.itemAt(0)
                                }

                                if (newFocusItem) {
                                    homePage.ensureItemVisible(newFocusItem)
                                    newFocusItem.forceActiveFocus()
                                } else {
                                    homePage.focusFirstTile()
                                }
                            })
                        }
                    })
                }

                delegate: Rectangle {
                    id: websiteItem
                    width: scrollView.itemWidth
                    height: scrollView.itemHeight
                    color: (activeFocus || hovered)? "#9900969A":"#1AFFFFFF"
                    radius: 20
                    activeFocusOnTab: true
                    clip: true

                    property int modelIndex: index
                    property int row: Math.floor(index / scrollView.columnCount)
                    property int col: index % scrollView.columnCount

                    property bool hovered: false

                    function clicked() {
                        AppUtils.openUrl(modelData.url, 2)
                    }

                    property alias focusInterface: focusInterface
                    FocusInterface {
                        id: focusInterface
                        opName: "homePageWebsiteItem"

                        clickHandler: function() {
                            websiteItem.clicked()
                        }

                        menuHandler: function() {
                            var menuItems = [{"id":"deleteWebsite", "name":qsTr("Delete")}]
                            AppUtils.showMenu(menuItems, modelData.id)
                        }
                    }

                    Keys.onUpPressed: function(event) {
                        var prevIndex = index - scrollView.columnCount
                        if (prevIndex < 0) {
                            return
                        }

                        var prevItem = websiteRepeater.itemAt(prevIndex)
                        if (!homePage.isItemVisible(prevItem)) {
                            scrollView.lastPage()
                        }
                        prevItem.forceActiveFocus()
                    }

                    Keys.onDownPressed: function(event) {
                        var nextIndex = index + scrollView.columnCount
                        if (nextIndex >= websiteRepeater.count) {
                            return
                        }

                        var nextItem = websiteRepeater.itemAt(nextIndex)
                        if (!homePage.isItemVisible(nextItem)) {
                            scrollView.nextPage()
                        }
                        nextItem.forceActiveFocus()
                    }

                    Keys.onLeftPressed: function(event) {
                        if (col > 0) {
                            var leftIndex = index - 1
                            var leftItem = websiteRepeater.itemAt(leftIndex)
                            if (leftItem && homePage.isItemVisible(leftItem)) {
                                leftItem.forceActiveFocus()
                            }
                        }
                    }

                    Keys.onRightPressed: function(event) {
                        if (col < scrollView.columnCount - 1) {
                            var rightIndex = index + 1
                            if (rightIndex < websiteRepeater.count) {
                                var rightItem = websiteRepeater.itemAt(rightIndex)
                                if (rightItem && homePage.isItemVisible(rightItem)) {
                                    rightItem.forceActiveFocus()
                                }
                            }
                        }
                    }

                    Image {
                        width: 110
                        height: 110
                        anchors.top: parent.top
                        anchors.topMargin: 16
                        anchors.horizontalCenter: parent.horizontalCenter
                        source: modelData.icon===""?AppUtils.img("website-default-icon.svg"):modelData.icon
                        fillMode: Image.PreserveAspectFit
                    }

                    Text {
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 14
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        text: modelData.name
                        font.weight: 600
                        font.pixelSize: 24
                        color: "white"
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }

                    Image {
                        width: 70
                        height: 70
                        anchors.top: parent.top
                        anchors.right: parent.right
                        source: modelData.is_current ? AppUtils.img("current.svg") : AppUtils.img("has-open.svg")
                        fillMode: Image.PreserveAspectFit
                        visible: modelData.has_open
                    }
                }
            }
        }
    }

    // cppMainController
    Connections {
        id: cppMainControllerConnection
        target: cppMainController

        function onWebsiteListChange() {
            if (!homePage.visible) {
                homePage.websiteListStale = true
                return
            }
            homePage.refreshWebsiteList()
        }

    }

    Component.onCompleted: {
        refreshWebsiteList()
    }

    onVisibleChanged: {
        if (visible) {
            if (websiteListStale) {
                refreshWebsiteList()
            }
            focusFirstTile()
        }
    }

    function refreshWebsiteList() {
        websiteListStale = false
        gridItems = JSON.parse(cppMainController.getHomeWebsiteList())
    }

    function focusFirstTile() {
        scrollView.contentItem.contentY = 0
        Qt.callLater(function() {
            var item = websiteRepeater.itemAt(0)
            if (item) {
                item.forceActiveFocus()
            }
        })
    }

    function focusFirstVisibleItem() {
        for (var i = 0; i < websiteRepeater.count; i++) {
            var item = websiteRepeater.itemAt(i)
            if (item && isItemVisible(item)) {
                item.forceActiveFocus()
                return
            }
        }
    }

    function isItemVisible(item) {
        var itemY = item.mapToItem(scrollView.contentItem, 0, 0).y
        return itemY + item.height > 0 && itemY < scrollView.height
    }

    function ensureItemVisible(item) {
        if (!item || isItemVisible(item)) return

        var itemY = item.mapToItem(scrollView.contentItem, 0, 0).y

        if (itemY < 0) {
            scrollView.contentItem.contentY += itemY
        } else if (itemY + item.height > scrollView.height) {
            scrollView.contentItem.contentY += (itemY + item.height - scrollView.height)
        }
    }
}
