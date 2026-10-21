import QtQuick

QtObject {
    property string state: "idle"
    property string statusText: "未运行"
    property bool active: false
    property bool busy: false
    property bool hasResults: false
    property bool historical: false
    property var diagnostics: ({})
    property var slavesModel: []

    property int startCalls: 0
    property int stopCalls: 0
    property int resetCalls: 0
    property int requestedCycleUs: 0
    property int requestedShiftUs: 0

    function start(cycleUs, shiftUs) {
        startCalls++
        requestedCycleUs = cycleUs
        requestedShiftUs = shiftUs
        state = "starting"
        statusText = "正在启动"
        active = true
        busy = true
        historical = false
    }

    function stop() {
        stopCalls++
        state = "stopping"
        statusText = "正在停止"
        busy = true
    }

    function resetStatistics() {
        resetCalls++
    }
}
