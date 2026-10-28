import QtQuick
import QtQuick.Controls

ScrollView {
    id: scrollViewBase
    clip: true

    ScrollBar.vertical.policy: ScrollBar.AlwaysOff
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.interactive: false
    ScrollBar.horizontal.interactive: false

    property bool contentInteractive: false

    Component.onCompleted: {
        contentItem.interactive = contentInteractive
    }

    function scrollItemIntoView(item) {
        if (!item || !contentItem) {
            return
        }

        var pos = item.mapToItem(contentItem, 0, 0)
        var itemTop = pos.y
        var itemBottom = pos.y + item.height
        var viewTop = contentItem.contentY
        var viewBottom = viewTop + height
        if (itemBottom > viewBottom) {
            contentItem.contentY = itemBottom - height
        } else if (itemTop < viewTop) {
            contentItem.contentY = itemTop
        }
    }
}
