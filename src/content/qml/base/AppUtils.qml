import QtQml

pragma Singleton


QtObject {
    property bool enableDebug: false

    property string resPrefix: "qrc:/qt/qml/AeroCoreBrowser/src/content/res/"

    property real scaleFactor: 1.0

    function img(name) {
        return resPrefix + name
    }

    // ============ EventBus ============

    property var _events: ({})

    function onEvent(eventName, callback) {
        if (!_events[eventName]) {
            _events[eventName] = []
        }

        var handlers = _events[eventName]
        for (var i = 0; i < handlers.length; i++) {
            if (handlers[i] === callback) {
                if (enableDebug) console.warn("EventBus: callback already registered for", eventName)
                return
            }
        }

        handlers.push(callback)
    }

    function offEvent(eventName, callback) {
        var handlers = _events[eventName]
        if (!handlers) return
        if (callback) {
            var index = handlers.indexOf(callback)
            if (index >= 0) handlers.splice(index, 1)
        } else {
            delete _events[eventName]
        }
    }

    function emitEvent(eventName) {
        var handlers = _events[eventName]
        if (!handlers) return
        for (var i = 0; i < handlers.length; i++) {
            handlers[i]()
        }
    }

    function emitEventWith(eventName, data) {
        var handlers = _events[eventName]
        if (!handlers) return
        for (var i = 0; i < handlers.length; i++) {
            handlers[i](data)
        }
    }

    // ============ EventBus End============

    function showMessage(success, content) {
        emitEventWith("showMessage", {"success": success, "content": content})
    }

    function showMenu(menuItems, userData = null, options = null) {
        emitEventWith("showMenu", {"menuItems": menuItems, "userData": userData, "options": options})
    }

    function showAerocoreQr() {
        emitEvent("showAerocoreQr")
    }

    function showKeyboard(visible) {
        if (visible) {
            cppInputManager.openSteamKeyboard()
        } else {
            cppInputManager.closeSteamKeyboard()
        }
    }

    function openUrl(url, type) {
        emitEventWith("openUrl", {"url": url, "type": type})
    }

    function getDomainName(url2) {
        var url = url2 || ""
        var domainMatch = url.match(/https?:\/\/([^\/]+)/)
        var domainName = domainMatch ? domainMatch[1] : ""
        return domainName
    }
}