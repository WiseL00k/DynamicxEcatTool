import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root

    property var theme
    property string mode: "system"
    signal modeRequested(string mode)

    padding: 0
    implicitWidth: 142
    implicitHeight: theme.controlHeight
    background: null

    contentItem: RowLayout {
        spacing: root.theme.space6

        Text {
            text: qsTr("主题")
            color: root.theme.textMuted
            font.pixelSize: root.theme.fontBodySmall
        }

        ComboBox {
            id: modeBox
            Layout.fillWidth: true
            textRole: "label"
            valueRole: "value"
            model: [
                { "label": qsTr("跟随系统"), "value": "system" },
                { "label": qsTr("浅色"), "value": "light" },
                { "label": qsTr("深色"), "value": "dark" }
            ]
            currentIndex: root.mode === "light" ? 1 : root.mode === "dark" ? 2 : 0
            font.pixelSize: root.theme.fontBodySmall

            onActivated: function(index) {
                root.modeRequested(model[index].value)
            }
        }
    }
}
