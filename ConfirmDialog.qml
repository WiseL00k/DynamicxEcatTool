import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog

    property var theme
    property string titleText: qsTr("请确认操作")
    property string message: ""
    property string confirmText: qsTr("确认")
    property string tone: "warning"
    signal confirmed()

    parent: Overlay.overlay
    modal: true
    focus: true
    padding: theme ? theme.space20 : 20
    width: Math.min(460, parent ? Math.max(320, parent.width - 48) : 460)
    height: dialogContent.implicitHeight + topPadding + bottomPadding
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: dialog.theme.surfaceRaised
        radius: dialog.theme.radiusLarge
        border.width: 1
        border.color: dialog.tone === "danger"
                      ? dialog.theme.dangerBorder
                      : dialog.theme.warningBorder
    }

    contentItem: ColumnLayout {
        id: dialogContent
        spacing: dialog.theme.space16

        RowLayout {
            Layout.fillWidth: true
            spacing: dialog.theme.space12

            Rectangle {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                radius: dialog.theme.radiusMedium
                color: dialog.tone === "danger"
                       ? dialog.theme.dangerBackground
                       : dialog.theme.warningBackground

                Text {
                    anchors.centerIn: parent
                    text: dialog.tone === "danger" ? "!" : "i"
                    color: dialog.tone === "danger"
                           ? dialog.theme.dangerText
                           : dialog.theme.warningText
                    font.pixelSize: dialog.theme.fontSubtitle
                    font.bold: true
                }
            }

            Text {
                Layout.fillWidth: true
                text: dialog.titleText
                color: dialog.theme.textPrimary
                font.pixelSize: dialog.theme.fontSubtitle
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
        }

        Text {
            Layout.fillWidth: true
            text: dialog.message
            color: dialog.theme.textSecondary
            font.pixelSize: dialog.theme.fontBody
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: dialog.theme.divider
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: dialog.theme.space8

            Item { Layout.fillWidth: true }

            AppButton {
                theme: dialog.theme
                text: qsTr("取消")
                variant: "ghost"
                onClicked: dialog.close()
            }

            AppButton {
                theme: dialog.theme
                text: dialog.confirmText
                variant: dialog.tone === "danger" ? "danger" : "primary"
                onClicked: {
                    dialog.confirmed()
                    dialog.close()
                }
            }
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: dialog.theme.animationFast }
    }

    exit: Transition {
        NumberAnimation { property: "opacity"; to: 0; duration: dialog.theme.animationFast }
    }
}
