import QtQuick
import QtQuick.Controls

TextField {
    id: control

    property var theme
    property bool invalid: false

    implicitWidth: 120
    implicitHeight: theme.controlHeight
    leftPadding: theme.space10
    rightPadding: theme.space10
    topPadding: theme.space6
    bottomPadding: theme.space6
    font.family: theme.fontFamily
    font.pixelSize: theme.fontBody
    color: enabled ? theme.textPrimary : theme.textDisabled
    placeholderTextColor: theme.textMuted
    selectionColor: theme.accent
    selectedTextColor: theme.textOnAccent
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true
    hoverEnabled: true

    background: Rectangle {
        radius: control.theme.radiusSmall
        color: control.theme.inputBackground
        border.width: control.activeFocus ? 2 : 1
        border.color: control.invalid ? control.theme.danger
            : control.activeFocus ? control.theme.accent
            : control.enabled ? control.theme.borderStrong : control.theme.border
    }
}
