pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../panels"

Pane {
    id: root
    property var theme
    property var sessionUi
    property var controller
    property bool configurationOpen: false
    property int cycleUs: 2000
    property int shiftUs: 0
    property bool dcEnabled: false
    property var configurationDrafts: ({})
    readonly property bool canfd: controller && controller.slaveType === "ecat2canfd"
    readonly property bool isConnected: controller && controller.connected
    readonly property bool active: controller && controller.active
    readonly property bool busy: controller && controller.busy
    readonly property bool running: controller && controller.running
    readonly property bool occupiedByOtherMode: sessionUi && sessionUi.sessionActive && !active
    readonly property var canfdController: controller ? controller.canfd : null
    readonly property var canfdDiagnostics: canfdController && canfdController.diagnostics ? canfdController.diagnostics : ({})
    signal connectionChanged(bool connected)
    signal errorRequested(string message)
    onIsConnectedChanged: connectionChanged(isConnected)
    onCanfdChanged: { if (!canfd) configurationOpen = false }

    padding: 0
    font.family: theme ? theme.fontFamily : ""
    font.pixelSize: theme ? theme.fontBody : 13
    palette.window: theme ? theme.pageBackground : "transparent"
    palette.windowText: theme ? theme.textPrimary : "black"
    palette.base: theme ? theme.inputBackground : "white"
    palette.text: theme ? theme.textPrimary : "black"
    palette.button: theme ? theme.controlBackground : "white"
    palette.buttonText: theme ? theme.textPrimary : "black"
    palette.highlight: theme ? theme.accent : "blue"
    palette.highlightedText: theme ? theme.textOnAccent : "white"
    background: Rectangle { color: root.theme ? root.theme.pageBackground : "transparent" }

    function liveConfiguration(channel) {
        const configs = canfdController && canfdController.channelConfigs ? canfdController.channelConfigs : []
        for (const config of configs)
            if (Number(config.slave) === 1 && Number(config.channel) === channel)
                return config
        return null
    }
    function configuration(channel) {
        return configurationDrafts[channel] === undefined ? liveConfiguration(channel) : configurationDrafts[channel]
    }
    function updateDraft(channel, values) {
        const drafts = Object.assign({}, configurationDrafts)
        drafts[channel] = values
        configurationDrafts = drafts
    }
    function reconcileDrafts() {
        const drafts = Object.assign({}, configurationDrafts)
        let changed = false
        for (const key of Object.keys(drafts)) {
            const live = liveConfiguration(Number(key))
            const draft = drafts[key]
            if (!live || live.dirty || Number(live.status) !== 0 || Number(live.error) !== 0)
                continue
            if (Boolean(live.enabled) === Boolean(draft.enabled) && Number(live.format) === Number(draft.format)
                    && Number(live.nominalBitrate) === Number(draft.nominalBitrate)
                    && Number(live.dataBitrate) === Number(draft.dataBitrate) && Number(live.options) === Number(draft.options)) {
                delete drafts[key]
                changed = true
            }
        }
        if (changed)
            configurationDrafts = drafts
    }
    Connections {
        target: root.canfdController
        ignoreUnknownSignals: true
        function onResultsChanged() { root.reconcileDrafts() }
    }
    Connections {
        target: root.controller
        ignoreUnknownSignals: true
        function onErrorOccurred(message) { root.errorRequested(message) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: root.theme.space8
        PanelCard {
            Layout.fillWidth: true; theme: root.theme; padding: root.theme.space8
            Flow {
                width: parent.width; spacing: root.theme.space8
                ComboBox {
                    id: slaveType
                    objectName: "dmTypeSelector"; implicitWidth: 155; implicitHeight: root.theme.controlHeight
                    model: ["MIT 从站", "ECAT2CANFD 从站"]
                    currentIndex: root.canfd ? 1 : 0
                    enabled: root.controller && !root.active && !root.busy
                    onActivated: root.controller.setSlaveType(currentIndex === 0 ? "mit" : "ecat2canfd")
                }
                StatusBadge {
                    objectName: "dmSessionStatus"; theme: root.theme; height: root.theme.controlHeight
                    text: root.occupiedByOtherMode ? qsTr("总线被其他任务占用") : root.controller ? root.controller.statusText : qsTr("不可用")
                    tone: root.controller && (root.controller.state === "failed" || root.controller.state === "faulted")
                        ? "danger" : root.busy || root.occupiedByOtherMode ? "warning" : root.running ? "success" : root.isConnected ? "info" : "neutral"
                }
                AppButton {
                    objectName: "dmConnectButton"; theme: root.theme
                    text: !root.active ? qsTr("连接") : root.controller.state === "disconnecting" ? qsTr("正在断开") : root.busy ? qsTr("取消并断开") : qsTr("断开")
                    actionEnabled: root.controller && (root.active ? root.controller.state !== "disconnecting" : !root.busy && !root.occupiedByOtherMode && root.sessionUi && root.sessionUi.nicReady)
                    onClicked: root.active ? root.controller.disconnectBus() : root.controller.connectBus()
                }
                AppButton {
                    objectName: "dmStartButton"; visible: root.canfd; theme: root.theme; variant: root.running ? "danger" : "primary"
                    text: root.controller && root.controller.state === "starting" ? qsTr("取消启动") : root.running ? qsTr("停止运行") : qsTr("进入 OP")
                    actionEnabled: root.controller && (root.running || root.controller.state === "starting" || (!root.busy && root.controller.canStart))
                    onClicked: root.running || root.controller.state === "starting" ? root.controller.stop() : root.controller.start(root.cycleUs, root.dcEnabled, root.shiftUs)
                }
                AppButton {
                    objectName: "dmConfigurationButton"; visible: root.canfd; theme: root.theme
                    text: root.configurationOpen ? qsTr("返回控制") : qsTr("总线 / CAN 配置")
                    onClicked: root.configurationOpen = !root.configurationOpen
                }
                Label {
                    height: root.theme.controlHeight; verticalAlignment: Text.AlignVCenter
                    text: root.canfd ? qsTr("从站 1 · %1 μs · DC %2").arg(root.cycleUs).arg(root.dcEnabled ? "ON" : "OFF") : qsTr("从站 1 · 双 CAN · 9 槽 / 通道")
                    color: root.theme.textMuted; font.pixelSize: root.theme.fontBodySmall
                }
            }
        }
        Label {
            objectName: "dmErrorBanner"; Layout.fillWidth: true
            visible: root.controller && root.controller.errorText.length > 0
            text: visible ? root.controller.errorText : ""
            wrapMode: Text.WrapAnywhere; padding: root.theme.space8; color: root.theme.dangerText
            background: Rectangle { color: root.theme.dangerBackground; radius: root.theme.radiusSmall; border.color: root.theme.dangerBorder }
        }
        Label {
            Layout.fillWidth: true; visible: root.canfd && Boolean(root.canfdDiagnostics.cleanupWarning)
            text: visible ? String(root.canfdDiagnostics.cleanupWarning) : ""
            wrapMode: Text.WrapAnywhere; padding: root.theme.space8; color: root.theme.warningText
            background: Rectangle { color: root.theme.warningBackground; radius: root.theme.radiusSmall; border.color: root.theme.warningBorder }
        }
        ScrollView {
            id: pageScroll
            objectName: "dmPageScroll"; Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; contentWidth: availableWidth; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: pageScroll.availableWidth; spacing: root.theme.space8
                MITConfigPanel {
                    objectName: "dmControlPanel"; visible: !root.configurationOpen
                    Layout.fillWidth: true; theme: root.theme; controller: root.controller; cycleUs: root.cycleUs
                }
                ColumnLayout {
                    objectName: "dmConfigurationPage"; visible: root.configurationOpen
                    Layout.fillWidth: true; spacing: root.theme.space8
                    PanelCard {
                        Layout.fillWidth: true; theme: root.theme; padding: root.theme.space12
                        ColumnLayout {
                            width: parent.width; spacing: root.theme.space8
                            Label { text: qsTr("EtherCAT 周期与同步"); color: root.theme.textPrimary; font.bold: true }
                            Flow {
                                Layout.fillWidth: true; spacing: root.theme.space12
                                ColumnLayout {
                                    spacing: root.theme.space4
                                    Label { text: qsTr("周期 (μs)"); color: root.theme.textSecondary; font.pixelSize: root.theme.fontBodySmall }
                                    SpinBox { objectName: "dmCycleInput"; from: 250; to: 100000; stepSize: 250; value: root.cycleUs; editable: true; enabled: !root.running && !root.busy; onValueModified: { root.cycleUs = value; if (Math.abs(root.shiftUs) >= value) root.shiftUs = 0 } }
                                }
                                ColumnLayout {
                                    spacing: root.theme.space4
                                    Label { text: "DC SYNC0"; color: root.theme.textSecondary; font.pixelSize: root.theme.fontBodySmall }
                                    Switch { objectName: "dmDcSwitch"; text: qsTr("启用总线同步"); checked: root.dcEnabled; enabled: !root.running && !root.busy; onToggled: root.dcEnabled = checked }
                                }
                                ColumnLayout {
                                    spacing: root.theme.space4
                                    Label { text: qsTr("相移 (μs)"); color: root.theme.textSecondary; font.pixelSize: root.theme.fontBodySmall }
                                    SpinBox { objectName: "dmShiftInput"; from: -root.cycleUs + 1; to: root.cycleUs - 1; value: root.shiftUs; editable: true; enabled: root.dcEnabled && !root.running && !root.busy; onValueModified: root.shiftUs = value }
                                }
                            }
                            Label { Layout.fillWidth: true; text: qsTr("连接后停留在 PRE-OP；CAN 配置可在 PRE-OP 或 OP 中读写。电机控制需进入 OP。"); wrapMode: Text.Wrap; color: root.theme.textMuted; font.pixelSize: root.theme.fontBodySmall }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: root.theme.space8
                        Label { text: qsTr("从站 1 · CAN 配置"); color: root.theme.textPrimary; font.bold: true }
                        Item { Layout.fillWidth: true }
                        AppButton { theme: root.theme; text: qsTr("撤销编辑"); variant: "ghost"; actionEnabled: !root.busy; onClicked: root.configurationDrafts = ({}) }
                        AppButton { objectName: "dmReadConfigButton"; theme: root.theme; text: qsTr("读取配置"); actionEnabled: root.isConnected && !root.busy; onClicked: root.canfdController.readConfiguration(1) }
                        AppButton { objectName: "dmApplyConfigButton"; theme: root.theme; text: qsTr("应用配置"); variant: "primary"; actionEnabled: root.isConnected && !root.busy; onClicked: root.canfdController.applyConfiguration(1, [channel0.values(), channel1.values()]) }
                    }
                    GridLayout {
                        objectName: "dmCanChannelsGrid"; Layout.fillWidth: true
                        columns: width >= 720 ? 2 : 1; columnSpacing: root.theme.space8; rowSpacing: root.theme.space8
                        CanfdChannelConfig { id: channel0; Layout.fillWidth: true; theme: root.theme; channel: 0; configuration: root.configuration(0); liveConfiguration: root.liveConfiguration(0); localEdits: root.configurationDrafts[0] !== undefined; editable: !root.busy; onConfigurationEdited: function(values) { root.updateDraft(0, values) } }
                        CanfdChannelConfig { id: channel1; Layout.fillWidth: true; theme: root.theme; channel: 1; configuration: root.configuration(1); liveConfiguration: root.liveConfiguration(1); localEdits: root.configurationDrafts[1] !== undefined; editable: !root.busy; onConfigurationEdited: function(values) { root.updateDraft(1, values) } }
                    }
                    Label { Layout.fillWidth: true; text: qsTr("通道帧格式与速率需与电机参数一致。固件退出 / 故障失能覆盖两路 ID 1–9，包括非当前电机。配置仅在本次上电期间有效。"); wrapMode: Text.Wrap; color: root.theme.textMuted; font.pixelSize: root.theme.fontCaption; padding: root.theme.space4 }
                }
            }
        }
    }
}
