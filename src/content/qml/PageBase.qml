import QtQuick
import AppUtils
import "base"

Item {
    id: pageBase
    anchors.fill: parent

    property string pageName: ""    

    property var firstFocusItem: null    

    onVisibleChanged: {
        if (visible && firstFocusItem) {
            Qt.callLater(function() {
                firstFocusItem.forceActiveFocus()
            })
        }
    }
}
