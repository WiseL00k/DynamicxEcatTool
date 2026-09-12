import QtQuick

Rectangle {
    id: root

    property var theme
    property int padding: theme ? theme.space16 : 16
    property color backgroundColor: theme ? theme.surface : "transparent"
    property color borderColor: theme ? theme.border : "transparent"
    property real cornerRadius: theme ? theme.radiusMedium : 8
    default property alias contentData: content.data
    property alias contentItem: content

    readonly property Item firstContentItem: content.children.length > 0
                                              ? content.children[0]
                                              : null

    color: backgroundColor
    radius: cornerRadius
    border.width: 1
    border.color: borderColor
    implicitWidth: firstContentItem && firstContentItem.implicitWidth > 0
                   ? firstContentItem.implicitWidth + padding * 2
                   : 240
    implicitHeight: firstContentItem && firstContentItem.implicitHeight > 0
                    ? firstContentItem.implicitHeight + padding * 2
                    : 120

    Item {
        id: content
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
