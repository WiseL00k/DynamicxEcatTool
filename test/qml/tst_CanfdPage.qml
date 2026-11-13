pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import "../../qml/pages" as Pages
import "../../qml/theme" as Theme

TestCase {
    id: tests
    name: "CanfdPage"
    when: windowShown
    width: 1280; height: 900; visible: true
    property var controller
    property var session
    property var page
    property var theme
    Component { id: backendComponent; FakeCanfdController {} }
    Component { id: sessionComponent; FakeSessionUi {} }
    Component { id: themeComponent; Theme.DesignTokens { themeMode: "light"; fontFamily: "Microsoft YaHei UI" } }
    Component { id: pageComponent; Pages.Ecat2CanfdPage {} }
    Component { id: captureComponent; ScreenshotCapture { outputDirectory: DcUiCaptureDirectory } }

    function init() {
        controller = createTemporaryObject(backendComponent, tests)
        session = createTemporaryObject(sessionComponent, tests)
        theme = createTemporaryObject(themeComponent, tests)
        page = createTemporaryObject(pageComponent, tests, {width: 868, height: 480, theme: theme, controller: controller, sessionUi: session})
        verify(page !== null)
    }
    function cleanup() { if (page) page.destroy(); page = null }
    function child(name) {
        const result = findChild(page, name)
        verify(result !== null, "Missing " + name)
        return result
    }
    function running() {
        controller.connected = true
        controller.running = true
        controller.active = true
        controller.statusText = "OP"
        session.sessionActive = true
        session.sessionMode = "ECAT2CANFD"
    }
    function test_connectionOwnershipAndPreopConfiguration() {
        compare(child("canfdExpectedCount").to, 199)
        session.nicReady = false
        verify(!child("canfdConnectButton").enabled)
        session.nicReady = true
        session.sessionActive = true
        session.sessionMode = "DC测试"
        verify(!child("canfdConnectButton").enabled)
        session.sessionActive = false
        mouseClick(child("canfdConnectButton"))
        compare(controller.connectCalls, 1)
        mouseClick(child("canfdConfigurationButton"))
        wait(30)
        verify(child("canfdReadButton").enabled)
        verify(child("canfdApplyButton").enabled)
        mouseClick(child("canfdReadButton"))
        compare(controller.readCalls, 1)
        mouseClick(child("canfdApplyButton"))
        compare(controller.applyCalls, 1)
        compare(controller.lastConfig.length, 2)
        compare(controller.lastConfig[1].channel, 1)
        controller.busy = true
        verify(!child("canfdApplyButton").enabled)
        verify(!child("canfdReadButton").enabled)
    }
    function test_disconnectCancelsBusyConnectionStartupAndSdo() {
        for (const state of ["connecting", "starting", "preop", "running", "stopping"]) {
            controller.state = state
            controller.active = true
            controller.busy = true
            controller.connected = state !== "connecting"
            controller.running = state === "running"
            const button = child("canfdConnectButton")
            verify(button.enabled, state + " must remain cancellable")
            const calls = controller.disconnectCalls
            mouseClick(button)
            compare(controller.disconnectCalls, calls + 1)
            verify(!button.enabled, "A repeated disconnect must be disabled")
        }
    }
    function test_startupAndBusyRunningCanStopWithoutDisconnecting() {
        controller.active = true
        controller.connected = true
        controller.state = "starting"
        controller.busy = true
        const button = child("canfdStartButton")
        compare(button.text, "取消启动")
        verify(button.enabled)
        mouseClick(button)
        compare(controller.stopCalls, 1)
        verify(!button.enabled)
        controller.state = "running"
        controller.running = true
        controller.busy = true
        verify(button.enabled)
        mouseClick(button)
        compare(controller.stopCalls, 2)
        compare(controller.disconnectCalls, 0)
    }
    function test_faultedStatusRemainsVisibleAndRecoverable() {
        controller.active = true
        controller.connected = true
        controller.state = "faulted"
        controller.statusText = "已停止 · 状态未确认"
        controller.errorText = "PRE-OP cleanup was not confirmed."
        compare(child("canfdSessionStatus").tone, "danger")
        compare(child("canfdSessionStatus").text, "已停止 · 状态未确认")
        verify(child("canfdErrorBanner").visible)
        verify(!child("canfdStartButton").enabled)
        verify(child("canfdConnectButton").enabled)
        mouseClick(child("canfdConnectButton"))
        compare(controller.disconnectCalls, 1)
    }
    function test_historicalPdoKeepsItsOwnWorkingCounter() {
        running()
        child("canfdViewTabs").currentIndex = 1
        controller.pdoSnapshot = {valid: true, historical: false, wkc: 3, expectedWkc: 3}
        compare(child("canfdViewStatistics").text, "WKC 3 / 3")
        controller.running = false
        controller.state = "preop"
        controller.diagnostics = {wkc: 1, expectedWkc: 3}
        controller.pdoSnapshot = {valid: true, historical: true, wkc: 3, expectedWkc: 3}
        compare(child("canfdPdoQuality").text, "历史快照")
        compare(child("canfdViewStatistics").text, "WKC 3 / 3")
    }
    function test_offlineDraftSurvivesReadbackAndSlaveSwitch() {
        page.expectedCount = 3
        page.configurationOpen = true
        compare(page.slaveChoices.length, 3)
        page.selectSlave(2)
        const card = child("canfdChannelCard0")
        const draft = {channel: 0, enabled: true, format: 2, nominalBitrate: 500000, dataBitrate: 2500000, options: 7}
        card.configurationEdited(draft)
        compare(card.values().format, 2)
        page.selectSlave(1)
        page.selectSlave(2)
        compare(card.values().nominalBitrate, 500000)
        controller.slaves = [{position: 1}, {position: 2}, {position: 3}]
        controller.connected = true
        controller.channelConfigs = [{slave: 2, channel: 0, enabled: true, format: 0, nominalBitrate: 1000000, dataBitrate: 5000000, options: 5, status: 0, error: 0, dirty: false}]
        page.reconcileDrafts()
        compare(card.values().format, 2)
        verify(card.localEdits)
        controller.channelConfigs = [Object.assign({}, draft, {slave: 2, status: 0, error: 0, dirty: false})]
        page.reconcileDrafts()
        verify(!card.localEdits)
        compare(card.values().format, 2)
    }
    function test_captureLayoutsForVisualReview() {
        if (!DcUiCaptureEnabled)
            skip("Set DC_UI_CAPTURE_SCREENSHOTS=1 to capture layouts")
        const capture = createTemporaryObject(captureComponent, tests)
        running()
        controller.frames.append({timestamp: "12:34:56.123456", slave: 1, channel: 0, idHex: "123", dlc: 8, dataHex: "00 11 22 33 44 55 66 77"})
        const rows = []
        for (let channel = 0; channel < 2; ++channel)
            for (let slot = 0; slot < 9; ++slot)
                rows.push({channel: channel, slot: slot, valid: slot === 0, idHex: "123", dlc: 8, dataHex: "00 11 22 33 44 55 66 77", frameInfoHex: "8813"})
        controller.pdoSnapshot = {rxRows: rows, txRows: rows, rxControlwordHex: "0001", txStatuswordHex: "0002", digitalOutputsHex: "0000", digitalInputsHex: "0000"}
        for (const dark of [false, true]) {
            theme.themeMode = dark ? "dark" : "light"
            for (const width of [868, 1052]) {
                page.width = width
                for (const mode of ["frames", "config", "pdo"]) {
                    page.configurationOpen = mode === "config"
                    child("canfdViewTabs").currentIndex = mode === "pdo" ? 1 : 0
                    wait(180)
                    const revision = capture.revision + 1
                    capture.save(page, "canfd-" + (dark ? "dark" : "light") + "-" + width + "-" + mode)
                    tryCompare(capture, "revision", revision, 3000)
                    verify(capture.lastSuccess)
                }
            }
        }
    }
    function test_sendRequiresOpReadyChannelAndExactPayload() {
        verify(!child("canfdSendOnceButton").enabled)
        running()
        verify(child("canfdSendOnceButton").enabled)
        child("canfdIdInput").text = "800"
        verify(!child("canfdSendOnceButton").enabled)
        child("canfdIdInput").text = "0x7FF"
        child("canfdPayloadInput").text = "01 02"
        verify(!child("canfdSendOnceButton").enabled)
        child("canfdDlcInput").value = 2
        verify(child("canfdSendOnceButton").enabled)
        mouseClick(child("canfdSendOnceButton"))
        compare(controller.sendCalls, 1)
        compare(controller.lastFrame[2], "0x7FF")
        compare(controller.lastFrame[3], 2)
        const configs = controller.channelConfigs.slice()
        configs[0] = Object.assign({}, configs[0], {dirty: true, status: 1})
        controller.channelConfigs = configs
        verify(!child("canfdSendOnceButton").enabled)
    }
    function test_liveSlaveRefreshPreservesTransmitSelection() {
        controller.slaves = [{position: 1}, {position: 2}]
        running()
        page.sendSlavePosition = 2
        compare(child("canfdSendSlave").currentValue, 2)
        controller.slaves = [{position: 1, state: 8}, {position: 2, state: 8}]
        compare(child("canfdSendSlave").currentValue, 2)
    }
    function test_periodicStopRemainsAvailableAndSettingsLock() {
        running()
        child("canfdIntervalInput").value = 3000
        verify(child("canfdPeriodicButton").enabled)
        compare(page.actualIntervalUs, 4000)
        mouseClick(child("canfdPeriodicButton"))
        verify(controller.periodicSending)
        verify(!child("canfdIdInput").enabled)
        controller.busy = true
        verify(child("canfdPeriodicButton").enabled)
        mouseClick(child("canfdPeriodicButton"))
        verify(!controller.periodicSending)
        page.configurationOpen = true
        verify(!child("canfdCycleInput").enabled)
        verify(!child("canfdDcSwitch").enabled)
    }
    function test_framesAndAllPdoSlotsRemainAvailable() {
        running()
        controller.frames.append({timestamp: "12:34:56.123456", slave: 1, channel: 0, idHex: "123", dlc: 2, dataHex: "01 02"})
        compare(child("canfdFrameList").count, 1)
        const rows = []
        for (let channel = 0; channel < 2; ++channel)
            for (let slot = 0; slot < 9; ++slot)
                rows.push({channel: channel, slot: slot, valid: slot === 0, idHex: "123", dlc: 8, dataHex: "00 11 22 33 44 55 66 77", frameInfoHex: "8813"})
        controller.pdoSnapshot = {slave: 1, rxRows: rows, txRows: rows, rxControlwordHex: "0001", txStatuswordHex: "0002", digitalOutputsHex: "0000", digitalInputsHex: "0000", rxHex: "00 ".repeat(185), txHex: "00 ".repeat(185)}
        child("canfdViewTabs").currentIndex = 1
        compare(child("canfdRxPdo").rows.length, 18)
        compare(child("canfdTxPdo").rows.length, 18)
        compare(child("canfdRxPdo").wordHex, "0001")
        verify(child("canfdPdoScroll").contentWidth <= child("canfdPdoScroll").availableWidth + 1)
        controller.pdoSnapshot = Object.assign({}, controller.pdoSnapshot, {valid: false})
        compare(child("canfdPdoQuality").text, "本周期无效")
        controller.pdoSnapshot = Object.assign({}, controller.pdoSnapshot, {historical: true})
        compare(child("canfdPdoQuality").text, "历史快照")
    }
    function test_layout_data() {
        return [{tag: "compact-light", width: 868, dark: false}, {tag: "wide-dark", width: 1052, dark: true}, {tag: "narrow-dark", width: 628, dark: true}]
    }
    function test_layout(data) {
        page.width = data.width
        theme.themeMode = data.dark ? "dark" : "light"
        wait(30)
        const list = child("canfdFrameList")
        verify(list.height > 60)
        const send = child("canfdSendOnceButton")
        const origin = send.mapToItem(page, 0, 0)
        verify(origin.x >= 0 && origin.x + send.width <= page.width + 1)
        page.configurationOpen = true
        wait(30)
        const config = child("canfdConfigurationPage")
        verify(config.contentWidth <= config.availableWidth + 1)
    }
}
