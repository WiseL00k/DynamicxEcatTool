import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog

    property string text: ""
    property var theme

    function show(message) {
        text = message
        open()
    }

    parent: Overlay.overlay
    modal: true
    focus: true
    padding: 20
    width: Math.min(460, parent ? Math.max(320, parent.width - 48) : 460)
    height: Math.min(parent ? Math.max(220, parent.height - 48) : 420,
                     Math.max(210, dialogContent.implicitHeight + topPadding + bottomPadding))
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        radius: dialog.theme.radiusLarge
        color: dialog.theme.surfaceRaised
        border.color: dialog.theme.dangerBorder
        border.width: 1
    }

    contentItem: ColumnLayout {
        id: dialogContent
        spacing: dialog.theme.space16

        RowLayout {
            Layout.fillWidth: true
            spacing: dialog.theme.space10

            Rectangle {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                radius: dialog.theme.radiusMedium
                color: dialog.theme.dangerBackground

                Text {
                    anchors.centerIn: parent
                    text: "!"
                    font.pixelSize: dialog.theme.fontTitle
                    font.bold: true
                    color: dialog.theme.dangerText
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("系统提示")
                font.pixelSize: dialog.theme.fontSubtitle
                font.bold: true
                color: dialog.theme.textPrimary
                verticalAlignment: Text.AlignVCenter
            }
        }

        ScrollView {
            id: messageScroll
            Layout.fillWidth: true
            Layout.minimumHeight: 44
            Layout.preferredHeight: Math.min(240, Math.max(44, messageLabel.contentHeight + 8))
            Layout.fillHeight: messageLabel.contentHeight > 240
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            TextArea {
                id: messageLabel
                width: messageScroll.availableWidth
                text: dialog.text
                readOnly: true
                wrapMode: TextArea.Wrap
                font.pixelSize: dialog.theme.fontBody
                color: dialog.theme.textSecondary
                selectByMouse: true
                padding: 0
                background: null
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: dialog.theme.divider
        }

        RowLayout {
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            AppButton {
                theme: dialog.theme
                text: qsTr("确定")
                variant: "primary"
                onClicked: dialog.close()
            }
        }
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: dialog.theme.animationFast
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            to: 0
            duration: dialog.theme.animationFast
        }
    }
}
