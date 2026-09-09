pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root

    property var theme
    property var sessionUi
    signal connectionChanged(bool connected)
    signal errorRequested(string message)

    readonly property bool isConnected: sessionUi ? sessionUi.testConnected : false
    readonly property bool sessionIdle: sessionUi ? sessionUi.idle : !EthercatBackend.sessionActive
    readonly property bool flashBusy: firmwareBusy || eepromBusy
                                      || (sessionUi ? sessionUi.flashingActive : false)
    readonly property string selectedNicName: sessionUi
                                               && sessionUi.selectedNicIndex >= 0
                                               && sessionUi.selectedNicIndex < sessionUi.nicList.length
                                               ? sessionUi.nicList[sessionUi.selectedNicIndex]
                                               : qsTr("未选择网卡")

    property url firmwareFile: ""
    property url eepromFile: ""
    property int firmwareProgress: 0
    property int eepromProgress: 0
    property bool firmwareBusy: false
    property bool eepromBusy: false
    property string firmwareResult: ""
    property string eepromResult: ""
    property string firmwareUiState: "idle"
    property string eepromUiState: "idle"
    property string pendingFlashType: ""
    property string pendingFlashFileName: ""
    property int pendingFlashSlave: 0
    property string awaitingFlashStart: ""

    onIsConnectedChanged: connectionChanged(isConnected)

    function displayFileName(fileUrl) {
        const path = fileUrl ? fileUrl.toString() : ""
        if (!path.length)
            return "未选择文件"
        const slash = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        return slash >= 0 ? path.substring(slash + 1) : path
    }

    function flashStateText(state) {
        if (state === "success")
            return "烧录成功"
        if (state === "error")
            return "烧录失败"
        if (state === "running")
            return "烧录进行中"
        return "等待任务"
    }

    function flashStateTone(state) {
        if (state === "success")
            return "success"
        if (state === "error")
            return "danger"
        if (state === "running")
            return "warning"
        return "neutral"
    }

    function requestFlash(type) {
        const file = type === "firmware" ? firmwareFile : eepromFile
        const slave = type === "firmware" ? firmwareSlaveIdBox.value : eepromSlaveIdBox.value
        if (!file || !file.toString().length) {
            errorRequested(type === "firmware" ? "请先选择固件 BIN 文件" : "请先选择 EEPROM HEX 文件")
            return
        }
        if (!sessionUi || !sessionUi.nicReady) {
            errorRequested("未发现可用网卡，请先刷新网卡列表")
            return
        }
        if (!sessionIdle || flashBusy) {
            errorRequested("总线当前正被其他任务占用")
            return
        }

        pendingFlashType = type
        pendingFlashFileName = displayFileName(file)
        pendingFlashSlave = slave
        flashConfirm.open()
    }

    function startConfirmedFlash() {
        const type = pendingFlashType
        if (type !== "firmware" && type !== "eeprom")
            return
        if (!sessionUi || !sessionUi.nicReady) {
            errorRequested("当前没有可用网卡")
            return
        }
        if (!sessionIdle || flashBusy) {
            errorRequested("总线当前正被其他任务占用")
            return
        }

        awaitingFlashStart = type
        flashStartGuard.restart()
        if (type === "firmware") {
            firmwareProgress = 0
            firmwareResult = "正在提交固件烧录任务…"
            firmwareBusy = true
            firmwareUiState = "running"
            EthercatBackend.flashFirmware(firmwareSlaveIdBox.value, firmwareFile)
        } else {
            eepromProgress = 0
            eepromResult = "正在提交 EEPROM 烧录任务…"
            eepromBusy = true
            eepromUiState = "running"
            EthercatBackend.flashEEprom(eepromSlaveIdBox.value, eepromFile)
        }
    }

    Timer {
        id: flashStartGuard
        interval: 2000
        repeat: false
        onTriggered: {
            if (root.sessionUi && root.sessionUi.flashingActive) {
                if (root.awaitingFlashStart === "firmware")
                    root.firmwareResult = "固件烧录任务已提交，等待设备响应…"
                else if (root.awaitingFlashStart === "eeprom")
                    root.eepromResult = "EEPROM 烧录任务已提交，等待设备响应…"
                root.awaitingFlashStart = ""
                return
            }
            if (root.awaitingFlashStart === "firmware") {
                root.firmwareBusy = false
                root.firmwareResult = "任务未启动，请查看错误提示后重试"
                root.firmwareUiState = "error"
            } else if (root.awaitingFlashStart === "eeprom") {
                root.eepromBusy = false
                root.eepromResult = "任务未启动，请查看错误提示后重试"
                root.eepromUiState = "error"
            }
            root.awaitingFlashStart = ""
        }
    }

    ScrollView {
        id: pageScroll
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            width: pageScroll.availableWidth
            spacing: 14

            PanelCard {
                Layout.fillWidth: true
                Layout.margins: 2
                theme: root.theme
                padding: 18

                ColumnLayout {
                    width: parent.width
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Label {
                                text: "EtherCAT 链路测试"
                                font.pixelSize: 20
                                font.bold: true
                                color: root.theme.textPrimary
                            }

                            Label {
                                Layout.fillWidth: true
                                text: "建立测试会话，观察工作计数器与过程数据交换是否稳定。"
                                color: root.theme.textSecondary
                                wrapMode: Text.WordWrap
                            }
                        }

                        StatusBadge {
                            theme: root.theme
                            text: root.sessionUi ? root.sessionUi.statusText : (root.isConnected ? "测试已连接" : "总线空闲")
                            tone: root.sessionUi ? root.sessionUi.statusTone : (root.isConnected ? "success" : "neutral")
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            text: root.isConnected
                                  ? "测试会话运行中；停止后才能执行烧录任务。"
                                  : (!root.sessionIdle ? "其他总线会话正在运行。" : "当前可启动链路测试或执行离线烧录。")
                            color: !root.sessionIdle && !root.isConnected ? root.theme.dangerText : root.theme.textMuted
                            wrapMode: Text.WordWrap
                        }

                        AppButton {
                            theme: root.theme
                            text: root.isConnected ? "停止测试" : "启动测试"
                            variant: root.isConnected ? "danger" : "primary"
                            actionEnabled: root.isConnected
                                           || (root.sessionIdle
                                               && !root.flashBusy
                                               && root.sessionUi
                                               && root.sessionUi.nicReady)
                            onClicked: {
                                if (root.isConnected)
                                    EthercatBackend.stopTest()
                                else
                                    EthercatBackend.startTest()
                            }
                        }
                    }
                }
            }

            PanelCard {
                Layout.fillWidth: true
                Layout.margins: 2
                theme: root.theme
                padding: 18

                ColumnLayout {
                    width: parent.width
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Label {
                                text: "从站烧录"
                                font.pixelSize: 18
                                font.bold: true
                                color: root.theme.textPrimary
                            }
                            Label {
                                Layout.fillWidth: true
                                text: "烧录会独占所选网卡。确认从站地址与文件后再开始。"
                                color: root.theme.textSecondary
                                wrapMode: Text.WordWrap
                            }
                        }

                        StatusBadge {
                            theme: root.theme
                            text: {
                                const selectedState = flashTabs.currentIndex === 0
                                                      ? root.firmwareUiState
                                                      : root.eepromUiState
                                return root.flashStateText(root.flashBusy && selectedState === "idle"
                                                           ? "running"
                                                           : selectedState)
                            }
                            tone: {
                                const selectedState = flashTabs.currentIndex === 0
                                                      ? root.firmwareUiState
                                                      : root.eepromUiState
                                return root.flashStateTone(root.flashBusy && selectedState === "idle"
                                                           ? "running"
                                                           : selectedState)
                            }
                            compact: true
                        }
                    }

                    TabBar {
                        id: flashTabs
                        Layout.fillWidth: true
                        enabled: !root.flashBusy

                        TabButton { text: "固件 BIN" }
                        TabButton { text: "EEPROM HEX" }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        currentIndex: flashTabs.currentIndex

                        GridLayout {
                            id: firmwareTaskGrid
                            Layout.fillWidth: true
                            columns: width >= 760 ? 2 : 1
                            columnSpacing: root.theme.space12
                            rowSpacing: root.theme.space12

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.preferredWidth: 420
                                Layout.alignment: Qt.AlignTop
                                spacing: root.theme.space8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: root.theme.space8

                                    Label {
                                        text: "从站地址"
                                        color: root.theme.textSecondary
                                    }
                                    SpinBox {
                                        id: firmwareSlaveIdBox
                                        Layout.preferredWidth: 132
                                        from: 1
                                        to: 255
                                        value: 1
                                        editable: true
                                        enabled: !root.flashBusy
                                    }
                                    Item { Layout.fillWidth: true }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: root.theme.space8

                                    TextField {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        readOnly: true
                                        text: root.displayFileName(root.firmwareFile)
                                        color: root.firmwareFile.toString().length
                                               ? root.theme.textPrimary
                                               : root.theme.textMuted
                                    }
                                    AppButton {
                                        theme: root.theme
                                        text: "选择 BIN"
                                        variant: "secondary"
                                        actionEnabled: !root.flashBusy
                                        onClicked: firmwareFileDialog.open()
                                    }
                                    AppButton {
                                        theme: root.theme
                                        text: "烧录固件"
                                        busy: root.firmwareBusy
                                        busyText: "固件烧录中"
                                        variant: "primary"
                                        actionEnabled: root.sessionIdle
                                                       && !root.flashBusy
                                                       && root.sessionUi
                                                       && root.sessionUi.nicReady
                                        onClicked: root.requestFlash("firmware")
                                    }
                                }
                            }

                            FlashProgressView {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.preferredWidth: 320
                                Layout.alignment: Qt.AlignTop
                                theme: root.theme
                                taskName: "固件"
                                progress: root.firmwareProgress
                                busy: root.firmwareBusy
                                state: root.firmwareUiState
                                message: root.firmwareResult
                            }
                        }

                        GridLayout {
                            id: eepromTaskGrid
                            Layout.fillWidth: true
                            columns: width >= 760 ? 2 : 1
                            columnSpacing: root.theme.space12
                            rowSpacing: root.theme.space12

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.preferredWidth: 420
                                Layout.alignment: Qt.AlignTop
                                spacing: root.theme.space8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: root.theme.space8

                                    Label {
                                        text: "从站地址"
                                        color: root.theme.textSecondary
                                    }
                                    SpinBox {
                                        id: eepromSlaveIdBox
                                        Layout.preferredWidth: 132
                                        from: 1
                                        to: 255
                                        value: 1
                                        editable: true
                                        enabled: !root.flashBusy
                                    }
                                    Item { Layout.fillWidth: true }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: root.theme.space8

                                    TextField {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        readOnly: true
                                        text: root.displayFileName(root.eepromFile)
                                        color: root.eepromFile.toString().length
                                               ? root.theme.textPrimary
                                               : root.theme.textMuted
                                    }
                                    AppButton {
                                        theme: root.theme
                                        text: "选择 HEX"
                                        variant: "secondary"
                                        actionEnabled: !root.flashBusy
                                        onClicked: eepromFileDialog.open()
                                    }
                                    AppButton {
                                        theme: root.theme
                                        text: "烧录 EEPROM"
                                        busy: root.eepromBusy
                                        busyText: "EEPROM 烧录中"
                                        variant: "primary"
                                        actionEnabled: root.sessionIdle
                                                       && !root.flashBusy
                                                       && root.sessionUi
                                                       && root.sessionUi.nicReady
                                        onClicked: root.requestFlash("eeprom")
                                    }
                                }
                            }

                            FlashProgressView {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.preferredWidth: 320
                                Layout.alignment: Qt.AlignTop
                                theme: root.theme
                                taskName: "EEPROM"
                                progress: root.eepromProgress
                                busy: root.eepromBusy
                                state: root.eepromUiState
                                message: root.eepromResult
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 2 }
        }
    }

    ConfirmDialog {
        id: flashConfirm
        theme: root.theme
        titleText: root.pendingFlashType === "firmware" ? "确认固件烧录" : "确认 EEPROM 烧录"
        message: qsTr("网卡：%1\n从站地址：%2\n文件：%3\n\n烧录期间请勿断电、拔线或切换网卡。")
                 .arg(root.selectedNicName)
                 .arg(root.pendingFlashSlave)
                 .arg(root.pendingFlashFileName)
        confirmText: "确认烧录"
        tone: "danger"
        onConfirmed: root.startConfirmedFlash()
    }

    FileDialog {
        id: firmwareFileDialog
        title: "选择固件 BIN 文件"
        nameFilters: ["BIN files (*.bin)"]
        onAccepted: root.firmwareFile = selectedFile
    }

    FileDialog {
        id: eepromFileDialog
        title: "选择 EEPROM HEX 文件"
        nameFilters: ["HEX files (*.hex)"]
        onAccepted: root.eepromFile = selectedFile
    }

    Connections {
        target: EthercatBackend

        function onFlashProgress(type, percent) {
            if (root.awaitingFlashStart === type) {
                root.awaitingFlashStart = ""
                flashStartGuard.stop()
            }
            if (type === "firmware") {
                root.firmwareBusy = true
                root.firmwareProgress = percent
                root.firmwareResult = "固件烧录进行中"
                root.firmwareUiState = "running"
            } else if (type === "eeprom") {
                root.eepromBusy = true
                root.eepromProgress = percent
                root.eepromResult = "EEPROM 烧录进行中"
                root.eepromUiState = "running"
            }
        }

        function onFlashFinished(type, success, msg) {
            if (root.awaitingFlashStart === type) {
                root.awaitingFlashStart = ""
                flashStartGuard.stop()
            }
            if (type === "firmware") {
                root.firmwareBusy = false
                root.firmwareProgress = success ? 100 : 0
                root.firmwareResult = success ? "固件烧录成功：" + msg : "固件烧录失败：" + msg
                root.firmwareUiState = success ? "success" : "error"
            } else if (type === "eeprom") {
                root.eepromBusy = false
                root.eepromProgress = success ? 100 : 0
                root.eepromResult = success ? "EEPROM 烧录成功：" + msg : "EEPROM 烧录失败：" + msg
                root.eepromUiState = success ? "success" : "error"
            }
        }
    }
}
