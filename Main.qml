import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

ApplicationWindow {
    id: root

    width: 1280
    height: 720
    minimumWidth: 960
    minimumHeight: 640
    visible: true
    title: qsTr("Dynamicx EtherCAT Tool")

    property int currentPageIndex: 0
    readonly property var sessionUi: sessionAdapter
    readonly property string currentPageKey: ["test", "debug", "params", "bus"][currentPageIndex]
    readonly property string currentPageTitle: [
        qsTr("测试与烧录"),
        qsTr("设备调试"),
        qsTr("MIT 参数"),
        qsTr("总线配置")
    ][currentPageIndex]
    readonly property bool compactWindow: width < 1120
    readonly property bool navigationCollapsed: compactWindow || appearanceSettings.navigationCollapsed

    function isDarkColor(colorValue) {
        return (0.2126 * colorValue.r
                + 0.7152 * colorValue.g
                + 0.0722 * colorValue.b) < 0.5
    }

    function persistAppearance() {
        appearanceSettings.sync()
    }

    SystemPalette {
        id: systemPalette
        colorGroup: SystemPalette.Active
    }

    Settings {
        id: appearanceSettings
        location: StandardPaths.writableLocation(StandardPaths.GenericConfigLocation)
                  + "/DynamicxEcatTool-ui.ini"
        category: "appearance"
        property string themeMode: "system"
        property bool navigationCollapsed: false
        property bool logDockExpanded: false
    }

    DesignTokens {
        id: appTheme
        themeMode: appearanceSettings.themeMode
        systemDark: root.isDarkColor(systemPalette.window)
        fontFamily: ApplicationFontFamily
    }

    SessionUiAdapter {
        id: sessionAdapter
        backend: EthercatBackend
    }

    LogUiAdapter {
        id: logAdapter
        backend: EthercatBackend
        contextKey: root.currentPageKey
        sessionActive: sessionAdapter.sessionActive
        sessionMode: sessionAdapter.sessionMode
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
    palette.toolTipBase: appTheme.surfaceRaised
    palette.toolTipText: appTheme.textPrimary
    palette.link: appTheme.accent
    palette.linkVisited: appTheme.accent
    color: appTheme.pageBackground

    ErrorPopup {
        id: errorPopup
        theme: appTheme
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: navigationRail
            Layout.fillHeight: true
            Layout.preferredWidth: root.navigationCollapsed
                                   ? appTheme.navigationCollapsedWidth
                                   : appTheme.navigationExpandedWidth
            Layout.minimumWidth: Layout.preferredWidth
            Layout.maximumWidth: Layout.preferredWidth
            color: appTheme.surface
            border.width: 0

            Behavior on Layout.preferredWidth {
                NumberAnimation {
                    duration: appTheme.animationNormal
                    easing.type: Easing.OutCubic
                }
            }

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: appTheme.border
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: appTheme.space12
                spacing: appTheme.space8

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44

                    RowLayout {
                        anchors.fill: parent
                        spacing: appTheme.space10

                        Image {
                            source: Qt.resolvedUrl("icons/logo-mark.svg")
                            sourceSize.width: 32
                            sourceSize.height: 32
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                        }

                        Text {
                            visible: !root.navigationCollapsed
                            Layout.fillWidth: true
                            text: qsTr("Dynamicx\nEtherCAT Tool")
                            color: appTheme.textPrimary
                            font.pixelSize: appTheme.fontBody
                            font.weight: Font.DemiBold
                            lineHeight: 0.9
                            elide: Text.ElideRight
                        }

                        AppButton {
                            visible: !root.navigationCollapsed
                            theme: appTheme
                            variant: "ghost"
                            icon.source: Qt.resolvedUrl("icons/chevron-left.svg")
                            accessibleName: qsTr("收起导航")
                            actionEnabled: !root.compactWindow
                            Layout.preferredWidth: 32
                            implicitHeight: 32
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("收起导航")
                            onClicked: {
                                appearanceSettings.navigationCollapsed = true
                                root.persistAppearance()
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: appTheme.divider
                }

                SideNavButton {
                    Layout.fillWidth: true
                    theme: appTheme
                    text: qsTr("测试与烧录")
                    iconSource: Qt.resolvedUrl("icons/test.svg")
                    selected: root.currentPageIndex === 0
                    sessionOwned: sessionAdapter.sessionActive && sessionAdapter.modeKey === "test"
                    collapsed: root.navigationCollapsed
                    onClicked: root.currentPageIndex = 0
                }

                SideNavButton {
                    Layout.fillWidth: true
                    theme: appTheme
                    text: qsTr("设备调试")
                    iconSource: Qt.resolvedUrl("icons/debug.svg")
                    selected: root.currentPageIndex === 1
                    sessionOwned: sessionAdapter.sessionActive && sessionAdapter.modeKey === "debug"
                    collapsed: root.navigationCollapsed
                    onClicked: root.currentPageIndex = 1
                }

                SideNavButton {
                    Layout.fillWidth: true
                    theme: appTheme
                    text: qsTr("MIT 参数")
                    iconSource: Qt.resolvedUrl("icons/motor.svg")
                    selected: root.currentPageIndex === 2
                    sessionOwned: sessionAdapter.sessionActive && sessionAdapter.modeKey === "params"
                    collapsed: root.navigationCollapsed
                    onClicked: root.currentPageIndex = 2
                }

                SideNavButton {
                    Layout.fillWidth: true
                    theme: appTheme
                    text: qsTr("总线配置")
                    iconSource: Qt.resolvedUrl("icons/bus.svg")
                    selected: root.currentPageIndex === 3
                    sessionOwned: sessionAdapter.sessionActive && sessionAdapter.modeKey === "bus"
                    collapsed: root.navigationCollapsed
                    onClicked: root.currentPageIndex = 3
                }

                Item { Layout.fillHeight: true }

                StatusBadge {
                    visible: !root.navigationCollapsed
                    Layout.fillWidth: true
                    theme: appTheme
                    text: sessionAdapter.statusText
                    tone: sessionAdapter.statusTone
                }

                AppButton {
                    visible: root.navigationCollapsed && !root.compactWindow
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 40
                    theme: appTheme
                    variant: "ghost"
                    icon.source: Qt.resolvedUrl("icons/chevron-right.svg")
                    accessibleName: qsTr("展开导航")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("展开导航")
                    onClicked: {
                        appearanceSettings.navigationCollapsed = false
                        root.persistAppearance()
                    }
                }

                Text {
                    visible: !root.navigationCollapsed
                    Layout.fillWidth: true
                    text: qsTr("Dynamicx EtherCAT Tool")
                    color: appTheme.textMuted
                    font.pixelSize: appTheme.fontCaption
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: appTheme.space12
                spacing: appTheme.space8

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: appTheme.topBarHeight
                    radius: appTheme.radiusLarge
                    color: appTheme.surface
                    border.width: 1
                    border.color: appTheme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: appTheme.space16
                        anchors.rightMargin: appTheme.space12
                        spacing: appTheme.space10

                        ColumnLayout {
                            spacing: 0

                            Text {
                                text: root.currentPageTitle
                                color: appTheme.textPrimary
                                font.pixelSize: appTheme.fontTitle
                                font.weight: Font.DemiBold
                            }

                            Text {
                                visible: root.width >= 1180
                                text: qsTr("工程调试工作台")
                                color: appTheme.textMuted
                                font.pixelSize: appTheme.fontCaption
                            }
                        }

                        StatusBadge {
                            theme: appTheme
                            text: sessionAdapter.statusText
                            tone: sessionAdapter.statusTone
                        }

                        Item { Layout.fillWidth: true }

                        NicSelector {
                            Layout.preferredWidth: root.width < 1160 ? 270 : 330
                            Layout.maximumWidth: 350
                            theme: appTheme
                            model: sessionAdapter.nicList
                            currentIndex: sessionAdapter.selectedNicIndex
                            actionEnabled: sessionAdapter.idle
                            busy: sessionAdapter.nicRefreshPending
                            onSelectionRequested: function(index) {
                                sessionAdapter.selectNic(index)
                            }
                            onRefreshRequested: sessionAdapter.refreshNics()
                        }

                        ThemeModeControl {
                            Layout.preferredWidth: 144
                            theme: appTheme
                            mode: appearanceSettings.themeMode
                            onModeRequested: function(mode) {
                                appearanceSettings.themeMode = mode
                                root.persistAppearance()
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    StackLayout {
                        anchors.fill: parent
                        currentIndex: root.currentPageIndex

                        TestPage {
                            id: testPage
                            theme: appTheme
                            sessionUi: root.sessionUi
                            onErrorRequested: function(message) {
                                errorPopup.show(message)
                            }
                        }

                        DebugPage {
                            id: debugPage
                            theme: appTheme
                            sessionUi: root.sessionUi
                            onErrorRequested: function(message) {
                                errorPopup.show(message)
                            }
                        }

                        ParamsConfigPage {
                            id: paramsPage
                            theme: appTheme
                            sessionUi: root.sessionUi
                            onErrorRequested: function(message) {
                                errorPopup.show(message)
                            }
                        }

                        BusConfigurationPage {
                            id: busPage
                            theme: appTheme
                            sessionUi: root.sessionUi
                            onErrorRequested: function(message) {
                                errorPopup.show(message)
                            }
                        }
                    }
                }

                LogDock {
                    id: logDock
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    theme: appTheme
                    contextName: root.currentPageTitle
                    logText: logAdapter.currentText
                    expanded: appearanceSettings.logDockExpanded
                    expandedHeight: Math.max(120,
                                             Math.min(appTheme.logDockExpandedHeight,
                                                      root.height - 520))
                    onToggleRequested: function(expanded) {
                        appearanceSettings.logDockExpanded = expanded
                        root.persistAppearance()
                    }
                    onClearRequested: logAdapter.clearCurrent()
                }
            }
        }
    }

    Component.onCompleted: {
        if (["system", "light", "dark"].indexOf(appearanceSettings.themeMode) < 0) {
            appearanceSettings.themeMode = "system"
            root.persistAppearance()
        }
        sessionAdapter.refreshNics()
    }

    Connections {
        target: EthercatBackend

        function onSoemErrorOccurred(message) {
            errorPopup.show(message)
        }
    }

}
