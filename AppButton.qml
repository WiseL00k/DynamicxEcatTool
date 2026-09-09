import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control

    property var theme
    property string variant: "secondary"
    property bool busy: false
    property string busyText: qsTr("处理中…")
    property bool actionEnabled: true
    property string accessibleName: text

    readonly property bool filled: variant === "primary" || variant === "danger"
    readonly property bool iconOnly: text.length === 0
                                     && icon.source.toString().length > 0
    readonly property color baseColor: {
        if (variant === "primary")
            return theme.accent
        if (variant === "danger")
            return theme.danger
        if (variant === "ghost")
            return "transparent"
        return theme.controlBackground
    }
    readonly property color hoverColor: {
        if (variant === "primary")
            return theme.accentHover
        if (variant === "danger")
            return theme.dangerHover
        return theme.controlHover
    }
    readonly property color pressedColor: {
        if (variant === "primary")
            return theme.accentPressed
        if (variant === "danger")
            return theme.dangerPressed
        return theme.selectedBackground
    }
    readonly property color foregroundColor: filled
                                                    ? theme.textOnAccent
                                                    : (variant === "danger"
                                                       ? theme.dangerText
                                                       : theme.textPrimary)

    enabled: actionEnabled && !busy
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    leftPadding: iconOnly ? 0 : theme.space12
    rightPadding: iconOnly ? 0 : theme.space12
    topPadding: 0
    bottomPadding: 0
    spacing: theme.space6
    implicitHeight: theme.controlHeight
    implicitWidth: iconOnly
                   ? implicitHeight
                   : Math.max(76, contentRow.implicitWidth + leftPadding + rightPadding)

    Accessible.name: accessibleName

    background: Rectangle {
        radius: control.theme.radiusMedium
        color: !control.enabled
               ? control.baseColor
               : control.down
                 ? control.pressedColor
                 : control.hovered
                   ? control.hoverColor
                   : control.baseColor
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus
                      ? control.theme.accent
                      : control.filled
                        ? "transparent"
                        : control.variant === "danger"
                          ? control.theme.dangerBorder
                          : control.variant === "ghost"
                            ? "transparent"
                            : control.theme.borderStrong
        opacity: control.enabled ? 1.0 : 0.45

        Behavior on color {
            ColorAnimation { duration: control.theme.animationFast }
        }
    }

    contentItem: RowLayout {
        id: contentRow
        spacing: control.spacing

        Image {
            visible: control.icon.source.toString().length > 0
            source: control.icon.source
            sourceSize.width: 16
            sourceSize.height: 16
            Layout.preferredWidth: visible ? 16 : 0
            Layout.preferredHeight: visible ? 16 : 0
            opacity: control.enabled ? 1.0 : 0.6
        }

        Text {
            Layout.fillWidth: true
            text: control.busy ? control.busyText : control.text
            color: control.foregroundColor
            font.family: control.font.family
            font.pixelSize: control.theme.fontBody
            font.weight: control.filled ? Font.DemiBold : Font.Medium
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
