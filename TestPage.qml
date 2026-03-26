import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {

    property bool isConnected: false
    signal connectionChanged(bool connected)

    Rectangle {
        anchors.fill: parent
        color: "#f4f6f8"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 16

            // ================= 顶部控制区 =================
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 170
                radius: 10
                color: "white"
                border.color: "#e6e6e6"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 24

                    // ===== 网卡列表 =====
                    ColumnLayout {
                        Layout.fillHeight: true
                        Layout.preferredWidth: 320
                        spacing: 10

                        RowLayout {
                            spacing: 10

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
                                model: EthercatBackend ? EthercatBackend.nicList : []
                                clip: true

                                delegate: Rectangle {
                                    width: ListView.view.width
                                    height: 36
                                    radius: 6

                                    color: ListView.isCurrentItem ? "#e3f2fd" : "transparent"
                                    border.color: ListView.isCurrentItem ? "#90caf9" : "transparent"

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 12
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

                                ScrollBar.vertical: ScrollBar {
                                    policy: ScrollBar.AsNeeded
                                    anchors.right: parent.right
                                }
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // ===== 操作区 =====
                    ColumnLayout {
                        Layout.preferredWidth: 240
                        spacing: 18

                        Label {
                            text: "EtherCAT 测试"
                            font.bold: true
                        }

                        RowLayout {

                            Layout.fillWidth: true
                            spacing: 10

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 40

                                radius: 6
                                color: isConnected ? "#e8f5e9" : "#ffebee"
                                border.color: isConnected ? "#81c784" : "#e57373"

                                RowLayout {
                                    anchors.centerIn: parent
                                    spacing: 8

                                    Rectangle {
                                        width: 10
                                        height: 10
                                        radius: 5
                                        color: isConnected ? "#4caf50" : "#f44336"

                                        Layout.alignment: Qt.AlignVCenter
                                    }

                                    Text {
                                        id: connectionStatus
                                        text: isConnected ? qsTr("已连接") : qsTr("未连接")
                                        font.bold: true
                                        color: "#333"

                                        Layout.alignment: Qt.AlignVCenter
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Button {
                                text: "连接"
                                Layout.fillWidth: true
                                onClicked: EthercatBackend.startTest()
                            }

                            Button {
                                text: "停止"
                                Layout.fillWidth: true
                                onClicked: EthercatBackend.stopTest()
                            }
                        }
                    }
                }
            }

            // ================= 日志 + EEPROM =================
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 16

                // ===== 日志区 =====
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 10
                    color: "white"
                    border.color: "#e6e6e6"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12

                        Label { text: "运行日志"; font.bold: true }

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            TextArea {
                                id: logArea
                                readOnly: true
                                font.family: "Consolas"
                            }
                        }
                    }
                }

                // ===== EEPROM 烧录 =====
                Rectangle {
                    Layout.preferredWidth: 320
                    Layout.fillHeight: true
                    radius: 10
                    color: "white"
                    border.color: "#e6e6e6"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12

                        Label {
                            text: "EEPROM 烧录"
                            font.bold: true
                        }

                        // 从站地址
                        RowLayout {
                            Label { text: "从站地址:" }

                            SpinBox {
                                id: slaveIdBox
                                from: 1
                                to: 255
                                value: 1
                            }
                        }

                        // 文件选择
                        RowLayout {
                            Button {
                                text: "选择 HEX"
                                onClicked: fileDialog.open()
                            }

                            Label {
                                id: fileLabel
                                text: "未选择"
                                elide: Label.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        // 进度条
                        ColumnLayout {
                            spacing: 4

                            ProgressBar {
                                id: progressBar
                                from: 0
                                to: 100
                                value: 0
                                Layout.fillWidth: true
                            }

                            Label {
                                text: progressBar.value + "%"
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }
                        }

                        // 按钮
                        Button {
                            text: "开始烧录"
                            Layout.fillWidth: true
                            enabled: !isConnected
                            onClicked: {
                                progressBar.value = 0
                                EthercatBackend.flashEEprom(
                                            slaveIdBox.value,
                                            fileDialog.selectedFile)
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }

    // ================= 文件选择 =================
    FileDialog {
        id: fileDialog
        nameFilters: ["HEX (*.hex)"]

        onAccepted: {
            fileLabel.text = selectedFile
        }
    }

    // ================= 后端信号 =================
    Connections {
        target: EthercatBackend

        function onLogAppend(line) {
            logArea.append(line)
        }

        function onConnectedUpdated(status) {
            isConnected = (status === 1)
        }

        // 进度更新
        function onFlashProgress(percent) {
            progressBar.value = percent
        }

        // 烧录结果
        function onFlashFinished(success, msg) {
            logArea.text = success ? "烧录成功: " + msg
                                        : "烧录失败: " + msg
        }
    }
}
