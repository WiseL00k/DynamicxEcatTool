pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root

    property var theme
    property var sessionUi
    property string yamlPath: ""
    property bool connectionRequestPending: false
    signal connectionChanged(bool connected)
    signal errorRequested(string message)

    readonly property bool isConnected: sessionUi ? sessionUi.debugConnected : false
    readonly property bool sessionIdle: sessionUi ? sessionUi.idle : !EthercatBackend.sessionActive

    onIsConnectedChanged: connectionChanged(isConnected)

    function toggleConnection() {
        if (connectionRequestPending)
            return
        connectionRequestPending = true
        connectionRequestTimer.start()
    }

    Timer {
        id: connectionRequestTimer
        interval: 1
        repeat: false
        onTriggered: {
            if (root.isConnected)
                EthercatBackend.stopCommunication()
            else
                EthercatBackend.startCommunication()
            root.connectionRequestPending = false
        }
    }

    ScrollView {
        id: pageScroll
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            width: pageScroll.availableWidth
            spacing: 14

            PanelCard {
                Layout.fillWidth: true
                Layout.margins: 2
                theme: root.theme
                padding: 18

                ColumnLayout {
                    width: parent.width
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Label {
                                text: "配置调试通信"
                                font.pixelSize: 20
                                font.bold: true
                                color: root.theme.textPrimary
                            }
                            Label {
                                Layout.fillWidth: true
                                text: "加载 YAML 从站配置并建立周期通信，在线状态会在下方按从站分组刷新。"
                                color: root.theme.textSecondary
                                wrapMode: Text.WordWrap
                            }
                        }

                        StatusBadge {
                            theme: root.theme
                            text: root.sessionUi ? root.sessionUi.statusText : (root.isConnected ? "调试通信已连接" : "总线空闲")
                            tone: root.sessionUi ? root.sessionUi.statusTone : (root.isConnected ? "success" : "neutral")
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.width >= 760 ? 2 : 1
                        columnSpacing: 16
                        rowSpacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Label {
                                text: "从站配置文件"
                                font.bold: true
                                color: root.theme.textPrimary
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                TextField {
                                    Layout.fillWidth: true
                                    readOnly: true
                                    text: root.yamlPath
                                    placeholderText: "选择 YAML 配置文件"
                                    color: root.yamlPath.length ? root.theme.textPrimary : root.theme.textMuted
                                    selectByMouse: true
                                }

                                AppButton {
                                    theme: root.theme
                                    text: "浏览"
                                    variant: "secondary"
                                    actionEnabled: !root.isConnected && !root.connectionRequestPending
                                    onClicked: yamlFileDialog.open()
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Label {
                                text: "主站会话"
                                font.bold: true
                                color: root.theme.textPrimary
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Label {
                                    Layout.fillWidth: true
                                    text: root.isConnected
                                          ? "通信运行中，可查看设备在线状态。"
                                          : (!root.sessionIdle ? "其他总线会话正在运行。" : "配置就绪后可连接主站。")
                                    color: !root.sessionIdle && !root.isConnected ? root.theme.dangerText : root.theme.textMuted
                                    wrapMode: Text.WordWrap
                                }

                                AppButton {
                                    theme: root.theme
                                    text: root.isConnected ? "断开连接" : "连接主站"
                                    variant: root.isConnected ? "danger" : "primary"
                                    busy: root.connectionRequestPending
                                    busyText: root.isConnected ? "正在断开" : "正在连接"
                                    actionEnabled: root.isConnected
                                                   || (root.sessionIdle
                                                       && root.sessionUi
                                                       && root.sessionUi.nicReady)
                                    onClicked: root.toggleConnection()
                                }
                            }
                        }
                    }
                }
            }

            PanelCard {
                Layout.fillWidth: true
                Layout.margins: 2
                theme: root.theme
                padding: 14

                ColumnLayout {
                    width: parent.width
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Label {
                                text: "设备在线状态"
                                font.pixelSize: 18
                                font.bold: true
                                color: root.theme.textPrimary
                            }
                            Label {
                                text: root.isConnected
                                      ? "状态由 EtherCAT PDO 周期监控刷新"
                                      : "连接后显示配置中的电机与 IMU"
                                color: root.theme.textSecondary
                            }
                        }

                        StatusBadge {
                            theme: root.theme
                            text: root.isConnected ? (EthercatBackend.slaveCount + " 个从站") : "未连接"
                            tone: root.isConnected ? "info" : "neutral"
                            compact: true
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: root.theme.border
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 5

                        Repeater {
                            id: deviceRepeater
                            model: EthercatBackend ? EthercatBackend.deviceStatusList : null

                            delegate: Rectangle {
                                id: deviceRow
                                required property int index
                                required property string type
                                required property string name
                                required property bool online
                                required property int canBus
                                required property int canId
                                required property string slaveName

                                Layout.fillWidth: true
                                Layout.preferredHeight: type === "slaveHeader" ? 34 : 46
                                radius: 6
                                color: type === "slaveHeader"
                                       ? root.theme.selectedBackground
                                       : (index % 2 === 0 ? root.theme.surfaceMuted : root.theme.surface)
                                border.color: type === "slaveHeader" ? root.theme.selectedBorder : root.theme.border

                                Label {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    visible: deviceRow.type === "slaveHeader"
                                    text: deviceRow.slaveName
                                    font.bold: true
                                    color: root.theme.accentText
                                    elide: Text.ElideRight
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    visible: deviceRow.type !== "slaveHeader"
                                    spacing: 10

                                    Label {
                                        Layout.fillWidth: true
                                        text: deviceRow.name
                                        font.bold: true
                                        color: root.theme.textPrimary
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        text: deviceRow.type === "motor"
                                              ? "CAN" + deviceRow.canBus + " · ID " + deviceRow.canId
                                              : "IMU · CAN" + deviceRow.canBus
                                        color: root.theme.textMuted
                                    }

                                    StatusBadge {
                                        theme: root.theme
                                        text: deviceRow.online ? "在线" : "离线"
                                        tone: deviceRow.online ? "success" : "danger"
                                        compact: true
                                    }
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 120
                            visible: deviceRepeater.count === 0
                            text: root.isConnected ? "配置中没有可显示的设备" : "尚未建立调试通信"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: root.theme.textMuted
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 2 }
        }
    }

    FileDialog {
        id: yamlFileDialog
        title: "选择 YAML 配置文件"
        nameFilters: ["YAML files (*.yaml *.yml)"]
        onAccepted: root.yamlPath = EthercatBackend.changeConfigFilePath(selectedFile)
    }
}
