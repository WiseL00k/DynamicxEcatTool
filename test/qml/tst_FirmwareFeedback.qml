pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml/components" as Components
import "../../qml/dialogs" as Dialogs
import "../../qml/theme" as Theme

TestCase {
    id: tests
    name: "FirmwareFeedback"
    when: windowShown
    width: 1000
    height: 800
    visible: true
    property var preview
    readonly property string sample: "固件下载失败 [FW010]\n从站：1 · 阶段：FoE 数据传输\n\n失败原因\n从站返回 FoE ERROR\n从站说明：从站通用更新错误；需结合升级包校验或从站日志定位\n\n错误码\nSOEM 返回值：-5\nFoE 错误码：0x00008000\n\n从站状态\nAL 状态：BOOT（0x0003）\nAL 错误码：0x0000（无错误）\n\n处理说明\n未发送退出 BOOT 请求，便于保留失败现场。\n\n耗时：128 ms"

    Component {
        id: windowComponent
        ApplicationWindow {
            id: window
            width: 900
            height: 760
            visible: true
            color: theme.pageBackground
            property alias popup: popup
            property alias card: card
            property alias theme: theme
            Theme.DesignTokens { id: theme; themeMode: "light"; fontFamily: "Sans Serif" }
            Components.FlashProgressView {
                id: card
                x: 24; y: 24; width: 420
                theme: window.theme
                taskName: "固件"
                message: tests.sample
                state: "error"
            }
            Dialogs.ErrorPopup { id: popup; theme: window.theme }
        }
    }
    function init() {
        preview = createTemporaryObject(windowComponent, tests)
        verify(preview !== null)
        waitForRendering(preview.contentItem)
    }
    function cleanup() {
        if (preview) preview.close()
        preview = null
    }
    function capture(name) {
        if (!DcUiCaptureEnabled) return
        wait(preview.theme.animationNormal + 30)
        verify(UiTestCapture.captureWindow(preview, name + "-" + DcUiCaptureScale))
    }
    function test_errorDetailsRemainReadableAndCanBeCollapsed() {
        tryCompare(preview.card, "detailsExpanded", true)
        const details = findChild(preview.card, "flashMessageDetails")
        verify(details !== null)
        compare(details.text, sample)
        verify(details.visible)
        compare(details.textFormat, TextEdit.PlainText)
        const toggle = findChild(preview.card, "flashDetailsToggle")
        mouseClick(toggle)
        compare(preview.card.detailsExpanded, false)
        mouseClick(toggle)
        compare(preview.card.detailsExpanded, true)
        verify(findChild(preview.card, "flashCopyResult").enabled)
        capture("firmware-result-card")
    }
    function test_popupFitsWindowAndPreservesTheFullDiagnostic() {
        preview.popup.show(sample)
        tryCompare(preview.popup, "visible", true)
        const message = findChild(preview.popup, "errorMessageText")
        compare(message.text, sample)
        compare(message.textFormat, TextEdit.PlainText)
        verify(message.readOnly)
        verify(preview.popup.width <= preview.width - 48)
        verify(preview.popup.height <= preview.height - 48)
        verify(findChild(preview.popup, "copyErrorMessageButton").enabled)
        capture("firmware-error-dialog")
        preview.width = 480
        preview.height = 520
        waitForRendering(preview.contentItem)
        verify(preview.popup.width <= preview.width - 48)
        verify(preview.popup.height <= preview.height - 48)
        verify(message.contentWidth <= message.width + 2)
        capture("firmware-error-dialog-small")
    }
    function test_darkPopup() {
        preview.theme.themeMode = "dark"
        preview.popup.show(sample)
        tryCompare(preview.popup, "visible", true)
        capture("firmware-error-dialog-dark")
    }
}
