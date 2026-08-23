import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root

    property var theme
    property var model: []
    property int currentIndex: -1
    property bool actionEnabled: true
    property bool busy: false
    property string labelText: qsTr("网卡")
    signal selectionRequested(int index)
    signal refreshRequested()

    padding: 0
    implicitHeight: theme.controlHeight
    implicitWidth: 330

    background: null

    contentItem: RowLayout {
        spacing: root.theme.space8

        Text {
            text: root.labelText
            color: root.theme.textMuted
            font.pixelSize: root.theme.fontBodySmall
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }

        ComboBox {
            id: nicBox
            Layout.fillWidth: true
            Layout.minimumWidth: 150
            model: root.model
            currentIndex: root.currentIndex
            enabled: root.actionEnabled && !root.busy && count > 0
            displayText: root.busy
                         ? qsTr("正在刷新…")
                         : count > 0
                           ? currentText
                           : qsTr("未发现网卡")
            hoverEnabled: true
            font.pixelSize: root.theme.fontBodySmall
            onActivated: function(index) {
                root.selectionRequested(index)
            }

            ToolTip.visible: hovered && currentText.length > 28
            ToolTip.text: currentText
            ToolTip.delay: 450
        }

        AppButton {
            theme: root.theme
            text: qsTr("刷新")
            variant: "ghost"
            actionEnabled: root.actionEnabled
            busy: root.busy
            busyText: qsTr("刷新中")
            implicitWidth: 58
            onClicked: root.refreshRequested()
        }
    }
}
