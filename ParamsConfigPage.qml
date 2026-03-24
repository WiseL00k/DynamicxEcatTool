import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {

    property bool isConnected: false
    signal connectionChanged(bool connected)

    // 当前选择
    property string selectedSlaveType: "MIT"
    property string selectedFunction: "CAN_ID"
    property int selectedSlaveId: 1

    Rectangle {
        anchors.fill: parent
        color: "#f4f6f8"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 14

            // =========================
            // 顶部综合控制区（新增网卡 + 状态）
            // =========================
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                radius: 10
                color: "white"
                border.color: "#e6e6e6"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 20

                    // =========================
                    // 网卡选择
                    // =========================
                    ColumnLayout {
                        Layout.preferredWidth: 60
                        spacing: 8

                        RowLayout {
                            spacing: 8

                            Button {
                                text: "刷新网卡"
                                onClicked: EthercatBackend.refreshNicsAsync()
                            }

                            Label {
                                text: "可用网卡"
                                font.bold: true
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 6
                            border.color: "#e0e0e0"

                            ListView {
                                id: nicList
                                anchors.fill: parent
                                anchors.margins: 4
                                spacing: 4
                                clip: true

                                model: EthercatBackend ? EthercatBackend.nicList : []

                                ScrollBar.vertical: ScrollBar {}

                                delegate: Rectangle {
                                    width: ListView.view.width
                                    height: 32
                                    radius: 6

                                    color: ListView.isCurrentItem ? "#e3f2fd" : "transparent"
                                    border.color: ListView.isCurrentItem ? "#90caf9" : "transparent"

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData
                                        color: "#333"
                                        elide: Text.ElideRight
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            nicList.currentIndex = index
                                            EthercatBackend.changedSelectedNic(
                                                        index)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // =========================
                    // 中间：从站配置
                    // =========================
                    ColumnLayout {
                        Layout.preferredWidth: 260
                        spacing: 8

                        Label {
                            text: "从站类型"
                            font.bold: true
                        }

                        ComboBox {
                            id: slaveTypeBox
                            model: ["MIT"]

                            onCurrentTextChanged: {
                                selectedSlaveType = currentText
                                selectedFunction = "CAN_ID"
                                updatePanel()
                            }
                        }

                        Label {
                            text: "Slave ID"
                            font.bold: true
                        }

                        SpinBox {
                            from: 1
                            to: 256
                            value: 1

                            onValueChanged: {
                                selectedSlaveId = value
                                updatePanel()
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    // =========================
                    // 右侧：连接状态 + YAML
                    // =========================
                    ColumnLayout {
                        Layout.preferredWidth: 50
                        spacing: 12

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 40

                                radius: 6
                                color: isConnected ? "#e8f5e9" : "#ffebee"
                                border.color: isConnected ? "#81c784" : "#e57373"

                                RowLayout {
                                    anchors.centerIn: parent
                                    spacing: 6

                                    Rectangle {
                                        width: 10
                                        height: 10
                                        radius: 5
                                        color: isConnected ? "#4caf50" : "#f44336"
                                    }

                                    Text {
                                        id: connectionStatus
                                        text: isConnected ? "已连接" : "未连接"
                                        font.bold: true
                                    }
                                }
                            }

                            Button {
                                text: isConnected ? "断开连接" : "连接主站"
                                Layout.fillWidth: true
                                onClicked: {
                                    if (!isConnected)
                                        EthercatBackend.enterPreOpAll()
                                    else
                                        EthercatBackend.exitPreOpAll()
                                }
                            }


                        // RowLayout {
                        //     spacing: 6

                        //     TextField {
                        //         id: yamlPathField
                        //         readOnly: true
                        //         Layout.fillWidth: true
                        //         placeholderText: "选择 YAML"
                        //     }

                        //     Button {
                        //         text: "浏览"
                        //         onClicked: fileDialog.open()
                        //     }
                        // }
                    }
                }
            }

            // =========================
            // 功能选择区
            // =========================
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 90
                radius: 10
                color: "white"
                border.color: "#e6e6e6"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Label {
                        text: "可用 SDO 功能"
                        font.bold: true
                    }

                    RowLayout {
                        spacing: 10

                        Repeater {
                            model: selectedSlaveType === "MIT" ? ["CAN_ID"] : []

                            delegate: Rectangle {
                                radius: 6
                                border.color: "#409eff"
                                border.width: 1
                                color: "transparent"

                                implicitWidth: textItem.implicitWidth + 16
                                implicitHeight: textItem.implicitHeight + 10

                                Label {
                                    id: textItem
                                    anchors.centerIn: parent
                                    text: "达妙电机CAN ID 配置"
                                }
                            }
                        }
                    }
                }
            }

            // =========================
            // 动态面板区
            // =========================
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: "white"
                border.color: "#e6e6e6"

                Loader {
                    id: configLoader
                    anchors.fill: parent
                    anchors.margins: 12
                }
            }
        }
    }

    // =========================
    // 文件选择
    // =========================
    FileDialog {
        id: fileDialog
        nameFilters: ["YAML files (*.yaml *.yml)"]

        onAccepted: {
            yamlPathField.text = EthercatBackend.changeConfigFilePath(
                        selectedFile)
        }
    }

    // =========================
    // 动态加载逻辑
    // =========================
    function updatePanel() {

        if (selectedSlaveType === "MIT" && selectedFunction === "CAN_ID") {

            configLoader.setSource("panels/MITConfigPanel.qml", {
                                       "slaveId": selectedSlaveId,
                                       "isConnected": isConnected
                                   })
        } else {
            configLoader.setSource("", {})
        }
    }

    // =========================
    // 后端通信
    // =========================
    Connections {
        target: EthercatBackend

        function onConnectedUpdated(status) {
            isConnected = (status === 1)
            connectionStatus.text = isConnected ? "已连接" : "未连接"

            connectionChanged(isConnected)
            updatePanel()
        }
    }

    Component.onCompleted: {
        updatePanel()
    }
}
