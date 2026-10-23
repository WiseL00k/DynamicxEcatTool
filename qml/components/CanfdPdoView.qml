pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

PanelCard {
    id: root
    property string title: ""
    property string direction: ""
    property string wordName: ""
    property string wordHex: "—"
    property string digitalHex: "—"
    property string rawHex: ""
    property var rows: []
    property bool showRaw: false
    padding: theme.space10

    ColumnLayout {
        width: parent.width; spacing: root.theme.space6
        RowLayout {
            Layout.fillWidth: true
            Label { text: root.title; font.bold: true; font.pixelSize: root.theme.fontSubtitle; color: root.theme.textPrimary }
            Label { text: root.direction; color: root.theme.textMuted; Layout.fillWidth: true; elide: Text.ElideRight }
            Label { text: "185 B"; color: root.theme.textMuted }
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: root.wordName + ": " + root.wordHex + "     Digital I/O: " + root.digitalHex
            color: root.theme.textSecondary; font.family: "Consolas"; font.pixelSize: root.theme.fontBodySmall
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Label { text: "CH"; Layout.preferredWidth: 24; color: root.theme.textMuted }
            Label { text: qsTr("槽"); Layout.preferredWidth: 22; color: root.theme.textMuted }
            Label { text: "V"; Layout.preferredWidth: 18; color: root.theme.textMuted }
            Label { text: "Info"; Layout.preferredWidth: 42; color: root.theme.textMuted }
            Label { text: "ID"; Layout.preferredWidth: 34; color: root.theme.textMuted }
            Label { text: "DLC"; Layout.preferredWidth: 32; color: root.theme.textMuted }
            Label { text: qsTr("Data (HEX)"); Layout.fillWidth: true; color: root.theme.textMuted }
        }
        Repeater {
            model: root.rows
            delegate: Rectangle {
                id: entry
                required property var modelData
                required property int index
                Layout.fillWidth: true; implicitHeight: 25
                color: modelData.valid ? root.theme.selectedBackground : (index % 2 ? root.theme.surfaceMuted : "transparent")
                radius: 2
                RowLayout {
                    anchors.fill: parent; spacing: 4
                    Label { text: entry.modelData.channel; Layout.preferredWidth: 24; color: root.theme.textSecondary }
                    Label { text: entry.modelData.slot; Layout.preferredWidth: 22; color: root.theme.textSecondary }
                    Label { text: entry.modelData.valid ? "1" : "0"; Layout.preferredWidth: 18; color: entry.modelData.valid ? root.theme.successText : root.theme.textMuted }
                    Label { text: entry.modelData.frameInfoHex; Layout.preferredWidth: 42; font.family: "Consolas"; font.pixelSize: 11; color: root.theme.textSecondary }
                    Label { text: entry.modelData.idHex; Layout.preferredWidth: 34; font.family: "Consolas"; font.pixelSize: 11; color: root.theme.textPrimary }
                    Label { text: entry.modelData.dlc; Layout.preferredWidth: 32; color: root.theme.textSecondary }
                    Label { text: entry.modelData.dataHex; Layout.fillWidth: true; font.family: "Consolas"; font.pixelSize: 11; color: root.theme.textPrimary; elide: Text.ElideRight; ToolTip.visible: dataHover.hovered; ToolTip.text: text; HoverHandler { id: dataHover } }
                }
            }
        }
        Label { visible: root.rows.length === 0; text: qsTr("启动通信后显示过程数据"); color: root.theme.textMuted; padding: 12 }
        AppButton { theme: root.theme; variant: "ghost"; text: root.showRaw ? qsTr("收起完整字节") : qsTr("完整 PDO 字节"); implicitHeight: root.theme.controlHeightSmall; actionEnabled: root.rawHex.length > 0; onClicked: root.showRaw = !root.showRaw }
        TextArea {
            visible: root.showRaw; Layout.fillWidth: true; readOnly: true; selectByMouse: true
            text: root.rawHex; wrapMode: Text.Wrap; font.family: "Consolas"; font.pixelSize: root.theme.fontBodySmall; color: root.theme.textSecondary
        }
    }
}
