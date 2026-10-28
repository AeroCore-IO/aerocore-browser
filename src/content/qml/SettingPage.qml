import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "base"

PageBase {
    id: settingPage
    pageName: "setting"
    firstFocusItem: inputSettingMenu

    property string currentMenuName: ""

    component SettingMenuItem : Rectangle {
        id: menuItem
        width: parent.width
        height: 80
        activeFocusOnTab: true
        color: {
            if (activeFocus) {
                return "#00969A"
            } else if (menuName === currentMenuName) {
                return "#3300969A"
            } else {
                return "transparent"
            }
        }

        property string menuName: ""

        property string icon: ""

        property string name: ""

        property string description: ""

        Image {
            width: 60
            height: 60
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            source: menuItem.icon
            fillMode: Image.PreserveAspectFit
        }

        Text {
            anchors.top: parent.top
            anchors.topMargin: 10
            anchors.left: parent.left
            anchors.leftMargin: 80
            anchors.right: parent.right
            anchors.rightMargin: 10
            font.weight: 500
            font.pixelSize: 24
            color: "#FFFFFF"
            text: menuItem.name
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            clip: true
        }

        Rectangle {
            visible: menuItem.menuName === "system" && cppUpdater.updateAvailable
            width: 14
            height: 14
            radius: width / 2
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 10
            anchors.rightMargin: 12
            color: "#E53935"
            z: 1

        }

        Text {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            anchors.left: parent.left
            anchors.leftMargin: 80
            anchors.right: parent.right
            anchors.rightMargin: 10
            font.weight: 400
            font.pixelSize: 16
            color: "#99FFFFFF"
            text: menuItem.description
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            clip: true
        }

        onActiveFocusChanged: {
            if (activeFocus) {
                currentMenuName = menuName
            }
        }
    }

    component SettingItemPanel : Rectangle {
        id: settingItemPanel
        width: parent.width
        height: Math.max(100, settingItemDescription.y + settingItemDescription.implicitHeight + 12)
        color: {
            if (canEdit) {
                return "#9900969A"
            } else if (activeFocus) {
                return "#6600969A"
            } else {
                return "#1A00969A"
            }
        }
        radius: 8
        activeFocusOnTab: true

        property bool canEdit: false
        property real descriptionRightMargin: 30
        property var menuItems: [{"id":"resetDisplaySetting", "name":qsTr("Reset display settings")}]
        property var menuUserData: null

        property string title: ""

        property string description: ""

        Text {
            id: settingItemTitle
            anchors.top: parent.top
            anchors.topMargin: 15
            anchors.left: parent.left
            anchors.leftMargin: 30
            font.weight: 500
            font.pixelSize: 24
            color: "#FFFFFF"
            text: settingItemPanel.title
        }

        Text {
            id: settingItemDescription
            anchors.top: settingItemTitle.bottom
            anchors.topMargin: 2
            anchors.left: parent.left
            anchors.leftMargin: 30
            anchors.right: parent.right
            anchors.rightMargin: settingItemPanel.descriptionRightMargin
            height: implicitHeight
            font.weight: 400
            font.pixelSize: 20
            color: "#99FFFFFF"
            text: settingItemPanel.description.replace(/\s+/g, " ").trim()
            wrapMode: Text.WordWrap
        }


        onActiveFocusChanged: {
            if (!activeFocus) {
                canEdit = false
                return
            }
            settingItemPanel.scrollIntoEnclosingView()
        }

        function scrollIntoEnclosingView() {
            var p = settingItemPanel.parent
            while (p) {
                if (typeof p.scrollItemIntoView === "function") {
                    p.scrollItemIntoView(settingItemPanel)
                    return
                }
                p = p.parent
            }
        }

        property alias focusInterface: focusInterface
        FocusInterface {
            id: focusInterface
            opName: "settingPageItemPanel"

            clickHandler: function() {
                settingItemPanel.canEdit = !settingItemPanel.canEdit
            }

            backHandler: function() {
                if (canEdit) {
                    canEdit = false
                    return true
                }

                if (parent.backHandler) {
                    return parent.backHandler()
                }
                return false
            }

            menuHandler: function() {
                if (!settingItemPanel.menuItems || settingItemPanel.menuItems.length === 0) {
                    return
                }
                AppUtils.showMenu(settingItemPanel.menuItems, settingItemPanel.menuUserData)
            }
        }
    }

    component InfoItemPanel : Rectangle {
        id: infoItemPanel
        width: parent.width
        height: 100
        color: infoItemPanel.activeFocus ? "#6600969A" : "#1A00969A"
        radius: 8

        onActiveFocusChanged: {
            if (!activeFocus) {
                return
            }
            var p = infoItemPanel.parent
            while (p) {
                if (typeof p.scrollItemIntoView === "function") {
                    p.scrollItemIntoView(infoItemPanel)
                    return
                }
                p = p.parent
            }
        }

        property string title: ""

        property string value: ""

        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 30
            font.weight: 500
            font.pixelSize: 24
            color: "#FFFFFF"
            text: infoItemPanel.title
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 30
            anchors.left: parent.left
            anchors.leftMargin: 300
            horizontalAlignment: Text.AlignRight
            font.weight: 400
            font.pixelSize: 22
            color: "#99FFFFFF"
            text: infoItemPanel.value
            clip: true
        }
    }

    component ProgressItem : Item {
        id: progressItem
        height: 45

        property int currentPercent: 50

        property string text: ""

        function add(percent) {
            var newPercent = currentPercent + percent
            newPercent = Math.min(Math.max(newPercent, 0), 100)
            currentPercent = newPercent
        }

        Image {
            width: 30
            height: 30
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            source: AppUtils.img("minus.svg")
            fillMode: Image.PreserveAspectFit
        }

        Image {
            width: 30
            height: 30
            anchors.right: parent.right
            anchors.rightMargin: 183
            anchors.verticalCenter: parent.verticalCenter
            source: AppUtils.img("plus.svg")
            fillMode: Image.PreserveAspectFit
        }

        Rectangle {
            id: background
            height: 8
            anchors.left: parent.left
            anchors.leftMargin: 50
            anchors.right: parent.right
            anchors.rightMargin: 233
            anchors.verticalCenter: parent.verticalCenter
            radius: 4
            color: "#1A00969A"
        }

        Rectangle {
            id: foreground
            width: {
                var newWidth = Math.min(Math.round(progressItem.currentPercent/100*background.width), background.width)
                if (newWidth < 22) {
                    newWidth = 22
                }
                return newWidth
            }
            height: 8
            anchors.left: parent.left
            anchors.leftMargin: 50
            anchors.verticalCenter: parent.verticalCenter
            radius: 4
            color: "#00969A"
        }

        Rectangle {
            width: 22
            height: 22
            anchors.right: foreground.right
            anchors.verticalCenter: foreground.verticalCenter
            radius: 22
            color: "#BDD7E4"
        }

        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            font.weight: 400
            font.pixelSize: 32
            color: "#E8F2FC"
            text: progressItem.text?progressItem.text:progressItem.currentPercent.toString()+"%"
        }
    }

    component OptionGroup : Flow {
        id: optionGroupFlow
        height: 70
        spacing: 20

        property var optionNames: []
        property int optionWidth: 188

        property int currentIndex: 0

        Repeater {
            id: optionGroupRepeater
            model: optionNames

            delegate: Rectangle {
                width: optionGroupFlow.optionWidth
                height: optionGroupFlow.height
                radius: 10
                color: index===currentIndex?"#00969A":"#1A00969A"

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 16
                    height: parent.height - 8
                    font.weight: 400
                    font.pixelSize: 24
                    color: "white"
                    text: modelData
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WordWrap
                    clip: true
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
        radius: 20
        color: "#1A00969A"

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 28
            anchors.top: parent.top
            anchors.topMargin: 32
            font.weight: 600
            font.pixelSize: 34
            color: "#EEF7FF"
            text: qsTr("Settings")
        }

        Flow {
            id: settingMenuFlow
            width: parent.width
            anchors.top: parent.top
            anchors.topMargin: 120
            spacing: 20

            SettingMenuItem {
                id: displaySettingMenu
                visible: false
                menuName: "display"
                name: qsTr("Display and readability")
                icon: AppUtils.img("display-setting.svg")
                description: qsTr("Interface scaling (does not affect web pages)")

                Keys.onDownPressed: {
                    inputSettingMenu.forceActiveFocus()
                }

                Keys.onRightPressed: function(event) {
                    appUiScalePanel.forceActiveFocus()
                }

                property alias focusInterface: displayMenuFocusInterface
                FocusInterface {
                    id: displayMenuFocusInterface
                    clickHandler: function() {
                        appUiScalePanel.forceActiveFocus()
                    }
                }
            }

            SettingMenuItem {
                id: inputSettingMenu
                menuName: "input"
                name: qsTr("Input and controller")
                icon: AppUtils.img("setting-input.svg")
                description: qsTr("Cursor speed, size, auto-hide, and edge scrolling")

                Keys.onDownPressed: {
                    privacySettingMenu.forceActiveFocus()
                }

                Keys.onRightPressed: function(event) {
                    cursorSpeedPanel.forceActiveFocus()
                }

                property alias focusInterface: inputMenuFocusInterface
                FocusInterface {
                    id: inputMenuFocusInterface
                    clickHandler: function() {
                        cursorSpeedPanel.forceActiveFocus()
                    }
                }
            }

            SettingMenuItem {
                id: privacySettingMenu
                menuName: "privacy"
                name: qsTr("Privacy and data")
                icon: AppUtils.img("setting-privacy.svg")
                description: qsTr("Clear browsing history, sign-in data, and cache")

                Keys.onUpPressed: {
                    inputSettingMenu.forceActiveFocus()
                }

                Keys.onDownPressed: {
                    systemSettingMenu.forceActiveFocus()
                }

                Keys.onRightPressed: function(event) {
                    clearHistoryPanel.forceActiveFocus()
                }

                property alias focusInterface: privacyMenuFocusInterface
                FocusInterface {
                    id: privacyMenuFocusInterface
                    clickHandler: function() {
                        clearHistoryPanel.forceActiveFocus()
                    }
                }
            }

            SettingMenuItem {
                id: systemSettingMenu
                menuName: "system"
                name: qsTr("System and about")
                icon: AppUtils.img("setting-system.svg")
                description: qsTr("Version, GPU mode, and data directory")

                Keys.onUpPressed: {
                    privacySettingMenu.forceActiveFocus()
                }

                Keys.onRightPressed: function(event) {
                    gpuModePanel.forceActiveFocus()
                }

                property alias focusInterface: systemMenuFocusInterface
                FocusInterface {
                    id: systemMenuFocusInterface
                    clickHandler: function() {
                        gpuModePanel.forceActiveFocus()
                    }
                }
            }
        }
    }

    Item {
        anchors.left: parent.left
        anchors.leftMargin: 450
        anchors.right: parent.right
        anchors.rightMargin: 30
        anchors.top: parent.top
        anchors.topMargin: 135
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 70

        ScrollViewBase {
            id: displayScrollView
            anchors.fill: parent
            visible: currentMenuName === "display"

            Flow {
                id: displayFlow
                width: displayScrollView.width
                spacing: 10

                Text {
                    width: parent.width
                    font.weight: 600
                    font.pixelSize: 24
                    color: "#EEF7FF"
                    text: qsTr("Interface scale")
                }

                SettingItemPanel {
                    id: appUiScalePanel
                    title: qsTr("App UI scale")
                    description: qsTr("Adjust the size of menus, text, and interface elements (does not affect web pages)")

                    ProgressItem {
                        id: appUiScaleProgress
                        anchors.left: parent.left
                        anchors.leftMargin: 587
                        anchors.right: parent.right
                        anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                        currentPercent: Math.round((cppSettings.appUiScale - 1.0) / 1.4 * 100)
                        text: Math.round(cppSettings.appUiScale * 100).toString() + "%"
                    }

                    Keys.onLeftPressed: {
                        if (canEdit) {
                            addAppUiScale(-0.1)
                        } else {
                            displaySettingMenu.forceActiveFocus()
                        }
                    }

                    Keys.onRightPressed: {
                        if (!canEdit) {
                            return
                        }
                        addAppUiScale(0.1)
                    }
                }

                property var backHandler: function() {
                    displaySettingMenu.forceActiveFocus()
                    return true
                }
            }
        }

        ScrollViewBase {
            id: inputScrollView
            anchors.fill: parent
            visible: currentMenuName === "input"

            Flow {
                id: inputFlow
                width: inputScrollView.width
                spacing: 10

                Text {
                    width: parent.width
                    font.weight: 600
                    font.pixelSize: 24
                    color: "#EEF7FF"
                    text: qsTr("Cursor and scrolling")
                }

                SettingItemPanel {
                    id: cursorSpeedPanel
                    title: qsTr("Base cursor speed")
                    description: qsTr("Base cursor movement speed controlled by the gamepad stick")
                    descriptionRightMargin: 900

                    ProgressItem {
                        id: cursorSpeedProgress
                        anchors.left: parent.left
                        anchors.leftMargin: 587
                        anchors.right: parent.right
                        anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                        currentPercent: Math.round((cppSettings.cursorSpeedScale - 0.5) / 1.5 * 100)
                        text: Math.round(cppSettings.cursorSpeedScale * 100).toString() + "%"
                    }

                    Keys.onDownPressed: {
                        wheelSpeedPanel.forceActiveFocus()
                    }

                    Keys.onLeftPressed: {
                        if (canEdit) {
                            addCursorSpeedScale(-0.1)
                        } else {
                            inputSettingMenu.forceActiveFocus()
                        }
                    }

                    Keys.onRightPressed: {
                        if (!canEdit) {
                            return
                        }
                        addCursorSpeedScale(0.1)
                    }
                }

                SettingItemPanel {
                    id: wheelSpeedPanel
                    title: qsTr("Base scrolling speed")
                    description: qsTr("Web page scrolling speed controlled by the gamepad stick")
                    descriptionRightMargin: 900

                    ProgressItem {
                        id: wheelSpeedProgress
                        anchors.left: parent.left
                        anchors.leftMargin: 587
                        anchors.right: parent.right
                        anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                        currentPercent: Math.round((cppSettings.wheelSpeedScale - 0.5) / 1.5 * 100)
                        text: Math.round(cppSettings.wheelSpeedScale * 100).toString() + "%"
                    }

                    Keys.onUpPressed: {
                        cursorSpeedPanel.forceActiveFocus()
                    }

                    Keys.onDownPressed: {
                        cursorSizePanel.forceActiveFocus()
                    }

                    Keys.onLeftPressed: {
                        if (canEdit) {
                            addWheelSpeedScale(-0.1)
                        } else {
                            inputSettingMenu.forceActiveFocus()
                        }
                    }

                    Keys.onRightPressed: {
                        if (!canEdit) {
                            return
                        }
                        addWheelSpeedScale(0.1)
                    }
                }

                SettingItemPanel {
                    id: cursorSizePanel
                    title: qsTr("Cursor size")
                    description: qsTr("Gamepad cursor size on browser pages")
                    descriptionRightMargin: 850

                    OptionGroup {
                        id: cursorSizeOptionGroup
                        anchors.right: parent.right
                        anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                        optionNames: [qsTr("Small (44 px)"), qsTr("Medium (56 px)"), qsTr("Large (68 px)"), qsTr("Extra large (80 px)")]
                        currentIndex: 1

                        onCurrentIndexChanged: {
                            cppSettings.cursorSize = getCurrentValue()
                        }

                        function setCurrentValue(value) {
                            if (value <= 44) {
                                currentIndex = 0
                            } else if (value <= 56) {
                                currentIndex = 1
                            } else if (value <= 68) {
                                currentIndex = 2
                            } else {
                                currentIndex = 3
                            }
                        }

                        function getCurrentValue() {
                            switch(currentIndex) {
                            case 0:
                                return 44
                            case 1:
                                return 56
                            case 2:
                                return 68
                            case 3:
                                return 80
                            default:
                                return 56
                            }
                        }
                    }

                    Keys.onUpPressed: {
                        wheelSpeedPanel.forceActiveFocus()
                    }

                    Keys.onDownPressed: {
                        cursorAutoHidePanel.forceActiveFocus()
                    }

                    Keys.onLeftPressed: {
                        if (canEdit) {
                            if (cursorSizeOptionGroup.currentIndex > 0) {
                                cursorSizeOptionGroup.currentIndex -= 1
                            }
                        } else {
                            inputSettingMenu.forceActiveFocus()
                        }
                    }

                    Keys.onRightPressed: {
                        if (!canEdit) {
                            return
                        }

                        if (cursorSizeOptionGroup.currentIndex < cursorSizeOptionGroup.optionNames.length-1) {
                            cursorSizeOptionGroup.currentIndex += 1
                        }
                    }
                }

                SettingItemPanel {
                    id: cursorAutoHidePanel
                    title: qsTr("Auto-hide cursor")
                    description: qsTr("Hide the cursor after the configured idle time; 0 seconds disables auto-hide")
                    descriptionRightMargin: 900

                    ProgressItem {
                        id: cursorAutoHideProgress
                        anchors.left: parent.left
                        anchors.leftMargin: 587
                        anchors.right: parent.right
                        anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                        currentPercent: cppSettings.cursorAutoHideDelay * 10
                        text: cppSettings.cursorAutoHideDelay === 0
                              ? qsTr("Never")
                              : qsTr("%1 sec").arg(cppSettings.cursorAutoHideDelay)
                    }

                    Keys.onUpPressed: {
                        cursorSizePanel.forceActiveFocus()
                    }

                    Keys.onDownPressed: {
                        edgeScrollPanel.forceActiveFocus()
                    }

                    Keys.onLeftPressed: {
                        if (canEdit) {
                            addCursorAutoHideDelay(-1)
                        } else {
                            inputSettingMenu.forceActiveFocus()
                        }
                    }

                    Keys.onRightPressed: {
                        if (!canEdit) {
                            return
                        }
                        addCursorAutoHideDelay(1)
                    }
                }

                SettingItemPanel {
                    id: edgeScrollPanel
                    title: qsTr("Edge scrolling")
                    description: qsTr("Scroll the page when the cursor reaches the edge of the screen")
                    descriptionRightMargin: 500

                    OptionGroup {
                        id: edgeScrollOptionGroup
                        anchors.right: parent.right
                        anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                        optionNames: [qsTr("On"), qsTr("Off")]
                        currentIndex: 0

                        onCurrentIndexChanged: {
                            cppSettings.edgeScrollEnabled = (currentIndex === 0)
                        }
                    }

                    Keys.onUpPressed: {
                        cursorAutoHidePanel.forceActiveFocus()
                    }

                    Keys.onDownPressed: {
                        gamepadInfoPanel.forceActiveFocus()
                    }

                    Keys.onLeftPressed: {
                        if (canEdit) {
                            if (edgeScrollOptionGroup.currentIndex > 0) {
                                edgeScrollOptionGroup.currentIndex -= 1
                            }
                        } else {
                            inputSettingMenu.forceActiveFocus()
                        }
                    }

                    Keys.onRightPressed: {
                        if (!canEdit) {
                            return
                        }

                        if (edgeScrollOptionGroup.currentIndex < edgeScrollOptionGroup.optionNames.length-1) {
                            edgeScrollOptionGroup.currentIndex += 1
                        }
                    }
                }

                InfoItemPanel {
                    id: gamepadInfoPanel
                    title: qsTr("Connected controller")
                    value: cppSettings.inputDeviceName ? cppSettings.inputDeviceName : qsTr("Not connected")

                    activeFocusOnTab: true

                    Keys.onUpPressed: {
                        edgeScrollPanel.forceActiveFocus()
                    }

                    Keys.onLeftPressed: {
                        inputSettingMenu.forceActiveFocus()
                    }
                }

                property var backHandler: function() {
                    inputSettingMenu.forceActiveFocus()
                    return true
                }
            }
        }

        ScrollViewBase {
            id: privacyScrollView
            anchors.fill: parent
            visible: currentMenuName === "privacy"

            Flow {
                id: privacyFlow
                width: privacyScrollView.width
                spacing: 10

                Text {
                    width: parent.width
                    font.weight: 600
                    font.pixelSize: 24
                    color: "#EEF7FF"
                    text: qsTr("Clear data")
                }

                SettingItemPanel {
                    id: clearHistoryPanel
                    title: qsTr("Clear browsing history")
                    description: qsTr("Delete all saved browsing history")

                    Keys.onDownPressed: {
                        clearCookiesPanel.forceActiveFocus()
                    }

                    Keys.onLeftPressed: {
                        privacySettingMenu.forceActiveFocus()
                    }

                    focusInterface.clickHandler: function() {
                        showClearConfirmMenu("confirmClearHistory", qsTr("Clear browsing history"))
                    }
                }

                SettingItemPanel {
                    id: clearCookiesPanel
                    title: qsTr("Clear sign-in data")
                    description: qsTr("Clear website cookies; you will need to sign in again")

                    Keys.onUpPressed: {
                        clearHistoryPanel.forceActiveFocus()
                    }

                    Keys.onDownPressed: {
                        clearCachePanel.forceActiveFocus()
                    }

                    Keys.onLeftPressed: {
                        privacySettingMenu.forceActiveFocus()
                    }

                    focusInterface.clickHandler: function() {
                        showClearConfirmMenu("confirmClearCookies", qsTr("Clear sign-in data"))
                    }
                }

                SettingItemPanel {
                    id: clearCachePanel
                    title: qsTr("Clear cache")
                    description: qsTr("Clear cached web images, scripts, and other data")

                    Keys.onUpPressed: {
                        clearCookiesPanel.forceActiveFocus()
                    }

                    Keys.onDownPressed: {
                        clearAllPanel.forceActiveFocus()
                    }

                    Keys.onLeftPressed: {
                        privacySettingMenu.forceActiveFocus()
                    }

                    focusInterface.clickHandler: function() {
                        showClearConfirmMenu("confirmClearCache", qsTr("Clear cache"))
                    }
                }

                SettingItemPanel {
                    id: clearAllPanel
                    title: qsTr("Clear all data")
                    description: qsTr("Clear browsing history, sign-in data, and cache")

                    Keys.onUpPressed: {
                        clearCachePanel.forceActiveFocus()
                    }

                    Keys.onLeftPressed: {
                        privacySettingMenu.forceActiveFocus()
                    }

                    focusInterface.clickHandler: function() {
                        showClearConfirmMenu("confirmClearAll", qsTr("Clear all data"))
                    }
                }

                property var backHandler: function() {
                    privacySettingMenu.forceActiveFocus()
                    return true
                }
            }
        }

        ScrollViewBase {
            id: systemScrollView
            anchors.fill: parent
            visible: currentMenuName === "system"

            Flow {
                id: systemFlow
                width: systemScrollView.width
                spacing: 10

                Text {
                    width: parent.width
                    font.weight: 600
                    font.pixelSize: 24
                    color: "#EEF7FF"
                    text: qsTr("System")
                }

                SettingItemPanel {
                    id: gpuModePanel
                    title: qsTr("GPU mode")
                    description: cppSettings.restartRequired ? qsTr("GPU acceleration for web rendering (restart the app for changes to take effect)") : qsTr("GPU acceleration for web rendering")
                    descriptionRightMargin: 650

                    OptionGroup {
                        id: gpuModeOptionGroup
                        anchors.right: parent.right
                        anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                        optionNames: [qsTr("Automatic"), qsTr("Hardware acceleration"), qsTr("Software rendering")]
                        currentIndex: 0

                        onCurrentIndexChanged: {
                            cppSettings.gpuMode = getCurrentValue()
                        }

                        function setCurrentValue(value) {
                            switch(value) {
                            case "unsafe":
                                currentIndex = 1
                                break
                            case "off":
                                currentIndex = 2
                                break
                            default:
                                currentIndex = 0
                            }
                        }

                        function getCurrentValue() {
                            switch(currentIndex) {
                            case 1:
                                return "unsafe"
                            case 2:
                                return "off"
                            default:
                                return "auto"
                            }
                        }
                    }

                    Keys.onDownPressed: {
                        profilePathPanel.forceActiveFocus()
                    }

                    Keys.onLeftPressed: {
                        if (canEdit) {
                            if (gpuModeOptionGroup.currentIndex > 0) {
                                gpuModeOptionGroup.currentIndex -= 1
                            }
                        } else {
                            systemSettingMenu.forceActiveFocus()
                        }
                    }

                    Keys.onRightPressed: {
                        if (!canEdit) {
                            return
                        }

                        if (gpuModeOptionGroup.currentIndex < gpuModeOptionGroup.optionNames.length-1) {
                            gpuModeOptionGroup.currentIndex += 1
                        }
                    }
                }

                InfoItemPanel {
                    id: profilePathPanel
                    title: qsTr("Data directory")
                    value: cppSettings.cefProfilePath

                    activeFocusOnTab: true

                    Keys.onUpPressed: {
                        gpuModePanel.forceActiveFocus()
                    }

                    Keys.onDownPressed: {
                        logPathPanel.forceActiveFocus()
                    }

                    Keys.onLeftPressed: {
                        systemSettingMenu.forceActiveFocus()
                    }
                }

                InfoItemPanel {
                    id: logPathPanel
                    title: qsTr("Log directory")
                    value: cppSettings.cefLogPath

                    activeFocusOnTab: true

                    Keys.onUpPressed: {
                        profilePathPanel.forceActiveFocus()
                    }

                    Keys.onDownPressed: {
                        versionInfoPanel.forceActiveFocus()
                    }

                    Keys.onLeftPressed: {
                        systemSettingMenu.forceActiveFocus()
                    }
                }

                InfoItemPanel {
                    id: versionInfoPanel
                    title: qsTr("Version")
                    value: cppSettings.appVersion

                    activeFocusOnTab: true

                    Keys.onUpPressed: {
                        logPathPanel.forceActiveFocus()
                    }

                    Keys.onDownPressed: {
                        checkUpdateButton.forceActiveFocus()
                    }

                    Keys.onLeftPressed: {
                        systemSettingMenu.forceActiveFocus()
                    }
                }

                SettingItemPanel {
                    id: updatePanel
                    title: qsTr("Software updates")
                    description: cppUpdater.statusText
                    descriptionRightMargin: 350
                    activeFocusOnTab: false

                    TextButton {
                        id: checkUpdateButton
                        width: 300
                        height: 64
                        anchors.right: parent.right
                        anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                        normalBgColor: "#334D5961"
                        focusBgColor: "#00969A"
                        text: cppUpdater.busy
                              ? qsTr("Processing…")
                              : (cppUpdater.phase === "succeeded"
                                 ? qsTr("Restart after quitting")
                                 : (cppUpdater.updateAvailable ? qsTr("Update now") : qsTr("Check for updates")))
                        textColor: "#FFFFFF"
                        textFont.pixelSize: 24
                        radius: 6

                        onClicked: {
                            if (cppUpdater.busy) {
                                return
                            }
                            if (cppUpdater.phase === "succeeded") {
                                AppUtils.emitEvent("showQuitMenu")
                            } else if (cppUpdater.updateAvailable) {
                                cppUpdater.apply()
                            } else {
                                cppUpdater.check()
                            }
                        }

                        navigateUp: versionInfoPanel
                        navigateLeft: systemSettingMenu
                    }

                }

                property var backHandler: function() {
                    systemSettingMenu.forceActiveFocus()
                    return true
                }
            }
        }
    }

    onVisibleChanged: {
        if (!visible) {
            return
        }
        cursorSizeOptionGroup.setCurrentValue(cppSettings.cursorSize)
        edgeScrollOptionGroup.currentIndex = cppSettings.edgeScrollEnabled ? 0 : 1
        gpuModeOptionGroup.setCurrentValue(cppSettings.gpuMode)
    }

    Component.onCompleted: {
        AppUtils.onEvent("menuClick", handleMenuClick)
    }

    function handleMenuClick(menuData) {
        switch (menuData.id) {
        case "resetDisplaySetting":
            cppSettings.resetAppUiScale()
            AppUtils.showMessage(true, qsTr("Interface scaling restored to automatic"))
            break
        case "confirmClearHistory":
            cppSettings.clearBrowsingHistory()
            AppUtils.showMessage(true, qsTr("Browsing history cleared"))
            break
        case "confirmClearCookies":
            cppSettings.clearCefCookies()
            AppUtils.showMessage(true, qsTr("Sign-in data cleared"))
            break
        case "confirmClearCache":
            cppSettings.clearCefCache()
            AppUtils.showMessage(true, qsTr("Cache cleared"))
            break
        case "confirmClearAll":
            cppSettings.clearBrowsingHistory()
            cppSettings.clearCefCookies()
            cppSettings.clearCefCache()
            AppUtils.showMessage(true, qsTr("All privacy data cleared"))
            break
        default:
            break
        }
    }

    function showClearConfirmMenu(menuId, actionName) {
        var menuItems = [{"id": menuId, "name": qsTr("Confirm %1").arg(actionName)}]
        AppUtils.showMenu(menuItems, null, {"focusOnCancel": true})
    }

    function addAppUiScale(value) {
        var newScale = cppSettings.appUiScale + value
        newScale = Math.min(Math.max(newScale, 1.0), 2.4)
        cppSettings.appUiScale = newScale
    }

    function addCursorSpeedScale(value) {
        var newScale = cppSettings.cursorSpeedScale + value
        newScale = Math.min(Math.max(newScale, 0.5), 2.0)
        cppSettings.cursorSpeedScale = newScale
    }

    function addWheelSpeedScale(value) {
        var newScale = cppSettings.wheelSpeedScale + value
        newScale = Math.min(Math.max(newScale, 0.5), 2.0)
        cppSettings.wheelSpeedScale = newScale
    }

    function addCursorAutoHideDelay(value) {
        var newDelay = cppSettings.cursorAutoHideDelay + value
        newDelay = Math.min(Math.max(newDelay, 0), 10)
        cppSettings.cursorAutoHideDelay = newDelay
    }
}
