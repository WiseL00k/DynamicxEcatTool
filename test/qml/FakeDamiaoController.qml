import QtQuick

QtObject {
    id: root
    property string slaveType: "mit"
    property string state: "idle"
    property bool active: false
    property bool connected: false
    property bool busy: false
    property bool running: false
    readonly property bool canStart: slaveType === "ecat2canfd" && connected && !busy && state === "preop"
    property string statusText: "未连接"
    property string errorText: ""
    property bool periodicSending: false
    property int periodicIntervalUs: 0
    property bool targetLocked: false
    property int targetChannel: 0
    property string targetIdHex: "001"
    property FakeCanfdController canfd: FakeCanfdController {}
    property var calls: ({preview: 0})
    property int connectCalls: 0
    property int disconnectCalls: 0
    property int startCalls: 0
    property int stopCalls: 0
    property int sendCalls: 0
    property int rawCalls: 0
    property int enableCalls: 0
    property int disableCalls: 0
    property int stopSendingCalls: 0
    property var lastCommand: []

    function setSlaveType(type) {
        if (active || busy) return false
        slaveType = type
        return true
    }
    function connectBus() {
        connectCalls++
        active = connected = true
        running = slaveType === "mit"
        state = running ? "running" : "preop"
        statusText = running ? "OP" : "PRE-OP"
        canfd.connected = connected
        canfd.state = state
    }
    function disconnectBus() { disconnectCalls++; state = "disconnecting"; busy = true }
    function start(cycleUs, dcEnabled, shiftUs) {
        startCalls++; running = true; state = "running"; statusText = "OP"
    }
    function stop() { stopCalls++; running = false; periodicSending = false; state = "stopping"; busy = true }
    function preview(values) {
        calls.preview++
        for (const key of ["pos", "vel", "torque", "kp", "kd", "PMAX", "VMAX", "TMAX"])
            if (values[key] === undefined || !isFinite(Number(values[key])))
                return {valid: false, error: "参数中存在无效数字", bytes: [], hex: ""}
        if (values.PMAX <= 0 || values.VMAX <= 0 || values.TMAX <= 0
                || Math.abs(values.pos) > values.PMAX || Math.abs(values.vel) > values.VMAX
                || Math.abs(values.torque) > values.TMAX || values.kp < 0 || values.kp > 500
                || values.kd < 0 || values.kd > 5)
            return {valid: false, error: "参数超出允许范围", bytes: [], hex: ""}
        // Encoding is verified in the C++ tests. This fake only supplies the
        // validation result and records whether editing ever submits a command.
        return {valid: true, error: "", bytes: [127,255,127,240,0,0,7,255], hex: "7F FF 7F F0 00 00 07 FF"}
    }
    function sendControl(channel, id, values, periodic, intervalUs) {
        sendCalls++; lastCommand = [channel, id, values, periodic, intervalUs]
        periodicSending = periodic
        periodicIntervalUs = Math.ceil(intervalUs / 2000) * 2000
        return true
    }
    function sendRaw(channel, id, data, periodic, intervalUs) {
        rawCalls++; lastCommand = [channel, id, data, periodic, intervalUs]
        return true
    }
    function enableMotor(channel, id) { enableCalls++; lastCommand = [channel, id]; return true }
    function disableMotor(channel, id) { disableCalls++; lastCommand = [channel, id]; periodicSending = false; return true }
    function stopSending(channel, id) { stopSendingCalls++; periodicSending = false; targetLocked = false; return true }
}
