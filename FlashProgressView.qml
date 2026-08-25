pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root

    required property var theme
    property string taskName: ""
    property int progress: 0
    property bool busy: false
    property string state: "idle"
    property string message: ""

    readonly property int clampedProgress: Math.max(0, Math.min(100, progress))
    readonly property bool indeterminate: busy && clampedProgress === 0
    readonly property color stateColor: {
        if (state === "success")
            return theme.success
        if (state === "error")
            return theme.danger
        if (state === "running")
            return theme.accent
        return theme.textMuted
    }
    readonly property color stateTextColor: {
        if (state === "success")
            return theme.successText
        if (state === "error")
            return theme.dangerText
        if (state === "running")
            return theme.accentText
        return theme.textSecondary
    }
    readonly property color stateBorderColor: {
        if (state === "success")
            return theme.successBorder
        if (state === "error")
            return theme.dangerBorder
        if (state === "running")
            return theme.selectedBorder
        return theme.border
    }
    readonly property string stateText: {
        if (state === "success")
            return qsTr("烧录成功")
        if (state === "error")
            return qsTr("烧录失败")
        if (state === "running")
            return qsTr("烧录进行中")
        return qsTr("等待任务")
    }
    readonly property string displayMessage: message.length
                                                     ? message
                                                     : qsTr("尚未执行%1烧录").arg(taskName)

    property real animatedProgress: clampedProgress

    padding: theme.space12
    Accessible.name: qsTr("%1，进度 %2%，%3")
                     .arg(taskName)
                     .arg(clampedProgress)
                     .arg(stateText)

    Behavior on animatedProgress {
        NumberAnimation {
            duration: root.theme.animationNormal
            easing.type: Easing.OutCubic
        }
    }

    background: Rectangle {
        radius: root.theme.radiusMedium
        color: root.theme.surfaceMuted
        border.width: 1
        border.color: root.stateBorderColor

        Behavior on border.color {
            ColorAnimation { duration: root.theme.animationFast }
        }
    }

    contentItem: ColumnLayout {
        spacing: root.theme.space8

        RowLayout {
            Layout.fillWidth: true
            spacing: root.theme.space8

            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: root.stateColor
            }

            Text {
                text: root.stateText
                color: root.stateTextColor
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontBody
                font.weight: Font.DemiBold
            }

            Item { Layout.fillWidth: true }

            Text {
                Layout.minimumWidth: 52
                text: root.clampedProgress + "%"
                color: root.stateTextColor
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontSubtitle
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignRight
            }
        }

        Item {
            id: progressTrack
            Layout.fillWidth: true
            Layout.preferredHeight: 12
            clip: true

            Rectangle {
                anchors.fill: parent
                radius: height / 2
                color: root.theme.controlBackground
                border.width: 1
                border.color: root.theme.border
            }

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: root.indeterminate
                       ? 0
                       : Math.max(root.animatedProgress > 0 ? 4 : 0,
                                  progressTrack.width * root.animatedProgress / 100)
                radius: height / 2
                color: root.stateColor
            }

            Rectangle {
                id: busyHighlight
                visible: root.indeterminate
                width: Math.max(52, progressTrack.width * 0.24)
                height: progressTrack.height
                radius: height / 2
                x: -width
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop {
                        position: 0.0
                        color: Qt.rgba(root.stateColor.r,
                                       root.stateColor.g,
                                       root.stateColor.b,
                                       0.0)
                    }
                    GradientStop {
                        position: 0.5
                        color: Qt.rgba(root.stateColor.r,
                                       root.stateColor.g,
                                       root.stateColor.b,
                                       0.72)
                    }
                    GradientStop {
                        position: 1.0
                        color: Qt.rgba(root.stateColor.r,
                                       root.stateColor.g,
                                       root.stateColor.b,
                                       0.0)
                    }
                }

                SequentialAnimation on x {
                    running: root.indeterminate && root.visible
                    loops: Animation.Infinite

                    NumberAnimation {
                        from: -busyHighlight.width
                        to: progressTrack.width
                        duration: 1000
                        easing.type: Easing.InOutSine
                    }
                    PauseAnimation { duration: 140 }
                }
            }
        }

        Text {
            id: messageLabel
            Layout.fillWidth: true
            text: root.displayMessage
            textFormat: Text.PlainText
            color: root.state === "error" ? root.theme.dangerText : root.theme.textSecondary
            font.family: root.theme.fontFamily
            font.pixelSize: root.theme.fontBodySmall
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight

            HoverHandler { id: messageHover }

            ToolTip.visible: messageHover.hovered && messageLabel.truncated
            ToolTip.delay: 500
            ToolTip.timeout: 8000
            ToolTip.text: messageLabel.text
        }
    }
}
