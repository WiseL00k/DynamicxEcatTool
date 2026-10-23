pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import "../../qml/pages" as Pages
import "../../qml/theme" as Theme

TestCase {
    id: tests
    name: "DamiaoPage"
    when: windowShown
    width: 1280; height: 960; visible: true
    property var controller
    property var session
    property var page
    property var theme
    Component { id: controllerComponent; FakeDamiaoController {} }
    Component { id: sessionComponent; FakeSessionUi {} }
    Component { id: themeComponent; Theme.DesignTokens { themeMode: "light"; fontFamily: "Microsoft YaHei UI" } }
    Component { id: pageComponent; Pages.ParamsConfigPage {} }
    Component { id: captureComponent; ScreenshotCapture { outputDirectory: DcUiCaptureDirectory } }

    function init() {
        controller = createTemporaryObject(controllerComponent, tests)
        session = createTemporaryObject(sessionComponent, tests)
        theme = createTemporaryObject(themeComponent, tests)
        page = createTemporaryObject(pageComponent, tests, {
            width: 1052, height: 900, theme: theme, controller: controller, sessionUi: session})
        verify(page !== null)
    }
    function cleanup() { if (page) page.destroy(); page = null }
    function child(name) {
        const result = findChild(page, name)
        verify(result !== null, "Missing " + name)
        return result
    }
    function running(type) {
        controller.slaveType = type
        controller.active = controller.connected = controller.running = true
        controller.state = "running"; controller.statusText = "OP"
        controller.canfd.connected = controller.canfd.running = true
        controller.canfd.state = "running"
        session.sessionActive = true; session.sessionMode = "达妙电机MIT调试"
    }
    function test_previewChangesNeverSendControl() {
        const previews = controller.calls.preview
        child("dmPosition").text = "1.25"
        child("dmKp").text = "20"
        tryVerify(() => controller.calls.preview > previews)
        compare(controller.sendCalls, 0)
        compare(controller.rawCalls, 0)
        compare(controller.connectCalls, 0)
        verify(child("dmFramePreview").readOnly)
        verify(!child("dmSendControlButton").enabled)
        running("ecat2canfd")
        verify(child("dmSendControlButton").enabled)
        mouseClick(child("dmSendControlButton"))
        compare(controller.sendCalls, 1)
        compare(controller.lastCommand[2].pos, 1.25)
        compare(controller.lastCommand[2].kp, 20)
        child("dmPosition").text = "2.5"
        compare(controller.sendCalls, 1)
    }
    function test_invalidNumericInputsBlockSubmission_data() {
        return [
            {tag: "non-finite", field: "dmPosition", value: "1e999"},
            {tag: "position-range", field: "dmPosition", value: "12.5001"},
            {tag: "velocity-range", field: "dmVelocity", value: "-30.01"},
            {tag: "torque-range", field: "dmTorque", value: "10.01"},
            {tag: "kp-range", field: "dmKp", value: "500.01"},
            {tag: "kd-range", field: "dmKd", value: "-0.01"},
            {tag: "zero-pmax", field: "dmPmax", value: "0"},
            {tag: "empty-vmax", field: "dmVmax", value: ""},
            {tag: "negative-tmax", field: "dmTmax", value: "-1"}
        ]
    }
    function test_invalidNumericInputsBlockSubmission(data) {
        running("ecat2canfd")
        child(data.field).text = data.value
        verify(!child("dmSendControlButton").enabled)
        verify(child("dmValidationMessage").visible)
        compare(child("dmFramePreview").text, "-- -- -- -- -- -- -- --")
        compare(controller.sendCalls, 0)
    }
    function test_idRulesFollowSelectedTransport() {
        running("mit")
        for (const id of ["", "0"]) {
            child("dmMotorId").text = id
            verify(!child("dmSendControlButton").enabled)
            verify(!child("dmStopSendingButton").enabled)
            verify(child("dmEnableButton").enabled)
            verify(child("dmDisableButton").enabled)
        }
        child("dmMotorId").text = ""
        wait(30)
        mouseClick(child("dmDisableButton"))
        compare(controller.disableCalls, 1)
        child("dmMotorId").text = "9"
        verify(child("dmSendControlButton").enabled)
        verify(!child("dmPeriodicSwitch").visible)
        running("ecat2canfd")
        verify(child("dmPeriodicSwitch").visible)
        for (const id of ["0", "0x7FF", "7ff"] ) {
            child("dmMotorId").text = id
            verify(child("dmSendControlButton").enabled, id)
        }
        for (const id of ["800", "-1", "G1", ""] ) {
            child("dmMotorId").text = id
            verify(!child("dmSendControlButton").enabled, id)
            verify(!child("dmEnableButton").enabled, id)
            verify(!child("dmDisableButton").enabled, id)
            verify(!child("dmStopSendingButton").enabled, id)
        }
    }
    function test_connectionAndCommandStateGates() {
        session.nicReady = false
        verify(!child("dmConnectButton").enabled)
        session.nicReady = true
        session.sessionActive = true; session.sessionMode = "ECAT2CANFD"
        verify(!child("dmConnectButton").enabled)
        session.sessionActive = false
        controller.slaveType = "ecat2canfd"
        mouseClick(child("dmConnectButton"))
        compare(controller.connectCalls, 1)
        verify(!child("dmTypeSelector").enabled)
        verify(!child("dmSendControlButton").enabled)
        verify(child("dmStartButton").enabled)
        wait(30) // The status label changes the Flow positions after connect.
        mouseClick(child("dmStartButton"))
        compare(controller.startCalls, 1)
        verify(child("dmSendControlButton").enabled)
        controller.busy = true
        verify(!child("dmSendControlButton").enabled)
        verify(!child("dmEnableButton").enabled)
        verify(child("dmConnectButton").enabled)
        wait(30)
        mouseClick(child("dmConnectButton"))
        compare(controller.disconnectCalls, 1)
    }
    function test_lockedTargetStillAllowsStopAndDisable() {
        running("ecat2canfd")
        controller.periodicSending = controller.targetLocked = true
        controller.periodicIntervalUs = 4000
        verify(!child("dmCanChannel").enabled)
        verify(!child("dmMotorId").enabled)
        verify(!child("dmSendControlButton").enabled)
        verify(!child("dmEnableButton").enabled)
        verify(child("dmDisableButton").enabled)
        verify(child("dmStopSendingButton").enabled)
        mouseClick(child("dmDisableButton"))
        compare(controller.disableCalls, 1)
        mouseClick(child("dmStopSendingButton"))
        compare(controller.stopSendingCalls, 1)
        verify(child("dmMotorId").enabled)
    }
    function test_channelMustBeVerifiedAndReadyForEverySupportedFormat() {
        running("ecat2canfd")
        const original = controller.canfd.channelConfigs.slice()
        for (const format of [0, 1, 2]) {
            controller.canfd.channelConfigs = [Object.assign({}, original[0], {format: format}), original[1]]
            verify(child("dmSendControlButton").enabled, "format " + format)
        }
        for (const change of [{verified: false}, {enabled: false}, {status: 1}, {error: 3}]) {
            controller.canfd.channelConfigs = [Object.assign({}, original[0], change), original[1]]
            verify(!child("dmSendControlButton").enabled, JSON.stringify(change))
            verify(!child("dmEnableButton").enabled, JSON.stringify(change))
        }
    }
    function test_rawFrameRequiresExactlyEightBytes() {
        running("ecat2canfd")
        mouseClick(child("dmRawToggle"))
        child("dmRawInput").text = "01 02"
        verify(!child("dmSendRawButton").enabled)
        child("dmRawInput").text = "00 11 22 33 44 55 66 GG"
        verify(!child("dmSendRawButton").enabled)
        child("dmRawInput").text = "00 11 22 33 44 55 66 77"
        verify(child("dmSendRawButton").enabled)
        mouseClick(child("dmSendRawButton"))
        compare(controller.rawCalls, 1)
        compare(controller.lastCommand[2], "00 11 22 33 44 55 66 77")
    }
    function test_configurationUsesCanfdOnlyAndDisablesBusyWrites() {
        verify(!child("dmConfigurationButton").visible)
        controller.slaveType = "ecat2canfd"
        mouseClick(child("dmConnectButton"))
        mouseClick(child("dmConfigurationButton"))
        verify(child("dmConfigurationPage").visible)
        verify(child("dmReadConfigButton").enabled)
        verify(child("dmApplyConfigButton").enabled)
        mouseClick(child("dmReadConfigButton"))
        compare(controller.canfd.readCalls, 1)
        controller.canfd.busy = controller.busy = true
        verify(!child("dmReadConfigButton").enabled)
        verify(!child("dmApplyConfigButton").enabled)
    }
    function test_layout_data() {
        return [{tag: "narrow-dark", width: 628, dark: true},
            {tag: "compact-light", width: 868, dark: false},
            {tag: "wide-dark", width: 1052, dark: true}]
    }
    function test_layout(data) {
        running("ecat2canfd")
        page.width = data.width
        page.height = 660
        theme.themeMode = data.dark ? "dark" : "light"
        wait(30)
        const scroll = child("dmPageScroll")
        verify(scroll.contentWidth <= scroll.availableWidth + 1)
        for (const name of ["dmMotorId", "dmPosition", "dmFramePreview", "dmSendControlButton"]) {
            const item = child(name)
            const origin = item.mapToItem(page, 0, 0)
            verify(item.width > 30, name + " has usable width")
            verify(origin.x >= -1 && origin.x + item.width <= page.width + 1, name + " fits horizontally")
        }
        const previewCard = child("dmFramePreviewCard")
        const preview = child("dmFramePreview")
        const previewOrigin = preview.mapToItem(previewCard, 0, 0)
        verify(previewOrigin.y >= 0 && previewOrigin.y + preview.height <= previewCard.height + 1,
            "Frame preview stays inside its card")
        page.configurationOpen = true
        wait(30)
        verify(scroll.contentWidth <= scroll.availableWidth + 1)
        verify(child("dmCanChannelsGrid").width <= scroll.availableWidth + 1)
    }
    function test_captureLayoutsForVisualReview() {
        if (!DcUiCaptureEnabled)
            skip("Set DC_UI_CAPTURE_SCREENSHOTS=1 to capture layouts")
        const capture = createTemporaryObject(captureComponent, tests)
        const scale = String(DcUiCaptureScale).replace(/\./g, "_")
        page.height = 660
        for (const type of ["mit", "ecat2canfd"])
            for (const dark of [false, true])
                for (const width of [628, 868, 1052]) {
                    running(type)
                    page.width = width
                    theme.themeMode = dark ? "dark" : "light"
                    for (const mode of type === "mit" ? ["control"] : ["control", "config"]) {
                        page.configurationOpen = mode === "config"
                        wait(120)
                        const revision = capture.revision + 1
                        capture.save(page, "damiao-" + type + "-" + (dark ? "dark" : "light")
                            + "-" + width + "-" + mode + "-scale-" + scale)
                        tryCompare(capture, "revision", revision, 3000)
                        verify(capture.lastSuccess)
                    }
                }
    }
}
