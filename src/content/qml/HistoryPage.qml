import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "base"

PageBase {
    pageName: "history"

    property real scaleFactor: AppUtils.scaleFactor

    property var allHistoryList: []

    property var websiteList: []

    property string currentWebsiteId: ""

    property string allHistoryWebsiteId: "all_history"

    property var lastFocusedCardIdMap: ({})

    component HistoryCard: Item {
        id: historyCard
        width: 1380
        height: flow.implicitHeight+flow.anchors.topMargin
        visible: cardRepeater.count > 0

        property string title: ""

        property var cardData: []

        property alias flow: flow
        property alias repeater: cardRepeater

        Text {
            anchors.left: parent.left
            anchors.bottom: flow.top
            anchors.bottomMargin: 21
            font.weight: 600
            font.pixelSize: 24
            color: "#EEF7FF"
            text: title
        }

        Flow {
            id: flow
            width: parent.width
            anchors.top: parent.top
            anchors.topMargin: 90
            spacing: 20

            Repeater {
                id: cardRepeater
                model: cardData

                delegate: Rectangle {
                    id: cardItem
                    width: 680
                    height: 180
                    radius: 10
                    color: activeFocus? "#9900969A":"#1A00969A"
                    activeFocusOnTab: true

                    Image {
                        width: 200
                        height: 140
                        anchors.left: parent.left
                        anchors.leftMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        source: modelData.thumbnail
                                        ? "image://memory/roundedImage:" + (modelData.thumbnail + "?v=" + modelData.updated_at) + "?radius=" + Math.round(10 * scaleFactor) + "&tl=1&tr=1&bl=1&br=1&fillMode=2&w=" + Math.round(200 * scaleFactor) + "&h=" + Math.round(140 * scaleFactor)
                                        : AppUtils.img("website-default-icon.svg")
                        fillMode: modelData.thumbnail?Image.Stretch:Image.PreserveAspectFit
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 240
                        anchors.right: parent.right
                        anchors.rightMargin: 20
                        anchors.top: parent.top
                        anchors.topMargin: 38
                        text: modelData.title
                        font.weight: 600
                        font.pixelSize: 24
                        color: "#F4F8FF"
                        clip: true
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 240
                        anchors.top: parent.top
                        anchors.topMargin: 77
                        text: modelData.domainName
                        font.weight: 400
                        font.pixelSize: 20
                        color: "#8EA1B3"
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 240
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 41
                        text: modelData.website_name
                        font.weight: 400
                        font.pixelSize: 20
                        color: "#8EA1B3"
                    }

                    Image {
                        id: playIcon
                        width: 24
                        height: 24
                        anchors.right: parent.right
                        anchors.rightMargin: 20
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 43
                        source: AppUtils.img("play.svg")
                        fillMode: Image.PreserveAspectFit
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 49
                        anchors.verticalCenter: playIcon.verticalCenter
                        text: modelData.last_visit_time_str
                        font.weight: 400
                        font.pixelSize: 20
                        color: "#8EA1B3"
                    }

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            lastFocusedCardIdMap[currentWebsiteId] = modelData.id
                        }
                    }

                    function getFocusInterface() {
                        return focusInterface
                    }
                    FocusInterface {
                        id: focusInterface
                        opName: "historyPageCardItem"

                        clickHandler: function() {
                            AppUtils.openUrl(modelData.url, 3)
                        }

                        backHandler: function() {
                            var websiteItem = findCurrentWebsiteItem()
                            if (websiteItem) {
                                websiteItem.forceActiveFocus()
                                return true
                            }
                            return false
                        }

                        menuHandler: function() {
                            var menuItems = [{"id":"deleteHistory", "name":qsTr("Delete history entry")}]
                            AppUtils.showMenu(menuItems, modelData.id)
                        }
                    }

                    Keys.onUpPressed: function(event) {
                        if (index < 2) {
                            navigateToPrevHistoryCard(historyCard)
                            return
                        }

                        var prevItem = cardRepeater.itemAt(index - 2)
                        if (prevItem) {
                            ensureItemVisible(prevItem, cardScrollView)
                            prevItem.forceActiveFocus()
                        }
                    }

                    Keys.onDownPressed: function(event) {
                        if (index + 2 >= cardRepeater.count) {
                            navigateToNextHistoryCard(historyCard)
                            return
                        }

                        var nextItem = cardRepeater.itemAt(index + 2)
                        if (nextItem) {
                            ensureItemVisible(nextItem, cardScrollView)
                            nextItem.forceActiveFocus()
                        }
                    }

                    Keys.onLeftPressed: function(event) {
                        if (index % 2 === 0) {
                            var websiteItem = findCurrentWebsiteItem()
                            if (websiteItem) {
                                websiteItem.forceActiveFocus()
                            }
                            return
                        }

                        var leftItem = cardRepeater.itemAt(index - 1)
                        if (leftItem) {
                            leftItem.forceActiveFocus()
                        }
                    }

                    Keys.onRightPressed: function(event) {
                        if (index % 2 === 1 || index === cardRepeater.count - 1) {
                            return
                        }

                        var rightItem = cardRepeater.itemAt(index + 1)
                        if (rightItem) {
                            rightItem.forceActiveFocus()
                        }
                    }
                }                
            }
        }
    }

    Rectangle {
        width: 360
        anchors.left: parent.left
        anchors.leftMargin: 30
        anchors.top: parent.top
        anchors.topMargin: 130
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 70
        color: "#1A00969A"
        radius: 20

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 30
            anchors.top: parent.top
            anchors.topMargin: 30
            text: qsTr("Browsing history")
            font.weight: 600
            font.pixelSize: 34
            color: "#EEF7FF"
        }

        Image {
            width: 30
            height: 30
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 5
            source: AppUtils.img("pull-down.svg")
            fillMode: Image.PreserveAspectFit
            visible: {
                websiteRepeater.count > 0 &&
                websiteFlow.implicitHeight > scrollView.height &&
                scrollView.contentItem.contentY + scrollView.height < websiteFlow.implicitHeight
            }
        }

        ScrollView {
            id: scrollView
            width: parent.width
            anchors.top: parent.top
            anchors.topMargin: 108
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 52
            contentWidth: width
            contentHeight: websiteFlow.implicitHeight
            clip: true

            ScrollBar.vertical.policy: ScrollBar.AlwaysOff
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.interactive: false
            ScrollBar.horizontal.interactive: false

            Component.onCompleted: {
                contentItem.interactive = false
            }

            Flow {
                id: websiteFlow
                width: scrollView.width
                spacing: 0

                Repeater {
                    id: websiteRepeater
                    model: websiteList

                    delegate: Rectangle {
                        id: websiteItem
                        width: scrollView.width
                        height: 80
                        color: {
                            if (activeFocus) {
                                return "#00969A"
                            } else if (modelData.id===currentWebsiteId) {
                                return "#3300969A"
                            } else {
                                return "transparent"
                            }
                        }
                        activeFocusOnTab: true

                        onActiveFocusChanged: {
                            if (activeFocus) {
                                currentWebsiteId = modelData.id
                            }
                        }                        

                        Keys.onUpPressed: function(event) {
                            if (index === 0) return

                            var prevItem = websiteRepeater.itemAt(index - 1)
                            if (prevItem) {
                                ensureItemVisible(prevItem, scrollView)
                                prevItem.forceActiveFocus()
                            }
                        }

                        Keys.onDownPressed: function(event) {
                            if (index >= websiteRepeater.count - 1) return

                            var nextItem = websiteRepeater.itemAt(index + 1)
                            if (nextItem) {
                                ensureItemVisible(nextItem, scrollView)
                                nextItem.forceActiveFocus()
                            }
                        }

                        Keys.onRightPressed: function(event) {
                            navigateToWebsiteCards(modelData.id)
                        }

                        property alias focusInterface: websiteItemFocusInterface
                        FocusInterface {
                            id: websiteItemFocusInterface
                            clickHandler: function() {
                                navigateToWebsiteCards(modelData.id)
                            }
                        }

                        Image {
                            width: 60
                            height: 60
                            anchors.left: parent.left
                            anchors.leftMargin: 25
                            anchors.verticalCenter: parent.verticalCenter
                            source: (typeof modelData !== 'undefined' && modelData && modelData.icon) ? modelData.icon : AppUtils.img("website-default-icon.svg")
                            fillMode: Image.PreserveAspectFit
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 113
                            anchors.right: countText.left
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.name
                            font.weight: 600
                            font.pixelSize: 28
                            color: "#EEF7FF"
                            clip: true
                        }

                        Text {
                            id: countText
                            anchors.right: parent.right
                            anchors.rightMargin: 26
                            anchors.verticalCenter: parent.verticalCenter
                            font.weight: 600
                            font.pixelSize: 20
                            color: "#80FFFFFF"
                            text: modelData.count
                        }
                    }
                }
            }
        }
    }

    ScrollView {
        id: cardScrollView
        width: todayHistoryCard.width
        anchors.left: parent.left
        anchors.leftMargin: 450
        anchors.top: parent.top
        anchors.topMargin: 130
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 70
        contentWidth: width
        contentHeight: cardFlow.implicitHeight
        clip: true

        ScrollBar.vertical.policy: ScrollBar.AlwaysOff
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.interactive: false
        ScrollBar.horizontal.interactive: false

        Component.onCompleted: {
            contentItem.interactive = false
        }

        Flow {
            id: cardFlow
            width: todayHistoryCard.width
            spacing: 0

            HistoryCard {
                id: todayHistoryCard
                title: qsTr("Today")
            }

            HistoryCard {
                id: yesterdayHistoryCard
                title: qsTr("Yesterday")
            }

            HistoryCard {
                id: earlierHistoryCard
                title: qsTr("Earlier")
            }
        }
    }

    function getHistoryList() {
        allHistoryList = JSON.parse(cppMainController.getHistoryList())

        var newWebsiteList = []

        var totalCount = 0

        for (var i = 0; i < allHistoryList.length; i++) {
            var siteData = allHistoryList[i]
            var itemCount = siteData.items ? siteData.items.length : 0
            totalCount += itemCount

            var websiteItem = {
                "id": siteData.id,
                "name": siteData.name,
                "url": siteData.url,
                "icon": siteData.icon,
                "count": itemCount
            }

            newWebsiteList.push(websiteItem)
        }

        var allHistoryItem = {
            "id": allHistoryWebsiteId,
            "name": qsTr("All history"),
            "url": "",
            "icon": AppUtils.img("history.svg"),
            "count": totalCount
        }

        newWebsiteList.unshift(allHistoryItem)

        historyPage.websiteList = newWebsiteList

        currentWebsiteId = allHistoryWebsiteId
    }

    function getHistoryCardData() {
        var items = []

        if (currentWebsiteId === allHistoryWebsiteId) {
            for (var i = 0; i < allHistoryList.length; i++) {
                var siteItems = allHistoryList[i].items || []
                for (var j = 0; j < siteItems.length; j++) {
                    items.push(siteItems[j])
                }
            }
        } else {
            for (var i = 0; i < allHistoryList.length; i++) {
                if (allHistoryList[i].id === currentWebsiteId) {
                    items = allHistoryList[i].items || []
                    break
                }
            }
        }

        var now = new Date()
        var todayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime() / 1000
        var yesterdayStart = todayStart - 86400
        var yesterdayEnd = todayStart

        var todayItems = []
        var yesterdayItems = []
        var earlierItems = []

        for (var k = 0; k < items.length; k++) {
            var item = items[k]
            var timestamp = item.last_visit_time

            var itemDate = new Date(timestamp * 1000)

            var hours = itemDate.getHours().toString().padStart(2, '0')
            var minutes = itemDate.getMinutes().toString().padStart(2, '0')
            item.last_visit_time_str = hours + ":" + minutes

            var url = item.url || ""
            var domainMatch = url.match(/https?:\/\/([^\/]+)/)
            item.domainName = domainMatch ? domainMatch[1] : ""

            if (timestamp >= todayStart) {
                todayItems.push(item)
            } else if (timestamp >= yesterdayStart && timestamp < yesterdayEnd) {
                yesterdayItems.push(item)
            } else {
                earlierItems.push(item)
            }
        }

        var sortByTime = function(a, b) {
            return b.last_visit_time - a.last_visit_time
        }

        todayItems.sort(sortByTime)
        yesterdayItems.sort(sortByTime)
        earlierItems.sort(sortByTime)

        todayHistoryCard.cardData = todayItems
        yesterdayHistoryCard.cardData = yesterdayItems
        earlierHistoryCard.cardData = earlierItems

        if (cardScrollView.contentItem) {
            cardScrollView.contentItem.contentY = 0
        }
    }

    function findCurrentWebsiteItem() {
        for (var i = 0; i < websiteList.length; i++) {
            if (websiteList[i].id === currentWebsiteId) {
                return websiteRepeater.itemAt(i)
            }
        }

        return null
    }

    function ensureItemVisible(item, scrollView) {
        if (!item || !scrollView) return

        var flickable = scrollView.contentItem
        if (!flickable) return

        var itemPos = item.mapToItem(flickable.contentItem, 0, 0)
        if (!itemPos) return

        var itemTop = itemPos.y
        var itemBottom = itemTop + item.height
        var viewTop = flickable.contentY
        var viewBottom = viewTop + flickable.height

        if (itemTop < viewTop) {
            flickable.contentY = itemTop
        } else if (itemBottom > viewBottom) {
            flickable.contentY = itemBottom - flickable.height
        }
    }

    function findCardItemById(cardId) {
        var historyCards = [todayHistoryCard, yesterdayHistoryCard, earlierHistoryCard]

        for (var i = 0; i < historyCards.length; i++) {
            if (historyCards[i].visible) {
                var cardData = historyCards[i].cardData
                for (var j = 0; j < cardData.length; j++) {
                    if (cardData[j].id === cardId) {
                        return historyCards[i].repeater.itemAt(j)
                    }
                }
            }
        }

        return null
    }

    function navigateToFirstVisibleCard() {
        var historyCards = [todayHistoryCard, yesterdayHistoryCard, earlierHistoryCard]

        for (var i = 0; i < historyCards.length; i++) {
            if (historyCards[i].visible && historyCards[i].cardData.length > 0) {
                var firstItem = historyCards[i].repeater.itemAt(0)
                if (firstItem) {
                    ensureItemVisible(firstItem, cardScrollView)
                    firstItem.forceActiveFocus()
                    return
                }
            }
        }
    }

    function navigateToWebsiteCards(websiteId) {
        var lastFocusedId = lastFocusedCardIdMap[websiteId]
        if (lastFocusedId) {
            var targetItem = findCardItemById(lastFocusedId)
            if (targetItem) {
                ensureItemVisible(targetItem, cardScrollView)
                targetItem.forceActiveFocus()
                return
            }
        }

        navigateToFirstVisibleCard()
    }

    function getOrderedHistoryCards() {
        return [todayHistoryCard, yesterdayHistoryCard, earlierHistoryCard].filter(function(card) {
            return card.visible && card.cardData.length > 0
        })
    }

    function navigateToPrevHistoryCard(currentHistoryCard) {
        var visibleCards = getOrderedHistoryCards()
        if (visibleCards.length === 0) return

        var currentIndex = visibleCards.indexOf(currentHistoryCard)
        if (currentIndex < 0) {
            return
        }
        if (currentIndex === 0) {
            cardScrollView.contentItem.contentY = 0
            return
        }

        var prevCard = visibleCards[currentIndex - 1]
        var lastRowStartIndex = Math.floor((prevCard.repeater.count - 1) / 2) * 2
        var targetItem = prevCard.repeater.itemAt(lastRowStartIndex)

        if (targetItem) {
            ensureItemVisible(targetItem, cardScrollView)
            targetItem.forceActiveFocus()

            if (lastRowStartIndex < 2) {
                if (cardScrollView.contentItem) {
                    cardScrollView.contentItem.contentY = 0
                }
            }
        }
    }

    function navigateToNextHistoryCard(currentHistoryCard) {
        var visibleCards = getOrderedHistoryCards()
        if (visibleCards.length === 0) return

        var currentIndex = visibleCards.indexOf(currentHistoryCard)
        if (currentIndex >= visibleCards.length - 1) return

        var nextCard = visibleCards[currentIndex + 1]
        var targetItem = nextCard.repeater.itemAt(0)

        if (targetItem) {
            ensureItemVisible(targetItem, cardScrollView)
            targetItem.forceActiveFocus()
        }
    }

    onCurrentWebsiteIdChanged: {        
        getHistoryCardData()
    }

    onVisibleChanged: {
        if (visible) {
            Qt.callLater(function() {
                setInitialFocus()
            })
        }
    }

    function setInitialFocus() {
        var targetItem = findCurrentWebsiteItem()
        if (targetItem) {
            targetItem.forceActiveFocus()
        } else if (websiteRepeater.count > 0) {
            var firstItem = websiteRepeater.itemAt(0)
            if (firstItem) {
                firstItem.forceActiveFocus()
                if (websiteList.length > 0) {
                    currentWebsiteId = websiteList[0].id
                }
            }
        }
    }

    function removeHistoryFromAllList(historyId) {
        for (var i = 0; i < allHistoryList.length; i++) {
            var items = allHistoryList[i].items
            if (items) {
                for (var j = 0; j < items.length; j++) {
                    if (items[j].id === historyId) {
                        items.splice(j, 1)
                        return
                    }
                }
            }
        }
    }

    function removeCardDataById(historyId) {
        var historyCards = [todayHistoryCard, yesterdayHistoryCard, earlierHistoryCard]
        var result = {
            deletedIndex: -1,
            deletedCard: null
        }

        for (var i = 0; i < historyCards.length; i++) {
            var cardData = historyCards[i].cardData
            for (var j = 0; j < cardData.length; j++) {
                if (cardData[j].id === historyId) {
                    result.deletedIndex = j
                    result.deletedCard = historyCards[i]
                    cardData.splice(j, 1)
                    historyCards[i].cardData = cardData
                    return result
                }
            }
        }

        return result
    }

    function updateWebsiteCount() {
        for (var i = 0; i < allHistoryList.length; i++) {
            var siteData = allHistoryList[i]
            var itemCount = siteData.items ? siteData.items.length : 0

            for (var j = 0; j < websiteList.length; j++) {
                if (websiteList[j].id === siteData.id) {
                    websiteList[j].count = itemCount
                    break
                }
            }
        }

        for (var k = 0; k < websiteList.length; k++) {
            if (websiteList[k].id === allHistoryWebsiteId) {
                var totalCount = 0
                for (var m = 0; m < allHistoryList.length; m++) {
                    totalCount += allHistoryList[m].items ? allHistoryList[m].items.length : 0
                }
                websiteList[k].count = totalCount
                break
            }
        }

        websiteList = websiteList
    }

    function setNextFocusAfterDelete(deletedHistoryId, deletedCardIndex, deletedHistoryCard) {
        if (deletedHistoryCard && deletedHistoryCard.repeater) {
            if (deletedCardIndex < deletedHistoryCard.repeater.count) {
                var nextItem = deletedHistoryCard.repeater.itemAt(deletedCardIndex)
                if (nextItem) {
                    nextItem.forceActiveFocus()
                    return
                }
            } else if (deletedCardIndex > 0) {
                var prevItem = deletedHistoryCard.repeater.itemAt(deletedCardIndex - 1)
                if (prevItem) {
                    prevItem.forceActiveFocus()
                    return
                }
            } else {
                var allCards = [todayHistoryCard, yesterdayHistoryCard, earlierHistoryCard]
                for (var i = 0; i < allCards.length; i++) {
                    if (allCards[i] !== deletedHistoryCard && allCards[i].visible && allCards[i].repeater.count > 0) {
                        allCards[i].repeater.itemAt(0).forceActiveFocus()
                        return
                    }
                }
            }
        }

        var websiteItem = findCurrentWebsiteItem()
        if (websiteItem) {
            websiteItem.forceActiveFocus()
        }
    }

    function cleanupLastFocusedCardIdMap(deletedId) {
        for (var key in lastFocusedCardIdMap) {
            if (lastFocusedCardIdMap[key] === deletedId) {
                delete lastFocusedCardIdMap[key]
            }
        }
    }

    function deleteHistory(historyId) {
        cleanupLastFocusedCardIdMap(historyId)

        cppMainController.deleteHistory(historyId)

        removeHistoryFromAllList(historyId)

        var deleteResult = removeCardDataById(historyId)

        updateWebsiteCount()

        Qt.callLater(function() {
            setNextFocusAfterDelete(historyId, deleteResult.deletedIndex, deleteResult.deletedCard)
        })
    }

    Component.onCompleted: {
        getHistoryList()

        AppUtils.onEvent("menuClick", function(menuData) {
            if (menuData.id === "deleteHistory") {
                deleteHistory(menuData.userData)
            }
        })
    }

    // cppMainController
    Connections {
        id: cppMainControllerConnection
        target: cppMainController

        function onHistoryListChange() {
            getHistoryList()
        }
    }
}
