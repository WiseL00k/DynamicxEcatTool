import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    property int slaveId: -1
    property bool isConnected: false

    property int currentIdValue: 1
    property int targetIdValue: 1

    // 防止 Loader 高度异常
    implicitHeight: 220

    function makeWrite(slave, index, sub, bytes) {
        return {
            slave: slave,
            index: index,
            subindex: sub,
            type: "write",
            data: bytes
        }
    }

    function formatHex(v) {
        return v.toString(16).toUpperCase().padStart(3, "0")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            text: "MIT电机 CAN ID 配置"
            font.bold: true
        }

        Rectangle {
            Layout.fillWidth: true
            radius: 8
            color: "#fafafa"
            border.color: "#e6e6e6"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12

                RowLayout {
                    spacing: 16

                    ColumnLayout {
                        Label { text: "CAN 总线"; font.pixelSize: 12 }
                        ComboBox {
                            id: canBus
                            model: ["CAN0", "CAN1"]
                            Layout.preferredWidth: 100
                        }
                    }

                    ColumnLayout {
                        Label { text: "当前 ID (Hex)"; font.pixelSize: 12 }
                        TextField {
                            Layout.preferredWidth: 120
                            text: formatHex(currentIdValue)

                            validator: RegularExpressionValidator {
                                regularExpression: /^[0-9A-Fa-f]{1,3}$/
                            }

                            onTextChanged: {
                                if (acceptableInput)
                                    currentIdValue = parseInt(text, 16)
                            }
                        }
                    }

                    ColumnLayout {
                        Label { text: "目标 ID (Hex)"; font.pixelSize: 12 }
                        TextField {
                            Layout.preferredWidth: 120
                            text: formatHex(targetIdValue)

                            validator: RegularExpressionValidator {
                                regularExpression: /^[0-9A-Fa-f]{1,3}$/
                            }

                            onTextChanged: {
                                if (acceptableInput)
                                    targetIdValue = parseInt(text, 16)
                            }
                        }
                    }

                    Button {
                        text: "应用"
                        enabled: isConnected

                        onClicked: {
                            let configs = [
                                makeWrite(slaveId, 0x3100, 0x01, [canBus.currentIndex + 1]),
                                makeWrite(slaveId, 0x3100, 0x02, [currentIdValue]),
                                makeWrite(slaveId, 0x3100, 0x03, [targetIdValue]),
                                makeWrite(slaveId, 0x3100, 0x04, [1])
                            ]

                            let ret = EthercatBackend.applySDOConfigsQml(configs)
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            radius: 6
            color: "#fafafa"
            border.color: "#e0e0e0"

            Text {
                anchors.centerIn: parent
                text: "SDO: 0x3100 → 01(CAN) 02(Current) 03(Target) 04(Apply)"
                font.pixelSize: 12
                color: "#666"
            }
        }

    }
}
