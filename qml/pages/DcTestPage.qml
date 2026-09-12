pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root

    property var controller
    property var sessionUi
    property var theme

    readonly property var diagnostics: controller && controller.diagnostics
                                               ? controller.diagnostics
                                               : ({})
    readonly property bool dcOwned: sessionUi && sessionUi.modeKey === "dc"
    readonly property bool occupiedByOtherMode: sessionUi
                                                && sessionUi.sessionActive
                                                && !dcOwned
    readonly property bool canStart: controller
                                     && sessionUi
                                     && sessionUi.idle
                                     && sessionUi.nicReady
                                     && !controller.active
    readonly property bool canStop: controller && controller.active && dcOwned

    function valueText(key, suffix) {
        const value = diagnostics[key]
        if (value === undefined || value === null || value === "")
            return "—"
        return String(value) + (suffix || "")
    }

    function stateTone() {
        if (!controller)
            return "neutral"
        if (controller.state === "failed")
            return "danger"
        if (controller.state === "starting" || controller.state === "stopping")
            return "warning"
        if (controller.state === "running")
            return "success"
        return "neutral"
    }

    function slaveTone(slave) {
        if (slave.shutdownAttempted)
            return slave.shutdownVerified ? "success" : "warning"
        if (!slave.hasDc)
            return "neutral"
        if (slave.alStatusCode)
            return "danger"
        if (slave.sync0Verified)
            return "success"
        if (slave.sync0Requested)
            return "warning"
        return "neutral"
    }

    function slaveStatusText(slave) {
        if (slave.shutdownAttempted)
            return slave.shutdownVerified ? qsTr("SYNC0 已关闭") : qsTr("关闭未确认")
        if (!slave.hasDc)
            return qsTr("非 DC 从站")
        if (slave.sync0Verified)
            return qsTr("SYNC0 已验证")
        if (slave.sync0Requested)
            return qsTr("等待验证")
        return qsTr("DC 可用")
    }

    function hex16(value) {
        return ("0000" + Number(value || 0).toString(16).toUpperCase()).slice(-4)
    }

    ScrollView {
        id: pageScroll
        objectName: "dcPageScroll"
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout {
            width: pageScroll.availableWidth
            spacing: root.theme.space12

            PanelCard {
                Layout.fillWidth: true
                Layout.margins: root.theme.space2
                theme: root.theme
                padding: root.theme.space16

                ColumnLayout {
                    width: parent.width
                    spacing: root.theme.space12

                    GridLayout {
                        Layout.fillWidth: true
                        columns: width >= 700 ? 2 : 1
                        columnSpacing: root.theme.space12
                        rowSpacing: root.theme.space8

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: root.theme.space4

                            Label {
                                Layout.fillWidth: true
                                text: qsTr("DC 同步测试")
                                color: root.theme.textPrimary
                                font.pixelSize: root.theme.fontTitle
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            Label {
                                objectName: "dcSubtitle"
                                Layout.fillWidth: true
                                text: qsTr("简单检查 DC 状态与主机周期质量；普通操作系统下结果仅供调试参考。")
                                color: root.theme.textSecondary
                                wrapMode: Text.WordWrap
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            spacing: root.theme.space8

                            StatusBadge {
                                id: historyBadge
                                objectName: "dcHistoryBadge"
                                visible: root.controller ? root.controller.historical : false
                                theme: root.theme
                                text: qsTr("历史结果")
                                tone: "info"
                                compact: true
                            }

                            StatusBadge {
                                theme: root.theme
                                text: root.occupiedByOtherMode
                                      ? qsTr("总线被其他任务占用")
                                      : root.controller
                                        ? root.controller.statusText
                                        : qsTr("控制器不可用")
                                tone: root.occupiedByOtherMode ? "warning" : root.stateTone()
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: root.theme.divider
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: root.theme.space8
                        visible: root.controller
                                 && (root.controller.active || root.controller.hasResults)

                        StatusBadge {
                            objectName: "dcConfiguredBadge"
                            theme: root.theme
                            text: root.diagnostics.configured
                                  ? qsTr("配置回读成功")
                                  : qsTr("配置未验证")
                            tone: root.diagnostics.configured ? "success" : "warning"
                            compact: true
                        }

                        StatusBadge {
                            objectName: "dcOperationalBadge"
                            theme: root.theme
                            text: root.diagnostics.operational
                                  ? qsTr("通信正常")
                                  : qsTr("通信未进入 OP")
                            tone: root.diagnostics.operational ? "success" : "warning"
                            compact: true
                        }
                    }

                    GridLayout {
                        id: controlGrid
                        objectName: "dcControlGrid"
                        Layout.fillWidth: true
                        columns: width >= 720 ? 2 : 1
                        columnSpacing: root.theme.space20
                        rowSpacing: root.theme.space12

                        GridLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            columns: 2
                            columnSpacing: root.theme.space8
                            rowSpacing: root.theme.space8

                            Label {
                                text: qsTr("周期 (μs)")
                                color: root.theme.textSecondary
                            }
                            SpinBox {
                                id: cycleInput
                                objectName: "dcCycleInput"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                from: 250
                                to: 1000000
                                value: 2000
                                editable: true
                                enabled: root.controller && !root.controller.active
                            }

                            Label {
                                text: qsTr("相移 (μs)")
                                color: root.theme.textSecondary
                            }
                            SpinBox {
                                id: shiftInput
                                objectName: "dcShiftInput"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                from: -cycleInput.value + 1
                                to: cycleInput.value - 1
                                value: 0
                                editable: true
                                enabled: root.controller && !root.controller.active
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: root.theme.space8

                            Label {
                                Layout.fillWidth: true
                                text: root.occupiedByOtherMode
                                      ? qsTr("请先结束当前总线任务。")
                                      : !root.sessionUi || !root.sessionUi.nicReady
                                        ? qsTr("请选择可用网卡后再启动。")
                                        : root.controller && root.controller.active
                                          ? qsTr("参数已锁定；停止后可修改。")
                                          : qsTr("测试独占当前网卡，停止后保留最后一次结果。")
                                color: root.occupiedByOtherMode
                                       ? root.theme.warningText
                                       : root.theme.textMuted
                                wrapMode: Text.WordWrap
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: root.theme.space8

                                AppButton {
                                    id: startButton
                                    objectName: "dcStartButton"
                                    Layout.fillWidth: true
                                    theme: root.theme
                                    text: qsTr("启动 DC 测试")
                                    variant: "primary"
                                    busy: root.controller ? root.controller.state === "starting" : false
                                    busyText: qsTr("正在启动")
                                    actionEnabled: root.canStart
                                    onClicked: root.controller.start(cycleInput.value, shiftInput.value)
                                }

                                AppButton {
                                    id: stopButton
                                    objectName: "dcStopButton"
                                    Layout.fillWidth: true
                                    theme: root.theme
                                    text: qsTr("停止")
                                    variant: "danger"
                                    busy: root.controller ? root.controller.state === "stopping" : false
                                    busyText: qsTr("正在停止")
                                    actionEnabled: root.canStop
                                    onClicked: root.controller.stop()
                                }

                                AppButton {
                                    objectName: "dcResetButton"
                                    theme: root.theme
                                    text: qsTr("重置统计")
                                    variant: "secondary"
                                    actionEnabled: root.controller
                                                   && root.controller.hasResults
                                                   && !root.controller.busy
                                    onClicked: root.controller.resetStatistics()
                                }
                            }
                        }
                    }
                }
            }

            Label {
                objectName: "dcErrorBanner"
                Layout.fillWidth: true
                Layout.leftMargin: root.theme.space2
                Layout.rightMargin: root.theme.space2
                visible: root.diagnostics.error !== undefined
                         && String(root.diagnostics.error).length > 0
                text: qsTr("运行错误：") + (visible ? String(root.diagnostics.error) : "")
                color: root.theme.dangerText
                wrapMode: Text.WrapAnywhere
                padding: root.theme.space12
                background: Rectangle {
                    radius: root.theme.radiusMedium
                    color: root.theme.dangerBackground
                    border.width: 1
                    border.color: root.theme.dangerBorder
                }
            }

            Label {
                objectName: "dcCleanupBanner"
                Layout.fillWidth: true
                Layout.leftMargin: root.theme.space2
                Layout.rightMargin: root.theme.space2
                visible: root.diagnostics.cleanupWarning !== undefined
                         && String(root.diagnostics.cleanupWarning).length > 0
                text: qsTr("清理警告：") + (visible ? String(root.diagnostics.cleanupWarning) : "")
                color: root.theme.warningText
                wrapMode: Text.WrapAnywhere
                padding: root.theme.space12
                background: Rectangle {
                    radius: root.theme.radiusMedium
                    color: root.theme.warningBackground
                    border.width: 1
                    border.color: root.theme.warningBorder
                }
            }

            GridLayout {
                id: metricsGrid
                objectName: "dcMetricsGrid"
                Layout.fillWidth: true
                Layout.leftMargin: root.theme.space2
                Layout.rightMargin: root.theme.space2
                columns: width >= 1040 ? 4 : width >= 520 ? 2 : 1
                columnSpacing: root.theme.space10
                rowSpacing: root.theme.space10

                Repeater {
                    model: [
                        { label: qsTr("DC 时间 (ns)"), value: root.diagnostics.dcTimeValid ? root.valueText("dcTimeNs") : "—", objectName: "dcTimeValue" },
                        { label: qsTr("周期计数"), value: root.valueText("cycleCount"), objectName: "dcCycleCountValue" },
                        { label: qsTr("WKC"), value: root.diagnostics.wkc === undefined || root.diagnostics.expectedWkc === undefined
                                                              ? "—"
                                                              : String(root.diagnostics.wkc) + " / " + String(root.diagnostics.expectedWkc) },
                        { label: qsTr("WKC 错误"), value: root.valueText("wkcErrorCount"), objectName: "dcWkcErrorCountValue" },
                        { label: qsTr("丢失周期"), value: root.valueText("missedCycles"), objectName: "dcMissedCyclesValue" },
                        { label: qsTr("主机周期 (ns)"), value: root.valueText("actualCycleNs"), objectName: "dcActualCycleValue" },
                        { label: qsTr("主机最短周期 (ns)"), value: root.valueText("minCycleNs") },
                        { label: qsTr("主机最长周期 (ns)"), value: root.valueText("maxCycleNs") },
                        { label: qsTr("主机周期最大偏差 (ns)"), value: root.valueText("maxDeviationNs") },
                        { label: qsTr("主机相位误差 (ns)"), value: root.diagnostics.phaseValid ? root.valueText("phaseErrorNs") : "—", objectName: "dcPhaseErrorValue" },
                        { label: qsTr("主机周期校正量 (ns)"), value: root.diagnostics.phaseValid ? root.valueText("correctionNs") : "—", objectName: "dcCorrectionValue" },
                        { label: qsTr("DC 从站"), value: root.valueText("dcSlaveCount") },
                        { label: qsTr("参考从站"), value: root.valueText("referenceSlave") },
                        { label: qsTr("配置周期 (ns)"), value: root.valueText("cycleNs") }
                    ]

                    delegate: PanelCard {
                        id: metricCard
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 210
                        theme: root.theme
                        padding: root.theme.space12
                        backgroundColor: root.theme.surfaceMuted

                        ColumnLayout {
                            width: parent.width
                            spacing: root.theme.space4

                            Label {
                                Layout.fillWidth: true
                                text: metricCard.modelData.label
                                color: root.theme.textMuted
                                font.pixelSize: root.theme.fontBodySmall
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                objectName: metricCard.modelData.objectName || ""
                                text: metricCard.modelData.value
                                color: root.theme.textPrimary
                                font.pixelSize: root.theme.fontSubtitle
                                font.bold: true
                                elide: Text.ElideRight
                                ToolTip.visible: valueHover.hovered && truncated
                                ToolTip.text: text

                                HoverHandler { id: valueHover }
                            }
                        }
                    }
                }
            }

            PanelCard {
                Layout.fillWidth: true
                Layout.margins: root.theme.space2
                theme: root.theme
                padding: root.theme.space12

                ColumnLayout {
                    width: parent.width
                    spacing: root.theme.space8

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("DC 从站详情")
                            color: root.theme.textPrimary
                            font.pixelSize: root.theme.fontSubtitle
                            font.bold: true
                        }

                        StatusBadge {
                            theme: root.theme
                            text: root.controller ? String(root.controller.slavesModel.length) + qsTr(" 个从站") : qsTr("0 个从站")
                            tone: "info"
                            compact: true
                        }
                    }

                    Label {
                        id: emptySlavesLabel
                        objectName: "dcEmptySlavesLabel"
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? 72 : 0
                        visible: !root.controller || root.controller.slavesModel.length === 0
                        text: root.controller && root.controller.hasResults
                              ? qsTr("本次测试没有可显示的从站。")
                              : qsTr("启动测试后显示 DC 能力与同步状态。")
                        color: root.theme.textMuted
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        id: slaveRepeater
                        objectName: "dcSlaveRepeater"
                        model: root.controller ? root.controller.slavesModel : []

                        delegate: Rectangle {
                            id: slaveRow
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            implicitHeight: slaveContent.implicitHeight + root.theme.space16
                            radius: root.theme.radiusSmall
                            color: root.theme.surfaceMuted
                            border.width: 1
                            border.color: root.theme.border

                            ColumnLayout {
                                id: slaveContent
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: root.theme.space10
                                anchors.rightMargin: root.theme.space10
                                spacing: root.theme.space6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: root.theme.space8

                                    Label {
                                        text: "#" + String(slaveRow.modelData.address)
                                        color: root.theme.accentText
                                        font.bold: true
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: slaveRow.modelData.name || qsTr("未命名从站")
                                        color: root.theme.textPrimary
                                        font.bold: true
                                        elide: Text.ElideRight
                                        ToolTip.visible: slaveNameHover.hovered && truncated
                                        ToolTip.text: text

                                        HoverHandler { id: slaveNameHover }
                                    }

                                    StatusBadge {
                                        objectName: "dcSlaveStatus-" + String(slaveRow.modelData.address)
                                        theme: root.theme
                                        text: root.slaveStatusText(slaveRow.modelData)
                                        tone: root.slaveTone(slaveRow.modelData)
                                        compact: true
                                    }
                                }

                                GridLayout {
                                    id: slaveDetails
                                    objectName: "dcSlaveDetails-" + String(slaveRow.modelData.address)
                                    Layout.fillWidth: true
                                    columns: width >= 660 ? 4 : 2
                                    columnSpacing: root.theme.space12
                                    rowSpacing: root.theme.space4

                                    Label { text: qsTr("周期 ") + String(slaveRow.modelData.cycleNs) + " ns"; color: root.theme.textSecondary }
                                    Label { text: qsTr("相移 ") + String(slaveRow.modelData.shiftNs) + " ns"; color: root.theme.textSecondary }
                                    Label { text: qsTr("传播延迟 ") + String(slaveRow.modelData.propagationDelayNs) + " ns"; color: root.theme.textSecondary }
                                }

                                Label {
                                    objectName: "dcSlaveAl-" + String(slaveRow.modelData.address)
                                    Layout.fillWidth: true
                                    text: qsTr("AL 0x") + root.hex16(slaveRow.modelData.state)
                                          + qsTr(" · 错误 0x") + root.hex16(slaveRow.modelData.alStatusCode)
                                          + " · "
                                          + (slaveRow.modelData.alStatusText && slaveRow.modelData.alStatusText.length > 0
                                             ? slaveRow.modelData.alStatusText
                                             : qsTr("无描述"))
                                    color: slaveRow.modelData.alStatusCode ? root.theme.dangerText : root.theme.textMuted
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: root.theme.space2 }
        }
    }
}
