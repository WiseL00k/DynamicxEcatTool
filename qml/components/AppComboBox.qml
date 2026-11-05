pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls

ComboBox {
    id: control

    property var theme

    implicitWidth: 120
    implicitHeight: theme.controlHeight
    leftPadding: theme.space10 + (mirrored ? indicator.width + theme.space8 : 0)
    rightPadding: theme.space10 + (mirrored ? 0 : indicator.width + theme.space8)
    topPadding: theme.space4
    bottomPadding: theme.space4
    font.family: theme.fontFamily
    font.pixelSize: theme.fontBody
    hoverEnabled: true
    palette.window: theme.surfaceRaised
    palette.windowText: theme.textPrimary
    palette.base: theme.inputBackground
    palette.text: theme.textPrimary
    palette.button: theme.controlBackground
    palette.buttonText: theme.textPrimary
    palette.highlight: theme.accent
    palette.highlightedText: theme.textOnAccent
    palette.mid: theme.borderStrong
    palette.dark: theme.textMuted

    contentItem: TextField {
        padding: 0
        text: control.editable ? control.editText : control.displayText
        font: control.font
        color: control.enabled ? control.theme.textPrimary : control.theme.textDisabled
        selectionColor: control.theme.accent
        selectedTextColor: control.theme.textOnAccent
        verticalAlignment: TextInput.AlignVCenter
        enabled: control.editable
        autoScroll: control.editable
        readOnly: control.down
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        selectByMouse: control.selectTextByMouse
        clip: true
        background: null
    }

    indicator: Canvas {
        x: control.mirrored ? control.theme.space10 : control.width - width - control.theme.space10
        y: (control.height - height) / 2
        width: 10; height: 6
        property color arrowColor: control.enabled ? control.theme.textSecondary : control.theme.textDisabled
        onArrowColorChanged: requestPaint()
        onPaint: {
            const context = getContext("2d")
            context.clearRect(0, 0, width, height)
            context.fillStyle = arrowColor
            context.beginPath()
            context.moveTo(0, 0)
            context.lineTo(width, 0)
            context.lineTo(width / 2, height)
            context.closePath()
            context.fill()
        }
    }

    background: Rectangle {
        radius: control.theme.radiusSmall
        color: control.theme.inputBackground
        readonly property bool focused: control.activeFocus || control.contentItem.activeFocus
        border.width: focused ? 2 : 1
        border.color: focused ? control.theme.accent
            : control.enabled ? control.theme.borderStrong : control.theme.border
    }

    delegate: ItemDelegate {
        id: option
        required property var model
        required property int index
        width: ListView.view.width
        implicitHeight: control.theme.controlHeight
        leftPadding: control.theme.space10
        rightPadding: control.theme.space10
        topPadding: 0
        bottomPadding: 0
        text: control.textRole.length > 0 ? model[control.textRole] : control.textAt(index)
        font: control.font
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
        contentItem: Text {
            text: option.text
            font: option.font
            color: option.highlighted ? control.theme.accentText : control.theme.textPrimary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: control.theme.radiusSmall - 1
            color: option.highlighted ? control.theme.selectedBackground
                : option.hovered ? control.theme.controlHover : "transparent"
        }
    }

    popup: Popup {
        y: control.height + control.theme.space4
        width: control.width
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                         Math.max(0, control.Window.height - topMargin - bottomMargin))
        topMargin: control.theme.space6
        bottomMargin: control.theme.space6
        padding: control.theme.space4
        font: control.font
        palette: control.palette
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightRangeMode: ListView.ApplyRange
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar { }
        }
        background: Rectangle {
            radius: control.theme.radiusSmall
            color: control.theme.surfaceRaised
            border.color: control.theme.borderStrong
        }
    }
}
