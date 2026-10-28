import QtQuick
import AppUtils
import InputTypes 1.0
import "base"
import "hud"

WindowBase {
    id: mainWindow

    visibility: AppUtils.enableDebug?Window.Windowed:Window.FullScreen
    width: AppUtils.enableDebug?Screen.width*0.75:Screen.width
    height: AppUtils.enableDebug?Screen.height*0.75:Screen.height

    property string currentPage: "home"
    property var navPageList: ["home", "open", "add", "history", "setting", "help", "about"]

    property string lastPage: ""

    property var editedItem: null

    property bool backToBrowserPending: false
    property bool bLongPressHandled: false

    property string helpPreviousPage: "browser"

    property bool isBrowserPage: currentPage === "browser"

    property string focusItemOpName: ""

    readonly property bool shellModalVisible: menuPanel.visible || aerocoreQrPanel.visible

    property int joystickCursorZvalue: 1000
    property int messagePanelZvalue: 1100
    property int aerocoreQrPanelZvalue: 1150
    property int menuPanelZvalue: 1200

    Timer {
        id: activateTimer
        interval: 500
        repeat: true
        running: !isBrowserPage && !cppInputManager.textInputActive
        onTriggered: {
            if (!active && !cppInputManager.textInputActive) {
                requestActivate()
            }
        }
    }

    MessagePanel {
        id: messagePanel
        z: messagePanelZvalue
        successIcon: AppUtils.img("message-success.svg")
        errorIcon: AppUtils.img("message-error.svg")

        Component.onCompleted: {
            AppUtils.onEvent("showMessage", function(data) {
                messagePanel.showMessage(data["success"], data["content"])
            })
        }
    }

    MenuPanel {
        id: menuPanel
        z: menuPanelZvalue

        property var previousFocusItem: null

        Component.onCompleted: {
            AppUtils.onEvent("showMenu", function(menuData) {
                if (menuPanel.visible) {
                    return
                }

                menuPanel.previousFocusItem = Window.window.activeFocusItem
                menuPanel.menuItems = menuData.menuItems
                menuPanel.userData = menuData.userData                
                if (menuData.options && menuData.options.focusOnCancel !== undefined) {
                   menuPanel.focusOnCancel = menuData.options.focusOnCancel
                } else {
                   menuPanel.focusOnCancel = false
                }
                menuPanel.visible = true
            })
        }

        onVisibleChanged: {
            cppInputManager.modalOverlayActive = visible
            if (!visible && previousFocusItem) {
                previousFocusItem.forceActiveFocus()
                previousFocusItem = null
            }
        }
    }

    AerocoreQrPanel {
        id: aerocoreQrPanel
        z: aerocoreQrPanelZvalue
        visible: distributionVariant === "zh-CN"

        property var previousFocusItem: null

        Component.onCompleted: {
            AppUtils.onEvent("showAerocoreQr", function() {
                if (aerocoreQrPanel.visible || menuPanel.visible) {
                    return
                }
                aerocoreQrPanel.previousFocusItem = Window.window.activeFocusItem
                aerocoreQrPanel.visible = true
            })
        }

        onVisibleChanged: {
            cppInputManager.modalOverlayActive = visible
            if (!visible && previousFocusItem) {
                previousFocusItem.forceActiveFocus()
                previousFocusItem = null
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#081a24"

        Item {
            id: navBar
            width: parent.width - anchors.leftMargin
            height: 80
            anchors.left: parent.left
            anchors.leftMargin: 30
            anchors.top: parent.top
            anchors.topMargin: 20
            z: 1

            // logo
            Image {
                width: parent.height
                height: parent.height
                source: AppUtils.img("logo.png")
                fillMode: Image.PreserveAspectFit
            }

            Row {
                id: navBarButtonRow
                anchors.left: parent.left
                anchors.leftMargin: 170
                anchors.top: parent.top
                anchors.topMargin: 15
                height: 60
                spacing: 60

                ImageButton {
                    id: navHomeButton
                    width: height
                    height: parent.height
                    checked: currentPage == "home"
                    normalImage: AppUtils.img("nav-home.svg")
                    checkedImage: AppUtils.img("nav-home-hover.svg")
                    normalOpacity: 1.0
                    focus: true

                    onClicked: {
                        switchPage("home")
                    }

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            switchPage("home")
                        }
                    }
                }

                ImageButton {
                    id: navOpenButton
                    width: height
                    height: parent.height

                    checked: currentPage == "open"
                    normalImage: AppUtils.img("nav-open.svg")
                    checkedImage: AppUtils.img("nav-open-hover.svg")
                    normalOpacity: 1.0

                    onClicked: {
                        switchPage("open")
                    }

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            switchPage("open")
                        }
                    }
                }

                ImageButton {
                    id: navAddButton
                    width: height
                    height: parent.height

                    checked: currentPage == "add"
                    normalImage: AppUtils.img("nav-add.svg")
                    checkedImage: AppUtils.img("nav-add-hover.svg")
                    normalOpacity: 1.0

                    onClicked: {
                        switchPage("add")
                    }

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            switchPage("add")
                        }
                    }
                }

                ImageButton {
                    id: navHistoryButton
                    width: height
                    height: parent.height

                    checked: currentPage == "history"
                    normalImage: AppUtils.img("nav-history.svg")
                    checkedImage: AppUtils.img("nav-history-hover.svg")
                    normalOpacity: 1.0

                    onClicked: {
                        switchPage("history")
                    }

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            switchPage("history")
                        }
                    }
                }

                ImageButton {
                    id: navSettingButton
                    width: height
                    height: parent.height

                    checked: currentPage == "setting"
                    normalImage: AppUtils.img("nav-settings.svg")
                    checkedImage: AppUtils.img("nav-settings-hover.svg")
                    normalOpacity: 1.0

                    Rectangle {
                        visible: cppUpdater.updateAvailable
                        width: 14
                        height: 14
                        radius: width / 2
                        anchors.top: parent.top
                        anchors.right: parent.right
                        color: "#E53935"
                        z: 1

                    }

                    onClicked: {
                        switchPage("setting")
                    }

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            switchPage("setting")
                        }
                    }
                }

                ImageButton {
                    id: navHelpButton
                    width: height
                    height: parent.height

                    checked: currentPage == "help"
                    normalImage: AppUtils.img("nav-help.svg")
                    checkedImage: AppUtils.img("nav-help-hover.svg")
                    normalOpacity: 1.0

                    onClicked: {
                        switchPage("help")
                    }

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            switchPage("help")
                        }
                    }
                }

                ImageButton {
                    id: navAboutButton
                    width: height
                    height: parent.height

                    checked: currentPage == "about"
                    normalImage: AppUtils.img("nav-quit.svg")
                    checkedImage: AppUtils.img("nav-quit-hover.svg")
                    normalOpacity: 1.0

                    property alias focusInterface: focusInterfaceAbout
                    FocusInterface {
                        id: focusInterfaceAbout
                        opName: ""

                        clickHandler: function() {
                            navAboutButton.clicked()
                        }
                    }

                    onClicked: {
                        showQuitMenu()
                    }

                    onActiveFocusChanged: {
                        if (activeFocus) {
                            switchPage("about")
                        }
                    }
                }
            }

            Text {
                id: navLabel
                anchors.top: navBarButtonRow.bottom
                anchors.topMargin: 6
                text: {
                    switch (currentPage) {
                        case "home": return qsTr("Home")
                        case "open": return qsTr("Open")
                        case "add": return qsTr("Add")
                        case "history": return qsTr("History")
                        case "setting": return qsTr("Settings")
                        case "help": return qsTr("Help")
                        case "about": return qsTr("Quit")
                        default: return ""
                    }
                }
                color: "#99FFFFFF"
                font.weight: 500
                font.pixelSize: 18

                x: {
                    switch (currentPage) {
                        case "home": return navBarButtonRow.x + navHomeButton.x + navHomeButton.width / 2 - width / 2
                        case "open": return navBarButtonRow.x + navOpenButton.x + navOpenButton.width / 2 - width / 2
                        case "add": return navBarButtonRow.x + navAddButton.x + navAddButton.width / 2 - width / 2
                        case "history": return navBarButtonRow.x + navHistoryButton.x + navHistoryButton.width / 2 - width / 2
                        case "setting": return navBarButtonRow.x + navSettingButton.x + navSettingButton.width / 2 - width / 2
                        case "help": return navBarButtonRow.x + navHelpButton.x + navHelpButton.width / 2 - width / 2
                        case "about": return navBarButtonRow.x + navAboutButton.x + navAboutButton.width / 2 - width / 2
                        default: return 0
                    }
                }
            }
        }

        Item {
            anchors.fill: parent

            HomePage {
                id: homePage
                visible: currentPage == "home"
            }

            OpenPage {
                id: openPage
                visible: currentPage == "open"
            }

            AddPage {
                id: addPage
                visible: currentPage == "add"
            }

            HistoryPage {
                id: historyPage
                visible: currentPage == "history"
            }

            SettingPage {
                id: settingPage
                visible: currentPage == "setting"
            }

            HelpPage {
                id: helpPage
                visible: currentPage == "help"
            }

            AboutPage {
                id: aboutPage
                visible: currentPage == "about"
            }

            BrowserPage {
                id: browserPage
                visible: currentPage == "browser"
                scaleFactor: mainWindow.scaleFactor
            }
        }
    }

    OperateBar {
        id: opBar
        visible: {
            if (navBar.visible) {
                return true
            } else if (isBrowserPage && browserPage.hudVisible && !browserPage.hudAutoHide) {
                return true
            } else {
                return false
            }
        }
        rightOpName: {
            if (aerocoreQrPanel.visible) {
                return ""
            }
            switch (currentPage) {
                case "home": return "homePageRight"
                case "open": return "openPageRight"
                case "add": return "addPageRight"
                case "share": return "sharePageRight"
                case "history": return "historyPageRight"
                case "setting": return "settingPageRight"
                case "help": return "helpPageRight"
                case "about": return "aboutPageRight"
                case "browser":
                    if (navBar.visible) {
                        return "right"
                    }
                    return "browserPageRight"
                default: return "right"
            }
        }
        leftOpName: {
            if (aerocoreQrPanel.visible) {
                return "aerocoreQrPanel"
            }
            if (isBrowserPage) {
                if (browserPage.hudVisible) {
                    return "browserPageItemPanel"
                } else {
                    return ""
                }
            } else {
                return focusItemOpName
            }
        }
    }

    function getCurrentFocusInterface() {
        var focusedItem = mainWindow.activeFocusItem
        if (!focusedItem) {
            return null
        }

        if (focusedItem.focusInterface) {
            return focusedItem.focusInterface
        } else if (focusedItem.getFocusInterface) {
            return focusedItem.getFocusInterface()
        } else {
            return null
        }
    }

    Connections {
        target: mainWindow

        function onActiveFocusItemChanged() {
            var focusInterface = getCurrentFocusInterface()
            if (focusInterface) {
                // console.info("focus item op name: ", focusInterface.opName)
                focusItemOpName = focusInterface.opName
            } else {
                focusItemOpName = ""
            }
        }
    }

    Connections {
        id: cppInputManagerConnection
        target: cppInputManager

        function onCursorPositionChanged() {            
            if (cppInputManager.inputRoute !== InputTypes.InputRoute.BrowserPointer) {
                return
            }
            if (browserPage.loadingVisible) {
                return
            }
            cursorPanel.changeCursorPos(cppInputManager.cursorPosition.x, cppInputManager.cursorPosition.y)
        }

        function onTextInputActiveChanged() {
            if (cppInputManager.textInputActive) {
                editedItem = mainWindow.activeFocusItem
            } else {
                var item = editedItem
                editedItem = null
                if (item && !isBrowserPage) {
                    Qt.callLater(function() {
                        if (item.visible && !item.activeFocus) {
                            item.forceActiveFocus()
                        }
                    })
                }
            }
        }

        function onCursorSyncRequested() {
            if (cppInputManager.inputRoute !== InputTypes.InputRoute.BrowserPointer) {
                return
            }
            if (browserPage.loadingVisible) {
                return
            }
            cursorPanel.changeCursorPos(cppInputManager.cursorPosition.x, cppInputManager.cursorPosition.y)
        }

        function onInputRouteChanged() {
            if (!isBrowserPage) {
                return
            }
            if (cppInputManager.inputRoute !== InputTypes.InputRoute.BrowserPointer) {
                cursorPanel.hideCursor()
                return
            }
            // Entering page scope: show overlay immediately (system cursor stays hidden
            // in custom-overlay mode; JoystickCursor starts with visible_=false).
            if (!browserPage.loadingVisible) {
                cursorPanel.changeCursorPos(cppInputManager.cursorPosition.x, cppInputManager.cursorPosition.y)
            }
        }

        function onGamepadButtonPressed(buttonName) {
            console.info("click key: ", buttonName)

            if (cppInputManager.textInputActive) {
                if (buttonName === "B") {
                    AppUtils.showKeyboard(false)
                }
                return
            }

            if (isBrowserPage) {
                browserPage.onGamepadButtonPressed(buttonName)
                return
            }

            if (buttonName === "LB") {
                navigatePage(false)
                return
            }
            if (buttonName === "RB") {
                navigatePage(true)
                return
            }
            //     switchPage("browser")
            //     if (ltRtSessionList.length > 1) {
            //         var activeIndex = -1
            //         for (var s = 0; s < ltRtSessionList.length; s++) {
            //             if (ltRtSessionList[s].isActive) {
            //                 activeIndex = s
            //                 break
            //             }
            //         }
            //         var targetIndex
            //         if (activeIndex === -1) {
            //             targetIndex = (buttonName === "LT") ? ltRtSessionList.length - 1 : 0
            //         } else {
            //             var delta = (buttonName === "LT") ? -1 : 1
            //             targetIndex = (activeIndex + delta + ltRtSessionList.length) % ltRtSessionList.length
            //         }
            //         cppMainController.openSession(ltRtSessionList[targetIndex].sessionId)
            //     }
            //     browserPage.handleAction(
            //                 buttonName === "LT" ? "previousSession" : "nextSession")
            //     return
            // }

            var focusInterface = getCurrentFocusInterface()

            if (buttonName === "B") {
                if (shellModalVisible) {
                    closeShellModal()
                    return
                }

                if (focusInterface && focusInterface.backHandler() === true) {
                    return
                }

                var backSessionList = JSON.parse(cppMainController.getSessionListJson())
                if (backSessionList && backSessionList.length > 0) {
                    bLongPressHandled = false
                    backToBrowserPending = true
                }
                return
            }

            if (focusInterface) {
                focusInterface.buttonClicked(buttonName)
            }
        }

        function onGamepadButtonReleased(buttonName) {
            if (isBrowserPage) {
                backToBrowserPending = false
                bLongPressHandled = false
                browserPage.onGamepadButtonReleased(buttonName)
                return
            }

            if (buttonName !== "B") {
                return
            }
            if (!backToBrowserPending) {
                return
            }
            backToBrowserPending = false
            if (bLongPressHandled) {
                bLongPressHandled = false
                return
            }
            if (shellModalVisible) {
                return
            }

            var returnPage = (currentPage === "help" && helpPreviousPage !== "browser")
                    ? helpPreviousPage : "browser"

            var backSessionList = JSON.parse(cppMainController.getSessionListJson())
            if (!backSessionList || backSessionList.length === 0) {
                if (returnPage !== "browser") {
                    switchPage(returnPage)
                }
                return
            }

            var targetSession = null
            for (var i = 0; i < backSessionList.length; i++) {
                if (backSessionList[i].isActive) {
                    targetSession = backSessionList[i]
                    break
                }
            }
            if (!targetSession) {
                targetSession = backSessionList[0]
            }
            if (buttonName === "B") {
                cppInputManager.consumeBackButton()
            }
            switchPage(returnPage)
            if (returnPage === "browser") {
                cppMainController.openSession(targetSession.sessionId)
            }
        }

        function onGamepadButtonLongPressed(buttonName) {
            if (buttonName !== "B" || isBrowserPage) {
                return
            }
            bLongPressHandled = true
            backToBrowserPending = false
            if (shellModalVisible) {
                return
            }
            cppInputManager.consumeBackButton()
            showQuitMenu()
        }

        function onActionTriggered(action) {
            console.info("action:" + action)

            if (action === "openHelp") {
                helpPreviousPage = currentPage
                switchPage("help")
                return
            }

            if (!isBrowserPage) {                
                return
            }

            if (action === "closeSession") {
                if (browserPage.deleteSession()) {
                    switchPage("home")
                }
                return
            }            

            if (action === "back") {
                if (navBar.visible) {
                    navBar.visible = false
                    return
                }

                if (browserPage.goBack()) {
                    return
                }

                switchPage("home")
                return
            }

            if (action === "home") {
                if (!browserPage.handleAction(action)) {
                    switchPage("home")
                }
                return;
            }

            if (action === "history") {
                switchPage("history")
                return;
            }

            if (action === "address") {
                switchPage("open")
                return;
            }

            if (action === "homeFocusToggle") {
                navBar.visible = true
                return
            }

            if (action === "previousTab") {
                if (navBar.visible) {
                    navigatePage(false)
                    return
                }
            }
            if (action === "nextTab") {
                if (navBar.visible) {
                    navigatePage(true)
                    return
                }
            }

            browserPage.handleAction(action)
        }
    }

    Component.onCompleted: {
        AppUtils.scaleFactor = scaleFactor
        console.log("scale factor: ", scaleFactor)

        AppUtils.onEvent("switchPage", switchPage)
        AppUtils.onEvent("openUrl", openUrl)
        AppUtils.onEvent("showQuitMenu", showQuitMenu)
        AppUtils.onEvent("exitApp", function() {
            Qt.callLater(function() {
                mainWindow.close()
            })
        })
        AppUtils.onEvent("menuClick", function(menuData) {
            if (menuData.id === "exitApp") {
                Qt.callLater(function() {
                    mainWindow.close()
                })
            }
        })
        updateInputRoute()
    }

    onScaleFactorChanged: {
        AppUtils.scaleFactor = scaleFactor
        console.log("scale factor: ", scaleFactor)
    }

    function closeShellModal() {
        if (menuPanel.visible) {
            menuPanel.visible = false
        }
        if (aerocoreQrPanel.visible) {
            aerocoreQrPanel.close()
        }
    }

    function showQuitMenu() {
        if (shellModalVisible) {
            return
        }
        var menuItems = [{"id":"exitApp", "name":qsTr("Quit application")}]
        AppUtils.showMenu(menuItems, null, {"focusOnCancel": true})
    }

    function switchPage(newPage) {
        if (aerocoreQrPanel.visible) {
            aerocoreQrPanel.close()
        }
        lastPage = currentPage
        currentPage = newPage

        navBar.visible = !isBrowserPage
        updateInputRoute()

        if (isBrowserPage) {
            Qt.callLater(function() {
                if (!browserPage.loadingVisible) {
                    cursorPanel.changeCursorPos(
                                cppInputManager.cursorPosition.x,
                                cppInputManager.cursorPosition.y)
                }
            })
        }

        if (isBrowserPage) {
            cppBrowserController.showBrowser()
        } else {
            cppBrowserController.goHome()
        }
    }

    function updateInputRoute() {
        cppInputManager.inputRoute = (currentPage === "browser")
            ? InputTypes.InputRoute.BrowserPointer
            : InputTypes.InputRoute.UiFocus
    }

    function navigatePage(next) {
        if (!navBar.visible) {
            return
        }

        var index = navPageList.indexOf(currentPage)
        if (index === -1) {
            switchPage("home")
            return
        }

        if (next) {
            if (index < navPageList.length - 1) {
                switchPage(navPageList[index + 1])
            } else {
                switchPage(navPageList[0])
            }
        } else {
            if (index > 0) {
                switchPage(navPageList[index - 1])
            } else {
                switchPage(navPageList[navPageList.length - 1])
            }
        }
    }

    function openUrl(param) {
        var t0 = Date.now()
        var timingOn = browserPage.browserContainer.isLoadTimingEnabled()
        if (timingOn) {
            console.info("[load-timing] MainWindow.openUrl start type=" + param.type + " url=" + param.url)
        }
        switchPage("browser")
        if (timingOn) {
            console.info("[load-timing] MainWindow after switchPage +" + (Date.now() - t0) + "ms")
        }
        browserPage.openUrl(param)
        if (timingOn) {
            console.info("[load-timing] MainWindow after browserPage.openUrl +" + (Date.now() - t0) + "ms total")
        }
    }

    JoystickCursor {
        id: cursorPanel
        anchors.fill: parent
        cursorImage: AppUtils.img("cursor-pointer.svg")
        cursorSize: cppSettings.cursorSize
        autoHideDelay: cppSettings.cursorAutoHideDelay * 1000
        uiScale: Math.max(mainWindow.scaleFactor, 0.01)
        z: joystickCursorZvalue
        visible: {
            if (!isBrowserPage) {
                return false
            }
            if (browserPage.loadingVisible) {
                return false
            }
            if (browserPage.hudVisible && !browserPage.hudAutoHide) {
                return false
            }
            return true
        }

        Connections {
            target: browserPage
            function onLoadingVisibleChanged() {
                if (browserPage.loadingVisible) {
                    cursorPanel.hideCursor()
                    return
                }
                if (!isBrowserPage) {
                    return
                }
                if (cppInputManager.inputRoute !== InputTypes.InputRoute.BrowserPointer) {
                    return
                }
                cursorPanel.changeCursorPos(
                            cppInputManager.cursorPosition.x,
                            cppInputManager.cursorPosition.y)
            }
        }
    }
}
