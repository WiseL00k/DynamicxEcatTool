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
            spacing: 14

            // =========================
            // 顶部控制区
            // =========================

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                radius: 10
                color: "white"
                border.color: "#e6e6e6"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 24

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

                                id: debugNicList
                                anchors.fill: parent
                                anchors.margins: 4
                                spacing: 4
                                clip: true

                                model: EthercatBackend ? EthercatBackend.nicList : []

                                ScrollBar.vertical: ScrollBar {}

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
                                            debugNicList.currentIndex = index
                                            EthercatBackend.changedSelectedNic(debugNicList.currentIndex)
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

                    ColumnLayout {

                        Layout.fillHeight: true
                        Layout.preferredWidth: 260
                        spacing: 14

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
                                        text: qsTr("未连接")
                                        font.bold: true
                                        color: "#333"

                                        Layout.alignment: Qt.AlignVCenter
                                    }
                                }
                            }

                            Button {
                                text: isConnected ? qsTr("断开连接") : qsTr("连接主站")

                                onClicked: {
                                    if (!isConnected)
                                        EthercatBackend.startCommunication()
                                    else
                                        EthercatBackend.stopCommunication()
                                }
                            }
                        }

                        RowLayout {

                            Layout.fillWidth: true
                            spacing: 8

                            TextField {
                                id: yamlPathField
                                readOnly: true
                                Layout.fillWidth: true
                                placeholderText: qsTr("选择 YAML 配置文件")
                            }

                            Button {
                                text: qsTr("浏览")
                                onClicked: fileDialog.open()
                            }
                        }
                    }
                }
            }

            // =========================
            // 日志 + 电机状态
            // =========================

            Rectangle {

                Layout.fillWidth: true
                Layout.fillHeight: true

                radius: 10
                color: "white"
                border.color: "#e6e6e6"

                RowLayout {

                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 18

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

                    Rectangle {

                        Layout.preferredWidth: 420
                        Layout.fillHeight: true

                        radius: 6
                        color: "#fafafa"
                        border.color: "#e0e0e0"

                        ColumnLayout {

                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10

                            Label {
                                text: qsTr("电机在线状态")
                                font.bold: true
                                font.pixelSize: 14
                            }

                            ListView {

                                id: motorStatusList
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                clip: true
                                spacing: 6

                                model: EthercatBackend ? EthercatBackend.motorStatusList : []

                                ScrollBar.vertical: ScrollBar { id: vbar }

                                delegate: Item {

                                    width: ListView.view.width - (vbar.visible ? vbar.width : 0)

                                    property var style:
                                        type === "slaveHeader"
                                        ? { bg:"#e3f2fd", border:"#90caf9", height:32 }
                                        : { bg:"#ffffff", border:"#e6e6e6", height:52 }

                                    height: style.height

                                    Rectangle {

                                        anchors.fill: parent
                                        radius: 6
                                        color: style.bg
                                        border.color: style.border

                                        RowLayout {

                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 10
                                            visible: type === "motor"

                                            Column {

                                                Layout.fillWidth: true
                                                spacing: 2

                                                Label {
                                                    text: name || ""
                                                    font.bold: true
                                                    color: "#333"
                                                }

                                                Label {
                                                    text: "CAN Bus: " + canBus + "   ID: " + canId
                                                    font.pixelSize: 12
                                                    color: "#666"
                                                }
                                            }

                                            Rectangle {
                                                width: 14
                                                height: 14
                                                radius: 7
                                                color: online ? "#4CAF50" : "#E53935"
                                            }

                                            Label {
                                                text: online ? qsTr("在线") : qsTr("离线")
                                                color: online ? "#4CAF50" : "#E53935"
                                                font.bold: true
                                            }
                                        }

                                        Label {

                                            visible: type === "slaveHeader"

                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left
                                            anchors.leftMargin: 10

                                            text: slaveName || ""
                                            font.bold: true
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        nameFilters: ["YAML files (*.yaml *.yml)"]

        onAccepted: {
            yamlPathField.text = EthercatBackend.changeConfigFilePath(selectedFile)
        }
    }

    Connections {

        target: EthercatBackend

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

        function onLogUpdated(line) {
            logArea.text = line
        }

        function onLogAppend(line) {
            logArea.append(line)
        }
    }
}
