pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../dialogs"

ColumnLayout {
    id: root
    property var theme
    property var controller
    property int cycleUs: 2000
    property bool rawOpen: false
    readonly property bool canfd: controller && controller.slaveType === "ecat2canfd"
    readonly property bool busy: controller && controller.busy
    readonly property bool targetLocked: canfd && controller && controller.targetLocked
    readonly property var selectedChannelConfiguration: {
        const configs = controller && controller.canfd ? controller.canfd.channelConfigs : []
        for (const config of configs)
            if (Number(config.slave) === 1 && Number(config.channel) === canChannel.currentIndex)
                return config
        return null
    }
    readonly property bool channelReady: !canfd || (selectedChannelConfiguration !== null
        && selectedChannelConfiguration.verified === true && selectedChannelConfiguration.enabled
        && Number(selectedChannelConfiguration.status) === 0 && Number(selectedChannelConfiguration.error) === 0)
    readonly property string channelStatusText: {
        if (!canfd || channelReady) return ""
        const config = selectedChannelConfiguration
        if (!config || config.verified !== true) return qsTr("当前 CAN 通道尚未完成配置回读，请在总线 / CAN 配置中读取并确认。")
        if (!config.enabled) return qsTr("当前 CAN 通道未启用，请先应用通道配置。")
        if (Number(config.error) !== 0) return qsTr("当前 CAN 通道配置报错，请检查总线 / CAN 配置中的错误信息。")
        return qsTr("当前 CAN 通道配置尚未生效，请等待配置完成或重新读取确认。")
    }
    readonly property bool canTransmit: controller && controller.running && !busy && !targetLocked && channelReady
    readonly property bool idValid: canfd
        ? /^(0[xX])?[0-9A-Fa-f]{1,3}$/.test(motorId.text.trim()) && parseInt(motorId.text.trim().replace(/^0x/i, ""), 16) <= 0x7ff
        : /^[1-9]$/.test(motorId.text.trim())
    readonly property bool outsideExitDisableRange: canfd && idValid
        && (parseInt(motorId.text.trim().replace(/^0x/i, ""), 16) === 0
            || parseInt(motorId.text.trim().replace(/^0x/i, ""), 16) > 9)
    readonly property var parameters: ({pos: number(position.text), vel: number(velocity.text), torque: number(torque.text),
        kp: number(kp.text), kd: number(kd.text), PMAX: number(pmax.text), VMAX: number(vmax.text), TMAX: number(tmax.text)})
    readonly property var framePreview: controller ? controller.preview(parameters) : ({valid: false, error: qsTr("控制器不可用"), hex: ""})
    readonly property bool rawValid: /^(?:[0-9A-Fa-f]{2}\s+){7}[0-9A-Fa-f]{2}$/.test(rawInput.text.trim())
    readonly property int actualIntervalUs: controller && controller.periodicSending
        ? controller.periodicIntervalUs : Math.ceil(interval.value / cycleUs) * cycleUs
    spacing: theme.space8

    function number(text) {
        const value = String(text).trim()
        return /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(value) ? Number(value) : NaN
    }

    component NumericField: ColumnLayout {
        id: field
        required property var fieldTheme
        required property string fieldName
        required property string labelText
        property string unitText: ""
        property string rangeText: ""
        property real minimumValue: -Infinity
        property real maximumValue: Infinity
        property bool positiveOnly: false
        property alias text: editor.text
        readonly property real numericValue: root.number(editor.text)
        readonly property bool valid: isFinite(numericValue) && numericValue >= minimumValue
            && numericValue <= maximumValue && (!positiveOnly || numericValue > 0)
        spacing: fieldTheme.space4
        Label { text: field.labelText; color: field.fieldTheme.textSecondary; font.pixelSize: field.fieldTheme.fontBodySmall }
        AppTextField {
            theme: field.fieldTheme
            invalid: !field.valid
            id: editor
            objectName: field.fieldName
            Layout.fillWidth: true; Layout.minimumWidth: 80
            implicitHeight: field.fieldTheme.controlHeight
            selectByMouse: true; font.family: "Consolas"; font.pixelSize: field.fieldTheme.fontBody
            color: field.fieldTheme.textPrimary
            leftPadding: field.fieldTheme.space10
            rightPadding: unit.implicitWidth + field.fieldTheme.space16
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            validator: RegularExpressionValidator { regularExpression: /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/ }
            Label {
                id: unit
                anchors.right: parent.right; anchors.rightMargin: field.fieldTheme.space8
                anchors.verticalCenter: parent.verticalCenter
                text: field.unitText; color: field.fieldTheme.textMuted; font.pixelSize: field.fieldTheme.fontCaption
            }
            ToolTip.visible: hovered && field.rangeText.length > 0
            ToolTip.text: field.rangeText
        }
    }

    PanelCard {
        Layout.fillWidth: true; theme: root.theme; padding: root.theme.space12
        ColumnLayout {
            width: parent.width; spacing: root.theme.space10
            Flow {
                objectName: "dmTargetRoute"
                Layout.fillWidth: true; spacing: root.theme.space8
                Row {
                    spacing: root.theme.space8
                    Label { height: root.theme.controlHeight; verticalAlignment: Text.AlignVCenter; text: qsTr("目标电机"); font.bold: true; color: root.theme.textPrimary }
                    AppComboBox {
                        theme: root.theme
                        id: canChannel; objectName: "dmCanChannel"
                        width: 110; implicitHeight: root.theme.controlHeight
                        model: ["CAN 0", "CAN 1"]; enabled: !root.targetLocked && !root.busy
                    }
                }
                Row {
                    spacing: root.theme.space8
                    Label { height: root.theme.controlHeight; verticalAlignment: Text.AlignVCenter; text: root.canfd ? "ID (HEX)" : "ID"; color: root.theme.textSecondary }
                    AppTextField {
                        theme: root.theme
                        id: motorId; objectName: "dmMotorId"
                        width: 112; implicitHeight: root.theme.controlHeight
                        text: "1"; maximumLength: root.canfd ? 5 : 1; selectByMouse: true
                        enabled: !root.targetLocked && !root.busy
                        font.family: "Consolas"; color: root.theme.textPrimary
                        invalid: !root.idValid
                    }
                }
                Label { height: root.theme.controlHeight; verticalAlignment: Text.AlignVCenter; text: root.canfd ? qsTr("000–7FF · 11 位 ID") : qsTr("1–9 · 固定槽位"); color: root.theme.textMuted; font.pixelSize: root.theme.fontBodySmall }
                StatusBadge {
                    objectName: "dmChannelStatus"
                    theme: root.theme; height: root.theme.controlHeight
                    text: root.targetLocked ? qsTr("发送中 · 目标已锁定") : root.busy ? qsTr("操作处理中")
                        : root.controller && root.controller.running ? (root.channelReady ? qsTr("可控制") : qsTr("通道未就绪")) : qsTr("等待运行")
                    tone: root.targetLocked || root.busy ? "warning" : root.controller && root.controller.running ? (root.channelReady ? "success" : "warning") : "neutral"
                }
            }
            Label {
                objectName: "dmChannelMessage"; Layout.fillWidth: true
                visible: root.canfd && root.controller && root.controller.running && !root.channelReady
                text: root.channelStatusText; wrapMode: Text.Wrap
                color: root.theme.warningText; font.pixelSize: root.theme.fontBodySmall
            }
            GridLayout {
                objectName: "dmTargetsGrid"; Layout.fillWidth: true
                columns: width >= 720 ? 5 : width >= 450 ? 3 : 2
                columnSpacing: root.theme.space8; rowSpacing: root.theme.space8
                NumericField { id: position; Layout.fillWidth: true; fieldTheme: root.theme; fieldName: "dmPosition"; labelText: qsTr("目标位置"); unitText: "rad"; text: "0.0"; minimumValue: -root.number(pmax.text); maximumValue: root.number(pmax.text); rangeText: "±PMAX" }
                NumericField { id: velocity; Layout.fillWidth: true; fieldTheme: root.theme; fieldName: "dmVelocity"; labelText: qsTr("目标速度"); unitText: "rad/s"; text: "0.0"; minimumValue: -root.number(vmax.text); maximumValue: root.number(vmax.text); rangeText: "±VMAX" }
                NumericField { id: torque; Layout.fillWidth: true; fieldTheme: root.theme; fieldName: "dmTorque"; labelText: qsTr("前馈转矩"); unitText: "N·m"; text: "0.0"; minimumValue: -root.number(tmax.text); maximumValue: root.number(tmax.text); rangeText: "±TMAX" }
                NumericField { id: kp; Layout.fillWidth: true; fieldTheme: root.theme; fieldName: "dmKp"; labelText: "Kp"; text: "0.0"; minimumValue: 0; maximumValue: 500; rangeText: "0–500" }
                NumericField { id: kd; Layout.fillWidth: true; fieldTheme: root.theme; fieldName: "dmKd"; labelText: "Kd"; text: "0.0"; minimumValue: 0; maximumValue: 5; rangeText: "0–5" }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: root.theme.divider }
            GridLayout {
                id: rangesGrid; objectName: "dmRangesGrid"; Layout.fillWidth: true
                columns: width >= 560 ? 4 : 3
                columnSpacing: root.theme.space8; rowSpacing: root.theme.space6
                ColumnLayout {
                    Layout.fillWidth: true; visible: rangesGrid.columns === 4; spacing: 2
                    Label { text: qsTr("电机协议范围"); color: root.theme.textSecondary; font.bold: true }
                    Label { text: qsTr("需与电机设置一致"); color: root.theme.textMuted; font.pixelSize: root.theme.fontCaption }
                }
                NumericField { id: pmax; Layout.fillWidth: true; fieldTheme: root.theme; fieldName: "dmPmax"; labelText: "PMAX"; unitText: "rad"; text: "12.5"; positiveOnly: true }
                NumericField { id: vmax; Layout.fillWidth: true; fieldTheme: root.theme; fieldName: "dmVmax"; labelText: "VMAX"; unitText: "rad/s"; text: "30"; positiveOnly: true }
                NumericField { id: tmax; Layout.fillWidth: true; fieldTheme: root.theme; fieldName: "dmTmax"; labelText: "TMAX"; unitText: "N·m"; text: "10"; positiveOnly: true }
            }
            Rectangle {
                id: previewCard
                objectName: "dmFramePreviewCard"
                Layout.fillWidth: true; implicitHeight: 44; radius: root.theme.radiusSmall
                color: root.theme.surfaceMuted; border.color: root.theme.border
                RowLayout {
                    anchors.fill: previewCard; anchors.margins: root.theme.space8; spacing: root.theme.space12
                    Label { text: qsTr("8 B 控制帧"); color: root.theme.textSecondary; font.pixelSize: root.theme.fontBodySmall }
                    TextField {
                        objectName: "dmFramePreview"; Layout.fillWidth: true
                        text: root.framePreview.valid ? root.framePreview.hex : "-- -- -- -- -- -- -- --"
                        font.family: "Consolas"; font.pixelSize: 15; color: root.theme.accentText
                        readOnly: true; selectByMouse: true; background: null; padding: 0
                    }
                    Label { text: qsTr("预览不发送"); color: root.theme.textMuted; font.pixelSize: root.theme.fontCaption }
                }
            }
            Label {
                objectName: "dmValidationMessage"; Layout.fillWidth: true
                visible: !root.idValid || !root.framePreview.valid
                text: !root.idValid ? (root.canfd ? qsTr("CAN ID 请输入 000–7FF 的十六进制值") : qsTr("CAN ID 请输入 1–9")) : root.framePreview.error || qsTr("请检查控制参数")
                wrapMode: Text.Wrap; color: root.theme.dangerText; font.pixelSize: root.theme.fontBodySmall
            }
            Flow {
                Layout.fillWidth: true; spacing: root.theme.space8
                Switch { id: periodic; objectName: "dmPeriodicSwitch"; visible: root.canfd; text: qsTr("周期发送"); checked: false; enabled: !root.targetLocked && !root.busy }
                AppSpinBox { theme: root.theme; id: interval; objectName: "dmIntervalInput"; visible: root.canfd && periodic.checked; from: 1; to: 1000000000; value: 2000; stepSize: root.cycleUs; editable: true; implicitWidth: 150; implicitHeight: root.theme.controlHeight; enabled: !root.targetLocked && !root.busy }
                Label { visible: root.canfd && periodic.checked; height: root.theme.controlHeight; verticalAlignment: Text.AlignVCenter; text: qsTr("μs · 实际 %1 μs").arg(root.actualIntervalUs); color: root.theme.textMuted; font.pixelSize: root.theme.fontBodySmall }
                AppButton {
                    objectName: "dmSendControlButton"; theme: root.theme; variant: "primary"
                    text: !root.canfd ? qsTr("写入控制槽位") : periodic.checked ? qsTr("启动周期发送") : qsTr("发送一次")
                    actionEnabled: root.canTransmit && root.idValid && root.framePreview.valid
                    onClicked: root.controller.sendControl(canChannel.currentIndex, motorId.text.trim(), root.parameters, root.canfd && periodic.checked, interval.value)
                }
                AppButton {
                    objectName: "dmEnableButton"; theme: root.theme
                    text: root.canfd ? qsTr("使能当前电机") : qsTr("使能全部电机")
                    actionEnabled: root.canTransmit && (!root.canfd || root.idValid)
                    onClicked: root.canfd ? root.controller.enableMotor(canChannel.currentIndex, motorId.text.trim()) : enableConfirm.open()
                }
                AppButton {
                    objectName: "dmDisableButton"; theme: root.theme; variant: "danger"
                    text: root.canfd ? qsTr("失能当前电机") : qsTr("全部失能")
                    actionEnabled: root.controller && root.controller.running && !root.busy && (!root.canfd || root.idValid) && root.channelReady
                    onClicked: root.controller.disableMotor(canChannel.currentIndex, motorId.text.trim())
                }
                AppButton {
                    objectName: "dmStopSendingButton"; theme: root.theme; variant: "secondary"
                    text: root.canfd ? qsTr("停止发送") : qsTr("复位当前槽位")
                    actionEnabled: root.controller && root.controller.running && root.idValid
                    onClicked: root.controller.stopSending(canChannel.currentIndex, motorId.text.trim())
                }
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: root.canfd ? qsTr("单次指主站提交一次，DC 下 CAN 端可能重复；停止发送不等于失能。使能 / 失能仅作用于当前电机。") : qsTr("MIT 从站持续发送槽位内容。复位槽位写入默认控制帧，失能不会清空其他槽位。")
                color: root.theme.textMuted; font.pixelSize: root.theme.fontCaption
            }
            Label {
                objectName: "dmExitDisableRangeMessage"; Layout.fillWidth: true
                visible: root.outsideExitDisableRange; wrapMode: Text.Wrap
                text: qsTr("此 ID 不在固件退出失能范围内，请先显式失能再断开。")
                color: root.theme.warningText; font.pixelSize: root.theme.fontBodySmall; font.bold: true
            }
        }
    }
    PanelCard {
        Layout.fillWidth: true; theme: root.theme; padding: root.theme.space10
        ColumnLayout {
            width: parent.width; spacing: root.theme.space8
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("原始 MIT 帧"); color: root.theme.textPrimary; font.bold: true }
                Label { Layout.fillWidth: true; text: qsTr("8 字节 HEX · 使用同一目标和发送方式"); color: root.theme.textMuted; font.pixelSize: root.theme.fontBodySmall; elide: Text.ElideRight }
                AppButton { objectName: "dmRawToggle"; theme: root.theme; variant: "ghost"; text: root.rawOpen ? qsTr("收起") : qsTr("展开"); implicitHeight: root.theme.controlHeightSmall; onClicked: root.rawOpen = !root.rawOpen }
            }
            RowLayout {
                visible: root.rawOpen; Layout.fillWidth: true; spacing: root.theme.space8
                AppTextField { theme: root.theme; id: rawInput; objectName: "dmRawInput"; Layout.fillWidth: true; implicitHeight: root.theme.controlHeight; text: ""; placeholderText: "7F FF 7F F0 00 00 07 FF"; selectByMouse: true; font.family: "Consolas"; color: root.theme.textPrimary; invalid: rawInput.text.length > 0 && !root.rawValid }
                AppButton { objectName: "dmSendRawButton"; theme: root.theme; variant: "primary"; text: root.canfd ? (periodic.checked ? qsTr("周期发送原始帧") : qsTr("发送原始帧")) : qsTr("写入原始帧"); actionEnabled: root.canTransmit && root.idValid && root.rawValid; onClicked: root.controller.sendRaw(canChannel.currentIndex, motorId.text.trim(), rawInput.text.trim(), root.canfd && periodic.checked, interval.value) }
            }
            Label { visible: root.rawOpen && rawInput.text.length > 0 && !root.rawValid; Layout.fillWidth: true; text: qsTr("请输入恰好 8 个两位十六进制字节，以空格分隔。"); color: root.theme.dangerText; font.pixelSize: root.theme.fontBodySmall }
        }
    }
    ConfirmDialog {
        id: enableConfirm
        objectName: "dmEnableConfirm"
        theme: root.theme; titleText: qsTr("使能全部电机")
        message: qsTr("将使能当前 MIT 从站上的全部电机，各槽位中已写入的控制帧会继续发送。请确认现有槽位命令符合预期。")
        confirmText: qsTr("确认使能"); tone: "danger"
        onConfirmed: { if (root.controller && root.controller.running && !root.busy && !root.canfd) root.controller.enableMotor(canChannel.currentIndex, motorId.text.trim()) }
    }
}
