import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property var theme
    property var sessionUi
    readonly property bool isConnected: sessionUi
                                                ? sessionUi.mitConnected
                                                : EthercatBackend.connected
                                                  && EthercatBackend.sessionMode === "MIT参数调试"
    readonly property bool occupiedByOtherMode: sessionUi
                                                ? sessionUi.sessionActive && !sessionUi.mitConnected
                                                : EthercatBackend.sessionActive
                                                  && EthercatBackend.sessionMode !== "MIT参数调试"

    signal connectionChanged(bool connected)
    signal errorRequested(string message)

    onIsConnectedChanged: connectionChanged(isConnected)

    Rectangle {
        anchors.fill: parent
        color: theme.pageBackground

        ScrollView {
            id: pageScroll
            anchors.fill: parent
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: pageScroll.availableWidth
                spacing: 12

                PanelCard {
                    Layout.fillWidth: true
                    theme: root.theme
                    padding: 16

                    GridLayout {
                        anchors.fill: parent
                        columns: root.width >= 760 ? 2 : 1
                        columnSpacing: 20
                        rowSpacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Label {
                                text: qsTr("MIT 电机控制")
                                color: theme.textPrimary
                                font.pixelSize: 18
                                font.bold: true
                            }

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("当前模式固定连接地址 1 的单块 MIT 从站。参数与原始帧在切换页面后仍会保留。")
                                color: theme.textSecondary
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            spacing: 10

                            StatusBadge {
                                theme: root.theme
                                text: root.isConnected ? qsTr("MIT 已连接")
                                                       : root.occupiedByOtherMode
                                                         ? qsTr("总线被其他任务占用")
                                                         : qsTr("未连接")
                                tone: root.isConnected ? "success"
                                                       : root.occupiedByOtherMode ? "warning" : "danger"
                            }

                            AppButton {
                                theme: root.theme
                                text: root.isConnected ? qsTr("断开连接") : qsTr("连接主站")
                                variant: root.isConnected ? "secondary" : "primary"
                                actionEnabled: root.isConnected
                                               || (!root.occupiedByOtherMode
                                                   && root.sessionUi
                                                   && root.sessionUi.nicReady)
                                onClicked: {
                                    if (root.isConnected)
                                        EthercatBackend.exitMitSlaveDebugMode()
                                    else
                                        EthercatBackend.enterMitSlaveDebugMode()
                                }
                            }
                        }
                    }
                }

                MITConfigPanel {
                    Layout.fillWidth: true
                    Layout.minimumHeight: implicitHeight
                    theme: root.theme
                    isConnected: root.isConnected
                    onErrorRequested: message => root.errorRequested(message)
                }
            }
        }
    }
}
