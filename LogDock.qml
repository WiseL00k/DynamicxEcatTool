import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var theme
    property string titleText: qsTr("上下文日志")
    property string contextName: ""
    property string logText: ""
    property bool expanded: false
    property bool followTail: true
    property bool wrapLines: false
    property int expandedHeight: theme ? theme.logDockExpandedHeight : 190
    signal toggleRequested(bool expanded)
    signal clearRequested()

    function scrollToTail() {
        if (!expanded || !followTail || logArea.selectedText.length > 0)
            return
        Qt.callLater(function() {
            if (!root.followTail || logArea.selectedText.length > 0)
                return
            root.internalScroll = true
            logArea.cursorPosition = logArea.length
            verticalBar.position = Math.max(0, 1 - verticalBar.size)
            root.internalScroll = false
        })
    }

    function copyAll() {
        const oldStart = logArea.selectionStart
        const oldEnd = logArea.selectionEnd
        internalSelectionChange = true
        logArea.selectAll()
        logArea.copy()
        logArea.select(oldStart, oldEnd)
        internalSelectionChange = false
    }

    property bool internalScroll: false
    property bool internalSelectionChange: false

    onLogTextChanged: scrollToTail()
    onFollowTailChanged: {
        if (followTail)
            scrollToTail()
    }

    implicitHeight: expanded ? expandedHeight : theme.logDockCollapsedHeight
    radius: theme.radiusMedium
    color: theme.surface
    border.width: 1
    border.color: theme.border
    clip: true

    Behavior on implicitHeight {
        NumberAnimation { duration: root.theme.animationNormal; easing.type: Easing.OutCubic }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.theme.logDockCollapsedHeight

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: root.theme.space12
                anchors.rightMargin: root.theme.space8
                spacing: root.theme.space8

                Image {
                    source: Qt.resolvedUrl("icons/terminal.svg")
                    sourceSize.width: 16
                    sourceSize.height: 16
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    opacity: 0.78
                }

                Text {
                    text: root.titleText
                    color: root.theme.textPrimary
                    font.pixelSize: root.theme.fontBody
                    font.weight: Font.DemiBold
                }

                StatusBadge {
                    visible: root.contextName.length > 0
                    theme: root.theme
                    text: root.contextName
                    tone: "neutral"
                    showDot: false
                    compact: true
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    visible: root.expanded
                    theme: root.theme
                    text: root.followTail ? qsTr("暂停跟随") : qsTr("恢复跟随")
                    variant: root.followTail ? "ghost" : "secondary"
                    implicitHeight: root.theme.controlHeightSmall
                    onClicked: root.followTail = !root.followTail
                }

                AppButton {
                    visible: root.expanded
                    theme: root.theme
                    text: root.wrapLines ? qsTr("不换行") : qsTr("自动换行")
                    variant: "ghost"
                    implicitHeight: root.theme.controlHeightSmall
                    onClicked: root.wrapLines = !root.wrapLines
                }

                AppButton {
                    visible: root.expanded
                    theme: root.theme
                    text: qsTr("复制全部")
                    variant: "ghost"
                    implicitHeight: root.theme.controlHeightSmall
                    actionEnabled: root.logText.length > 0
                    onClicked: root.copyAll()
                }

                AppButton {
                    visible: root.expanded
                    theme: root.theme
                    text: qsTr("清空")
                    variant: "ghost"
                    implicitHeight: root.theme.controlHeightSmall
                    onClicked: root.clearRequested()
                }

                AppButton {
                    theme: root.theme
                    text: root.expanded ? qsTr("收起") : qsTr("展开")
                    variant: "ghost"
                    implicitHeight: root.theme.controlHeightSmall
                    onClicked: root.toggleRequested(!root.expanded)
                }
            }
        }

        Rectangle {
            visible: root.expanded
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 1 : 0
            color: root.theme.divider
        }

        ScrollView {
            visible: root.expanded
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ScrollBar.vertical: ScrollBar {
                id: verticalBar
                onPressedChanged: {
                    if (pressed && !root.internalScroll)
                        root.followTail = false
                }
            }

            TextArea {
                id: logArea
                text: root.logText
                readOnly: true
                selectByMouse: true
                wrapMode: root.wrapLines ? TextArea.Wrap : TextArea.NoWrap
                padding: root.theme.space10
                color: root.theme.textSecondary
                font.family: root.theme.fontFamily
                font.pixelSize: root.theme.fontBodySmall
                background: Rectangle { color: root.theme.inputBackground }

                onSelectedTextChanged: {
                    if (!root.internalSelectionChange && selectedText.length > 0)
                        root.followTail = false
                }

                WheelHandler {
                    onWheel: function(event) {
                        root.followTail = false
                        event.accepted = false
                    }
                }
            }
        }
    }
}
