import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root

    required property var theme
    readonly property var explorer: EthercatBackend.busExplorer
    readonly property bool wideLayout: width >= 980

    FolderDialog {
        id: esiFolderDialog
        title: "选择 ESI 目录"
        onAccepted: explorer.esiDirectory = selectedFolder
    }

    Rectangle {
        anchors.fill: parent
        color: theme.pageBackground

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: headerContent.implicitHeight + 24
                Layout.minimumHeight: headerContent.implicitHeight + 24
                radius: 8
                color: theme.surface
                border.color: theme.border

                ColumnLayout {
                    id: headerContent
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: "网卡"
                            font.bold: true
                            color: theme.textPrimary
                        }

                        ComboBox {
                            id: nicBox
                            Layout.fillWidth: true
                            Layout.minimumWidth: 180
                            Layout.maximumWidth: 520
                            model: EthercatBackend.nicList
                            enabled: !explorer.busy && !explorer.scanned
                            hoverEnabled: true
                            ToolTip.visible: hovered && currentText.length > 28
                            ToolTip.text: currentText
                            ToolTip.delay: 500
                            onActivated: EthercatBackend.changedSelectedNic(currentIndex)
                        }

                        Button {
                            text: "刷新"
                            enabled: !explorer.busy && !explorer.scanned
                            onClicked: EthercatBackend.refreshNicsAsync()
                        }

                        Button {
                            text: explorer.busy ? "处理中" : "扫描"
                            highlighted: true
                            enabled: !explorer.busy && !explorer.scanned && nicBox.currentIndex >= 0
                            onClicked: explorer.scan()
                        }

                        Button {
                            text: "重置"
                            enabled: explorer.busy || explorer.scanned || explorer.status !== 0
                            onClicked: explorer.reset()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: "ESI目录"
                            font.bold: true
                            color: theme.textPrimary
                        }

                        TextField {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 180
                            text: explorer.esiDirectory
                            readOnly: true
                            selectByMouse: true
                            hoverEnabled: true
                            color: theme.textPrimary
                            ToolTip.visible: hovered && text.length > 45
                            ToolTip.text: text
                            ToolTip.delay: 500
                        }

                        Button {
                            text: "选择"
                            enabled: !explorer.busy && !explorer.scanned
                            onClicked: esiFolderDialog.open()
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        Layout.preferredHeight: childrenRect.height
                        spacing: 8

                        Label {
                            width: implicitWidth
                            height: 30
                            text: "全站状态"
                            font.bold: true
                            color: theme.textPrimary
                            verticalAlignment: Text.AlignVCenter
                        }

                        Button {
                            width: implicitWidth
                            height: 30
                            text: "INIT"
                            checkable: true
                            checked: explorer.currentState === 1
                            enabled: explorer.scanned && !explorer.busy
                            onClicked: explorer.requestState(1)
                        }

                        Button {
                            width: implicitWidth
                            height: 30
                            text: "PRE-OP"
                            checkable: true
                            checked: explorer.currentState === 2
                            enabled: explorer.scanned && !explorer.busy
                            onClicked: explorer.requestState(2)
                        }

                        Button {
                            width: implicitWidth
                            height: 30
                            text: "SAFE-OP"
                            checkable: true
                            checked: explorer.currentState === 4
                            enabled: explorer.scanned && !explorer.busy
                            onClicked: explorer.requestState(4)
                        }

                        Button {
                            width: implicitWidth
                            height: 30
                            text: "OP"
                            checkable: true
                            checked: explorer.currentState === 8
                            enabled: explorer.scanned && explorer.mappingReady
                                     && explorer.allEsiTrusted && !explorer.busy
                            onClicked: explorer.requestState(8)
                        }

                        Rectangle {
                            width: 116
                            height: 30
                            radius: 6
                            color: explorer.scanned ? theme.successBackground : theme.controlBackground
                            border.color: explorer.scanned ? theme.successBorder : theme.borderStrong

                            Label {
                                anchors.centerIn: parent
                                text: "从站 " + explorer.slaveCount
                                color: explorer.scanned ? theme.successText : theme.textSecondary
                                font.bold: true
                            }
                        }

                        Rectangle {
                            width: 150
                            height: 30
                            radius: 6
                            color: explorer.allEsiTrusted ? theme.successBackground : theme.dangerBackground
                            border.color: explorer.allEsiTrusted ? theme.successBorder : theme.dangerBorder

                            Label {
                                anchors.centerIn: parent
                                text: explorer.allEsiTrusted ? "ESI可信" : "ESI未就绪"
                                color: explorer.allEsiTrusted ? theme.successText : theme.dangerText
                            }
                        }

                        Label {
                            id: busStatusText
                            width: Math.min(240, Math.max(92, implicitWidth))
                            height: 30
                            text: explorer.statusText
                            color: theme.textSecondary
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            HoverHandler { id: busStatusHover }
                            ToolTip.visible: busStatusHover.hovered && busStatusText.truncated
                            ToolTip.text: busStatusText.text
                            ToolTip.delay: 500
                        }
                    }
                }
            }

            SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                orientation: root.wideLayout ? Qt.Horizontal : Qt.Vertical

                Item {
                    SplitView.preferredWidth: root.wideLayout ? 410 : parent.width
                    SplitView.minimumWidth: root.wideLayout ? 330 : 0
                    SplitView.preferredHeight: root.wideLayout ? parent.height
                                                                : Math.max(300, parent.height * 0.52)
                    SplitView.minimumHeight: root.wideLayout ? 0 : 280
                    SplitView.fillWidth: !root.wideLayout

                    SplitView {
                        anchors.fill: parent
                        orientation: Qt.Vertical

                        Rectangle {
                            SplitView.fillWidth: true
                            SplitView.preferredHeight: Math.max(150, parent.height * 0.34)
                            SplitView.minimumHeight: 120
                            radius: 8
                            color: theme.surface
                            border.color: theme.border

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 6

                                Label {
                                    text: "扫描从站"
                                    font.bold: true
                                    color: theme.textPrimary
                                }

                                ListView {
                                    id: slaveList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    spacing: 4
                                    model: explorer.slavesModel
                                    ScrollBar.vertical: ScrollBar {}

                                    delegate: Rectangle {
                                        required property int index
                                        required property int address
                                        required property string name
                                        required property string stateText
                                        required property bool esiTrusted
                                        required property string productCodeText
                                        required property string revisionNoText

                                        width: slaveList.width
                                        height: 54
                                        radius: 6
                                        color: explorer.selectedSlaveAddress === address
                                               ? theme.selectedBackground : theme.surfaceMuted
                                        border.color: explorer.selectedSlaveAddress === address
                                                      ? theme.selectedBorder : theme.border

                                        Column {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.margins: 8
                                            spacing: 2

                                            Text {
                                                id: slaveNameText
                                                width: parent.width
                                                text: address + "  " + name + "  [" + stateText + "]"
                                                color: theme.textPrimary
                                                font.bold: true
                                                elide: Text.ElideRight
                                                HoverHandler { id: slaveNameHover }
                                                ToolTip.visible: slaveNameHover.hovered && slaveNameText.truncated
                                                ToolTip.text: slaveNameText.text
                                                ToolTip.delay: 500
                                            }

                                            Text {
                                                width: parent.width
                                                text: productCodeText + " / " + revisionNoText
                                                      + (esiTrusted ? "  ESI OK" : "  ESI !")
                                                color: esiTrusted ? theme.successText : theme.dangerText
                                                font.pixelSize: 11
                                                elide: Text.ElideRight
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: explorer.selectSlave(address)
                                        }
                                    }

                                    Label {
                                        anchors.centerIn: parent
                                        visible: slaveList.count === 0
                                        text: "暂无从站"
                                        color: theme.textMuted
                                    }
                                }
                            }
                        }

                        Rectangle {
                            SplitView.fillWidth: true
                            SplitView.fillHeight: true
                            SplitView.minimumHeight: 150
                            radius: 8
                            color: theme.surface
                            border.color: theme.border

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 6

                                Label {
                                    text: "PDO 实时数据"
                                    font.bold: true
                                    color: theme.textPrimary
                                }

                                ListView {
                                    id: pdoList
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    spacing: 4
                                    model: explorer.pdoVariableGroupsModel
                                    ScrollBar.vertical: ScrollBar {}

                                    delegate: Rectangle {
                                        required property int index
                                        required property string groupId
                                        required property string direction
                                        required property string pdoIndexText
                                        required property string pdoName
                                        required property string indexText
                                        required property string name
                                        required property bool isArray
                                        required property var elementLabels
                                        required property int elementCount
                                        required property int selectedElementIndex
                                        required property string selectedStableId
                                        required property string selectedSubIndexText
                                        required property string selectedDataType
                                        required property string selectedDisplayValue
                                        required property bool selectedWritable

                                        width: pdoList.width
                                        height: isArray ? 88 : 66
                                        radius: 6
                                        color: theme.surfaceMuted
                                        border.color: theme.border

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 7
                                            spacing: 4

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 7

                                                ColumnLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 1
                                                Text {
                                                    id: pdoGroupName
                                                    Layout.fillWidth: true
                                                    text: name
                                                    color: theme.textPrimary
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                    HoverHandler { id: pdoNameHover }
                                                    ToolTip.visible: pdoNameHover.hovered && pdoGroupName.truncated
                                                    ToolTip.text: pdoGroupName.text
                                                    ToolTip.delay: 500
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: direction + "  PDO " + pdoIndexText
                                                          + "  OD " + indexText + ":"
                                                          + selectedSubIndexText + "  "
                                                          + selectedDataType
                                                    color: theme.textMuted
                                                    font.pixelSize: 11
                                                    elide: Text.ElideRight
                                                }
                                                }

                                            TextField {
                                                id: pdoEditor
                                                Layout.preferredWidth: Math.min(150, Math.max(108, pdoList.width * 0.32))
                                                readOnly: !selectedWritable || explorer.currentState !== 8
                                                selectByMouse: true
                                                Binding {
                                                    target: pdoEditor
                                                    property: "text"
                                                    value: selectedDisplayValue
                                                    when: !pdoEditor.activeFocus
                                                    restoreMode: Binding.RestoreNone
                                                }
                                                onEditingFinished: {
                                                    if (!readOnly && selectedStableId.length > 0
                                                            && text !== selectedDisplayValue) {
                                                        explorer.writePdoValue(selectedStableId, text)
                                                    }
                                                }
                                            }
                                        }

                                            RowLayout {
                                                Layout.fillWidth: true
                                                visible: isArray
                                                spacing: 7

                                                ComboBox {
                                                    Layout.fillWidth: true
                                                    Layout.minimumWidth: 120
                                                    model: elementLabels
                                                    currentIndex: selectedElementIndex
                                                    enabled: elementCount > 0
                                                    hoverEnabled: true
                                                    ToolTip.visible: hovered && currentText.length > 20
                                                    ToolTip.text: currentText
                                                    ToolTip.delay: 500
                                                    onActivated: function(elementIndex) {
                                                        explorer.selectPdoArrayElement(groupId, elementIndex)
                                                    }
                                                }

                                                Label {
                                                    Layout.preferredWidth: 118
                                                    text: indexText + ":" + selectedSubIndexText
                                                          + "  " + selectedDataType
                                                    color: theme.textSecondary
                                                    elide: Text.ElideRight
                                                    horizontalAlignment: Text.AlignRight
                                                }
                                            }
                                        }
                                    }

                                    Label {
                                        anchors.centerIn: parent
                                        visible: pdoList.count === 0
                                        text: "无可用 PDO 变量"
                                        color: theme.textMuted
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    SplitView.fillWidth: true
                    SplitView.fillHeight: true
                    SplitView.minimumWidth: root.wideLayout ? 500 : 0
                    SplitView.minimumHeight: root.wideLayout ? 0 : 240
                    radius: 8
                    color: theme.surface
                    border.color: theme.border

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        TabBar {
                            id: detailsTabs
                            Layout.fillWidth: true

                            TabButton { text: "对象字典" }
                            TabButton { text: "PDO 映射" }
                            TabButton { text: "从站信息" }
                            TabButton { text: "运行日志" }
                        }

                        StackLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            currentIndex: detailsTabs.currentIndex

                            ListView {
                                id: odList
                                clip: true
                                spacing: 3
                                model: explorer.objectDictionaryModel
                                ScrollBar.vertical: ScrollBar {}

                                delegate: Rectangle {
                                    required property int index
                                    required property string stableId
                                    required property string indexText
                                    required property string subIndexText
                                    required property string name
                                    required property string dataType
                                    required property string access
                                    required property string displayValue
                                    required property bool readable
                                    required property bool writable

                                    width: odList.width
                                    height: 70
                                    color: index % 2 === 0 ? theme.surfaceMuted : theme.surface
                                    border.color: theme.border

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 5
                                        spacing: 6

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6

                                            Label {
                                                Layout.preferredWidth: 92
                                                text: indexText + ":" + subIndexText
                                                color: theme.accentText
                                            }

                                            Text {
                                                id: odNameText
                                                Layout.fillWidth: true
                                                text: name
                                                color: theme.textPrimary
                                                elide: Text.ElideRight
                                                HoverHandler { id: odNameHover }
                                                ToolTip.visible: odNameHover.hovered && odNameText.truncated
                                                ToolTip.text: odNameText.text
                                                ToolTip.delay: 500
                                            }

                                            Label {
                                                Layout.preferredWidth: 100
                                                text: dataType
                                                color: theme.textSecondary
                                                elide: Text.ElideRight
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6

                                            Label {
                                                Layout.preferredWidth: 100
                                                text: "权限 " + access
                                                color: theme.textMuted
                                            }

                                            TextField {
                                                id: odEditor
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 100
                                                readOnly: !writable
                                                selectByMouse: true
                                                Binding {
                                                    target: odEditor
                                                    property: "text"
                                                    value: displayValue
                                                    when: !odEditor.activeFocus
                                                    restoreMode: Binding.RestoreNone
                                                }
                                            }

                                            Button {
                                                text: "读"
                                                enabled: readable && explorer.scanned && !explorer.busy
                                                onClicked: explorer.readSdoValue(stableId)
                                            }

                                            Button {
                                                text: "写"
                                                enabled: writable && explorer.scanned && !explorer.busy
                                                onClicked: explorer.writeSdoValue(stableId, odEditor.text)
                                            }
                                        }
                                    }
                                }
                            }

                            ListView {
                                id: mappingList
                                clip: true
                                spacing: 3
                                model: explorer.pdoMappingsModel
                                ScrollBar.vertical: ScrollBar {}

                                delegate: Rectangle {
                                    required property int index
                                    required property string direction
                                    required property string pdoIndexText
                                    required property string pdoName
                                    required property string indexText
                                    required property string subIndexText
                                    required property string name
                                    required property string dataType
                                    required property int bitLength
                                    required property int processBitOffset

                                    width: mappingList.width
                                    height: 62
                                    color: index % 2 === 0 ? theme.surfaceMuted : theme.surface
                                    border.color: theme.border

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        spacing: 3

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8

                                            Label {
                                                Layout.preferredWidth: 42
                                                text: direction
                                                color: theme.textSecondary
                                            }
                                            Label {
                                                Layout.preferredWidth: 76
                                                text: pdoIndexText
                                                color: theme.accentText
                                            }
                                            Text {
                                                id: mappingPdoName
                                                Layout.fillWidth: true
                                                text: pdoName
                                                color: theme.textPrimary
                                                elide: Text.ElideRight
                                                HoverHandler { id: mappingPdoHover }
                                                ToolTip.visible: mappingPdoHover.hovered
                                                                 && mappingPdoName.truncated
                                                ToolTip.text: mappingPdoName.text
                                                ToolTip.delay: 500
                                            }
                                            Label {
                                                Layout.preferredWidth: 74
                                                text: "@" + processBitOffset
                                                color: theme.textMuted
                                                horizontalAlignment: Text.AlignRight
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8

                                            Label {
                                                Layout.preferredWidth: 126
                                                text: indexText + ":" + subIndexText
                                                color: theme.textSecondary
                                            }
                                            Text {
                                                id: mappingEntryName
                                                Layout.fillWidth: true
                                                text: name
                                                color: theme.textPrimary
                                                elide: Text.ElideRight
                                                HoverHandler { id: mappingNameHover }
                                                ToolTip.visible: mappingNameHover.hovered
                                                                 && mappingEntryName.truncated
                                                ToolTip.text: mappingEntryName.text
                                                ToolTip.delay: 500
                                            }
                                            Label {
                                                Layout.preferredWidth: 82
                                                text: dataType
                                                color: theme.textSecondary
                                                elide: Text.ElideRight
                                            }
                                            Label {
                                                Layout.preferredWidth: 64
                                                text: bitLength + " bit"
                                                color: theme.textMuted
                                                horizontalAlignment: Text.AlignRight
                                            }
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                spacing: 12

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    columnSpacing: 18
                                    rowSpacing: 10

                                    Label { text: "从站地址"; color: theme.textMuted }
                                    Label { text: explorer.selectedSlaveAddress || "-"; color: theme.textPrimary }
                                    Label { text: "当前状态"; color: theme.textMuted }
                                    Label { text: explorer.currentState === 8 ? "OP"
                                                  : explorer.currentState === 4 ? "SAFE-OP"
                                                  : explorer.currentState === 2 ? "PRE-OP"
                                                  : explorer.currentState === 1 ? "INIT" : "-";
                                            color: theme.textPrimary }
                                    Label { text: "过程映像"; color: theme.textMuted }
                                    Label { text: explorer.mappingReady ? "已建立" : "未建立";
                                            color: explorer.mappingReady ? theme.successText : theme.dangerText }
                                    Label { text: "ESI状态"; color: theme.textMuted }
                                    Label { text: explorer.allEsiTrusted ? "全部可信" : "存在缺失或冲突";
                                            color: explorer.allEsiTrusted ? theme.successText : theme.dangerText }
                                }

                                Item { Layout.fillHeight: true }
                            }

                            TextArea {
                                text: explorer.logs.join("\n")
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.NoWrap
                                color: theme.textPrimary
                                background: Rectangle {
                                    color: theme.inputBackground
                                    border.color: theme.border
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
