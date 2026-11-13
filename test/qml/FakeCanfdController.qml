import QtQuick

QtObject {
    property string state: "idle"
    property string statusText: "未连接"
    property bool active: false
    property bool busy: false
    property bool connected: false
    property bool running: false
    readonly property bool canStart: connected && state === "preop" && !running && !busy
    property string errorText: ""
    property int slaveCount: 1
    property int selectedSlave: 1
    property var slaves: [{position: 1, name: "ECAT2CANFD", state: 2}]
    property var channelConfigs: [
        {slave: 1, channel: 0, enabled: true, verified: true, format: 0, nominalBitrate: 1000000, dataBitrate: 5000000, options: 5, status: 0, error: 0, dirty: false},
        {slave: 1, channel: 1, enabled: true, verified: true, format: 0, nominalBitrate: 1000000, dataBitrate: 5000000, options: 5, status: 0, error: 0, dirty: false}]
    property var diagnostics: ({wkc: 3, expectedWkc: 3})
    property var pdoSnapshot: ({wkc: 3, expectedWkc: 3})
    property ListModel frames: ListModel {}
    property bool periodicSending: false
    property int periodicIntervalUs: 0
    property string receivedCount: "0"
    property string droppedCount: "0"
    property int connectCalls: 0
    property int disconnectCalls: 0
    property int startCalls: 0
    property int stopCalls: 0
    property int sendCalls: 0
    property int readCalls: 0
    property int applyCalls: 0
    property var lastConfig: []
    property var lastFrame: []
    function connectBus(expectedCount) { connectCalls++; active = true; connected = true; state = "preop"; statusText = "PRE-OP" }
    function disconnectBus() { disconnectCalls++; running = false; busy = true; state = "disconnecting" }
    function start(cycleUs, dcEnabled, shiftUs) { startCalls++; running = true; state = "running"; statusText = "OP" }
    function stop() { stopCalls++; running = false; periodicSending = false; busy = true; state = "stopping" }
    function readConfiguration(slave) { readCalls++ }
    function applyConfiguration(slave, configs) { applyCalls++; lastConfig = configs }
    function selectSlave(slave) { selectedSlave = slave }
    function sendOnce(slave, channel, id, dlc, data) { sendCalls++; lastFrame = [slave, channel, id, dlc, data] }
    function startPeriodic(slave, channel, id, dlc, data, intervalUs) { lastFrame = [slave, channel, id, dlc, data, intervalUs]; periodicIntervalUs = Math.ceil(intervalUs / 2000) * 2000; periodicSending = true }
    function stopPeriodic() { periodicSending = false }
    function clearFrames() { frames.clear() }
}
