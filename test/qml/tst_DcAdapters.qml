import QtQuick
import QtTest
import "../../qml/adapters" as Adapters

TestCase {
    id: testCase
    name: "DcAdapters"

    Component { id: backendComponent; FakeBackend {} }
    Component { id: sessionComponent; Adapters.SessionUiAdapter {} }
    Component { id: logComponent; Adapters.LogUiAdapter {} }

    function test_dcSessionUsesStablePageKey() {
        const backend = createTemporaryObject(backendComponent, testCase)
        const session = createTemporaryObject(sessionComponent, testCase, { backend: backend })

        backend.connected = true
        backend.sessionActive = true
        backend.sessionMode = "DC测试"

        compare(session.modeKey, "dc")
        compare(session.dcConnected, true)
    }

    function test_dcLogNeverLeaksIntoSelectedUnrelatedPage() {
        const backend = createTemporaryObject(backendComponent, testCase)
        const log = createTemporaryObject(logComponent, testCase, {
            backend: backend,
            contextKey: "debug",
            sessionActive: false,
            sessionMode: "空闲"
        })

        backend.dcLogAppended("DC worker stopped")
        compare(log.currentText, "")

        log.contextKey = "dc"
        compare(log.currentText, "DC worker stopped")
    }

    function test_releasedDcErrorStaysInDcLogWhileDebugIsSelected() {
        const backend = createTemporaryObject(backendComponent, testCase)
        const log = createTemporaryObject(logComponent, testCase, {
            backend: backend,
            contextKey: "debug",
            sessionActive: false,
            sessionMode: "空闲"
        })

        backend.emitReleasedDcFailure("DC cleanup failed")
        compare(log.currentText, "")

        log.contextKey = "dc"
        compare(log.currentText, "DC cleanup failed")
    }

}
