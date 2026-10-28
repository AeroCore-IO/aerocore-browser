import QtQuick
import QtQuick.Controls
import "base"

Item {
    id: opBar
    width: parent.width
    height: 64
    anchors.bottom: parent.bottom

    property string leftOpName: ""

    property string rightOpName: "right"

    property var leftOpSettings: []

    property var rightOpSettings: []

    component OperateBlock : Flow {
        id: opBlock
        spacing: 30

        property var opSetting: []

        Repeater {
            model: opSetting
            delegate: Flow {
                height: opBlock.height
                spacing: 15

                Repeater {
                    model: modelData
                    delegate: Item {
                        id: opItem
                        height: opBlock.height
                        width: imageType?imageItem.width:textItem.width

                        property bool imageType: modelData.type === "image"

                        Image {
                            id: imageItem
                            width: modelData.imageWidth || 44
                            height: modelData.imageHeight || 44
                            anchors.left: parent.left
                            anchors.verticalCenter: opItem.verticalCenter
                            visible: opItem.imageType
                            source: opItem.imageType?modelData.value:""
                            fillMode: Image.PreserveAspectFit
                        }

                        Text {
                            id: textItem
                            anchors.verticalCenter: opItem.verticalCenter
                            visible: !opItem.imageType
                            text: opItem.imageType?"":modelData.value
                            font.pixelSize: 24
                            font.weight: 600
                            color: "#D1F7FBFF"
                        }
                    }
                }
            }
        }
    }

    OperateBlock {
        height: opBar.height
        anchors.left: opBar.left
        anchors.leftMargin: 30
        opSetting: leftOpSettings
    }

    OperateBlock {
        height: opBar.height
        anchors.right: opBar.right
        anchors.rightMargin: 30
        opSetting: rightOpSettings
    }

    function getOpSetting(name) {
        var opSettings = JSON.parse(cppMainController.getOpBarSetting(name))
        return opSettings || []
    }

    onLeftOpNameChanged: {
        if (leftOpName === "") {
            leftOpSettings = []
        } else {
            leftOpSettings = getOpSetting(leftOpName)
        }
    }

    onRightOpNameChanged: {
        if (rightOpName === "") {
            rightOpSettings = []
        } else {
            rightOpSettings = getOpSetting(rightOpName)
        }
    }

    Connections {
        target: cppMainController
        function onButtonIconLayoutChanged() {
            leftOpSettings = leftOpName === "" ? [] : getOpSetting(leftOpName)
            rightOpSettings = rightOpName === "" ? [] : getOpSetting(rightOpName)
        }
    }

    Component.onCompleted: {
        rightOpSettings = getOpSetting(rightOpName)
    }
}