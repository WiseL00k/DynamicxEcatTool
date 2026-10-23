pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

PanelCard {
    id: root
    property int channel: 0
    property var configuration: null
    property var liveConfiguration: configuration
    property bool localEdits: false
    property bool editable: false
    signal configurationEdited(var values)
    readonly property var nominalRates: [100000, 125000, 250000, 500000, 1000000]
    readonly property var dataRates: [2500000, 5000000]
    padding: theme.space12

    function loadConfiguration() {
        const c = configuration
        channelEnabled.checked = c ? Boolean(c.enabled) : true
        format.currentIndex = c ? Number(c.format) : 0
        nominal.currentIndex = c ? nominalRates.indexOf(Number(c.nominalBitrate)) : 4
        dataRate.currentIndex = c ? dataRates.indexOf(Number(c.dataBitrate)) : 1
        const options = c ? Number(c.options) : 5
        retransmission.checked = (options & 1) !== 0
        transmitPause.checked = (options & 2) !== 0
        protocolException.checked = (options & 4) !== 0
    }
    function values() {
        return {channel: channel, enabled: channelEnabled.checked, format: format.currentIndex,
            nominalBitrate: nominalRates[nominal.currentIndex], dataBitrate: dataRates[dataRate.currentIndex],
            options: (retransmission.checked ? 1 : 0) | (transmitPause.checked ? 2 : 0) | (protocolException.checked ? 4 : 0)}
    }
    onConfigurationChanged: loadConfiguration()
    Component.onCompleted: loadConfiguration()

    ColumnLayout {
        width: parent.width
        spacing: root.theme.space8
        RowLayout {
            Layout.fillWidth: true
            Label { text: "CAN " + root.channel; font.bold: true; font.pixelSize: root.theme.fontSubtitle; color: root.theme.textPrimary }
            Item { Layout.fillWidth: true }
            StatusBadge {
                theme: root.theme; compact: true
                text: root.localEdits ? qsTr("本地草稿") : !root.liveConfiguration ? qsTr("尚未读取") : [qsTr("已应用"), qsTr("未应用"), qsTr("应用中"), qsTr("配置错误")][Number(root.liveConfiguration.status)] || qsTr("未知")
                tone: root.localEdits ? "warning" : !root.liveConfiguration ? "neutral" : Number(root.liveConfiguration.error) !== 0 ? "danger" : root.liveConfiguration.dirty ? "warning" : "success"
            }
            Switch { id: channelEnabled; objectName: "canfdChannelEnabled" + root.channel; text: qsTr("启用"); enabled: root.editable; onToggled: root.configurationEdited(root.values()) }
        }
        GridLayout {
            columns: 2; Layout.fillWidth: true; columnSpacing: root.theme.space12; rowSpacing: root.theme.space8
            Label { text: qsTr("帧格式"); color: root.theme.textSecondary }
            ComboBox { id: format; objectName: "canfdFormat" + root.channel; Layout.fillWidth: true; model: ["Classic CAN", "CAN FD", "CAN FD + BRS"]; enabled: root.editable; onActivated: root.configurationEdited(root.values()) }
            Label { text: qsTr("仲裁速率"); color: root.theme.textSecondary }
            ComboBox { id: nominal; Layout.fillWidth: true; model: ["100 kbit/s", "125 kbit/s", "250 kbit/s", "500 kbit/s", "1 Mbit/s"]; enabled: root.editable; onActivated: root.configurationEdited(root.values()) }
            Label { text: qsTr("数据速率"); color: root.theme.textSecondary }
            ComboBox { id: dataRate; Layout.fillWidth: true; model: ["2.5 Mbit/s", "5 Mbit/s"]; enabled: root.editable && format.currentIndex === 2; onActivated: root.configurationEdited(root.values()) }
        }
        Flow {
            Layout.fillWidth: true; spacing: root.theme.space4
            CheckBox { id: retransmission; text: qsTr("自动重传"); enabled: root.editable; onToggled: root.configurationEdited(root.values()) }
            CheckBox { id: transmitPause; text: qsTr("发送暂停"); enabled: root.editable; onToggled: root.configurationEdited(root.values()) }
            CheckBox { id: protocolException; text: qsTr("协议异常处理"); enabled: root.editable; onToggled: root.configurationEdited(root.values()) }
        }
        Label {
            Layout.fillWidth: true; visible: root.liveConfiguration !== null && Number(root.liveConfiguration.error) !== 0
            text: qsTr("配置错误：0x") + (root.liveConfiguration ? Number(root.liveConfiguration.error).toString(16).toUpperCase().padStart(8, "0") : "")
            color: root.theme.dangerText; wrapMode: Text.WrapAnywhere
        }
    }
}
