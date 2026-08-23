import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control

    property var theme
    property url iconSource
    property bool selected: false
    property bool collapsed: false
    property bool sessionOwned: false

    checkable: true
    checked: selected
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    leftPadding: collapsed ? 0 : theme.space12
    rightPadding: collapsed ? 0 : theme.space12
    implicitHeight: 44

    Accessible.name: text
    ToolTip.visible: collapsed && hovered
    ToolTip.text: sessionOwned ? text + qsTr("\n当前总线任务") : text
    ToolTip.delay: 450

    background: Rectangle {
        radius: control.theme.radiusMedium
        color: control.selected
               ? control.theme.selectedBackground
               : control.down || control.hovered
                 ? control.theme.controlHover
                 : "transparent"
        border.width: control.visualFocus ? 1 : 0
        border.color: control.theme.accent

        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 22
            radius: 2
            visible: control.selected
            color: control.theme.accent
        }

        Behavior on color {
            ColorAnimation { duration: control.theme.animationFast }
        }
    }

    contentItem: RowLayout {
        spacing: control.theme.space10

        Item { Layout.preferredWidth: control.collapsed ? 0 : control.theme.space2 }

        Image {
            source: control.iconSource
            sourceSize.width: 19
            sourceSize.height: 19
            Layout.preferredWidth: 19
            Layout.preferredHeight: 19
            opacity: control.selected ? 1.0 : 0.78
        }

        Text {
            visible: !control.collapsed
            Layout.fillWidth: true
            text: control.text
            color: control.selected ? control.theme.accentText : control.theme.textSecondary
            font.pixelSize: control.theme.fontBody
            font.weight: control.selected ? Font.DemiBold : Font.Medium
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Rectangle {
            visible: control.sessionOwned
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            radius: 4
            color: control.theme.success
        }
    }
}
