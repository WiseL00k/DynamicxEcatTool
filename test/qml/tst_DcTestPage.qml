pragma ComponentBehavior: Bound

import QtQuick
import QtTest
import "../../qml/pages" as Pages
import "../../qml/theme" as Theme

TestCase {
    id: testCase
    name: "DcTestPage"
    when: windowShown
    width: 1280
    height: 900
    visible: true

    property var controller
    property var session
    property var theme
    property var page
    readonly property string applicationFontFamily: applicationFontProbe.font.family
    // qmllint disable unqualified
    readonly property bool captureEnabled: DcUiCaptureEnabled
    readonly property url captureDirectory: DcUiCaptureDirectory
    // qmllint enable unqualified

    Text {
        id: applicationFontProbe
        visible: false
    }

    Component { id: controllerComponent; FakeDcController {} }
    Component { id: sessionComponent; FakeSessionUi {} }
    Component {
        id: themeComponent
        Theme.DesignTokens {
            themeMode: "light"
            fontFamily: testCase.applicationFontFamily
        }
    }
    Component { id: pageComponent; Pages.DcTestPage {} }
    Component {
        id: captureComponent
        ScreenshotCapture {
            outputDirectory: testCase.captureDirectory
        }
    }

    function init() {
        controller = controllerComponent.createObject(testCase)
        session = sessionComponent.createObject(testCase)
        theme = themeComponent.createObject(testCase)
    }

    function cleanup() {
        if (page)
            page.destroy()
        if (controller)
            controller.destroy()
        if (session)
            session.destroy()
        if (theme)
            theme.destroy()
        controller = null
        session = null
        theme = null
        page = null
    }

    function createPage(width, height) {
        page = createTemporaryObject(pageComponent, testCase, {
            width: width,
            height: height,
            visible: true,
            controller: testCase.controller,
            sessionUi: testCase.session,
            theme: testCase.theme
        })
        verify(page !== null)
        verify(page.controller === testCase.controller)
        wait(20)
        return page
    }

    function child(name) {
        const found = findChild(page, name)
        verify(found !== null, "Missing object: " + name)
        return found
    }

    function diagnosticsWithHistory() {
        return {
            configured: true,
            operational: false,
            dcTimeValid: true,
            phaseValid: true,
            finished: true,
            cancelled: false,
            referenceSlave: 1,
            dcSlaveCount: 2,
            cycleNs: 2000000,
            shiftNs: -100000,
            wkc: 3,
            expectedWkc: 3,
            dcTimeNs: "9223372036854775806",
            cycleCount: "18446744073709551614",
            wkcErrorCount: "9007199254740993",
            missedCycles: "9007199254740995",
            actualCycleNs: 1999500,
            minCycleNs: 1999000,
            maxCycleNs: 2000100,
            maxDeviationNs: 1000,
            phaseErrorNs: -250,
            correctionNs: 125,
            error: "",
            cleanupWarning: ""
        }
    }

    function slavesForVisualReview() {
        return [
            {
                address: 1,
                name: "Reference DC slave with a long name for elision review",
                hasDc: true, sync0Requested: true, sync0Verified: true,
                shutdownAttempted: false, shutdownVerified: false,
                cycleNs: 2000000, shiftNs: -100000,
                propagationDelayNs: 2147483647,
                state: 8, alStatusCode: 0,
                alStatusText: "No error; reference clock selected and distributed-clock configuration read back successfully"
            },
            {
                address: 2,
                name: "Ordinary non-DC input and output terminal in a mixed bus",
                hasDc: false, sync0Requested: false, sync0Verified: false,
                shutdownAttempted: false, shutdownVerified: false,
                cycleNs: 0, shiftNs: 0, propagationDelayNs: 0,
                state: 8, alStatusCode: 0,
                alStatusText: "No error; this slave does not advertise distributed-clock support"
            },
            {
                address: 3,
                name: "DC slave whose shutdown verification requires operator attention",
                hasDc: true, sync0Requested: true, sync0Verified: true,
                shutdownAttempted: true, shutdownVerified: false,
                cycleNs: 2147483647, shiftNs: -2147483647,
                propagationDelayNs: 2147483647,
                state: 8, alStatusCode: 27,
                alStatusText: "Sync manager watchdog: distributed clock synchronization remained outside the configured tolerance after shutdown"
            }
        ]
    }

    function verifySlaveDetailGeometry(address) {
        const al = child("dcSlaveAl-" + address)
        const status = child("dcSlaveStatus-" + address)
        const details = child("dcSlaveDetails-" + address)
        compare(al.truncated, false)
        verify(al.x >= -1)
        verify(al.x + al.width <= al.parent.width + 1)
        verify(al.width >= page.width - 100,
               "Slave " + address + " AL detail does not span its row: "
               + al.width + " / " + page.width)
        verify(status.x >= -1)
        verify(status.x + status.width <= status.parent.width + 1)
        verify(details.width <= page.width - 20)

        for (const detail of details.children) {
            if (!detail.visible || detail.width <= 0)
                continue
            verify(detail.x >= -1, "Slave " + address + " detail starts outside its grid")
            verify(detail.x + detail.width <= details.width + 1,
                   "Slave " + address + " detail exceeds its grid")
        }
    }

    function test_startLocksParametersAndStopRequiresOwnedSession() {
        createPage(868, 456)
        const cycle = child("dcCycleInput")
        const shift = child("dcShiftInput")
        const start = child("dcStartButton")
        const stop = child("dcStopButton")

        cycle.value = 4000
        shift.value = -100
        verify(start.enabled)
        verify(!stop.enabled)
        mouseClick(start)

        compare(controller.startCalls, 1)
        compare(controller.requestedCycleUs, 4000)
        compare(controller.requestedShiftUs, -100)
        verify(!cycle.enabled)
        verify(!shift.enabled)
        verify(!start.enabled)
        verify(!stop.enabled)

        session.sessionActive = true
        session.sessionMode = "DC测试"
        wait(0)
        verify(stop.enabled)
        mouseClick(stop)
        compare(controller.stopCalls, 1)
        verify(!stop.enabled)
    }

    function test_captureLightAndDarkLayoutsForVisualReview() {
        if (!captureEnabled)
            skip("Set DC_UI_CAPTURE_SCREENSHOTS=1 to write visual review artifacts")

        controller.hasResults = true
        controller.historical = true
        controller.diagnostics = diagnosticsWithHistory()
        controller.slavesModel = slavesForVisualReview()
        const capture = captureComponent.createObject(testCase)

        for (const mode of ["light", "dark"]) {
            for (const width of [628, 868]) {
                theme.themeMode = mode
                createPage(width, 620)
                const expectedRevision = capture.revision + 1
                capture.save(page, "dc-" + mode + "-" + width)
                tryCompare(capture, "revision", expectedRevision, 3000)
                verify(capture.lastSuccess, capture.lastPath)

                for (const address of [1, 2, 3])
                    verifySlaveDetailGeometry(address)

                const scroll = child("dcPageScroll")
                verify(scroll.contentHeight > scroll.availableHeight)
                scroll.contentItem.contentY = scroll.contentHeight - scroll.availableHeight
                wait(20)
                const detailsRevision = capture.revision + 1
                capture.save(page, "dc-" + mode + "-" + width + "-details")
                tryCompare(capture, "revision", detailsRevision, 3000)
                verify(capture.lastSuccess, capture.lastPath)
                page.destroy()
                page = null
            }
        }
        capture.destroy()
    }

    function test_historyKeepsExactCountersAndMarksHistoricalResults() {
        controller.hasResults = true
        controller.historical = true
        controller.diagnostics = diagnosticsWithHistory()
        createPage(868, 456)

        compare(child("dcHistoryBadge").visible, true)
        compare(child("dcTimeValue").text, "9223372036854775806")
        compare(child("dcCycleCountValue").text, "18446744073709551614")
        compare(child("dcWkcErrorCountValue").text, "9007199254740993")
        compare(child("dcMissedCyclesValue").text, "9007199254740995")
    }

    function test_runtimeAndCleanupFailuresRemainVisibleWithHistoricalResults() {
        const diagnostics = diagnosticsWithHistory()
        diagnostics.error = "Cyclic exchange failed"
        diagnostics.cleanupWarning = "SYNC0 shutdown verification failed"
        controller.hasResults = true
        controller.historical = true
        controller.state = "failed"
        controller.diagnostics = diagnostics
        createPage(868, 456)

        compare(child("dcErrorBanner").visible, true)
        verify(child("dcErrorBanner").text.indexOf("Cyclic exchange failed") >= 0)
        compare(child("dcCleanupBanner").visible, true)
        verify(child("dcCleanupBanner").text.indexOf("SYNC0 shutdown verification failed") >= 0)
    }

    function test_independentBusStatesAndInvalidDcSamplesAreExplicit() {
        const diagnostics = diagnosticsWithHistory()
        diagnostics.configured = true
        diagnostics.operational = true
        diagnostics.dcTimeValid = false
        diagnostics.phaseValid = false
        controller.hasResults = true
        controller.state = "running"
        controller.active = true
        controller.diagnostics = diagnostics
        createPage(628, 620)

        compare(child("dcConfiguredBadge").text, "配置回读成功")
        compare(child("dcConfiguredBadge").tone, "success")
        compare(child("dcOperationalBadge").text, "通信正常")
        compare(child("dcOperationalBadge").tone, "success")
        compare(child("dcTimeValue").text, "—")
        compare(child("dcPhaseErrorValue").text, "—")
        compare(child("dcCorrectionValue").text, "—")
        compare(child("dcActualCycleValue").text, "1999500")
    }

    function test_slaveToneShutdownPrecedenceAndCompleteAlDetail() {
        controller.hasResults = true
        controller.slavesModel = [
            {
                address: 1, name: "Non-DC IO", hasDc: false,
                sync0Requested: false, sync0Verified: false,
                shutdownAttempted: false, shutdownVerified: false,
                cycleNs: 0, shiftNs: 0, propagationDelayNs: 0,
                state: 8, alStatusCode: 0, alStatusText: "No error"
            },
            {
                address: 2, name: "Stopped DC", hasDc: true,
                sync0Requested: true, sync0Verified: true,
                shutdownAttempted: true, shutdownVerified: true,
                cycleNs: 2000000, shiftNs: 0, propagationDelayNs: 120,
                state: 2, alStatusCode: 0, alStatusText: "No error"
            },
            {
                address: 3, name: "Unconfirmed DC", hasDc: true,
                sync0Requested: true, sync0Verified: true,
                shutdownAttempted: true, shutdownVerified: false,
                cycleNs: 2000000, shiftNs: 0, propagationDelayNs: 130,
                state: 8, alStatusCode: 27,
                alStatusText: "Sync manager watchdog: distributed clock synchronization remained outside the configured tolerance"
            }
        ]
        createPage(628, 620)

        compare(child("dcSlaveStatus-1").text, "非 DC 从站")
        compare(child("dcSlaveStatus-1").tone, "neutral")
        compare(child("dcSlaveStatus-2").text, "SYNC0 已关闭")
        compare(child("dcSlaveStatus-2").tone, "success")
        compare(child("dcSlaveStatus-3").text, "关闭未确认")
        compare(child("dcSlaveStatus-3").tone, "warning")
        verify(child("dcSlaveAl-3").text.indexOf("0x0008") >= 0)
        verify(child("dcSlaveAl-3").text.indexOf("0x001B") >= 0)
        verify(child("dcSlaveAl-3").text.indexOf("Sync manager watchdog") >= 0)
        compare(child("dcSlaveAl-3").elide, Text.ElideNone)
        compare(child("dcSlaveAl-3").truncated, false)
    }

    function test_subtitleDescribesHostTimingAsSimpleOsDiagnostic() {
        createPage(628, 620)
        const subtitle = child("dcSubtitle").text
        verify(subtitle.indexOf("DC 状态") >= 0)
        verify(subtitle.indexOf("主机周期质量") >= 0)
        verify(subtitle.indexOf("普通操作系统") >= 0)
    }

    function test_emptyAndManySlavesRemainUsableAtMinimumContentSize() {
        createPage(868, 300)
        compare(child("dcEmptySlavesLabel").visible, true)
        compare(child("dcSlaveRepeater").count, 0)

        const slaves = []
        for (let index = 0; index < 24; ++index) {
            slaves.push({
                address: index + 1,
                name: "DC slave with a very long descriptive device name " + index,
                hasDc: index % 3 !== 0,
                sync0Requested: index % 3 !== 0,
                sync0Verified: index % 2 === 0,
                shutdownAttempted: false,
                shutdownVerified: false,
                cycleNs: 2000000,
                shiftNs: 0,
                propagationDelayNs: 100 + index,
                state: 8,
                alStatusCode: 0,
                alStatusText: ""
            })
        }
        controller.slavesModel = slaves
        wait(0)

        compare(child("dcSlaveRepeater").count, 24)
        verify(child("dcPageScroll").contentHeight > child("dcPageScroll").availableHeight)
        verify(child("dcPageScroll").contentWidth <= child("dcPageScroll").availableWidth + 1)
    }

    function test_compactAndWideLayoutsDoNotPushControlsOutsideTheirGrid() {
        for (const width of [628, 868, 1052, 1280]) {
            createPage(width, 456)
            const grid = child("dcControlGrid")
            const start = child("dcStartButton")
            const stop = child("dcStopButton")
            verify(start.x >= 0)
            verify(stop.x + stop.width <= stop.parent.width + 1)
            verify(grid.width <= grid.parent.width + 1)
            verify(child("dcPageScroll").contentWidth <= child("dcPageScroll").availableWidth + 1)
            page.destroy()
            page = null
        }
    }
}
