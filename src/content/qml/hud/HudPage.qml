import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Effects
import "../base"

Item {
    width: 1780
    height: 720
    anchors.top: parent.top
    anchors.topMargin: 90
    anchors.horizontalCenter: parent.horizontalCenter

    property var browserContainer: null

    property real scaleFactor: AppUtils.scaleFactor

    property int maxIndicators: 20

    property var sessionList: []

    property var tabList: []

    property string currentSessionId: ""

    property string currentTabId: ""



    signal hideHud()

    ShaderEffectSource {
        id: bgSource
        anchors.fill: parent
        sourceItem: browserContainer
        sourceRect: Qt.rect(hudPage.x, hudPage.y, hudPage.width, hudPage.height)
        live: false
        visible: false
    }

    Rectangle {
        anchors.fill: parent
        radius: 10
        layer.enabled: true
        layer.effect: MultiEffect {
            source: bgSource
            blurEnabled: true
            blurMax: 30
            blur: 1.0
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 10
        color: "#FA00969A"
    }

    Item {
        width: 1720
        height: 666
        anchors.centerIn: parent

        Item {
            width: parent.width
            height: 44
            anchors.top: parent.top

            Image {
                width: 64
                height: 44
                source: {
                    cppMainController.buttonIconLayout
                    return cppMainController.iconUrl("op-lt.svg")
                }
                fillMode: Image.PreserveAspectFit
            }

            Text {                
                anchors.left: parent.left
                anchors.leftMargin: 79
                anchors.verticalCenter: parent.verticalCenter
                font.weight: 600
                font.pixelSize: 24
                color: "#FFFFFF"
                text: qsTr("Switch to the previous website")
            }

            Image {
                width: 64
                height: 44
                anchors.right: rtDesc.left
                anchors.rightMargin: 15
                source: {
                    cppMainController.buttonIconLayout
                    return cppMainController.iconUrl("op-rt.svg")
                }
                fillMode: Image.PreserveAspectFit
            }

            Text {
                id: rtDesc          
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                font.weight: 600
                font.pixelSize: 24
                color: "#FFFFFF"
                text: qsTr("Switch to the next website")
            }
        }

        Item {
            width: parent.width
            height: 44
            anchors.bottom: parent.bottom

            Image {
                width: 64
                height: 44
                source: {
                    cppMainController.buttonIconLayout
                    return cppMainController.iconUrl("op-lb.svg")
                }
                fillMode: Image.PreserveAspectFit
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 79
                anchors.verticalCenter: parent.verticalCenter
                font.weight: 600
                font.pixelSize: 24
                color: "#FFFFFF"
                text: qsTr("Switch to the previous tab")
            }

            Image {
                width: 64
                height: 44
                anchors.right: rbDesc.left
                anchors.rightMargin: 15
                source: {
                    cppMainController.buttonIconLayout
                    return cppMainController.iconUrl("op-rb.svg")
                }
                fillMode: Image.PreserveAspectFit
            }

            Text {
                id: rbDesc
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                font.weight: 600
                font.pixelSize: 24
                color: "#FFFFFF"
                text: qsTr("Switch to the next tab")
            }
        }

        ScrollViewBase {
            id: sessionScrollView
            width: parent.width
            height: 100
            anchors.top: parent.top
            anchors.topMargin: 64

            Flow {
                id: sessionFlow
                height: sessionScrollView.height
                spacing: 20

                Repeater {
                    id: sessionRepeater
                    model: sessionList

                    delegate: Rectangle {
                        id: sessionItem
                        width: 270
                        height: sessionScrollView.height
                        radius: 14
                        color: {
                            if (modelData.sessionId === currentSessionId) {
                                return "#ffffff"
                            } else {
                                return "#3EB9BB"
                            }
                        }

                        Image {
                            width: 64
                            height: 64
                            anchors.left: parent.left
                            anchors.leftMargin: 20
                            anchors.verticalCenter: parent.verticalCenter
                            source: {
                                if (modelData.icon) {
                                    return modelData.icon
                                } else if (modelData.sessionId === currentSessionId) {
                                    return AppUtils.img("website-default-icon2.svg")
                                } else {
                                    return AppUtils.img("website-default-icon.svg")
                                }
                            }
                            fillMode: Image.PreserveAspectFit
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 124
                            anchors.right: parent.right
                            anchors.rightMargin: 20
                            anchors.top: parent.top
                            anchors.topMargin: 17
                            font.weight: 600
                            font.pixelSize: 24
                            color: {
                                if (modelData.sessionId === currentSessionId) {
                                    return "#00969A"
                                } else {
                                    return "#F5FBFF"
                                }
                            }
                            text: modelData.siteName
                            clip: true
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 124
                            anchors.right: parent.right
                            anchors.rightMargin: 20
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 15
                            font.weight: 400
                            font.pixelSize: 22
                            color: {
                                if (modelData.sessionId === currentSessionId) {
                                    return "#00969A"
                                } else {
                                    return "#F5FBFF"
                                }
                            }
                            text: qsTr("%1 tabs").arg(modelData.tabCount)
                            clip: true
                        }
                    }
                }
            }
        }

        Text {
            id: currentSessionSiteNameText
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.topMargin: 194
            font.weight: 600
            font.pixelSize: 34
            color: "#FFFFFF"
        }
        Text {
            id: currentSessionTabCountText
            anchors.left: currentSessionSiteNameText.right
            anchors.leftMargin: 16
            anchors.bottom: currentSessionSiteNameText.bottom
            font.weight: 600
            font.pixelSize: 24
            color: "#99FFFFFF"
        }

        ScrollViewBase {
            id: tabScrollView
            width: parent.width
            height: 340
            anchors.top: parent.top
            anchors.topMargin: 262

            Flow {
                id: tabFlow
                height: tabScrollView.height
                spacing: 20

                Repeater {
                    id: tabRepeater
                    model: tabList

                    delegate: Rectangle {
                        id: tabItem
                        width: 415
                        height: tabScrollView.height
                        radius: 10
                        clip: true
                        color: {
                            if (modelData.tabId === currentTabId) {
                                return "#ffffff"
                            } else {
                                return "#3EB9BB"
                            }
                        }

                        Image {
                            width: parent.width
                            height: 220
                            anchors.top: parent.top
                            source: modelData.thumbnailUrl
                                        ? "image://memory/roundedImage:" + modelData.thumbnailUrl + "?radius=" + Math.round(10 * scaleFactor) + "&tl=1&tr=1&bl=0&br=0&fillMode=2&w=" + Math.round(parent.width * scaleFactor) + "&h=" + Math.round(220 * scaleFactor)
                                        : AppUtils.img("video-default-icon.svg")
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 20
                            anchors.right: parent.right
                            anchors.rightMargin: 20
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 56
                            font.weight: 400
                            font.pixelSize: 24
                            color: {
                                if (modelData.tabId === currentTabId) {
                                   return "#00969A"
                                } else {
                                   return "#FFFFFF"
                                }
                            }
                            text: modelData.title
                            clip: true
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 20
                            anchors.right: parent.right
                            anchors.rightMargin: 20
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 20
                            font.weight: 400
                            font.pixelSize: 22
                            color: {
                                if (modelData.tabId === currentTabId) {
                                   return "#00969A"
                                } else {
                                   return "#FFFFFF"
                                }
                            }
                            text: AppUtils.getDomainName(modelData.url)
                            clip: true
                        }
                    }
                }
            }
        }

        Flow {
            id: indicatorFlow
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 9
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 14

            Repeater {
                model: Math.min(Math.ceil(tabList.length / 4), maxIndicators)
                delegate: Rectangle {
                    width: 30
                    height: 5
                    radius: 2
                    color: {
                        var startIndex = index * 4
                        var endIndex
                        if (index === maxIndicators-1) {
                            endIndex = tabList.length - 1
                        } else {
                            endIndex = Math.min(startIndex + 3, tabList.length - 1)
                        }
                        for (var i = startIndex; i <= endIndex; i++) {
                            if (tabList[i].tabId === currentTabId) {
                                return "#FFFFFF"
                            }
                        }
                        return "#4EFFFFFF"
                    }
                }
            }
        }
    }

    Timer {
        id: bgUpdateTimer
        interval: 500
        repeat: true
        onTriggered: bgSource.scheduleUpdate()
    }    

    onVisibleChanged: {
        if (visible) {
            bgSource.scheduleUpdate()
            bgUpdateTimer.start()
        } else {
            bgUpdateTimer.stop()
        }
    }

    function updateAllData() {
        sessionList = []
        tabList = []
        currentSessionId = ""
        currentTabId = ""

        sessionList = JSON.parse(cppMainController.getSessionListJson())

        var activeSession = null
        for (var i = 0; i < sessionList.length; i++) {
            if (sessionList[i].isActive === true) {
                activeSession = sessionList[i]
                break
            }
        }
        if (activeSession !== null) {
            currentSessionId = activeSession.sessionId
        } else if (sessionList.length > 0) {
            currentSessionId = sessionList[0].sessionId
        }

        updateTabList()

        updateSessionInfo()

        Qt.callLater(function() {
            sessionFlow.forceLayout()
            ensureCurrentSessionVisible()
            tabFlow.forceLayout()
            ensureCurrentTabVisible()
        })
    }

    function updateTabList() {
        if (currentSessionId !== "") {
            currentTabId = ""
            var targetTabList = JSON.parse(cppMainController.getTabListJson(currentSessionId))

            // if (targetTabList.length > 0 && targetTabList.length < 80) {
            //     var baseTab = targetTabList[0]
            //     var originalLength = targetTabList.length
            //     for (var k = originalLength; k < 85; k++) {
            //         targetTabList.push({
            //             tabId: "mock_tab_" + k,
            //             sessionId: baseTab.sessionId,
            //             url: baseTab.url + "#tab" + k,
            //             thumbnailUrl: baseTab.thumbnailUrl,
            //             isActive: false
            //         })
            //     }
            // }

            tabList = targetTabList
            if (tabList.length > 0) {
                var activeTab = null
                for (var i = 0; i < tabList.length; i++) {
                    if (tabList[i].isActive === true) {
                        activeTab = tabList[i]
                        break
                    }
                }
                if (activeTab !== null) {
                    currentTabId = activeTab.tabId
                } else {
                    currentTabId = tabList[0].tabId
                }
            }
        } else {
            tabList = []
            currentTabId = ""
        }

        Qt.callLater(function() {
            if (tabScrollView.contentItem) {
                tabScrollView.contentItem.contentX = 0
            }
            tabFlow.forceLayout()
            ensureCurrentTabVisible()
        })
    }

    onCurrentSessionIdChanged: {
        updateTabList()

        updateSessionInfo()
    }

    function updateSessionInfo() {
        if (currentSessionId !== "") {
            for (var i = 0; i < sessionList.length; i++) {
                if (sessionList[i].sessionId === currentSessionId) {
                    currentSessionSiteNameText.text = sessionList[i].siteName
                    currentSessionTabCountText.text = qsTr("%1 tabs").arg(sessionList[i].tabCount)
                    break
                }
            }
        } else {
            currentSessionSiteNameText.text = ""
            currentSessionTabCountText.text = ""
        }
    }

    function ensureCurrentSessionVisible() {
        if (currentSessionId !== "") {
            for (var i = 0; i < sessionRepeater.count; i++) {
                var item = sessionRepeater.itemAt(i)
                if (item && sessionList[i].sessionId === currentSessionId) {
                    ensureItemVisible(sessionScrollView, item)
                    break
                }
            }
        }
    }

    function ensureCurrentTabVisible() {
        if (currentTabId !== "") {
            for (var i = 0; i < tabRepeater.count; i++) {
                var item = tabRepeater.itemAt(i)
                if (item && tabList[i].tabId === currentTabId) {
                    ensureItemVisible(tabScrollView, item)
                    break
                }
            }
        }
    }

    function getCurrentFocusInfo() {
        var result = {
            focusedOnSession: false,
            focusedOnTab: false,
            sessionIndex: -1,
            tabIndex: -1
        }

        for (var i = 0; i < sessionList.length; i++) {
            if (sessionList[i].sessionId === currentSessionId) {
                result.focusedOnSession = true
                result.sessionIndex = i
                break
            }
        }

        for (var j = 0; j < tabList.length; j++) {
            if (tabList[j].tabId === currentTabId) {
                result.focusedOnTab = true
                result.tabIndex = j
                break
            }
        }

        return result
    }

    function getCurrentSessionIndex() {
        for (var i = 0; i < sessionList.length; i++) {
            if (sessionList[i].sessionId === currentSessionId) {
                return i
            }
        }
        return -1
    }

    function getCurrentTabIndex() {
        for (var i = 0; i < tabList.length; i++) {
            if (tabList[i].tabId === currentTabId) {
                return i
            }
        }
        return -1
    }

    function ensureItemVisible(scrollView, item) {
        if (!item || !scrollView) return

        var itemX = item.x
        var itemY = item.y
        var itemWidth = item.width
        var itemHeight = item.height

        var viewWidth = scrollView.width
        var viewHeight = scrollView.height

        var contentX = scrollView.contentItem ? scrollView.contentItem.contentX : 0
        var contentY = scrollView.contentItem ? scrollView.contentItem.contentY : 0

        if (itemX < contentX) {
            if (scrollView.contentItem) {
                scrollView.contentItem.contentX = itemX
            }
        } else if (itemX + itemWidth > contentX + viewWidth) {
            if (scrollView.contentItem) {
                scrollView.contentItem.contentX = itemX + itemWidth - viewWidth
            }
        }

        if (itemY < contentY) {
            if (scrollView.contentItem) {
                scrollView.contentItem.contentY = itemY
            }
        } else if (itemY + itemHeight > contentY + viewHeight) {
            if (scrollView.contentItem) {
                scrollView.contentItem.contentY = itemY + itemHeight - viewHeight
            }
        }
    }

    function navigateSession(direction) {
        var focusInfo = getCurrentFocusInfo()
        var targetIndex = -1

        if (focusInfo.focusedOnSession) {
            targetIndex = direction === "next" ? focusInfo.sessionIndex + 1 : focusInfo.sessionIndex - 1
        } else {
            targetIndex = direction === "next" ? getCurrentSessionIndex() + 1 : getCurrentSessionIndex() - 1
        }

        if (targetIndex >= sessionList.length) {
            targetIndex = 0
        } else if (targetIndex < 0) {
            targetIndex = sessionList.length - 1
        }

        if (targetIndex >= 0 && targetIndex < sessionList.length) {
            var item = sessionRepeater.itemAt(targetIndex)
            if (item) {
                currentSessionId = sessionList[targetIndex].sessionId
                ensureItemVisible(sessionScrollView, item)
            }
        }
    }

    function navigateTab(direction) {
        var focusInfo = getCurrentFocusInfo()
        var targetIndex = -1

        if (focusInfo.focusedOnTab) {
            targetIndex = direction === "next" ? focusInfo.tabIndex + 1 : focusInfo.tabIndex - 1
        } else {
            targetIndex = direction === "next" ? getCurrentTabIndex() + 1 : getCurrentTabIndex() - 1
        }

        if (targetIndex >= tabList.length) {
            targetIndex = 0
        } else if (targetIndex < 0) {
            targetIndex = tabList.length - 1
        }

        if (targetIndex >= 0 && targetIndex < tabList.length) {
            var item = tabRepeater.itemAt(targetIndex)
            if (item) {
                currentTabId = tabList[targetIndex].tabId
                ensureItemVisible(tabScrollView, item)
            }
        }
    }

    function handleAction(actionName) {
        if (actionName === "previousSession") {
            navigateSession("previous")
        } else if (actionName === "nextSession") {
            navigateSession("next")
        } else if (actionName === "previousTab") {
            navigateTab("previous")
        } else if (actionName === "nextTab") {
            navigateTab("next")
        }
    }

    function onGamepadButtonPressed(buttonName) {
        if (buttonName === "A") {
            openFocusTab()
        } else if (buttonName === "X") {
            closeFocusTab()
        } else if (buttonName === "Y") {
            closeFocusSession()
        } else if (buttonName === "LT") {
            navigateSession("previous")
        } else if (buttonName === "RT") {
            navigateSession("next")
        } else if (buttonName === "LB") {
            navigateTab("previous")
        } else if (buttonName === "RB") {
            navigateTab("next")
        }
    }

    function onGamepadButtonReleased(buttonName) {
        if (buttonName === "B") {
            hideHud()
        }
    }

    function openFocusTab() {
        if (currentTabId.length > 0) {
            hideHud()
            cppMainController.openTab(currentTabId)
        }
    }

    function closeFocusTab() {
        if (currentTabId === "") return

        var focusIndex = -1
        for (var i = 0; i < tabList.length; i++) {
            if (tabList[i].tabId === currentTabId) {
                focusIndex = i
                break
            }
        }

        if (focusIndex === -1) return

        var isActiveSession = false
        var currentTabCount = tabList.length
        for (var s = 0; s < sessionList.length; s++) {
            if (sessionList[s].sessionId === currentSessionId && sessionList[s].isActive === true) {
                isActiveSession = true
                break
            }
        }

        cppMainController.closeTab(currentTabId)

        if (isActiveSession && currentTabCount <= 1) {
            var sessionIndex = -1
            for (var j = 0; j < sessionList.length; j++) {
                if (sessionList[j].sessionId === currentSessionId) {
                    sessionIndex = j
                    break
                }
            }

            var targetSessionId = ""
            if (sessionIndex > 0) {
                targetSessionId = sessionList[sessionIndex - 1].sessionId
            } else if (sessionIndex < sessionList.length - 1) {
                targetSessionId = sessionList[sessionIndex + 1].sessionId
            }

            if (targetSessionId !== "") {
                cppMainController.openSession(targetSessionId)
            }
        }

        Qt.callLater(function() {
            if (currentTabCount > 1) {
                updateTabList()
            } else {
                updateAllData()
                if (sessionList.length === 0) {
                    AppUtils.emitEventWith("switchPage", "home")
                }
            }
        })
    }

    function closeFocusSession() {
        if (currentSessionId === "") return

        var focusIndex = -1
        for (var i = 0; i < sessionList.length; i++) {
            if (sessionList[i].sessionId === currentSessionId) {
                focusIndex = i
                break
            }
        }

        if (focusIndex === -1) return

        var isActiveSession = false
        if (sessionList[focusIndex].isActive === true) {
            isActiveSession = true
        }

        cppMainController.closeSiteSession(currentSessionId)

        if (isActiveSession) {
            var targetSessionId = ""
            if (focusIndex > 0) {
                targetSessionId = sessionList[focusIndex - 1].sessionId
            } else if (focusIndex < sessionList.length - 1) {
                targetSessionId = sessionList[focusIndex + 1].sessionId
            }

            if (targetSessionId !== "") {
                cppMainController.openSession(targetSessionId)
            }
        }

        Qt.callLater(function() {
            updateAllData()
            if (sessionList.length === 0) {
                AppUtils.emitEventWith("switchPage", "home")
            }
        })
    }
}
