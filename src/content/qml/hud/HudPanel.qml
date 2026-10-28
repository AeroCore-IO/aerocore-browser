import QtQuick
import QtQuick.Controls
import InputTypes 1.0
import "../base"

Item {
    id: hudPanel
    anchors.fill: parent

    property var browserContainer: null

    property string currentPage: ""

    property bool enableAutoHide: true

    HudPage {
        id: hudPage
        visible: currentPage === "hud"
        browserContainer: hudPanel.browserContainer

        onHideHud: {
            hide()
        }
    }

    ScaleSettingPage {
        id: scaleSettingPage
        visible: currentPage === "scaleSetting"
        browserContainer: hudPanel.browserContainer
    }

    FontSizePage {
        id: fontSizePage
        visible: currentPage === "fontSetting"
        browserContainer: hudPanel.browserContainer
    }

    Timer {
        id: autoHideTimer
        interval: 1000
        repeat: false
        onTriggered: {
            if (enableAutoHide) {
                hide()
            }
        }
    }

    onVisibleChanged: {
        if (visible) {
            autoHideTimer.restart()
        } else {
            autoHideTimer.stop()
            currentPage = ""
            enableAutoHide = true

            cppInputManager.inputRoute = InputTypes.InputRoute.BrowserPointer
        }
    }

    function handleAction(actionName) {
        if (actionName === "back") {
            hide()
            return
        }

        autoHideTimer.restart()

        var value = 0
        if (actionName === "zoomIn" || actionName === "zoomOut") {
            if (currentPage != "scaleSetting") {
                currentPage = "scaleSetting"
                return
            }

            value = 0.1
            if (actionName === "zoomOut") {
                value = -0.1
            }
            scaleSettingPage.addZoomFactor(value)

            return
        }

        if (actionName === "minFontIn" || actionName === "minFontOut") {
            if (currentPage != "fontSetting") {
                currentPage = "fontSetting"
                return
            }

            value = 1
            if (actionName === "minFontIn") {
                value = -1
            }
            fontSizePage.addFontSize(value)
            return
        }

        // hud
        if (actionName === "toggleHud"
                || actionName === "previousTab" || actionName === "nextTab"
                || actionName === "previousSession" || actionName === "nextSession") {
            if (actionName === "toggleHud") {
                enableAutoHide = false
                cppInputManager.inputRoute = InputTypes.InputRoute.UiFocus
            }

            if (currentPage != "hud") {
                Qt.callLater(function() {
                    currentPage = "hud"
                    hudPage.updateAllData()
                })
                return
            }

            if (actionName === "toggleHud") {
                hide()
            } else {
                hudPage.handleAction(actionName)
            }
            return
        }

        if (actionName === "home" || actionName === "refresh") {
            if (hudPage.visible) {
                hudPage.handleAction(actionName)
            }
        }
    }

    function onGamepadButtonPressed(buttonName) {
        if (currentPage === "hud") {
            if (cppInputManager.inputRoute !== InputTypes.InputRoute.UiFocus
                    && (buttonName === "LT" || buttonName === "RT"
                        || buttonName === "LB" || buttonName === "RB")) {
                return
            }
            hudPage.onGamepadButtonPressed(buttonName)
        }
    }

    function onGamepadButtonReleased(buttonName) {
        if (currentPage === "hud") {
            hudPage.onGamepadButtonReleased(buttonName)
        }
    }

    function show() {
        visible = true
    }

    function hide() {
        visible = false
    }
}