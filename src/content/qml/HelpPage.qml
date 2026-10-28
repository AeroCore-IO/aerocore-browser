import QtQuick
import QtQuick.Controls
import "base"

PageBase {
    pageName: "help"

    property var keyList: []

    Item {
        anchors.fill: parent

        component GuideCallout : Row {
            id: callout
            property string side: "left"
            property var icons: []
            property int iconW: 44
            property int iconH: 44
            property int textMaxWidth: 300
            property string title: ""
            property string subtitle: ""

            spacing: 10
            layoutDirection: side === "left" ? Qt.RightToLeft : Qt.LeftToRight

            Row {
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter

                Repeater {
                    model: callout.icons
                    delegate: Image {
                        width: callout.iconW
                        height: callout.iconH
                        source: {
                            cppMainController.buttonIconLayout
                            return cppMainController.iconUrl(modelData)
                        }
                        fillMode: Image.PreserveAspectFit
                    }
                }
            }

            Column {
                id: calloutText
                width: Math.min(callout.textMaxWidth, Math.max(titleText.implicitWidth, subtitleText.implicitWidth))
                spacing: 2
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    id: titleText
                    width: calloutText.width
                    font.weight: 600
                    font.pixelSize: 22
                    color: "#F4FBFF"
                    text: callout.title
                    horizontalAlignment: callout.side === "right" ? Text.AlignRight : Text.AlignLeft
                    wrapMode: Text.WordWrap
                }

                Text {
                    id: subtitleText
                    visible: callout.subtitle.length > 0
                    width: calloutText.width
                    font.weight: 400
                    font.pixelSize: 18
                    color: "#99FFFFFF"
                    text: callout.subtitle
                    horizontalAlignment: callout.side === "right" ? Text.AlignRight : Text.AlignLeft
                    wrapMode: Text.WordWrap
                }
            }
        }

        Item {
            id: joystickGuide
            width: 1200
            height: 660
            anchors.left: parent.left
            anchors.leftMargin: 40
            anchors.top: parent.top
            anchors.topMargin: 275

            readonly property real viewBoxW: 1238
            readonly property real viewBoxH: 785
            readonly property real photoX: 192
            readonly property real photoY: 0
            readonly property real photoW: 785
            readonly property real photoH: 785
            readonly property real overlayH: height - 56
            readonly property real fit: Math.min(width / viewBoxW, overlayH / viewBoxH)
            readonly property real contentW: viewBoxW * fit
            readonly property real contentH: viewBoxH * fit
            readonly property real originX: (width - contentW) / 2
            readonly property real originY: (overlayH - contentH) / 2

            readonly property bool playStationLayout: cppMainController.buttonIconLayout === "playstation"
            readonly property var slots: {
                cppMainController.buttonIconLayout
                var table = {
                    "xbox": {
                        leftTopX: 132.955, leftTopY: 112.517,
                        leftMidX: 132.955, leftMidY: 310.077,
                        leftBottomX: 132.954, leftBottomY: 471,
                        rightTopX: 959.822, rightTopY: 112.517,
                        rightMidX: 1044.86, rightMidY: 323.158,
                        rightBottomX: 1046.17, rightBottomY: 471
                    },
                    "playstation": {
                        leftTopX: 131.646, leftTopY: 113.825,
                        leftMidX: 131.646, leftMidY: 311.386,
                        leftBottomX: 131.646, leftBottomY: 472.308,
                        rightTopX: 958.513, rightTopY: 113.825,
                        rightMidX: 1043.55, rightMidY: 322.504,
                        rightBottomX: 1044.86, rightBottomY: 472.308
                    },
                    "nintendo": {
                        leftTopX: 177.513, leftTopY: 133.45,
                        leftMidX: 177.513, leftMidY: 331.011,
                        leftBottomX: 130.338, leftBottomY: 492.588,
                        rightTopX: 957.204, rightTopY: 133.45,
                        rightMidX: 994.763, rightMidY: 344.092,
                        rightBottomX: 1043.55, rightBottomY: 491.933
                    },
                    "steamcontroller": {
                        leftTopX: 132.954, leftTopY: 229.395,
                        leftMidX: 132.954, leftMidY: 361.1,
                        leftBottomX: 132.954, leftBottomY: 468.383,
                        rightTopX: 939.779, rightTopY: 229.395,
                        rightMidX: 1044.86, rightMidY: 369.822,
                        rightBottomX: 1046.17, rightBottomY: 468.383
                    }
                }
                var layout = cppMainController.buttonIconLayout
                return table[layout] || table["xbox"]
            }

            Image {
                z: 0
                x: joystickGuide.originX + joystickGuide.photoX * joystickGuide.fit
                y: joystickGuide.originY + joystickGuide.photoY * joystickGuide.fit
                width: joystickGuide.photoW * joystickGuide.fit
                height: joystickGuide.photoH * joystickGuide.fit
                source: {
                    cppMainController.buttonIconLayout
                    return cppMainController.helpJoystickPhotoUrl()
                }
                fillMode: Image.PreserveAspectFit
            }

            Image {
                z: 1
                x: joystickGuide.originX
                y: joystickGuide.originY
                width: joystickGuide.contentW
                height: joystickGuide.contentH
                source: {
                    cppMainController.buttonIconLayout
                    return cppMainController.helpJoystickOverlayUrl()
                }
                fillMode: Image.Stretch
            }

            Column {
                z: 2
                spacing: 12
                x: joystickGuide.originX + joystickGuide.slots.leftTopX * joystickGuide.fit - width
                y: joystickGuide.originY + joystickGuide.slots.leftTopY * joystickGuide.fit - 20

                GuideCallout {
                    icons: ["op-lb.svg"]
                    iconW: 52
                    iconH: 40
                    textMaxWidth: 140
                    title: qsTr("Previous tab")
                }

                GuideCallout {
                    icons: ["op-lt.svg"]
                    iconW: 52
                    iconH: 40
                    textMaxWidth: 140
                    title: qsTr("Previous session")
                }
            }

            GuideCallout {
                z: 2
                textMaxWidth: 140
                icons: [joystickGuide.playStationLayout ? "op-direction.svg" : "op-stick.svg"]
                title: joystickGuide.playStationLayout ? qsTr("D-pad") : qsTr("Left stick")
                subtitle: joystickGuide.playStationLayout ? qsTr("Navigate") : qsTr("Move cursor")
                x: joystickGuide.originX + joystickGuide.slots.leftMidX * joystickGuide.fit - width
                y: joystickGuide.originY + joystickGuide.slots.leftMidY * joystickGuide.fit - height / 2
            }

            GuideCallout {
                z: 2
                textMaxWidth: 140
                icons: [joystickGuide.playStationLayout ? "op-stick.svg" : "op-direction.svg"]
                title: joystickGuide.playStationLayout ? qsTr("Left stick") : qsTr("D-pad")
                subtitle: joystickGuide.playStationLayout ? qsTr("Move cursor") : qsTr("Navigate")
                x: joystickGuide.originX + joystickGuide.slots.leftBottomX * joystickGuide.fit - width
                y: joystickGuide.originY + joystickGuide.slots.leftBottomY * joystickGuide.fit - height / 2
            }

            Column {
                z: 2
                spacing: 12
                x: joystickGuide.originX + joystickGuide.slots.rightTopX * joystickGuide.fit
                y: joystickGuide.originY + joystickGuide.slots.rightTopY * joystickGuide.fit - 20

                GuideCallout {
                    side: "right"
                    textMaxWidth: 220
                    icons: ["op-rb.svg"]
                    iconW: 52
                    iconH: 40
                    title: qsTr("Next tab")
                }

                GuideCallout {
                    side: "right"
                    textMaxWidth: 220
                    icons: ["op-rt.svg"]
                    iconW: 52
                    iconH: 40
                    title: qsTr("Next session")
                }
            }

            GuideCallout {
                z: 2
                side: "right"
                textMaxWidth: 130
                icons: ["op-keya.svg", "op-keyb.svg", "op-close.svg", "op-keyy.svg"]
                iconW: 28
                iconH: 28
                title: joystickGuide.playStationLayout ? qsTr("Shape buttons") : qsTr("ABXY buttons")
                subtitle: qsTr("Select an action")
                x: joystickGuide.originX + joystickGuide.slots.rightMidX * joystickGuide.fit
                y: joystickGuide.originY + joystickGuide.slots.rightMidY * joystickGuide.fit - height / 2
            }

            GuideCallout {
                z: 2
                side: "right"
                textMaxWidth: 220
                icons: ["op-stick.svg"]
                title: qsTr("Right stick")
                subtitle: qsTr("Scroll page")
                x: joystickGuide.originX + joystickGuide.slots.rightBottomX * joystickGuide.fit
                y: joystickGuide.originY + joystickGuide.slots.rightBottomY * joystickGuide.fit - height / 2
            }

            Row {
                z: 2
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 24
                spacing: 28

                GuideCallout {
                    side: "bottom"
                    icons: ["op-menu.svg"]
                    title: qsTr("Menu button")
                    subtitle: qsTr("Go to home")
                }

                GuideCallout {
                    side: "bottom"
                    icons: ["op-view.svg"]
                    title: qsTr("View button")
                    subtitle: qsTr("Show / hide HUD")
                }
            }
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 60
            anchors.top: parent.top
            anchors.topMargin: 200
            font.weight: 600
            font.pixelSize: 34
            color: "#F4FBFF"
            text: qsTr("Controller guide")
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 60
            anchors.top: parent.top
            anchors.topMargin: 254
            font.weight: 400
            font.pixelSize: 22
            color: "#99FFFFFF"
            text: qsTr("Use your controller to move the cursor, scroll, and click while browsing.")
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 30
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 69
            font.weight: 400
            font.pixelSize: 24
            color: "#99FFFFFF"
            text: " "
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 1280
            anchors.top: parent.top
            anchors.topMargin: 130
            font.weight: 600
            font.pixelSize: 34
            color: "#F4FBFF"
            text: qsTr("Button guide")
        }

        ScrollViewBase {
            id: keyDescScrollView
            anchors.left: parent.left
            anchors.leftMargin: 1280
            anchors.right: parent.right
            anchors.rightMargin: 30
            anchors.top: parent.top
            anchors.topMargin: 196
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 70
            contentInteractive: true

            Flow {
                id: keyDescFlow
                width: keyDescScrollView.width
                spacing: 8

                Repeater {
                    id: keyDescRepeater
                    model: keyList

                    delegate: Item {
                        width: keyDescScrollView.width
                        height: Math.max(65, descriptionText.y + descriptionText.implicitHeight + 4)

                        Flow {
                            id: imageFlow
                            height: 44
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10

                            Repeater {
                                model: modelData.image

                                delegate: Item {
                                    readonly property bool isSeparator: modelData === "+" || modelData === "/"
                                    width: isSeparator ? sepText.implicitWidth : 44
                                    height: 44

                                    Text {
                                        id: sepText
                                        visible: parent.isSeparator
                                        anchors.centerIn: parent
                                        font.weight: 600
                                        font.pixelSize: 26
                                        color: "#F4FBFF"
                                        text: parent.isSeparator ? modelData : ""
                                    }

                                    Image {
                                        visible: !parent.isSeparator
                                        anchors.fill: parent
                                        source: parent.isSeparator ? "" : modelData
                                        fillMode: Image.PreserveAspectFit
                                    }
                                }
                            }
                        }

                        Text {
                            id: titleText
                            anchors.left: imageFlow.right
                            anchors.leftMargin: 24
                            anchors.right: parent.right
                            anchors.top: parent.top
                            font.weight: 600
                            font.pixelSize: 22
                            color: "#FFFFFF"
                            text: modelData.title
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            id: descriptionText
                            anchors.left: imageFlow.right
                            anchors.leftMargin: 24
                            anchors.right: parent.right
                            anchors.top: titleText.bottom
                            anchors.topMargin: 2
                            font.weight: 400
                            font.pixelSize: 20
                            color: "#99FFFFFF"
                            text: modelData.description
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        keyList = JSON.parse(cppMainController.getHelpKeySetting())
    }

    Connections {
        target: cppMainController
        function onButtonIconLayoutChanged() {
            keyList = JSON.parse(cppMainController.getHelpKeySetting())
        }
    }

    onVisibleChanged: {
        if (visible) {
            Window.window.activeFocusItem.focus = false
        }
    }
}
