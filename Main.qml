import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    width: 1200
    height: 800
    visible: true
    title: qsTr("Dynamicx EtherCAT Tool")

    color: "#f4f6f8"

    property bool isConnected: false

    ErrorPopup {
        id: errorPopup
    }

    QtObject {
        id: errorManager
        signal show(string msg)
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                radius: 10
                color: "white"
                border.color: "#e6e6e6"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    TabBar {
                        id: tabBar
                        Layout.fillWidth: true

                        background: Rectangle {
                            color: "transparent"
                        }

                        TabButton {
                            id: tabTextBtn
                            text: qsTr("测试界面")

                            font.pixelSize: Math.max(12, Math.min(16, height * 0.4))

                            background: Rectangle {
                                implicitHeight: 36
                                radius: 6
                                color: tabBar.currentIndex === 0 ? "#e3f2fd" : "transparent"
                                border.color: tabBar.currentIndex === 0 ? "#90caf9" : "transparent"
                            }
                        }

                        TabButton {
                            id: tabDebugBtn
                            text: qsTr("调试界面")

                            font.pixelSize: Math.max(12, Math.min(16, height * 0.4))
                            background: Rectangle {
                                implicitHeight: 36
                                radius: 6
                                color: tabBar.currentIndex === 1 ? "#e3f2fd" : "transparent"
                                border.color: tabBar.currentIndex === 1 ? "#90caf9" : "transparent"
                            }
                        }

                        TabButton {
                            id: tabParamsBtn
                            text: qsTr("参数界面")

                            font.pixelSize: Math.max(12, Math.min(16, height * 0.4))
                            background: Rectangle {
                                implicitHeight: 36
                                radius: 6
                                color: tabBar.currentIndex === 2 ? "#e3f2fd" : "transparent"
                                border.color: tabBar.currentIndex === 2 ? "#90caf9" : "transparent"
                            }
                        }
                    }

                    Rectangle {
                        width: 120
                        height: 28
                        radius: 6
                        visible: root.isConnected
                        color: "#e8f5e9"
                        border.color: "#81c784"

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("EtherCAT 已连接")
                            font.pixelSize: 12
                            color: "#2e7d32"
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: "white"
                border.color: "#e6e6e6"

                StackLayout {
                    id: pageStack
                    anchors.fill: parent
                    anchors.margins: 10
                    currentIndex: tabBar.currentIndex

                    TestPage {
                        isConnected: root.isConnected
                        onConnectionChanged: root.isConnected = connected
                    }

                    DebugPage {
                        isConnected: root.isConnected
                        onConnectionChanged: root.isConnected = connected
                    }

                    ParamsConfigPage {
                        isConnected: root.isConnected
                        onConnectionChanged: root.isConnected = connected
                    }

                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                radius: 6
                color: "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 10

                    // 软件名
                    Text {
                        text: qsTr("Dynamicx EtherCAT Tool")
                        font.pixelSize: 11
                        opacity: 0.55
                    }

                    // 分隔
                    Text {
                        text: "|"
                        opacity: 0.25
                        font.pixelSize: 11
                    }

                    // 作者
                    Text {
                        text: "author: wiselook"
                        font.pixelSize: 11
                        opacity: 0.35
                    }

                    // GitHub
                    Text {
                        text: "github: https://github.com/WiseL00k"
                        font.pixelSize: 11
                        opacity: 0.35
                    }

                    // 吃掉剩余空间
                    Item {
                        Layout.fillWidth: true
                    }

                    // 连接状态（最右）
                    Text {
                        text: root.isConnected ? qsTr("● 已连接") : qsTr("● 未连接")
                        font.pixelSize: 11
                        color: root.isConnected ? "#4caf50" : "#f44336"
                        opacity: 0.9
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        EthercatBackend.refreshNicsAsync()
    }

    Connections {
        target: EthercatBackend

        function onSoemErrorOccurred(msg) {
            errorPopup.show(msg)
        }
    }

    Connections {
        target: errorManager

        function onShow(msg) {
            errorPopup.show(msg)
        }
    }
}
