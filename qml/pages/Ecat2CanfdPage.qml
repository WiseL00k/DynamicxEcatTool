pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Pane {
    id: root
    property var theme
    property var sessionUi
    property var controller
    padding: 0
    font.family: theme ? theme.fontFamily : ""
    font.pixelSize: theme ? theme.fontBody : 13
    palette.window: theme ? theme.pageBackground : "transparent"
    palette.windowText: theme ? theme.textPrimary : "black"
    palette.base: theme ? theme.inputBackground : "white"
    palette.alternateBase: theme ? theme.surfaceMuted : "white"
    palette.text: theme ? theme.textPrimary : "black"
    palette.button: theme ? theme.controlBackground : "white"
    palette.buttonText: theme ? theme.textPrimary : "black"
    palette.highlight: theme ? theme.accent : "blue"
    palette.highlightedText: theme ? theme.textOnAccent : "white"
    background: Rectangle { color: root.theme ? root.theme.pageBackground : "transparent" }
    property bool configurationOpen: false
    property bool followTail: true
    property int selectedSlave: 1
    property int sendSlavePosition: 1
    property int expectedCount: 1
    property int cycleUs: 2000
    property int shiftUs: 0
    property bool dcEnabled: false
    property var configurationDrafts: ({})
    readonly property bool connected: controller ? controller.connected : false
    readonly property bool running: controller ? controller.running : false
    readonly property bool busy: controller ? controller.busy : false
    readonly property bool owned: sessionUi && sessionUi.modeKey === "ecat2canfd"
    readonly property bool occupied: sessionUi && sessionUi.sessionActive && !owned
    readonly property var snapshot: controller && controller.pdoSnapshot ? controller.pdoSnapshot : ({})
    readonly property var diagnostics: controller && controller.diagnostics ? controller.diagnostics : ({})
    readonly property var slaveChoices: {
        const found = controller && controller.slaves ? controller.slaves : []
        const result = []
        for (let i = 0; i < found.length; ++i)
            result.push({label: qsTr("从站 %1").arg(found[i].position), position: Number(found[i].position)})
        if (!connected) {
            result.length = 0
            for (let position = 1; position <= expectedCount; ++position)
                result.push({label: qsTr("从站 %1").arg(position), position: position})
        }
        return result
    }
    readonly property var sendConfig: channelConfig(sendSlave.currentValue || 1, sendChannel.currentIndex)
    readonly property bool channelReady: sendConfig !== null && sendConfig.enabled && !sendConfig.dirty
                                        && Number(sendConfig.status) === 0 && Number(sendConfig.error) === 0
    readonly property bool payloadValid: {
        const text = payload.text.trim()
        if (dlcInput.value === 0)
            return text.length === 0
        const bytes = text.split(/\s+/)
        if (bytes.length !== dlcInput.value)
            return false
        return bytes.every(function(value) { return /^[0-9A-Fa-f]{2}$/.test(value) })
    }
    readonly property bool idValid: /^(0[xX])?[0-9a-fA-F]{1,3}$/.test(idInput.text.trim())
                                   && parseInt(idInput.text.trim().replace(/^0x/i, ""), 16) <= 0x7ff
    readonly property bool canSend: running && !busy && channelReady && payloadValid && idValid
    readonly property int actualIntervalUs: controller && controller.periodicSending
                                            ? controller.periodicIntervalUs : Math.ceil(intervalInput.value / cycleUs) * cycleUs
    readonly property bool intervalValid: intervalInput.value >= cycleUs
    onSlaveChoicesChanged: {
        if (!slaveChoices.some(function(item) { return item.position === root.selectedSlave }) && slaveChoices.length > 0)
            selectSlave(slaveChoices[0].position)
        if (!slaveChoices.some(function(item) { return item.position === root.sendSlavePosition }) && slaveChoices.length > 0)
            sendSlavePosition = slaveChoices[0].position
    }
    onConnectedChanged: {
        if (connected && controller)
            controller.selectSlave(selectedSlave)
    }

    function channelDraft(slave, channel) {
        const draft = configurationDrafts[slave + ":" + channel]
        return draft === undefined ? channelConfig(slave, channel) : draft
    }
    function saveDraft(channel, values) {
        const drafts = Object.assign({}, configurationDrafts)
        drafts[selectedSlave + ":" + channel] = values
        configurationDrafts = drafts
    }
    function discardDrafts() {
        const drafts = Object.assign({}, configurationDrafts)
        delete drafts[selectedSlave + ":0"]
        delete drafts[selectedSlave + ":1"]
        configurationDrafts = drafts
    }
    function reconcileDrafts() {
        const drafts = Object.assign({}, configurationDrafts)
        let changed = false
        for (const key of Object.keys(drafts)) {
            const address = key.split(":")
            const live = channelConfig(Number(address[0]), Number(address[1]))
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

    function channelConfig(slave, channel) {
        const configs = controller && controller.channelConfigs ? controller.channelConfigs : []
        for (let i = 0; i < configs.length; ++i)
            if (Number(configs[i].slave) === Number(slave) && Number(configs[i].channel) === channel)
                return configs[i]
        return null
    }
    function selectSlave(position) {
        selectedSlave = position
        if (controller && connected)
            controller.selectSlave(position)
    }
    function send(periodic) {
        if (!canSend)
            return
        const args = [Number(sendSlave.currentValue || 1), sendChannel.currentIndex,
                      idInput.text.trim(), dlcInput.value, payload.text.trim()]
        if (periodic)
            controller.startPeriodic(args[0], args[1], args[2], args[3], args[4], intervalInput.value)
        else
            controller.sendOnce(args[0], args[1], args[2], args[3], args[4])
    }
    function diag(key) {
        return diagnostics[key] === undefined ? "—" : String(diagnostics[key])
    }
    function pdoWorkingCounter(key) {
        return snapshot[key] === undefined ? diag(key) : String(snapshot[key])
    }

    Connections {
        target: root.controller
        ignoreUnknownSignals: true
        function onResultsChanged() { root.reconcileDrafts() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: root.theme.space8

        PanelCard {
            Layout.fillWidth: true; padding: root.theme.space8; theme: root.theme
            Flow {
                width: parent.width; spacing: root.theme.space8
                StatusBadge {
                    objectName: "canfdSessionStatus"
                    theme: root.theme; height: root.theme.controlHeight
                    text: root.occupied ? qsTr("总线被其他任务占用") : root.controller ? root.controller.statusText : qsTr("不可用")
                    tone: root.controller && (root.controller.state === "failed" || root.controller.state === "faulted") ? "danger" : root.occupied || root.busy ? "warning" : root.running ? "success" : root.connected ? "info" : "neutral"
                }
                AppButton {
                    objectName: "canfdConnectButton"; theme: root.theme
                    text: !root.controller || !root.controller.active ? qsTr("连接")
                          : root.controller.state === "connecting" ? qsTr("取消连接")
                          : root.controller.state === "disconnecting" ? qsTr("正在断开")
                          : root.busy ? qsTr("取消并断开") : qsTr("断开")
                    actionEnabled: root.controller && (root.controller.active
                                   ? root.controller.state !== "disconnecting"
                                   : !root.busy && !root.occupied && root.sessionUi && root.sessionUi.nicReady)
                    onClicked: root.controller.active ? root.controller.disconnectBus() : root.controller.connectBus(root.expectedCount)
                }
                AppButton {
                    objectName: "canfdStartButton"; theme: root.theme; variant: root.running ? "danger" : "primary"
                    text: root.controller && root.controller.state === "starting" ? qsTr("取消启动") : root.running ? qsTr("停止运行") : qsTr("进入 OP")
                    actionEnabled: root.controller && (root.running || root.controller.state === "starting" || (!root.busy && root.controller.canStart))
                    onClicked: root.running || root.controller.state === "starting" ? root.controller.stop() : root.controller.start(root.cycleUs, root.dcEnabled, root.shiftUs)
                }
                AppButton {
                    objectName: "canfdConfigurationButton"; theme: root.theme
                    text: root.configurationOpen ? qsTr("返回监视") : qsTr("配置")
                    onClicked: root.configurationOpen = !root.configurationOpen
                }
                Label {
                    height: root.theme.controlHeight; verticalAlignment: Text.AlignVCenter
                    text: qsTr("%1 个从站 · %2 μs · DC %3").arg(root.connected ? root.controller.slaveCount : root.expectedCount).arg(root.cycleUs).arg(root.dcEnabled ? "ON" : "OFF")
                    color: root.theme.textMuted; font.pixelSize: root.theme.fontBodySmall
                }
            }
        }

        Label {
            objectName: "canfdErrorBanner"; Layout.fillWidth: true
            visible: root.controller && root.controller.errorText.length > 0
            text: visible ? root.controller.errorText : ""
            color: root.theme.dangerText; wrapMode: Text.WrapAnywhere; padding: root.theme.space8
            background: Rectangle { color: root.theme.dangerBackground; radius: root.theme.radiusSmall; border.color: root.theme.dangerBorder }
        }
        Label {
            Layout.fillWidth: true; visible: Boolean(root.diagnostics.cleanupWarning)
            text: visible ? String(root.diagnostics.cleanupWarning) : ""
            color: root.theme.warningText; wrapMode: Text.WrapAnywhere; padding: root.theme.space8
            background: Rectangle { color: root.theme.warningBackground; radius: root.theme.radiusSmall; border.color: root.theme.warningBorder }
        }

        ScrollView {
            id: configScroll
            objectName: "canfdConfigurationPage"
            visible: root.configurationOpen
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: configScroll.availableWidth; spacing: root.theme.space8
                PanelCard {
                    Layout.fillWidth: true; theme: root.theme; padding: root.theme.space12
                    ColumnLayout {
                        width: parent.width; spacing: root.theme.space8
                        Label { text: qsTr("总线配置"); color: root.theme.textPrimary; font.bold: true; font.pixelSize: root.theme.fontSubtitle }
                        Flow {
                            Layout.fillWidth: true; spacing: root.theme.space12
                            ColumnLayout {
                                spacing: 2
                                Label { text: qsTr("期望从站数"); color: root.theme.textSecondary }
                                SpinBox { objectName: "canfdExpectedCount"; from: 1; to: 199; value: root.expectedCount; editable: true; enabled: !root.connected && !root.busy; onValueModified: root.expectedCount = value }
                            }
                            ColumnLayout {
                                spacing: 2
                                Label { text: qsTr("EtherCAT 周期 (μs)"); color: root.theme.textSecondary }
                                SpinBox { objectName: "canfdCycleInput"; from: 250; to: 100000; stepSize: 250; value: root.cycleUs; editable: true; enabled: !root.running && !root.busy; onValueModified: { root.cycleUs = value; if (Math.abs(root.shiftUs) >= value) root.shiftUs = 0 } ToolTip.visible: hovered; ToolTip.text: qsTr("最大周期 100 ms，为固件 200 ms 看门狗保留余量") }
                            }
                            ColumnLayout {
                                spacing: 2
                                Label { text: qsTr("DC SYNC0"); color: root.theme.textSecondary }
                                Switch { objectName: "canfdDcSwitch"; text: qsTr("启用同步"); checked: root.dcEnabled; enabled: !root.running && !root.busy; onToggled: root.dcEnabled = checked }
                            }
                            ColumnLayout {
                                spacing: 2
                                Label { text: qsTr("相移 (μs)"); color: root.theme.textSecondary }
                                SpinBox { from: -root.cycleUs + 1; to: root.cycleUs - 1; value: root.shiftUs; editable: true; enabled: root.dcEnabled && !root.running && !root.busy; onValueModified: root.shiftUs = value }
                            }
                        }
                        Label { Layout.fillWidth: true; text: qsTr("连接后停留在 PRE-OP；CAN 配置支持 PRE-OP 与 OP。运行前请完成配置回读。"); color: root.theme.textMuted; wrapMode: Text.Wrap }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: root.theme.space8
                    Label { text: qsTr("通道配置"); color: root.theme.textPrimary; font.bold: true }
                    ComboBox {
                        objectName: "canfdConfigurationSlave"; Layout.preferredWidth: 130
                        model: root.slaveChoices; textRole: "label"; valueRole: "position"
                        currentIndex: Math.max(0, root.slaveChoices.findIndex(function(item) { return item.position === root.selectedSlave }))
                        enabled: !root.busy
                        onActivated: root.selectSlave(currentValue)
                    }
                    Item { Layout.fillWidth: true }
                    AppButton { theme: root.theme; text: qsTr("撤销编辑"); variant: "ghost"; actionEnabled: !root.busy; onClicked: root.discardDrafts() }
                    AppButton { objectName: "canfdReadButton"; theme: root.theme; text: qsTr("读取配置"); actionEnabled: root.connected && !root.busy; onClicked: root.controller.readConfiguration(root.selectedSlave) }
                    AppButton {
                        objectName: "canfdApplyButton"; theme: root.theme; text: qsTr("应用此从站"); variant: "primary"
                        actionEnabled: root.connected && !root.busy
                        onClicked: root.controller.applyConfiguration(root.selectedSlave, [channel0.values(), channel1.values()])
                    }
                }
                GridLayout {
                    objectName: "canfdChannelsGrid"; Layout.fillWidth: true; columns: width >= 720 ? 2 : 1
                    columnSpacing: root.theme.space8; rowSpacing: root.theme.space8
                    CanfdChannelConfig { id: channel0; objectName: "canfdChannelCard0"; Layout.fillWidth: true; theme: root.theme; channel: 0; configuration: root.channelDraft(root.selectedSlave, 0); liveConfiguration: root.channelConfig(root.selectedSlave, 0); localEdits: root.configurationDrafts[root.selectedSlave + ":0"] !== undefined; editable: !root.busy; onConfigurationEdited: function(values) { root.saveDraft(0, values) } }
                    CanfdChannelConfig { id: channel1; objectName: "canfdChannelCard1"; Layout.fillWidth: true; theme: root.theme; channel: 1; configuration: root.channelDraft(root.selectedSlave, 1); liveConfiguration: root.channelConfig(root.selectedSlave, 1); localEdits: root.configurationDrafts[root.selectedSlave + ":1"] !== undefined; editable: !root.busy; onConfigurationEdited: function(values) { root.saveDraft(1, values) } }
                }
                Label { Layout.fillWidth: true; text: qsTr("当前固件使用标准 11 位 CAN ID，PDO 每帧最多 8 字节。帧格式按通道统一设置；配置仅在从站本次上电期间有效。"); color: root.theme.textMuted; wrapMode: Text.Wrap; padding: root.theme.space4 }
            }
        }

        ColumnLayout {
            visible: !root.configurationOpen
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: root.theme.space8
            PanelCard {
                Layout.fillWidth: true; theme: root.theme; padding: root.theme.space8
                ColumnLayout {
                    width: parent.width; spacing: root.theme.space6
                    Flow {
                        Layout.fillWidth: true; spacing: root.theme.space8
                        ColumnLayout {
                            spacing: 2
                            Label { text: qsTr("从站"); color: root.theme.textSecondary; font.pixelSize: root.theme.fontBodySmall }
                            ComboBox { id: sendSlave; objectName: "canfdSendSlave"; implicitWidth: 110; model: root.slaveChoices; textRole: "label"; valueRole: "position"; currentIndex: Math.max(0, root.slaveChoices.findIndex(function(item) { return item.position === root.sendSlavePosition })); enabled: !root.controller || !root.controller.periodicSending; onActivated: root.sendSlavePosition = currentValue }
                        }
                        ColumnLayout {
                            spacing: 2
                            Label { text: qsTr("通道"); color: root.theme.textSecondary; font.pixelSize: root.theme.fontBodySmall }
                            ComboBox { id: sendChannel; objectName: "canfdSendChannel"; implicitWidth: 90; model: ["CAN 0", "CAN 1"]; enabled: !root.controller || !root.controller.periodicSending }
                        }
                        ColumnLayout {
                            spacing: 2
                            Label { text: "CAN ID (HEX)"; color: root.theme.textSecondary; font.pixelSize: root.theme.fontBodySmall }
                            TextField { id: idInput; objectName: "canfdIdInput"; implicitWidth: 110; text: "123"; placeholderText: "000–7FF"; selectByMouse: true; maximumLength: 5; enabled: !root.controller || !root.controller.periodicSending; font.family: "Consolas" }
                        }
                        ColumnLayout {
                            spacing: 2
                            Label { text: "DLC"; color: root.theme.textSecondary; font.pixelSize: root.theme.fontBodySmall }
                            SpinBox { id: dlcInput; objectName: "canfdDlcInput"; implicitWidth: 100; from: 0; to: 8; value: 8; editable: true; enabled: !root.controller || !root.controller.periodicSending }
                        }
                        ColumnLayout {
                            spacing: 2
                            Label { text: qsTr("发送间隔 (μs)"); color: root.theme.textSecondary; font.pixelSize: root.theme.fontBodySmall }
                            SpinBox { id: intervalInput; objectName: "canfdIntervalInput"; implicitWidth: 135; from: root.cycleUs; to: 10000000; value: 100000; stepSize: root.cycleUs; editable: true; enabled: !root.controller || !root.controller.periodicSending }
                        }
                        ColumnLayout {
                            spacing: 2
                            Label { text: root.sendConfig ? ["Classic", "FD", "FD+BRS"][root.sendConfig.format] : qsTr("未配置"); color: root.theme.textMuted; font.pixelSize: root.theme.fontBodySmall }
                            RowLayout {
                                spacing: root.theme.space6
                                AppButton { objectName: "canfdSendOnceButton"; theme: root.theme; text: qsTr("发送一次"); actionEnabled: root.canSend && !root.controller.periodicSending; onClicked: root.send(false) }
                                AppButton {
                                    objectName: "canfdPeriodicButton"; theme: root.theme
                                    text: root.controller && root.controller.periodicSending ? qsTr("停止发送") : qsTr("周期发送")
                                    variant: root.controller && root.controller.periodicSending ? "danger" : "primary"
                                    actionEnabled: root.controller && (root.controller.periodicSending || (root.canSend && root.intervalValid))
                                    onClicked: root.controller.periodicSending ? root.controller.stopPeriodic() : root.send(true)
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: root.theme.space8
                        Label { text: "Data (HEX)"; color: root.theme.textSecondary; font.pixelSize: root.theme.fontBodySmall }
                        TextField { id: payload; objectName: "canfdPayloadInput"; Layout.fillWidth: true; Layout.minimumWidth: 120; text: "00 00 00 00 00 00 00 00"; placeholderText: qsTr("字节以空格分隔，与 DLC 一致"); selectByMouse: true; font.family: "Consolas"; enabled: !root.controller || !root.controller.periodicSending }
                        Label { text: root.idValid && root.payloadValid ? qsTr("标准帧 · 0–8 B") : qsTr("请检查 ID / 数据长度"); color: root.idValid && root.payloadValid ? root.theme.textMuted : root.theme.warningText; font.pixelSize: root.theme.fontBodySmall }
                    }
                    Label { Layout.fillWidth: true; text: qsTr("实际发送间隔 %1 μs（按 EtherCAT 周期向上取整）").arg(root.actualIntervalUs); color: root.theme.textMuted; font.pixelSize: root.theme.fontCaption }
                }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: root.theme.space8
                TabBar {
                    id: viewTabs; objectName: "canfdViewTabs"; Layout.preferredWidth: 220
                    TabButton { text: qsTr("接收报文") }
                    TabButton { text: qsTr("PDO 监视") }
                }
                ComboBox { objectName: "canfdPdoSlave"; visible: viewTabs.currentIndex === 1; Layout.preferredWidth: 120; model: root.slaveChoices; textRole: "label"; valueRole: "position"; currentIndex: Math.max(0, root.slaveChoices.findIndex(function(item) { return item.position === root.selectedSlave })); onActivated: root.selectSlave(currentValue) }
                StatusBadge {
                    objectName: "canfdPdoQuality"; visible: viewTabs.currentIndex === 1; theme: root.theme; compact: true
                    text: root.snapshot.historical ? qsTr("历史快照") : root.snapshot.valid === false ? qsTr("本周期无效") : root.snapshot.valid ? qsTr("实时") : qsTr("未采样")
                    tone: root.snapshot.historical ? "neutral" : root.snapshot.valid === false ? "warning" : root.snapshot.valid ? "success" : "neutral"
                }
                Label { objectName: "canfdViewStatistics"; Layout.fillWidth: true; text: viewTabs.currentIndex === 0 ? qsTr("接收 %1 · 丢弃 %2").arg(root.controller ? root.controller.receivedCount : 0).arg(root.controller ? root.controller.droppedCount : 0) : qsTr("WKC %1 / %2").arg(root.pdoWorkingCounter("wkc")).arg(root.pdoWorkingCounter("expectedWkc")); color: root.theme.textMuted; elide: Text.ElideRight; font.pixelSize: root.theme.fontBodySmall }
                AppButton { visible: viewTabs.currentIndex === 0; theme: root.theme; variant: "ghost"; text: root.followTail ? qsTr("暂停跟随") : qsTr("恢复跟随"); implicitHeight: root.theme.controlHeightSmall; onClicked: { root.followTail = !root.followTail; if (root.followTail) frameList.positionViewAtEnd() } }
                AppButton { objectName: "canfdClearButton"; visible: viewTabs.currentIndex === 0; theme: root.theme; variant: "ghost"; text: qsTr("清空"); implicitHeight: root.theme.controlHeightSmall; onClicked: { if (root.controller) root.controller.clearFrames() } }
            }

            Rectangle {
                visible: viewTabs.currentIndex === 0; Layout.fillWidth: true; Layout.fillHeight: true
                color: root.theme.surface; radius: root.theme.radiusMedium; border.color: root.theme.border; clip: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: root.theme.space8; spacing: 0
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: 28; spacing: 8
                        Label { text: qsTr("相对时间 (主机)"); Layout.preferredWidth: 145; color: root.theme.textMuted }
                        Label { text: qsTr("从站"); Layout.preferredWidth: 42; color: root.theme.textMuted }
                        Label { text: "CAN"; Layout.preferredWidth: 36; color: root.theme.textMuted }
                        Label { text: "ID"; Layout.preferredWidth: 70; color: root.theme.textMuted }
                        Label { text: "DLC"; Layout.preferredWidth: 35; color: root.theme.textMuted }
                        Label { text: "Data (HEX)"; Layout.fillWidth: true; color: root.theme.textMuted }
                    }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.theme.divider }
                    ListView {
                        id: frameList; objectName: "canfdFrameList"; Layout.fillWidth: true; Layout.fillHeight: true
                        model: root.controller ? root.controller.frames : null; clip: true; reuseItems: true
                        ScrollBar.vertical: ScrollBar {}
                        onCountChanged: { if (root.followTail) Qt.callLater(positionViewAtEnd) }
                        onMovementStarted: root.followTail = false
                        delegate: Rectangle {
                            id: frame
                            required property int index
                            required property string timestamp
                            required property int slave
                            required property int channel
                            required property string idHex
                            required property int dlc
                            required property string dataHex
                            width: ListView.view.width; height: 27
                            color: index % 2 ? root.theme.surfaceMuted : "transparent"
                            RowLayout {
                                anchors.fill: parent; spacing: 8
                                Label { text: frame.timestamp; Layout.preferredWidth: 145; font.family: "Consolas"; font.pixelSize: root.theme.fontBodySmall; color: root.theme.textSecondary; elide: Text.ElideRight }
                                Label { text: frame.slave; Layout.preferredWidth: 42; color: root.theme.textSecondary }
                                Label { text: frame.channel; Layout.preferredWidth: 36; color: root.theme.textSecondary }
                                Label { text: frame.idHex; Layout.preferredWidth: 70; font.family: "Consolas"; color: root.theme.accentText }
                                Label { text: frame.dlc; Layout.preferredWidth: 35; color: root.theme.textSecondary }
                                Label { text: frame.dataHex; Layout.fillWidth: true; font.family: "Consolas"; font.pixelSize: root.theme.fontBodySmall; color: root.theme.textPrimary }
                            }
                        }
                        Label { anchors.centerIn: parent; visible: frameList.count === 0; text: root.running ? qsTr("等待有效 CAN 报文…") : qsTr("连接并进入 OP 后接收 CAN 报文"); color: root.theme.textMuted }
                    }
                }
            }

            ScrollView {
                id: pdoScroll; objectName: "canfdPdoScroll"
                visible: viewTabs.currentIndex === 1; Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; contentWidth: availableWidth; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                GridLayout {
                    width: pdoScroll.availableWidth; columns: width >= 900 ? 2 : 1
                    columnSpacing: root.theme.space8; rowSpacing: root.theme.space8
                    CanfdPdoView {
                        objectName: "canfdRxPdo"; Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; theme: root.theme
                        title: "RxPDO"; direction: qsTr("主站 → 从站"); wordName: "Controlword"
                        wordHex: root.snapshot.rxControlwordHex || "—"; digitalHex: root.snapshot.digitalOutputsHex || "—"
                        rows: root.snapshot.rxRows || []; rawHex: root.snapshot.rxHex || ""
                    }
                    CanfdPdoView {
                        objectName: "canfdTxPdo"; Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; theme: root.theme
                        title: "TxPDO"; direction: qsTr("从站 → 主站"); wordName: "Statusword"
                        wordHex: root.snapshot.txStatuswordHex || "—"; digitalHex: root.snapshot.digitalInputsHex || "—"
                        rows: root.snapshot.txRows || []; rawHex: root.snapshot.txHex || ""
                    }
                }
            }
        }
    }
}
