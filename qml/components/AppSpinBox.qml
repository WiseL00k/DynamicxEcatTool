pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

SpinBox {
    id: control

    property var theme

    implicitWidth: 120
    implicitHeight: theme.controlHeight
    leftPadding: theme.space10 + (mirrored ? theme.space24 : 0)
    rightPadding: theme.space10 + (mirrored ? 0 : theme.space24)
    topPadding: theme.space4
    bottomPadding: theme.space4
    font.family: theme.fontFamily
    font.pixelSize: theme.fontBody
    hoverEnabled: true
    palette.base: theme.inputBackground
    palette.text: theme.textPrimary
    palette.button: theme.controlBackground
    palette.buttonText: theme.textPrimary
    palette.highlight: theme.accent
    palette.highlightedText: theme.textOnAccent

    // Keep the standard SpinBox editor hooks; the base control owns parsing,
    // keyboard stepping, range validation and valueModified emission.
    contentItem: TextInput {
        text: control.displayText
        font: control.font
        color: control.enabled ? control.theme.textPrimary : control.theme.textDisabled
        selectionColor: control.theme.accent
        selectedTextColor: control.theme.textOnAccent
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
        selectByMouse: true
        clip: true
    }

    component StepIndicator: Rectangle {
        id: step
        required property bool stepUp
        readonly property bool pressed: stepUp ? control.up.pressed : control.down.pressed
        readonly property bool hovered: stepUp ? control.up.hovered : control.down.hovered
        implicitWidth: control.theme.space24
        implicitHeight: control.theme.controlHeight / 2 - 2
        x: control.mirrored ? 2 : control.width - width - 2
        y: stepUp ? 2 : control.height - height - 2
        width: implicitWidth
        height: Math.max(0, (control.height - 4) / 2)
        radius: Math.max(1, control.theme.radiusSmall - 2)
        color: pressed ? control.theme.selectedBackground
            : hovered && enabled ? control.theme.controlHover : control.theme.controlBackground

        Canvas {
            anchors.centerIn: parent
            width: 8; height: 5
            rotation: step.stepUp ? 180 : 0
            property color arrowColor: step.enabled ? control.theme.textSecondary : control.theme.textDisabled
            onArrowColorChanged: requestPaint()
            onPaint: {
                const context = getContext("2d")
                context.clearRect(0, 0, width, height)
                context.fillStyle = arrowColor
                context.beginPath()
                context.moveTo(0, 0)
                context.lineTo(width, 0)
                context.lineTo(width / 2, height)
                context.closePath()
                context.fill()
            }
        }
    }

    up.indicator: StepIndicator { stepUp: true }
    down.indicator: StepIndicator { stepUp: false }

    background: Rectangle {
        radius: control.theme.radiusSmall
        color: control.theme.inputBackground
        readonly property bool focused: control.activeFocus || control.contentItem.activeFocus
        border.width: focused ? 2 : 1
        border.color: focused ? control.theme.accent
            : control.enabled ? control.theme.borderStrong : control.theme.border
    }
}
