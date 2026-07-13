import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root

    required property var theme
    readonly property var explorer: EthercatBackend.busExplorer

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
                Layout.preferredHeight: 118
                radius: 8
                color: theme.surface
                border.color: theme.border

                ColumnLayout {
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
                            Layout.preferredWidth: 270
                            model: EthercatBackend.nicList
                            enabled: !explorer.busy && !explorer.scanned
                            onActivated: EthercatBackend.changedSelectedNic(currentIndex)
                        }

                        Button {
                            text: "刷新"
                            enabled: !explorer.busy && !explorer.scanned
                            onClicked: EthercatBackend.refreshNicsAsync()
                        }

                        Label {
                            text: "ESI"
                            font.bold: true
                            color: theme.textPrimary
                        }

                        TextField {
                            Layout.fillWidth: true
                            text: explorer.esiDirectory
                            readOnly: true
                            color: theme.textPrimary
                        }

                        Button {
                            text: "选择"
                            enabled: !explorer.busy && !explorer.scanned
                            onClicked: esiFolderDialog.open()
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
                            text: "全站状态"
                            font.bold: true
                            color: theme.textPrimary
                        }

                        Button {
                            text: "INIT"
                            checkable: true
                            checked: explorer.currentState === 1
                            enabled: explorer.scanned && !explorer.busy
                            onClicked: explorer.requestState(1)
                        }

                        Button {
                            text: "PRE-OP"
                            checkable: true
                            checked: explorer.currentState === 2
                            enabled: explorer.scanned && !explorer.busy
                            onClicked: explorer.requestState(2)
                        }

                        Button {
                            text: "SAFE-OP"
                            checkable: true
                            checked: explorer.currentState === 4
                            enabled: explorer.scanned && !explorer.busy
                            onClicked: explorer.requestState(4)
                        }

                        Button {
                            text: "OP"
                            checkable: true
                            checked: explorer.currentState === 8
                            enabled: explorer.scanned && explorer.mappingReady
                                     && explorer.allEsiTrusted && !explorer.busy
                            onClicked: explorer.requestState(8)
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            Layout.preferredWidth: 116
                            Layout.preferredHeight: 30
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
                            Layout.preferredWidth: 150
                            Layout.preferredHeight: 30
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
                            Layout.preferredWidth: 92
                            text: explorer.statusText
                            color: theme.textSecondary
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
            }

            SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                orientation: Qt.Horizontal

                Item {
                    SplitView.preferredWidth: 410
                    SplitView.minimumWidth: 330

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.max(190, parent.height * 0.34)
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
                                                width: parent.width
                                                text: address + "  " + name + "  [" + stateText + "]"
                                                color: theme.textPrimary
                                                font.bold: true
                                                elide: Text.ElideRight
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
                            Layout.fillWidth: true
                            Layout.fillHeight: true
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
                                    model: explorer.pdoEntriesModel
                                    ScrollBar.vertical: ScrollBar {}

                                    delegate: Rectangle {
                                        required property string stableId
                                        required property string direction
                                        required property string indexText
                                        required property string subIndexText
                                        required property string name
                                        required property string dataType
                                        required property string displayValue
                                        required property bool writable

                                        width: pdoList.width
                                        height: 62
                                        radius: 6
                                        color: theme.surfaceMuted
                                        border.color: theme.border

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 7
                                            spacing: 7

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 1

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: name
                                                    color: theme.textPrimary
                                                    font.bold: true
                                                    elide: Text.ElideRight
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: direction + "  " + indexText + ":" + subIndexText
                                                          + "  " + dataType
                                                    color: theme.textMuted
                                                    font.pixelSize: 11
                                                    elide: Text.ElideRight
                                                }
                                            }

                                            TextField {
                                                id: pdoEditor
                                                Layout.preferredWidth: 122
                                                readOnly: !writable || explorer.currentState !== 8
                                                selectByMouse: true
                                                Binding {
                                                    target: pdoEditor
                                                    property: "text"
                                                    value: displayValue
                                                    when: !pdoEditor.activeFocus
                                                    restoreMode: Binding.RestoreNone
                                                }
                                                onEditingFinished: {
                                                    if (!readOnly && text !== displayValue)
                                                        explorer.writePdoValue(stableId, text)
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
                    SplitView.minimumWidth: 520
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
                                    height: 48
                                    color: index % 2 === 0 ? theme.surfaceMuted : theme.surface
                                    border.color: theme.border

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 5
                                        spacing: 6

                                        Label {
                                            Layout.preferredWidth: 86
                                            text: indexText + ":" + subIndexText
                                            color: theme.accentText
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: name
                                            color: theme.textPrimary
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.preferredWidth: 88
                                            text: dataType
                                            color: theme.textSecondary
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.preferredWidth: 42
                                            text: access
                                            color: theme.textMuted
                                        }

                                        TextField {
                                            id: odEditor
                                            Layout.preferredWidth: 110
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

                            ListView {
                                id: mappingList
                                clip: true
                                spacing: 3
                                model: explorer.pdoMappingsModel
                                ScrollBar.vertical: ScrollBar {}

                                delegate: Rectangle {
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
                                    height: 44
                                    color: index % 2 === 0 ? theme.surfaceMuted : theme.surface
                                    border.color: theme.border

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        spacing: 8

                                        Label { Layout.preferredWidth: 42; text: direction }
                                        Label { Layout.preferredWidth: 74; text: pdoIndexText; color: theme.accentText }
                                        Label { Layout.preferredWidth: 130; text: pdoName; elide: Text.ElideRight }
                                        Label { Layout.preferredWidth: 92; text: indexText + ":" + subIndexText }
                                        Label { Layout.fillWidth: true; text: name; elide: Text.ElideRight }
                                        Label { Layout.preferredWidth: 78; text: dataType; elide: Text.ElideRight }
                                        Label { Layout.preferredWidth: 72; text: bitLength + " bit" }
                                        Label { Layout.preferredWidth: 72; text: "@" + processBitOffset }
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
