pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root

    property var theme
    property var sessionUi

    signal errorRequested(string message)

    readonly property var explorer: EthercatBackend.busExplorer
    readonly property bool wideLayout: width >= 980

    property string pendingSdoStableId: ""
    property string pendingSdoValue: ""

    function alStateText(state) {
        switch (state) {
        case 1: return "INIT"
        case 2: return "PRE-OP"
        case 4: return "SAFE-OP"
        case 8: return "OP"
        default: return "未知"
        }
    }

    function confirmSdoWrite(stableId, value) {
        pendingSdoStableId = stableId
        pendingSdoValue = value
        sdoWriteConfirm.open()
    }

    FolderDialog {
        id: esiFolderDialog
        title: "选择 ESI 文件夹"
        onAccepted: root.explorer.esiDirectory = selectedFolder.toString()
    }

    ConfirmDialog {
        id: resetConfirm
        theme: root.theme
        titleText: qsTr("确认重置总线会话")
        message: root.explorer.busy
                 ? qsTr("当前操作仍在进行。重置将停止当前会话并清空已扫描的从站数据。")
                 : qsTr("重置将清空已扫描的从站、对象字典和 PDO 数据。")
        confirmText: qsTr("确认重置")
        tone: "warning"
        onConfirmed: {
            if (root.explorer.busy || root.explorer.scanned || root.explorer.status !== 0)
                root.explorer.reset()
        }
    }

    ConfirmDialog {
        id: opConfirm
        theme: root.theme
        titleText: qsTr("确认切换到 OP")
        message: qsTr("从站将进入 OP，实时 PDO 写入会随之解锁。请确认设备和现场条件允许切换。")
        confirmText: qsTr("进入 OP")
        tone: "warning"
        onConfirmed: {
            if (root.explorer.scanned
                    && !root.explorer.busy
                    && root.explorer.mappingReady
                    && root.explorer.allEsiTrusted) {
                root.explorer.requestState(8)
            }
        }
    }

    ConfirmDialog {
        id: sdoWriteConfirm
        theme: root.theme
        titleText: qsTr("确认写入 SDO")
        message: qsTr("即将写入对象 %1，值为“%2”。")
                 .arg(root.pendingSdoStableId)
                 .arg(root.pendingSdoValue)
        confirmText: qsTr("确认写入")
        tone: "warning"
        onConfirmed: {
            if (root.explorer.scanned
                    && !root.explorer.busy
                    && root.pendingSdoStableId.length > 0) {
                root.explorer.writeSdoValue(root.pendingSdoStableId,
                                            root.pendingSdoValue)
            }
        }
        onClosed: {
            root.pendingSdoStableId = ""
            root.pendingSdoValue = ""
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.theme.pageBackground

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: headerContent.implicitHeight + 20
                color: root.theme.surface
                radius: 8
                border.color: root.theme.border

                ColumnLayout {
                    id: headerContent
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: "ESI"
                            color: root.theme.textSecondary
                            font.bold: true
                        }

                        TextField {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 180
                            text: root.explorer.esiDirectory
                            readOnly: true
                            selectByMouse: true
                            placeholderText: "请选择 ESI 文件夹"
                            onTextChanged: cursorPosition = 0
                            ToolTip.visible: hovered && text.length > 0
                            ToolTip.text: text
                        }

                        Button {
                            text: "选择…"
                            enabled: !root.explorer.busy
                                     && !root.explorer.scanned
                            onClicked: esiFolderDialog.open()
                        }

                        Button {
                            text: root.explorer.busy ? "扫描中…" : "扫描"
                            enabled: !root.explorer.busy
                                     && !root.explorer.scanned
                                     && root.sessionUi
                                     && root.sessionUi.idle
                                     && root.sessionUi.nicList.length > 0
                                     && root.sessionUi.selectedNicIndex >= 0
                            onClicked: root.explorer.scan()
                        }

                        Button {
                            text: "重置"
                            enabled: root.explorer.busy
                                     || root.explorer.scanned
                                     || root.explorer.status !== 0
                            onClicked: resetConfirm.open()
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        Layout.preferredHeight: childrenRect.height
                        spacing: 6

                        Label {
                            height: 30
                            verticalAlignment: Text.AlignVCenter
                            text: "AL 状态"
                            color: root.theme.textSecondary
                            font.bold: true
                        }

                        Button {
                            height: 30
                            text: "INIT"
                            checkable: true
                            checked: root.explorer.currentState === 1
                            enabled: root.explorer.scanned && !root.explorer.busy
                            onClicked: root.explorer.requestState(1)
                        }

                        Button {
                            height: 30
                            text: "PRE-OP"
                            checkable: true
                            checked: root.explorer.currentState === 2
                            enabled: root.explorer.scanned && !root.explorer.busy
                            onClicked: root.explorer.requestState(2)
                        }

                        Button {
                            height: 30
                            text: "SAFE-OP"
                            checkable: true
                            checked: root.explorer.currentState === 4
                            enabled: root.explorer.scanned && !root.explorer.busy
                            onClicked: root.explorer.requestState(4)
                        }

                        Button {
                            height: 30
                            text: "OP"
                            checkable: true
                            checked: root.explorer.currentState === 8
                            enabled: root.explorer.scanned
                                     && !root.explorer.busy
                                     && root.explorer.mappingReady
                                     && root.explorer.allEsiTrusted
                            onClicked: opConfirm.open()
                        }

                        Rectangle {
                            height: 30
                            width: slaveBadge.implicitWidth + 18
                            radius: 15
                            color: root.theme.surfaceMuted
                            border.color: root.theme.border

                            Label {
                                id: slaveBadge
                                anchors.centerIn: parent
                                text: root.explorer.slaveCount + " 从站"
                                color: root.theme.textSecondary
                            }
                        }

                        Rectangle {
                            height: 30
                            width: trustBadge.implicitWidth + 18
                            radius: 15
                            color: root.explorer.allEsiTrusted
                                   ? root.theme.successBackground
                                   : root.theme.warningBackground
                            border.color: root.explorer.allEsiTrusted
                                          ? root.theme.successBorder
                                          : root.theme.warningBorder

                            Label {
                                id: trustBadge
                                anchors.centerIn: parent
                                text: root.explorer.allEsiTrusted ? "ESI 已信任" : "ESI 未就绪"
                                color: root.explorer.allEsiTrusted
                                       ? root.theme.successText
                                       : root.theme.warningText
                            }
                        }

                        Rectangle {
                            height: 30
                            width: Math.min(320, Math.max(120, statusBadge.implicitWidth + 18))
                            radius: 15
                            color: root.theme.surfaceMuted
                            border.color: root.theme.border

                            Label {
                                id: statusBadge
                                anchors.centerIn: parent
                                width: parent.width - 18
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                                text: root.explorer.statusText
                                color: root.theme.textSecondary
                                ToolTip.visible: statusHover.hovered && text.length > 0
                                ToolTip.text: text
                            }

                            HoverHandler {
                                id: statusHover
                            }
                        }
                    }
                }
            }

            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: root.wideLayout
                                 ? wideBodyComponent
                                 : compactBodyComponent
            }
        }
    }

    Component {
        id: wideBodyComponent

        SplitView {
            orientation: Qt.Horizontal

            Loader {
                SplitView.preferredWidth: 400
                SplitView.minimumWidth: 320
                SplitView.fillHeight: true
                sourceComponent: overviewPaneComponent
            }

            Loader {
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumWidth: 500
                sourceComponent: detailPaneComponent
            }
        }
    }

    Component {
        id: compactBodyComponent

        ColumnLayout {
            spacing: 6

            TabBar {
                id: primaryTabs
                Layout.fillWidth: true

                TabButton { text: "从站与实时 PDO" }
                TabButton { text: "对象、映射与信息" }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: primaryTabs.currentIndex

                Loader { sourceComponent: overviewPaneComponent }
                Loader { sourceComponent: detailPaneComponent }
            }
        }
    }

    Component {
        id: overviewPaneComponent

        SplitView {
            orientation: Qt.Vertical

            Rectangle {
                SplitView.preferredHeight: 220
                SplitView.minimumHeight: 88
                SplitView.fillWidth: true
                color: root.theme.surface
                radius: 8
                border.color: root.theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: "从站"
                            color: root.theme.textPrimary
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            text: root.explorer.selectedSlaveAddress > 0
                                  ? "已选 " + root.explorer.selectedSlaveAddress
                                  : "未选择"
                            color: root.theme.textMuted
                        }
                    }

                    ListView {
                        id: slaveList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: root.explorer.slavesModel
                        ScrollBar.vertical: ScrollBar { }

                        delegate: Rectangle {
                            required property int index
                            required property int address
                            required property string name
                            required property string stateText
                            required property string alStatusText
                            required property bool esiTrusted
                            required property string productCodeText
                            required property string revisionNoText
                            required property int inputBits
                            required property int outputBits

                            width: ListView.view.width
                            height: 56
                            radius: 6
                            color: root.explorer.selectedSlaveAddress === address
                                   ? root.theme.selectedBackground
                                   : root.theme.surfaceMuted
                            border.color: root.explorer.selectedSlaveAddress === address
                                          ? root.theme.selectedBorder
                                          : root.theme.border

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8

                                Rectangle {
                                    Layout.preferredWidth: 8
                                    Layout.preferredHeight: 8
                                    radius: 4
                                    color: esiTrusted
                                           ? root.theme.successText
                                           : root.theme.warningText
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Label {
                                            Layout.fillWidth: true
                                            text: address + "  " + name
                                            color: root.theme.textPrimary
                                            font.bold: true
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            text: stateText
                                            color: root.theme.accent
                                            font.bold: true
                                            ToolTip.visible: stateHover.hovered && alStatusText.length > 0
                                            ToolTip.text: alStatusText
                                        }

                                        HoverHandler { id: stateHover }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: productCodeText + " / " + revisionNoText
                                              + "   I/O " + inputBits + "/" + outputBits + " bit"
                                        color: root.theme.textMuted
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.explorer.selectSlave(address)
                            }
                        }
                    }
                }
            }

            Rectangle {
                SplitView.fillHeight: true
                SplitView.minimumHeight: 96
                SplitView.fillWidth: true
                color: root.theme.surface
                radius: 8
                border.color: root.theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: "实时 PDO"
                            color: root.theme.textPrimary
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            text: root.explorer.currentState === 8
                                  ? "OP · 写入已解锁"
                                  : "仅 OP 可写"
                            color: root.explorer.currentState === 8
                                   ? root.theme.success
                                   : root.theme.textMuted
                        }
                    }

                    ListView {
                        id: pdoList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: root.explorer.pdoVariableGroupsModel
                        ScrollBar.vertical: ScrollBar { }

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

                            width: ListView.view.width
                            height: isArray ? 96 : 68
                            radius: 6
                            color: root.theme.surfaceMuted
                            border.color: root.theme.border

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 6
                                spacing: 4

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 5

                                    Rectangle {
                                        Layout.preferredWidth: direction === "输出" ? 34 : 30
                                        Layout.preferredHeight: 20
                                        radius: 10
                                        color: direction === "输出"
                                               ? root.theme.selectedBackground
                                               : root.theme.successBackground

                                        Label {
                                            anchors.centerIn: parent
                                            text: direction
                                            color: direction === "输出"
                                                   ? root.theme.accent
                                                   : root.theme.success
                                            font.pixelSize: 10
                                            font.bold: true
                                        }
                                    }

                                    Label {
                                        text: pdoIndexText
                                        color: root.theme.accent
                                        font.bold: true
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: pdoName.length > 0 ? pdoName : indexText + " " + name
                                        color: root.theme.textPrimary
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        text: selectedDataType
                                        color: root.theme.textMuted
                                        font.pixelSize: 10
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    visible: isArray

                                    Label {
                                        text: indexText
                                        color: root.theme.textMuted
                                        font.pixelSize: 11
                                    }

                                    ComboBox {
                                        Layout.fillWidth: true
                                        model: elementLabels
                                        currentIndex: selectedElementIndex
                                        onActivated: root.explorer.selectPdoArrayElement(groupId,
                                                                                        currentIndex)
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 5

                                    Label {
                                        text: selectedSubIndexText
                                        color: root.theme.textMuted
                                        font.pixelSize: 11
                                    }

                                    TextField {
                                        id: pdoEditor
                                        Layout.fillWidth: true
                                        selectByMouse: true
                                        readOnly: !selectedWritable
                                                  || !root.explorer.scanned
                                                  || root.explorer.currentState !== 8
                                                  || root.explorer.busy

                                        Binding {
                                            target: pdoEditor
                                            property: "text"
                                            value: selectedDisplayValue
                                            when: !pdoEditor.activeFocus
                                            restoreMode: Binding.RestoreNone
                                        }

                                        onEditingFinished: {
                                            if (!readOnly
                                                    && selectedStableId.length > 0
                                                    && root.explorer.scanned
                                                    && root.explorer.currentState === 8
                                                    && !root.explorer.busy
                                                    && text !== selectedDisplayValue) {
                                                root.explorer.writePdoValue(selectedStableId,
                                                                            text)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: pdoList.count === 0
                            text: root.explorer.selectedSlaveAddress > 0
                                  ? "当前从站没有可用 PDO"
                                  : "选择从站后查看实时 PDO"
                            color: root.theme.textMuted
                        }
                    }
                }
            }
        }
    }

    Component {
        id: detailPaneComponent

        Rectangle {
            color: root.theme.surface
            radius: 8
            border.color: root.theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                TabBar {
                    id: detailTabs
                    Layout.fillWidth: true

                    TabButton { text: "对象字典" }
                    TabButton { text: "PDO 映射" }
                    TabButton { text: "从站信息" }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: detailTabs.currentIndex

                    Item {
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 5

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: "索引 / 子索引"
                                    color: root.theme.textMuted
                                    font.pixelSize: 11
                                }

                                Item { Layout.fillWidth: true }

                                Label {
                                    text: "SDO 写入需要确认"
                                    color: root.theme.warningText
                                    font.pixelSize: 11
                                }
                            }

                            ListView {
                                id: objectList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 4
                                model: root.explorer.objectDictionaryModel
                                ScrollBar.vertical: ScrollBar { }

                                delegate: Rectangle {
                                    required property int index
                                    required property string stableId
                                    required property string indexText
                                    required property string subIndexText
                                    required property string name
                                    required property string dataType
                                    required property string access
                                    required property int bitLength
                                    required property string pdoMapping
                                    required property string displayValue
                                    required property bool readable
                                    required property bool writable

                                    width: ListView.view.width
                                    height: 68
                                    radius: 6
                                    color: root.theme.surfaceMuted
                                    border.color: root.theme.border

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        spacing: 3

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6

                                            Label {
                                                text: indexText + ":" + subIndexText
                                                color: root.theme.accent
                                                font.bold: true
                                            }

                                            Label {
                                                Layout.fillWidth: true
                                                text: name
                                                color: root.theme.textPrimary
                                                elide: Text.ElideRight
                                            }

                                            Label {
                                                text: dataType + " · " + access
                                                      + " · " + bitLength + " bit"
                                                      + (pdoMapping.length > 0
                                                         ? " · PDO " + pdoMapping
                                                         : "")
                                                color: root.theme.textMuted
                                                font.pixelSize: 10
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 5

                                            TextField {
                                                id: odEditor
                                                Layout.fillWidth: true
                                                readOnly: !writable
                                                selectByMouse: true
                                                placeholderText: "值"

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
                                                enabled: readable
                                                         && root.explorer.scanned
                                                         && !root.explorer.busy
                                                onClicked: root.explorer.readSdoValue(stableId)
                                            }

                                            Button {
                                                text: "写"
                                                enabled: writable
                                                         && root.explorer.scanned
                                                         && !root.explorer.busy
                                                onClicked: root.confirmSdoWrite(stableId,
                                                                                odEditor.text)
                                            }
                                        }
                                    }
                                }

                                Label {
                                    anchors.centerIn: parent
                                    visible: objectList.count === 0
                                    text: root.explorer.selectedSlaveAddress > 0
                                          ? "没有可显示的对象字典"
                                          : "选择从站后查看对象字典"
                                    color: root.theme.textMuted
                                }
                            }
                        }
                    }

                    Item {
                        ListView {
                            id: mappingList
                            anchors.fill: parent
                            clip: true
                            spacing: 4
                            model: root.explorer.pdoMappingsModel
                            ScrollBar.vertical: ScrollBar { }

                            delegate: Rectangle {
                                required property int index
                                required property string direction
                                required property int syncManager
                                required property string pdoIndexText
                                required property string pdoName
                                required property string indexText
                                required property string subIndexText
                                required property string name
                                required property string dataType
                                required property int bitLength
                                required property int processBitOffset

                                width: ListView.view.width
                                height: 52
                                radius: 6
                                color: root.theme.surfaceMuted
                                border.color: root.theme.border

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    spacing: 2

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Label {
                                            text: direction
                                            color: direction === "输出"
                                                   ? root.theme.accent
                                                   : root.theme.success
                                            font.bold: true
                                        }

                                        Label {
                                            text: "SM" + syncManager + " · " + pdoIndexText
                                            color: root.theme.textMuted
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: pdoName
                                            color: root.theme.textPrimary
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            text: "bit " + processBitOffset
                                            color: root.theme.textMuted
                                            font.pixelSize: 10
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Label {
                                            text: indexText + ":" + subIndexText
                                            color: root.theme.accent
                                            font.bold: true
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: name
                                            color: root.theme.textSecondary
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            text: dataType + " · " + bitLength + " bit"
                                            color: root.theme.textMuted
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                            }

                            Label {
                                anchors.centerIn: parent
                                visible: mappingList.count === 0
                                text: root.explorer.selectedSlaveAddress > 0
                                      ? "当前从站没有 PDO 映射"
                                      : "选择从站后查看 PDO 映射"
                                color: root.theme.textMuted
                            }
                        }
                    }

                    Item {
                        ScrollView {
                            anchors.fill: parent
                            clip: true

                            GridLayout {
                                width: parent.width
                                columns: 2
                                columnSpacing: 14
                                rowSpacing: 10

                                Label { text: "从站地址"; color: root.theme.textMuted }
                                Label {
                                    Layout.fillWidth: true
                                    text: root.explorer.selectedSlaveAddress > 0
                                          ? root.explorer.selectedSlaveAddress
                                          : "未选择"
                                    color: root.theme.textPrimary
                                }

                                Label { text: "AL 状态"; color: root.theme.textMuted }
                                Label {
                                    Layout.fillWidth: true
                                    text: root.alStateText(root.explorer.currentState)
                                    color: root.theme.textPrimary
                                }

                                Label { text: "从站数量"; color: root.theme.textMuted }
                                Label {
                                    Layout.fillWidth: true
                                    text: root.explorer.slaveCount
                                    color: root.theme.textPrimary
                                }

                                Label { text: "映射"; color: root.theme.textMuted }
                                Label {
                                    Layout.fillWidth: true
                                    text: root.explorer.mappingReady ? "已就绪" : "未就绪"
                                    color: root.explorer.mappingReady
                                           ? root.theme.successText
                                           : root.theme.warningText
                                }

                                Label { text: "ESI 信任"; color: root.theme.textMuted }
                                Label {
                                    Layout.fillWidth: true
                                    text: root.explorer.allEsiTrusted ? "全部可信" : "存在未匹配或未信任项"
                                    color: root.explorer.allEsiTrusted
                                           ? root.theme.successText
                                           : root.theme.warningText
                                    wrapMode: Text.WordWrap
                                }

                                Label { text: "ESI 目录"; color: root.theme.textMuted }
                                Label {
                                    Layout.fillWidth: true
                                    text: root.explorer.esiDirectory.length > 0
                                          ? root.explorer.esiDirectory
                                          : "未设置"
                                    color: root.theme.textPrimary
                                    wrapMode: Text.WrapAnywhere
                                }

                                Label { text: "会话状态"; color: root.theme.textMuted }
                                Label {
                                    Layout.fillWidth: true
                                    text: root.explorer.statusText
                                    color: root.theme.textPrimary
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
