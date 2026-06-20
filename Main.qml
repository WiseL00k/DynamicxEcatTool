import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1200
    height: 900
    visible: true
    title: qsTr("Dynamicx EtherCAT Tool")

    property bool isConnected: false

    function isDarkColor(c) {
        return (0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b) < 0.5
    }

    readonly property bool darkMode: isDarkColor(systemPalette.window)
    readonly property QtObject theme: appTheme

    SystemPalette {
        id: systemPalette
        colorGroup: SystemPalette.Active
    }

    QtObject {
        id: appTheme

        readonly property bool dark: root.darkMode

        readonly property color pageBackground: dark ? "#111827" : "#f4f6f8"
        readonly property color surface: dark ? "#182231" : "#ffffff"
        readonly property color surfaceMuted: dark ? "#202b3a" : "#fafafa"
        readonly property color inputBackground: dark ? "#0f1724" : "#ffffff"
        readonly property color controlBackground: dark ? "#253244" : "#f8fafc"

        readonly property color border: dark ? "#344256" : "#e6e6e6"
        readonly property color borderStrong: dark ? "#42516a" : "#e0e0e0"

        readonly property color textPrimary: dark ? "#eef2f7" : "#1f2937"
        readonly property color textSecondary: dark ? "#c2cad7" : "#4b5563"
        readonly property color textMuted: dark ? "#93a0b3" : "#6b7280"

        readonly property color selectedBackground: dark ? "#173553" : "#e3f2fd"
        readonly property color selectedBorder: dark ? "#256a9f" : "#90caf9"
        readonly property color accent: dark ? "#60a5fa" : "#2563eb"
        readonly property color accentHover: dark ? "#3b82f6" : "#1d4ed8"
        readonly property color accentText: dark ? "#bfdbfe" : "#1d4ed8"
        readonly property color textOnAccent: dark ? "#0b1220" : "#ffffff"

        readonly property color success: dark ? "#4ade80" : "#4caf50"
        readonly property color successText: dark ? "#86efac" : "#2e7d32"
        readonly property color successBackground: dark ? "#123321" : "#e8f5e9"
        readonly property color successBorder: dark ? "#2d6a44" : "#81c784"

        readonly property color danger: dark ? "#f87171" : "#f44336"
        readonly property color dangerText: dark ? "#fca5a5" : "#d32f2f"
        readonly property color dangerBackground: dark ? "#3b171a" : "#ffebee"
        readonly property color dangerBorder: dark ? "#7f2d33" : "#e57373"

        readonly property string fontFamily: ApplicationFontFamily
    }

    palette.window: appTheme.pageBackground
    palette.windowText: appTheme.textPrimary
    palette.base: appTheme.inputBackground
    palette.alternateBase: appTheme.surfaceMuted
    palette.text: appTheme.textPrimary
    palette.button: appTheme.controlBackground
    palette.buttonText: appTheme.textPrimary
    palette.highlight: appTheme.accent
    palette.highlightedText: appTheme.textOnAccent
    palette.placeholderText: appTheme.textMuted
    palette.toolTipBase: appTheme.surface
    palette.toolTipText: appTheme.textPrimary
    palette.link: appTheme.accent
    palette.linkVisited: appTheme.accent

    color: appTheme.pageBackground

    ErrorPopup {
        id: errorPopup
        theme: appTheme
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
                color: appTheme.surface
                border.color: appTheme.border

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
                                color: tabBar.currentIndex === 0 ? appTheme.selectedBackground : "transparent"
                                border.color: tabBar.currentIndex === 0 ? appTheme.selectedBorder : "transparent"
                            }

                            contentItem: Text {
                                text: tabTextBtn.text
                                font: tabTextBtn.font
                                color: tabBar.currentIndex === 0 ? appTheme.accentText : appTheme.textSecondary
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        TabButton {
                            id: tabDebugBtn
                            text: qsTr("调试界面")

                            font.pixelSize: Math.max(12, Math.min(16, height * 0.4))
                            background: Rectangle {
                                implicitHeight: 36
                                radius: 6
                                color: tabBar.currentIndex === 1 ? appTheme.selectedBackground : "transparent"
                                border.color: tabBar.currentIndex === 1 ? appTheme.selectedBorder : "transparent"
                            }

                            contentItem: Text {
                                text: tabDebugBtn.text
                                font: tabDebugBtn.font
                                color: tabBar.currentIndex === 1 ? appTheme.accentText : appTheme.textSecondary
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        TabButton {
                            id: tabParamsBtn
                            text: qsTr("参数界面")

                            font.pixelSize: Math.max(12, Math.min(16, height * 0.4))
                            background: Rectangle {
                                implicitHeight: 36
                                radius: 6
                                color: tabBar.currentIndex === 2 ? appTheme.selectedBackground : "transparent"
                                border.color: tabBar.currentIndex === 2 ? appTheme.selectedBorder : "transparent"
                            }

                            contentItem: Text {
                                text: tabParamsBtn.text
                                font: tabParamsBtn.font
                                color: tabBar.currentIndex === 2 ? appTheme.accentText : appTheme.textSecondary
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Rectangle {
                        width: 120
                        height: 28
                        radius: 6
                        visible: root.isConnected
                        color: appTheme.successBackground
                        border.color: appTheme.successBorder

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("EtherCAT 已连接")
                            font.pixelSize: 12
                            color: appTheme.successText
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: appTheme.surface
                border.color: appTheme.border

                StackLayout {
                    id: pageStack
                    anchors.fill: parent
                    anchors.margins: 10
                    currentIndex: tabBar.currentIndex

                    TestPage {
                        theme: appTheme
                        isConnected: root.isConnected
                        onConnectionChanged: root.isConnected = connected
                    }

                    DebugPage {
                        theme: appTheme
                        isConnected: root.isConnected
                        onConnectionChanged: root.isConnected = connected
                    }

                    ParamsConfigPage {
                        theme: appTheme
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
                        color: appTheme.textMuted
                        opacity: 0.55
                    }

                    // 分隔
                    Text {
                        text: "|"
                        color: appTheme.textMuted
                        opacity: 0.25
                        font.pixelSize: 11
                    }

                    // 作者
                    Text {
                        text: "author: wiselook"
                        font.pixelSize: 11
                        color: appTheme.textMuted
                        opacity: 0.35
                    }

                    // GitHub
                    Text {
                        text: "github: https://github.com/WiseL00k"
                        font.pixelSize: 11
                        color: appTheme.textMuted
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
                        color: root.isConnected ? appTheme.success : appTheme.danger
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
