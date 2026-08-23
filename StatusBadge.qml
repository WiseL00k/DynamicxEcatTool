import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    property var theme
    property string text: ""
    property string tone: "neutral"
    property bool showDot: true
    property bool compact: false

    readonly property color toneColor: {
        if (tone === "success") return theme.success
        if (tone === "warning") return theme.warning
        if (tone === "danger") return theme.danger
        if (tone === "info") return theme.accent
        return theme.textMuted
    }
    readonly property color toneTextColor: {
        if (tone === "success") return theme.successText
        if (tone === "warning") return theme.warningText
        if (tone === "danger") return theme.dangerText
        if (tone === "info") return theme.accentText
        return theme.textSecondary
    }
    readonly property color toneBackgroundColor: {
        if (tone === "success") return theme.successBackground
        if (tone === "warning") return theme.warningBackground
        if (tone === "danger") return theme.dangerBackground
        if (tone === "info") return theme.selectedBackground
        return theme.controlBackground
    }
    readonly property color toneBorderColor: {
        if (tone === "success") return theme.successBorder
        if (tone === "warning") return theme.warningBorder
        if (tone === "danger") return theme.dangerBorder
        if (tone === "info") return theme.selectedBorder
        return theme.borderStrong
    }

    implicitHeight: compact ? 24 : 28
    implicitWidth: badgeContent.implicitWidth + (compact ? theme.space12 : theme.space16)
    radius: height / 2
    color: toneBackgroundColor
    border.width: 1
    border.color: toneBorderColor

    Accessible.name: text

    RowLayout {
        id: badgeContent
        anchors.centerIn: parent
        spacing: root.theme.space6

        Rectangle {
            visible: root.showDot
            Layout.preferredWidth: visible ? 7 : 0
            Layout.preferredHeight: visible ? 7 : 0
            radius: 4
            color: root.toneColor
        }

        Text {
            text: root.text
            color: root.toneTextColor
            font.pixelSize: root.theme.fontBodySmall
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }
    }
}
