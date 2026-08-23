import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Item {
    id: root

    property bool isConnected: false
    property var theme
    property var mitCanFrame: [127, 255, 127, 240, 0, 0, 7, 255]

    signal errorRequested(string message)

    implicitHeight: contentLayout.implicitHeight

    component NumericField: ColumnLayout {
        id: numericRoot

        property var fieldTheme
        property string labelText
        property string unitText
        property alias text: editor.text
        property string placeholderText

        spacing: 5

        Label {
            Layout.fillWidth: true
            text: labelText
            color: fieldTheme.textSecondary
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            TextField {
                id: editor
                Layout.fillWidth: true
                implicitHeight: 36
                placeholderText: numericRoot.placeholderText
                color: fieldTheme.textPrimary
                selectByMouse: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: RegularExpressionValidator {
                    regularExpression: /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/
                }
            }

            Label {
                visible: unitText.length > 0
                text: unitText
                color: fieldTheme.textMuted
                font.pixelSize: 11
            }
        }
    }

    function showError(message) {
        errorRequested(message)
    }

    function parseMitFrame(value) {
        const normalized = value.trim()
        if (normalized.length === 0)
            return null

        const parts = normalized.split(/\s+/)
        if (parts.length !== 8)
            return null

        const bytes = []
        for (let i = 0; i < parts.length; ++i) {
            if (!/^[0-9A-Fa-f]{2}$/.test(parts[i]))
                return null
            bytes.push(parseInt(parts[i], 16))
        }
        return bytes
    }

    function formatMit(bytes) {
        if (!bytes || bytes.length !== 8)
            return "-- -- -- -- -- -- -- --"

        const output = []
        for (let i = 0; i < bytes.length; ++i) {
            let value = Number(bytes[i]).toString(16).toUpperCase()
            if (value.length < 2)
                value = "0" + value
            output.push(value)
        }
        return output.join(" ")
    }

    function normalizeFrameText(value) {
        const raw = value.toUpperCase().replace(/[^0-9A-F]/g, "").slice(0, 16)
        const parts = []
        for (let index = 0; index < raw.length; index += 2)
            parts.push(raw.substring(index, Math.min(index + 2, raw.length)))
        return parts.join(" ")
    }

    function selectedCanId() {
        const id = canIdField.value
        if (id < 1 || id > 9) {
            showError(qsTr("CAN ID 必须在 1～9 之间"))
            return -1
        }
        return id
    }

    function buildControlFrame() {
        const numberPattern = /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/
        function strictNumber(value) {
            const normalized = String(value).trim()
            return numberPattern.test(normalized) ? Number(normalized) : NaN
        }

        const input = {
            pos: strictNumber(positionField.text),
            vel: strictNumber(velocityField.text),
            kp: strictNumber(kpField.text),
            kd: strictNumber(kdField.text),
            torque: strictNumber(torqueField.text),
            PMAX: strictNumber(pmaxField.text),
            VMAX: strictNumber(vmaxField.text),
            TMAX: strictNumber(tmaxField.text)
        }

        if (!isFinite(input.pos) || !isFinite(input.vel)
                || !isFinite(input.kp) || !isFinite(input.kd)
                || !isFinite(input.torque) || !isFinite(input.PMAX)
                || !isFinite(input.VMAX) || !isFinite(input.TMAX)) {
            showError(qsTr("参数中存在无效数字"))
            return
        }
        if (input.PMAX <= 0 || input.VMAX <= 0 || input.TMAX <= 0) {
            showError(qsTr("PMAX、VMAX 和 TMAX 必须大于 0"))
            return
        }
        if (input.kp < 0 || input.kp > 500) {
            showError(qsTr("Kp 超出允许范围：0～500"))
            return
        }
        if (input.kd < 0 || input.kd > 5) {
            showError(qsTr("Kd 超出允许范围：0～5"))
            return
        }
        if (Math.abs(input.pos) > input.PMAX) {
            showError(qsTr("目标位置超出 ±PMAX"))
            return
        }
        if (Math.abs(input.vel) > input.VMAX) {
            showError(qsTr("目标速度超出 ±VMAX"))
            return
        }
        if (Math.abs(input.torque) > input.TMAX) {
            showError(qsTr("转矩超出 ±TMAX"))
            return
        }

        controlFrameField.text = formatMit(MitMotorCommandQml.buildMitFrame(input))
    }

    function sendFrame(frameText) {
        const data = parseMitFrame(frameText)
        if (!data) {
            showError(qsTr("数据帧必须由 8 个两位 HEX 字节组成"))
            return
        }

        const id = selectedCanId()
        if (id < 0)
            return

        EthercatBackend.sendMitFrameQml(canBus.currentIndex, id, data)
    }

    ColumnLayout {
        id: contentLayout
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 12

        PanelCard {
            Layout.fillWidth: true
            theme: root.theme
            padding: 14
            backgroundColor: theme.surface

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: qsTr("控制参数")
                            color: theme.textPrimary
                            font.pixelSize: 16
                            font.bold: true
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("先生成控制帧，确认预览后再发送到目标 CAN 槽位。")
                            color: theme.textSecondary
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }

                    AppButton {
                        theme: root.theme
                        text: qsTr("更新帧预览")
                        variant: "primary"
                        onClicked: root.buildControlFrame()
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width >= 1000 ? 4 : root.width >= 620 ? 2 : 1
                    columnSpacing: 10
                    rowSpacing: 10

                    PanelCard {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        theme: root.theme
                        padding: 12
                        backgroundColor: theme.surfaceMuted

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 8

                            Label {
                                text: qsTr("路由")
                                color: theme.textPrimary
                                font.bold: true
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 5

                                    Label {
                                        text: qsTr("CAN 通道")
                                        color: theme.textSecondary
                                        font.pixelSize: 12
                                    }

                                    ComboBox {
                                        id: canBus
                                        Layout.fillWidth: true
                                        implicitHeight: 36
                                        model: ["CAN0", "CAN1"]
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 5

                                    Label {
                                        text: qsTr("CAN ID")
                                        color: theme.textSecondary
                                        font.pixelSize: 12
                                    }

                                    SpinBox {
                                        id: canIdField
                                        Layout.fillWidth: true
                                        implicitHeight: 36
                                        from: 1
                                        to: 9
                                        value: 1
                                        editable: true
                                    }
                                }
                            }
                        }
                    }

                    PanelCard {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        theme: root.theme
                        padding: 12
                        backgroundColor: theme.surfaceMuted

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 8

                            Label {
                                text: qsTr("目标值")
                                color: theme.textPrimary
                                font.bold: true
                            }

                            NumericField {
                                id: positionField
                                Layout.fillWidth: true
                                fieldTheme: root.theme
                                labelText: qsTr("目标位置")
                                unitText: "rad"
                                text: "0.0"
                            }

                            NumericField {
                                id: velocityField
                                Layout.fillWidth: true
                                fieldTheme: root.theme
                                labelText: qsTr("目标速度")
                                unitText: "rad/s"
                                text: "0.0"
                            }

                            NumericField {
                                id: torqueField
                                Layout.fillWidth: true
                                fieldTheme: root.theme
                                labelText: qsTr("转矩")
                                unitText: "N·m"
                                text: "0.0"
                            }
                        }
                    }

                    PanelCard {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        theme: root.theme
                        padding: 12
                        backgroundColor: theme.surfaceMuted

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 8

                            Label {
                                text: qsTr("控制增益")
                                color: theme.textPrimary
                                font.bold: true
                            }

                            NumericField {
                                id: kpField
                                Layout.fillWidth: true
                                fieldTheme: root.theme
                                labelText: "Kp"
                                text: "0.0"
                            }

                            NumericField {
                                id: kdField
                                Layout.fillWidth: true
                                fieldTheme: root.theme
                                labelText: "Kd"
                                text: "0.0"
                            }
                        }
                    }

                    PanelCard {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        theme: root.theme
                        padding: 12
                        backgroundColor: theme.surfaceMuted

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 8

                            Label {
                                text: qsTr("电机范围")
                                color: theme.textPrimary
                                font.bold: true
                            }

                            NumericField {
                                id: pmaxField
                                Layout.fillWidth: true
                                fieldTheme: root.theme
                                labelText: "PMAX"
                                unitText: "rad"
                                text: "12.5"
                            }

                            NumericField {
                                id: vmaxField
                                Layout.fillWidth: true
                                fieldTheme: root.theme
                                labelText: "VMAX"
                                unitText: "rad/s"
                                text: "30"
                            }

                            NumericField {
                                id: tmaxField
                                Layout.fillWidth: true
                                fieldTheme: root.theme
                                labelText: "TMAX"
                                unitText: "N·m"
                                text: "10"
                            }
                        }
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.width >= 780 ? 2 : 1
            columnSpacing: 12
            rowSpacing: 12

            PanelCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                theme: root.theme
                padding: 14

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Label {
                        text: qsTr("原始 MIT 帧")
                        color: theme.textPrimary
                        font.pixelSize: 16
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("直接编辑并发送 8 字节十六进制数据。")
                        color: theme.textSecondary
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    TextField {
                        id: rawFrameField
                        Layout.fillWidth: true
                        implicitHeight: 40
                        placeholderText: "7F FF 7F F0 00 00 07 FF"
                        font.family: theme.fontFamily
                        font.pixelSize: 14
                        selectByMouse: true
                        onTextEdited: {
                            const normalized = root.normalizeFrameText(text)
                            if (normalized !== text) {
                                text = normalized
                                cursorPosition = text.length
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        AppButton {
                            Layout.fillWidth: true
                            theme: root.theme
                            text: qsTr("发送原始帧")
                            variant: "primary"
                            actionEnabled: root.isConnected
                            onClicked: root.sendFrame(rawFrameField.text)
                        }

                        AppButton {
                            theme: root.theme
                            text: qsTr("清除")
                            variant: "ghost"
                            actionEnabled: root.isConnected
                            onClicked: {
                                const id = root.selectedCanId()
                                if (id < 0)
                                    return
                                EthercatBackend.clearMitFrameQml(canBus.currentIndex, id)
                                rawFrameField.clear()
                            }
                        }
                    }
                }
            }

            PanelCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                theme: root.theme
                padding: 14

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("电机控制")
                            color: theme.textPrimary
                            font.pixelSize: 16
                            font.bold: true
                        }

                        StatusBadge {
                            theme: root.theme
                            text: root.isConnected ? qsTr("可发送") : qsTr("等待连接")
                            tone: root.isConnected ? "success" : "neutral"
                            compact: true
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 72
                        radius: 7
                        color: theme.surfaceMuted
                        border.color: theme.borderStrong

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 4

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("控制帧预览")
                                horizontalAlignment: Text.AlignHCenter
                                color: theme.textSecondary
                                font.pixelSize: 11
                            }

                            TextField {
                                id: controlFrameField
                                Layout.fillWidth: true
                                readOnly: true
                                text: root.formatMit(root.mitCanFrame)
                                horizontalAlignment: Text.AlignHCenter
                                color: theme.accent
                                font.family: theme.fontFamily
                                font.pixelSize: 15
                                font.bold: true
                                background: null
                                selectByMouse: true
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 8

                        AppButton {
                            Layout.fillWidth: true
                            theme: root.theme
                            text: qsTr("使能全部电机")
                            variant: "danger"
                            actionEnabled: root.isConnected
                            onClicked: enableConfirm.open()
                        }

                        AppButton {
                            Layout.fillWidth: true
                            theme: root.theme
                            text: qsTr("立即失能")
                            variant: "secondary"
                            actionEnabled: root.isConnected
                            onClicked: EthercatBackend.disableMitSlaveMotors()
                        }

                        AppButton {
                            Layout.fillWidth: true
                            theme: root.theme
                            text: qsTr("发送控制帧")
                            variant: "primary"
                            actionEnabled: root.isConnected
                            onClicked: root.sendFrame(controlFrameField.text)
                        }

                        AppButton {
                            Layout.fillWidth: true
                            theme: root.theme
                            text: qsTr("停止当前槽位")
                            variant: "ghost"
                            actionEnabled: root.isConnected
                            onClicked: {
                                const id = root.selectedCanId()
                                if (id < 0)
                                    return
                                EthercatBackend.clearMitFrameQml(canBus.currentIndex, id)
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("失能不会清空已写入的 CAN 槽位；重新使能前请检查或停止相关槽位。")
                        color: theme.warningText
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    ConfirmDialog {
        id: enableConfirm
        theme: root.theme
        titleText: qsTr("确认使能全部电机")
        message: qsTr("该操作会使能当前 MIT 从站上的全部电机。此前写入各 CAN 槽位的控制帧可能继续发送；请确认槽位命令安全、机械结构处于安全状态，人员已远离运动范围。")
        confirmText: qsTr("确认使能")
        tone: "danger"
        onConfirmed: EthercatBackend.enableMitSlaveMotors()
    }
}
