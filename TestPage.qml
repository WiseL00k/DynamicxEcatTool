import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {

    property var theme
    property bool isConnected: false
    signal connectionChanged(bool connected)

    Rectangle {
        anchors.fill: parent
        color: theme.pageBackground

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 16

            // ================= 顶部控制区 =================
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 170
                radius: 10
                color: theme.surface
                border.color: theme.border

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
                            color: theme.inputBackground
                            border.color: theme.borderStrong

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

                                    color: ListView.isCurrentItem ? theme.selectedBackground : "transparent"
                                    border.color: ListView.isCurrentItem ? theme.selectedBorder : "transparent"

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 12
                                        text: modelData
                                        color: theme.textPrimary
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

                                ScrollBar.vertical: ScrollBar {
                                    policy: ScrollBar.AsNeeded
                                    anchors.right: parent.right
                                }
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                    }

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
                                color: isConnected ? theme.successBackground : theme.dangerBackground
                                border.color: isConnected ? theme.successBorder : theme.dangerBorder

                                RowLayout {
                                    anchors.centerIn: parent
                                    spacing: 8

                                    Rectangle {
                                        width: 10
                                        height: 10
                                        radius: 5
                                        color: isConnected ? theme.success : theme.danger

                                        Layout.alignment: Qt.AlignVCenter
                                    }

                                    Text {
                                        id: connectionStatus
                                        text: isConnected ? qsTr("已连接") : qsTr(
                                                                "未连接")
                                        font.bold: true
                                        color: theme.textPrimary

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
                    color: theme.surface
                    border.color: theme.border

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12

                        Label {
                            text: "运行日志"
                            font.bold: true
                        }

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            TextArea {
                                id: logArea
                                readOnly: true
                                font.family: theme.fontFamily
                            }
                        }
                    }
                }

                ColumnLayout {
                    //====================================================
                    // 固件烧录
                    //====================================================
                    Rectangle {
                        Layout.preferredWidth: 320
                        Layout.fillHeight: true
                        radius: 10
                        color: theme.surface
                        border.color: theme.border

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12

                            Label {
                                text: "固件烧录"
                                font.bold: true
                            }

                            RowLayout {
                                Label {
                                    text: "从站地址:"
                                }

                                SpinBox {
                                    id: firmwareSlaveIdBox
                                    from: 1
                                    to: 255
                                    value: 1
                                }
                            }

                            RowLayout {
                                Button {
                                    text: "选择 BIN"
                                    onClicked: firmwareFileDialog.open()
                                }

                                Label {
                                    id: firmwareFileLabel
                                    text: "未选择"
                                    elide: Label.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            ColumnLayout {
                                spacing: 4

                                ProgressBar {
                                    id: firmwareProgressBar
                                    from: 0
                                    to: 100
                                    value: 0
                                    Layout.fillWidth: true
                                }

                                Label {
                                    text: firmwareProgressBar.value + "%"
                                    horizontalAlignment: Text.AlignHCenter
                                    Layout.fillWidth: true
                                }
                            }

                            Button {
                                text: "开始烧录"
                                Layout.fillWidth: true
                                enabled: !isConnected

                                onClicked: {
                                    firmwareProgressBar.value = 0

                                    EthercatBackend.flashFirmware(
                                                firmwareSlaveIdBox.value,
                                                firmwareFileDialog.selectedFile)
                                }
                            }

                            Item {
                                Layout.fillHeight: true
                            }
                        }
                    }

                    // ===== EEPROM 烧录 =====
                    Rectangle {
                        Layout.preferredWidth: 320
                        Layout.fillHeight: true
                        radius: 10
                        color: theme.surface
                        border.color: theme.border

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
                                Label {
                                    text: "从站地址:"
                                }

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
                                    onClicked: eepromFileDialog.open()
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
                                    id: eepromProgressBar
                                    from: 0
                                    to: 100
                                    value: 0
                                    Layout.fillWidth: true
                                }

                                Label {
                                    text: eepromProgressBar.value + "%"
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
                                    eepromProgressBar.value = 0
                                    EthercatBackend.flashEEprom(
                                                slaveIdBox.value,
                                                eepromFileDialog.selectedFile)
                                }
                            }

                            Item {
                                Layout.fillHeight: true
                            }
                        }
                    }
                }
            }
        }
    }

    // ================= 文件选择 =================
    FileDialog {
        id: eepromFileDialog
        nameFilters: ["HEX (*.hex)"]

        onAccepted: {
            fileLabel.text = selectedFile
        }
    }

    FileDialog {
        id: firmwareFileDialog

        title: "选择固件 BIN 文件"
        nameFilters: ["BIN files (*.bin)"]

        onAccepted: {
            var path = selectedFile.toString()
            firmwareFileLabel.text = path.substring(path.lastIndexOf("/") + 1)
        }
    }

    // ================= 后端信号 =================
    Connections {
        target: EthercatBackend

        function onLogUpdated(line) {
            logArea.text = line
        }

        function onLogAppend(line) {
            logArea.append(line)
        }

        function onConnectedUpdated(status) {
            isConnected = (status === 1)
        }

        // 进度更新
        function onFlashProgress(type, percent) {
            if (type === "eeprom") {
                eepromProgressBar.value = percent
            } else if (type === "firmware") {
                firmwareProgressBar.value = percent
            }
        }

        // 烧录结果
        function onFlashFinished(type, success, msg) {
            if (type === "eeprom") {
                eepromLog.text = success ? "EEPROM成功: " + msg : "EEPROM失败: " + msg
            } else if (type === "firmware") {
                firmwareLog.text = success ? "固件成功: " + msg : "固件失败: " + msg
            }
        }
    }
}
