import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {

    property bool isConnected: false
    signal connectionChanged(bool connected)

    // 改为从站类型
    property string selectedSlaveType: "MIT"

    Rectangle {
        anchors.fill: parent
        color: "#f4f6f8"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 14

            // =========================
            // 顶部控制区
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
                        Layout.preferredWidth: 260
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

                                    color: ListView.isCurrentItem
                                           ? "#e3f2fd"
                                           : "transparent"

                                    border.color: ListView.isCurrentItem
                                                  ? "#90caf9"
                                                  : "transparent"

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
                                            EthercatBackend.changedSelectedNic(index)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // =========================
                    // 从站类型选择
                    // =========================
                    ColumnLayout {
                        Layout.preferredWidth: 220
                        spacing: 8

                        Label {
                            text: "从站类型"
                            font.bold: true
                        }

                        ComboBox {
                            id: slaveTypeBox
                            model: ["MIT"]
                            currentIndex: 0

                            onCurrentTextChanged: {
                                selectedSlaveType = currentText
                                updatePanel()
                            }
                        }

                        // 提示信息
                        Label {
                            text: "参数界面只能有一块从站"
                            color: "#d32f2f"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    // =========================
                    // 连接状态
                    // =========================
                    ColumnLayout {
                        Layout.preferredWidth: 180
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
                                    EthercatBackend.enterMitSlaveDebugMode()
                                else
                                    EthercatBackend.exitMitSlaveDebugMode()
                            }
                        }
                    }
                }
            }

            // =========================
            // MIT 控制面板
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

    function updatePanel() {
        configLoader.setSource(
            "panels/MITConfigPanel.qml",
            {
                "slaveType": selectedSlaveType,
                "isConnected": isConnected
            }
        )
    }

    Connections {
        target: EthercatBackend

        function onConnectedUpdated(status) {
            isConnected = (status === 1)

            connectionStatus.text =
                isConnected ? "已连接" : "未连接"

            connectionChanged(isConnected)
            updatePanel()
        }
    }

    Component.onCompleted: {
        updatePanel()
    }
}
