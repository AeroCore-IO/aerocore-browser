import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Effects
import "../base"

Item {
    id: fontSizePage
    width: 832
    height: 110
    anchors.top: parent.top
    anchors.topMargin: 20
    anchors.horizontalCenter: parent.horizontalCenter

    property var browserContainer: null

    property int defaultMinimumFontSize: 16

    property int minValue: 12
    property int maxValue: 24

    component ProgressItem : Item {
        id: progressItem
        height: 30

        property int currentPercent: 50

        function add(percent) {
            var newPercent = currentPercent + percent
            newPercent = Math.min(Math.max(newPercent, 0), 100)
            currentPercent = newPercent
        }

        Image {
            id: minusImage
            width: 30
            height: 30
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            source: AppUtils.img("minus.svg")
            fillMode: Image.PreserveAspectFit
        }

        Image {
            id: addImage
            width: 30
            height: 30
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            source: AppUtils.img("plus.svg")
            fillMode: Image.PreserveAspectFit
        }

        Rectangle {
            id: background
            height: 8
            anchors.left: minusImage.right
            anchors.leftMargin: 20
            anchors.right: addImage.left
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            radius: 4
            color: "#1AFFFFFF"
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
            anchors.left: minusImage.right
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            radius: 4
            color: "#FFFFFF"
        }

        Rectangle {
            width: 22
            height: 22
            anchors.right: foreground.right
            anchors.verticalCenter: foreground.verticalCenter
            radius: 22
            color: "#FFFFFF"
        }
    }

    ShaderEffectSource {
        id: bgSource
        anchors.fill: parent
        sourceItem: browserContainer
        sourceRect: Qt.rect(fontSizePage.x, fontSizePage.y, fontSizePage.width, fontSizePage.height)
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
        color: "#E6666666"
    }

    Text {
        id: title
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        font.weight: 600
        font.pixelSize: 24
        color: "#F5FBFE"
        text: qsTr("Minimum font size")
    }

    Text {
        id: fontSizeValue
        anchors.left: title.right
        anchors.leftMargin: 30
        anchors.verticalCenter: parent.verticalCenter
        font.weight: 600
        font.pixelSize: 24
        color: "#F5FBFE"
        text: defaultMinimumFontSize.toString()+"px"
    }

    ProgressItem {
        id: progressItem
        anchors.left: fontSizeValue.right
        anchors.leftMargin: 30
        anchors.right: parent.right
        anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter

        function setValue(value) {
            var percent = Math.round((value-minValue)*100/(maxValue-minValue))
            percent = Math.min(Math.max(percent, 0), 100)
            progressItem.currentPercent = percent
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
            // Prefer the active page's stored min-font so the HUD matches CEF.
            var pageUrl = cppBrowserController ? cppBrowserController.currentUrl : ""
            defaultMinimumFontSize = pageUrl
                    ? cppSettings.minimumFontForUrl(pageUrl)
                    : cppSettings.defaultMinimumFontSize
            progressItem.setValue(defaultMinimumFontSize)

            bgSource.scheduleUpdate()
            bgUpdateTimer.start()
        } else {
            bgUpdateTimer.stop()
        }
    }

    function addFontSize(value) {
        var newFontSize =  defaultMinimumFontSize + value
        newFontSize = Math.min(Math.max(newFontSize, minValue), maxValue)
        if (newFontSize === defaultMinimumFontSize) {
            return
        }
        cppSettings.defaultMinimumFontSize = newFontSize
        defaultMinimumFontSize = newFontSize
        progressItem.setValue(defaultMinimumFontSize)
        // Apply absolute size to the active page (avoid default+delta double-bump).
        if (browserContainer) {
            browserContainer.applyMinimumFontSize(newFontSize)
        }
    }
}