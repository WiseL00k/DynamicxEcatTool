import QtQuick

QtObject {
    id: root

    property var backend
    property string contextKey: "test"
    property bool sessionActive: false
    property string sessionMode: ""
    property int maximumLines: 500
    property int revision: 0
    property var buffers: ({ "test": [], "debug": [], "params": [], "bus": [] })

    readonly property string currentText: {
        const dependency = revision
        const lines = buffers[contextKey] || []
        return lines.join("\n")
    }

    function keyForMode(mode) {
        if (mode === "测试" || mode === "固件或EEPROM烧录") return "test"
        if (mode === "调试通信") return "debug"
        if (mode === "MIT参数调试") return "params"
        if (mode === "总线配置") return "bus"
        return contextKey
    }

    function destinationKey() {
        return sessionActive ? keyForMode(sessionMode) : contextKey
    }

    function replaceCurrent(line) {
        const key = destinationKey()
        const normalized = line ? String(line).replace(/\r\n/g, "\n").replace(/\r/g, "\n") : ""
        buffers[key] = normalized.length > 0 ? normalized.split("\n") : []
        trim(key)
        revision++
    }

    function appendCurrent(line) {
        if (!line || line.length === 0)
            return
        const key = destinationKey()
        if (!buffers[key])
            buffers[key] = []
        const normalized = String(line).replace(/\r\n/g, "\n").replace(/\r/g, "\n")
        const incomingLines = normalized.split("\n")
        for (let index = 0; index < incomingLines.length; ++index)
            buffers[key].push(incomingLines[index])
        trim(key)
        revision++
    }

    function clearContext(key) {
        buffers[key] = []
        revision++
    }

    function clearCurrent() {
        clearContext(contextKey)
    }

    function trim(key) {
        const lines = buffers[key]
        if (lines && lines.length > maximumLines)
            lines.splice(0, lines.length - maximumLines)
    }

    property QtObject backendConnections: Connections {
        target: root.backend
        ignoreUnknownSignals: true

        function onLogUpdated(line) {
            root.replaceCurrent(line)
        }

        function onLogAppend(line) {
            root.appendCurrent(line)
        }

        function onSoemErrorOccurred(message) {
            root.appendCurrent(qsTr("错误：") + message)
        }
    }
}
