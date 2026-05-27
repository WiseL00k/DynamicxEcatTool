import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {

    property int slaveId: -1
    property bool isConnected: false

    // =========================
    // 后端MIT CAN数据帧（8字节）
    // =========================
    property var mitCanFrame: [127, 255, 127, 240, 0, 0, 7, 255]

    anchors.fill: parent

    function makeWrite(slave, index, sub, bytes) {
        return {
            "slave": slave,
            "index": index,
            "subindex": sub,
            "type": "write",
            "data": bytes
        }
    }

    function parseMitFrame(str) {
        let arr = str.trim().split(" ")
        if (arr.length !== 8)
            return null

        let out = []
        for (var i = 0; i < 8; i++) {
            let v = parseInt(arr[i], 16)
            if (isNaN(v))
                return null
            out.push(v)
        }
        return out
    }

    // =========================
    // HEX格式化（显示用）
    // =========================
    function formatMit(arr) {
        if (!arr || arr.length !== 8)
            return "-- -- -- -- -- -- -- --"

        let s = ""
        for (var i = 0; i < 8; i++) {
            let v = arr[i].toString(16).toUpperCase()
            if (v.length < 2)
                v = "0" + v
            s += v
            if (i !== 7)
                s += " "
        }
        return s
    }

    ColumnLayout {

        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Label {
            text: "MIT 电机控制系统"
            font.pixelSize: 20
            font.bold: true
        }

        // =========================
        // 参数区
        // =========================
        Rectangle {

            Layout.fillWidth: true
            Layout.preferredHeight: 160
            radius: 10
            color: "#fafafa"
            border.color: "#e6e6e6"

            GridLayout {

                anchors.fill: parent
                anchors.margins: 10
                columns: 6

                Label {
                    text: "CAN"
                }
                ComboBox {
                    id: canBus
                    model: ["CAN0", "CAN1"]
                }

                Label {
                    text: "ID"
                }
                SpinBox {
                    id: canIdField

                    from: 1
                    to: 256
                    value: 1

                    editable: true
                }

                Label {
                    text: "Kp"
                }
                TextField {
                    id: kpField
                    text: "0.0"
                }

                Label {
                    text: "Kd"
                }
                TextField {
                    id: kdField
                    text: "0.0"
                }

                Label {
                    text: "目标位置"
                }
                TextField {
                    id: pdesField
                    text: "0.0"
                }

                Label {
                    text: "目标速度"
                }
                TextField {
                    id: vdesField
                    text: "0.0"
                }

                Label {
                    text: "转矩"
                }
                TextField {
                    id: torqueField
                    text: "0.0"
                }

                Label {
                    text: "PMAX"
                }
                TextField {
                    id: pmaxField
                    text: "12.5"
                }

                Label {
                    text: "VMAX"
                }
                TextField {
                    id: vmaxField
                    text: "30"
                }

                Label {
                    text: "TMAX"
                }
                TextField {
                    id: tmaxField
                    text: "10"
                }

                Button {
                    text: "更新"
                    Layout.row: 3
                    Layout.column: 4
                    Layout.alignment: Qt.AlignRight
                    onClicked: {
                        let input = {
                            pos: parseFloat(pdesField.text),
                            vel: parseFloat(vdesField.text),
                            kp: parseFloat(kpField.text),
                            kd: parseFloat(kdField.text),
                            torque: parseFloat(torqueField.text),
                            PMAX: parseFloat(pmaxField.text),
                            VMAX: parseFloat(vmaxField.text),
                            TMAX: parseFloat(tmaxField.text),
                        }

                        if (isNaN(input.kp) || isNaN(input.kd) ||
                                isNaN(input.pos) || isNaN(input.vel) ||
                                isNaN(input.torque) || isNaN(input.PMAX) ||
                                isNaN(input.VMAX) || isNaN(input.TMAX)) {
                                errorManager.show("存在非法输入(NaN)")
                                return
                            }

                            if (input.kp < 0 || input.kp > 500) {
                                errorManager.show("Kp超范围：0 ~ 500")
                                return
                            }

                            if (input.kd < 0 || input.kd > 5) {
                                errorManager.show("Kd超范围：0 ~ 5")
                                return
                            }

                            if (input.pos < -input.PMAX || input.pos > input.PMAX) {
                                errorManager.show("目标位置超出范围：±PMAX")
                                return
                            }

                            if (input.vel < -input.VMAX || input.vel > input.VMAX) {
                                errorManager.show("目标速度超出范围：±VMAX")
                                return
                            }

                            if (input.torque < -input.TMAX || input.torque > input.TMAX) {
                                errorManager.show("转矩超出范围：±TMAX")
                                return
                            }

                        controlMitFrameField.text = formatMit(MitMotorCommandQml.buildMitFrame(input))
                    }
                }
            }
        }

        // =========================
        // 主区域
        // =========================
        RowLayout {

            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // =========================
            // 左：MIT发送
            // =========================
            Rectangle {

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 2

                radius: 10
                color: "white"
                border.color: "#e6e6e6"

                ColumnLayout {

                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

                    Label {
                        text: "MIT原生帧发送"
                        font.bold: true
                        font.pixelSize: 16
                    }

                    TextField {

                        id: mitFrameField
                        Layout.fillWidth: true
                        implicitHeight: 36
                        placeholderText: "7F FF 7F F0 00 00 07 FF"
                        onTextChanged: {

                            let raw = text.toUpperCase().replace(/[^0-9A-F]/g,
                                                                 "")

                            let formatted = ""
                            for (var i = 0; i < raw.length && i < 16; i += 2) {
                                if (i > 0)
                                    formatted += " "
                                formatted += raw.substring(i, i + 2)
                            }

                            if (formatted !== text)
                                text = formatted
                        }
                    }

                    Item {
                        Layout.fillHeight: true
                    }

                    RowLayout {
                        Button {
                            Layout.fillWidth: true
                            text: "发送"
                            enabled: isConnected

                            onClicked: {
                                let data = parseMitFrame(mitFrameField.text)

                                if (!data) {
                                    errorManager.show("错误：必须8字节HEX")
                                    return
                                }

                                let id = canIdField.value

                                if (isNaN(id)) {
                                    errorManager.show("CAN ID错误")
                                    return
                                }

                                EthercatBackend.sendMitFrameQml(
                                            canBus.currentIndex, id, data)
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "清除"
                            enabled: isConnected

                            onClicked: {
                                let id = canIdField.value

                                if (isNaN(id)) {
                                    errorManager.show("CAN ID错误")
                                    return
                                }

                                EthercatBackend.clearMitFrameQml(
                                            canBus.currentIndex, id)
                                mitFrameField.text = ""
                            }
                        }
                    }
                }
            }

            // =========================
            // 右：控制 + MIT显示
            // =========================
            Rectangle {

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 3

                radius: 10
                color: "white"
                border.color: "#e6e6e6"

                RowLayout {

                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 30

                    ColumnLayout {

                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        Label {
                            text: "电机控制"
                            font.bold: true
                            font.pixelSize: 16
                        }

                        // =========================
                        // 2x2 按钮区
                        // =========================
                        GridLayout {

                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            columns: 2
                            rowSpacing: 10
                            columnSpacing: 10

                            Button {
                                text: "电机使能"
                                enabled: isConnected
                                implicitHeight: 45

                                onClicked: {
                                    EthercatBackend.enableMitSlaveMotors()
                                    // <unused>
                                    // EthercatBackend.applySDOConfigsQml([
                                    //     makeWrite(slaveId, 0x3100, 0x01, [canBus.currentIndex + 1]),
                                    //     makeWrite(slaveId, 0x3100, 0x02, [parseInt(canIdField.text)]),
                                    //     makeWrite(slaveId, 0x3100, 0x03, [1])
                                    // ])
                                }
                            }

                            Button {
                                text: "电机失能"
                                enabled: isConnected
                                implicitHeight: 45

                                onClicked: {
                                    EthercatBackend.disableMitSlaveMotors()
                                }
                            }

                            Button {
                                text: "发送"
                                enabled: isConnected
                                implicitHeight: 45

                                onClicked: {
                                    let data = parseMitFrame(controlMitFrameField.text)

                                    if (!data) {
                                        errorManager.show("错误：必须8字节HEX")
                                        return
                                    }

                                    let id = canIdField.value

                                    if (isNaN(id)) {
                                        errorManager.show("CAN ID错误")
                                        return
                                    }

                                    EthercatBackend.sendMitFrameQml(
                                                canBus.currentIndex, id, data)
                                }
                            }

                            Button {
                                text: "停止"
                                enabled: isConnected
                                implicitHeight: 45

                                onClicked: {
                                    let id = canIdField.value

                                    if (isNaN(id)) {
                                        errorManager.show("CAN ID错误")
                                        return
                                    }

                                    EthercatBackend.clearMitFrameQml(
                                                canBus.currentIndex, id)
                                }
                            }
                        }
                    }
                    // =========================
                    // MIT CAN 数据帧显示区
                    // =========================
                    Rectangle {

                        Layout.fillWidth: true
                        Layout.preferredHeight: 90 // 稍微加高一点

                        radius: 6
                        color: "#f6f6f6"
                        border.color: "#dddddd"

                        ColumnLayout {

                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 4

                            // =========================
                            // 标题（居中）
                            // =========================
                            Label {
                                Layout.fillWidth: true
                                text: "CAN 数据帧"
                                horizontalAlignment: Text.AlignHCenter

                                font.pixelSize: 12
                                color: "#666"
                            }

                            // =========================
                            // 数据帧（居中显示）
                            // =========================
                            Label {
                                Layout.fillWidth: true
                                id: controlMitFrameField
                                text: formatMit(mitCanFrame)

                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter

                                font.pixelSize: 18
                                font.bold: true
                                color: "#409eff"
                            }
                        }
                    }
                }
            }
        }
    }
}
