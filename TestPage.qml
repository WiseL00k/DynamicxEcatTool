import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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

            // ===== 顶部控制区 =====
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
                                text: qsTr("刷新网卡")
                                onClicked: EthercatBackend.refreshNics()
                            }

                            Label {
                                text: qsTr("可用网卡")
                                font.bold: true
                                font.pixelSize: 14
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
                                clip: true
                                spacing: 4

                                model: EthercatBackend ? EthercatBackend.nicList : []

                                ScrollBar.vertical: ScrollBar {
                                    policy: ScrollBar.AsNeeded
                                }

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
                                            EthercatBackend.changedSelectedNic(
                                                        index)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        Layout.preferredWidth: 80
                        Layout.fillWidth: true
                    }

                    // ===== 操作区 =====
                    ColumnLayout {
                        Layout.fillHeight: true
                        Layout.preferredWidth: 240
                        spacing: 18

                        // ===== 标题 =====
                        Label {
                            text: qsTr("EtherCAT 测试")
                            font.pixelSize: 16
                            font.bold: true
                        }

                        // ===== 连接状态 =====
                        Rectangle {
                            id: statusBox

                            Layout.fillWidth: true
                            Layout.preferredHeight: 36

                            radius: 6
                            color: isConnected ? "#e8f5e9" : "#ffebee"
                            border.color: isConnected ? "#81c784" : "#e57373"
                            border.width: 1

                            Row {
                                anchors.centerIn: parent
                                spacing: 8

                                // 状态灯
                                Rectangle {
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: isConnected ? "#4caf50" : "#f44336"
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                // 状态文字
                                Label {
                                    id: connectionStatus
                                    text: isConnected ? qsTr("已连接") : qsTr("未连接")
                                    font.pixelSize: 13
                                    font.bold: true
                                    color: "#333"
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }

                        // ===== 操作按钮 =====
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Button {
                                Layout.fillWidth: true
                                text: qsTr("开始测试")
                                enabled: nicList.currentIndex >= 0

                                onClicked: {
                                    EthercatBackend.startTest()
                                }
                            }

                            Button {
                                Layout.fillWidth: true
                                text: qsTr("停止")

                                onClicked: {
                                    EthercatBackend.stopTest()
                                }
                            }
                        }

                        // ===== 填充空间 =====
                        Item {
                            Layout.fillHeight: true
                        }
                    }
                }
            }

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
                    spacing: 6

                    Label {
                        text: qsTr("运行日志")
                        font.bold: true
                        font.pixelSize: 14
                    }

                    Rectangle {

                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        radius: 6
                        color: "#fafafa"
                        border.color: "#e0e0e0"

                        ScrollView {

                            anchors.fill: parent

                            ScrollBar.vertical: ScrollBar {}

                            TextArea {

                                id: logArea

                                readOnly: true
                                wrapMode: TextArea.NoWrap
                                font.family: "Consolas"
                                font.pixelSize: 13

                                padding: 10

                                background: null
                            }
                        }
                    }
                }
            }
        }
    }

    // ===== 信号连接 =====
    Connections {

        target: EthercatBackend

        function onLogUpdated(line) {
            logArea.text = line
        }

        function onLogAppend(line) {
            logArea.append(line)
        }

        function onConnectedUpdated(status) {

            switch (status) {
            case 0:
                isConnected = false
                connectionStatus.text = "未连接"
                break
            case 1:
                isConnected = true
                connectionStatus.text = "已连接"
                break
            }
            connectionChanged(isConnected)
        }
    }

    Component.onCompleted: {

        if (!EthercatBackend)
            return

        if (EthercatBackend.nicList.length === 0)
            return

        const nicName = EthercatBackend.nicList[0]

        nicList.currentIndex = 0

        EthercatBackend.changedSelectedNic(nicName)
    }
}
