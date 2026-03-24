import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    modal: true
    focus: true
    padding: 0
    width: 420
    height: contentItem.implicitHeight + 44

    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    x: (parent.width - width) / 2
    y: (parent.height - height) / 2

    property string text: ""

    function show(msg) {
        text = msg
        open()
    }

    background: Rectangle {
        radius: 12
        color: "#ffffff"
        border.color: "#e5e7eb"
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 18
        anchors.margins: 22
        anchors.fill: parent

        RowLayout {
            spacing: 10
            Layout.fillWidth: true

            Rectangle {
                width: 36
                height: 36
                radius: 8
                color: "#fee2e2"

                Text {
                    anchors.centerIn: parent
                    text: "!"
                    font.pixelSize: 20
                    font.bold: true
                    color: "#dc2626"
                }
            }

            Label {
                text: "系统提示"
                font.pixelSize: 18
                font.bold: true
                color: "#111827"
                Layout.fillWidth: true
                verticalAlignment: Text.AlignVCenter
            }
        }

        Label {
            text: dialog.text
            wrapMode: Text.WordWrap
            font.pixelSize: 14
            color: "#374151"
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#f3f4f6"
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10

            Item { Layout.fillWidth: true }

            Button {
                text: "确定"

                highlighted: true

                implicitWidth: 90
                implicitHeight: 36

                onClicked: dialog.close()

                background: Rectangle {
                    radius: 8

                    color: parent.hovered
                           ? "#2563eb"
                           : "#3b82f6"

                    border.color: "#1d4ed8"
                    border.width: parent.hovered ? 1 : 0

                    Behavior on color {
                        ColorAnimation { duration: 120 }
                    }
                }

                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 14
                    font.bold: true
                    color: "#ffffff"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onPressed: scale = 0.90
                onReleased: scale = 1.0

                transform: Scale {
                    id: scaleTransform
                    origin.x: width / 2
                    origin.y: height / 2
                    xScale: scale
                    yScale: scale
                }

                property real scale: 1
            }
        }
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: 120
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            to: 0
            duration: 100
        }
    }
}
