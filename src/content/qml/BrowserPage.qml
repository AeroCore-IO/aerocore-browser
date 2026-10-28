import QtQuick
import QtQuick.Controls
import AeroCoreBrowser
import "base"

Item {
    id: browserPage
    anchors.fill: parent

    property alias scaleFactor: browserContainer.scaleFactor

    property bool hudVisible: hudPanel.visible

    property bool hudAutoHide: hudPanel.enableAutoHide

    property bool loadingVisible: loadingPlayer.visible

    property alias browserContainer: browserContainer

    Item {
        anchors.fill: parent

        BrowserContainer {
            id: browserContainer
            anchors.fill: parent

            Component.onCompleted: {
                setBrowserController(cppBrowserController)
                cppMainController.bindBrowserContainer(browserContainer)
            }
        }

        RotatingImagePlayer {
            id: loadingPlayer
            anchors.centerIn: parent
            visible: false
            z: 1
            backgroundSource: AppUtils.img("spiner-bg.svg")
            foregroundSource: AppUtils.img("spiner-fg.svg")
            imageSize: 240
        }

        HudPanel {
            id: hudPanel
            visible: false
            browserContainer: browserContainer
        }
    }

    Connections {
        target: browserContainer
        function onActiveFrameAvailable() {
            browserContainer.markLoadTiming("qml activeFrameAvailable → hideLoading")
            hideLoading("activeFrameAvailable")
        }
    }

    readonly property int loadingPollIntervalMs: 100
    readonly property int loadEndedNoFrameGraceMs: 3000
    readonly property int loadEndedNoFrameMaxTicks: Math.ceil(loadEndedNoFrameGraceMs / loadingPollIntervalMs)
    property int loadEndedNoFrameTicks: 0

    Timer {
        id: timer
        interval: browserPage.loadingPollIntervalMs
        repeat: true
        running: false
        onTriggered: {
            if (!browserContainer.isLoading && browserContainer.hasFrame()) {
                browserPage.loadEndedNoFrameTicks = 0
                hideLoading("timer hasFrame+idle")
                return
            }
            if (!browserContainer.isLoading) {
                browserPage.loadEndedNoFrameTicks += 1
                if (browserPage.loadEndedNoFrameTicks >= browserPage.loadEndedNoFrameMaxTicks) {
                    hideLoading("timer load-ended-no-frame")
                }
            } else {
                browserPage.loadEndedNoFrameTicks = 0
            }
        }
    }

    function hideLoading(reason) {
        var wasVisible = loadingPlayer.visible
        if (wasVisible) {
            loadingPlayer.visible = false
        }
        timer.stop()
        loadEndedNoFrameTicks = 0
        if (wasVisible || reason) {
            browserContainer.markLoadTiming(
                        "spinner-hide reason=" + (reason || "unspecified")
                        + " wasVisible=" + (wasVisible ? "1" : "0")
                        + " elapsedMs=" + browserContainer.loadTimingElapsedMs())
        }
    }

    function openUrl(param) {
        var url = param.url
        var type = param.type
        var timingOn = browserContainer.isLoadTimingEnabled()
        var timingId = browserContainer.beginLoadTiming("BrowserPage.openUrl type=" + type, url)
        if (timingOn) {
            console.info("[load-timing] qml begin id=" + timingId + " type=" + type + " url=" + url
                         + " container=" + Math.round(browserContainer.width) + "x" + Math.round(browserContainer.height)
                         + " visible=" + (browserContainer.visible ? "1" : "0"))
        }

        loadingPlayer.visible = true

        if (type === 1) {
            cppBrowserController.openUrl(url)
        } else if (type === 2) {
            cppMainController.openSite(url)
        } else if (type === 3) {
            cppMainController.openHistoryUrl(url)
        }

        browserContainer.markLoadTiming(
                    "after open* isLoading=" + (browserContainer.isLoading ? "1" : "0")
                    + " hasFrame=" + (browserContainer.hasFrame() ? "1" : "0"))

        loadEndedNoFrameTicks = 0
        if (browserContainer.isLoading) {
            timer.start()
            browserContainer.markLoadTiming("spinner-keep (loading)")
        } else if (browserContainer.hasFrame()) {
            hideLoading("warm-hasFrame")
        } else {
            timer.start()
            browserContainer.markLoadTiming("spinner-keep (no frame yet)")
        }

        browserContainer.markLoadTiming("BrowserPage.openUrl end")
    }

    function goBack() {
        if (hudPanel.visible) {
            console.info("[DEBUG-a4f2] BrowserPage.goBack → hide hud")
            hudPanel.hide()
            return true
        }

        if (!browserContainer.isAtNavigationStart()) {
            console.info("[DEBUG-a4f2] BrowserPage.goBack → history back")
            browserContainer.goBack()
            return true
        }

        if (cppMainController.activeTabIndex() > 0) {
            var tabId = getCurrentActiveTabId()
            console.info("[DEBUG-a4f2] BrowserPage.goBack → close tab", tabId)
            cppMainController.closeTab(getCurrentActiveTabId())
            return true
        }

        console.info("[DEBUG-a4f2] BrowserPage.goBack → unhandled (will leave page scope)")
        return false
    }

    function getCurrentActiveTabId() {
        var sessionList = JSON.parse(cppMainController.getSessionListJson())
        var activeSession = null
        for (var i = 0; i < sessionList.length; i++) {
            if (sessionList[i].isActive === true) {
                activeSession = sessionList[i]
                break
            }
        }

        if (activeSession === null) {
            return ""
        }

        var tabs = JSON.parse(cppMainController.getTabListJson(activeSession.sessionId))

        for (var j = 0; j < tabs.length; j++) {
            if (tabs[j].isActive === true) {
                return tabs[j].tabId
            }
        }

        return ""
    }

    function handleAction(actionName) {
        if (actionName === "refresh") {
            if (hudVisible) {
                hudPanel.handleAction(actionName)
            } else {
                browserContainer.reloadPage()
            }
            return true
        } else if (actionName === "home") {
            if (hudVisible) {
                hudPanel.handleAction(actionName)
                return true
            }
            return false
        } else if (actionName === "closeTab") {
            var sessionList = JSON.parse(cppMainController.getSessionListJson())
            var activeSessionId = ""
            var activeSessionIndex = -1
            var isActiveSession = false
            var currentTabCount = 0

            for (var i = 0; i < sessionList.length; i++) {
                if (sessionList[i].isActive === true) {
                    activeSessionId = sessionList[i].sessionId
                    activeSessionIndex = i
                    isActiveSession = true
                    break
                }
            }

            if (activeSessionId !== "") {
                var tabs = JSON.parse(cppMainController.getTabListJson(activeSessionId))
                currentTabCount = tabs.length
            }

            var tabId = getCurrentActiveTabId()
            if (tabId === "") return true

            cppMainController.closeTab(tabId)

            if (isActiveSession && currentTabCount <= 1) {
                var targetSessionId = ""
                if (activeSessionIndex > 0) {
                    targetSessionId = sessionList[activeSessionIndex - 1].sessionId
                } else if (activeSessionIndex < sessionList.length - 1) {
                    targetSessionId = sessionList[activeSessionIndex + 1].sessionId
                }

                if (targetSessionId !== "") {
                    cppMainController.openSession(targetSessionId)
                } else {
                    AppUtils.emitEventWith("switchPage", "home")
                }
            }

            return true
        } else if (actionName === "browserBack") {
            browserContainer.goBack()
            return true
        } else if (actionName === "browserForward") {
            browserContainer.goForward()
            return true
        } else if (actionName === "toggleHud"
               || actionName === "previousTab" || actionName === "nextTab"
               || actionName === "previousSession" || actionName === "nextSession"
               || actionName === "zoomIn" || actionName === "zoomOut"
               || actionName === "minFontIn" || actionName === "minFontOut") {
            hudPanel.show()
            hudPanel.handleAction(actionName)
            return true
        }
        return false
    }

    function onGamepadButtonPressed(buttonName) {
        if (hudPanel.visible) {
            hudPanel.onGamepadButtonPressed(buttonName)
        }
    }

    function onGamepadButtonReleased(buttonName) {
        if (hudPanel.visible) {
            hudPanel.onGamepadButtonReleased(buttonName)
        }
    }

    function deleteSession() {
        var sessionList = JSON.parse(cppMainController.getSessionListJson())
        var currentSessionIndex = cppMainController.sessionIndex()
        var currentSessionId = ""
        if (sessionList && currentSessionIndex >= 0 && currentSessionIndex < sessionList.length) {
            currentSessionId = sessionList[currentSessionIndex].sessionId || ""
        }
        if (currentSessionId === "") {
            console.error("failed to delete session, session id is empty")
            return false
        }

        return cppMainController.closeSiteSession(currentSessionId)
    }
}
