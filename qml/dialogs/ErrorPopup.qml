import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Dialog {
    id: dialog

    property string text: ""
    property var theme

    function copyMessage() {
        const start = messageLabel.selectionStart
        const end = messageLabel.selectionEnd
        messageLabel.selectAll()
        messageLabel.copy()
        messageLabel.select(start, end)
    }

    function show(message) {
        text = message
        open()
    }

    parent: Overlay.overlay
    modal: true
    focus: true
    padding: 20
    width: Math.min(620, parent ? Math.max(0, parent.width - 48) : 620)
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
            Layout.preferredHeight: Math.min(420, Math.max(44, messageLabel.contentHeight + 8))
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: messageLabel.contentHeight > messageScroll.availableHeight + 1
                                       ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded

            TextArea {
                id: messageLabel
                objectName: "errorMessageText"
                width: messageScroll.availableWidth
                text: dialog.text
                readOnly: true
                textFormat: TextEdit.PlainText
                wrapMode: TextArea.Wrap
                font.pixelSize: dialog.theme.fontBody
                font.family: dialog.theme.fontFamily
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

            AppButton {
                objectName: "copyErrorMessageButton"
                theme: dialog.theme
                text: qsTr("复制完整信息")
                variant: "ghost"
                actionEnabled: dialog.text.length > 0
                onClicked: dialog.copyMessage()
            }

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
