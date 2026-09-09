import QtQuick

QtObject {
    id: root

    property var backend
    property int selectedNicIndex: -1
    property bool nicRefreshPending: false
    property bool refreshAfterSession: false

    readonly property var nicList: backend ? backend.nicList : []
    readonly property bool nicReady: !nicRefreshPending
                                     && selectedNicIndex >= 0
                                     && selectedNicIndex < nicList.length
    readonly property bool sessionActive: backend ? backend.sessionActive : false
    readonly property string sessionMode: backend ? backend.sessionMode : qsTr("空闲")
    readonly property bool connected: backend ? backend.connected : false
    readonly property bool idle: !sessionActive
    readonly property bool busy: sessionActive

    readonly property bool testConnected: connected && sessionMode === "测试"
    readonly property bool debugConnected: connected && sessionMode === "调试通信"
    readonly property bool mitConnected: connected && sessionMode === "MIT参数调试"
    readonly property bool explorerActive: sessionActive && sessionMode === "总线配置"
    readonly property bool flashingActive: sessionActive && sessionMode === "固件或EEPROM烧录"

    readonly property string modeKey: {
        if (sessionMode === "测试") return "test"
        if (sessionMode === "调试通信") return "debug"
        if (sessionMode === "MIT参数调试") return "params"
        if (sessionMode === "总线配置") return "bus"
        if (sessionMode === "固件或EEPROM烧录") return "test"
        return "idle"
    }
    readonly property string statusText: sessionActive ? sessionMode : qsTr("总线空闲")
    readonly property string statusTone: flashingActive
                                                  ? "warning"
                                                  : explorerActive
                                                    ? "info"
                                                    : sessionActive
                                                      ? "success"
                                                      : "neutral"

    function refreshNics() {
        if (!backend || sessionActive || nicRefreshPending)
            return
        selectedNicIndex = -1
        nicRefreshPending = true
        backend.refreshNicsAsync()
    }

    function selectNic(index) {
        if (!backend || sessionActive || nicRefreshPending
                || index < 0 || index >= nicList.length)
            return
        selectedNicIndex = index
        backend.changedSelectedNic(index)
    }

    property QtObject backendConnections: Connections {
        target: root.backend
        ignoreUnknownSignals: true

        function onNicListChanged() {
            root.nicRefreshPending = false
            if (root.sessionActive) {
                root.selectedNicIndex = -1
                root.refreshAfterSession = true
                return
            }
            if (root.nicList.length === 0) {
                root.selectedNicIndex = -1
            } else {
                root.selectedNicIndex = 0
                root.backend.changedSelectedNic(0)
            }
        }

        function onSessionChanged() {
            if (!root.sessionActive && root.refreshAfterSession) {
                root.refreshAfterSession = false
                Qt.callLater(root.refreshNics)
            }
        }
    }
}
