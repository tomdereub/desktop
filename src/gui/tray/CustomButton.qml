import QtQuick 2.15
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.2
import Style 1.0

Button {
    id: root

    property string imageSource: ""
    property string imageSourceHover: ""

    property string toolTipText: ""

    property color textColor: Style.ncTextColor
    property color textColorHovered: textColor

    property alias bgColor: bgRectangle.color

    property bool bold: false

    property real bgOpacity: 0.3

    background: NCButtonBackground {
        id: bgRectangle
        hovered: root.hovered
    }

    leftPadding: root.text === "" ? 5 : 10
    rightPadding: root.text === "" ? 5 : 10

    ToolTip {
        id: customButtonTooltip
        text: root.toolTipText
        delay: Qt.styleHints.mousePressAndHoldInterval
        visible: root.toolTipText !== "" && root.hovered
        contentItem: Label {
            text: customButtonTooltip.text
            color: Style.ncTextColor
        }
        background: Rectangle {
            border.color: Style.menuBorder
            color: Style.backgroundColor
        }
    }

    contentItem: NCButtonContents {
        hovered: root.hovered
        imageSourceHover: root.imageSourceHover
        imageSource: root.imageSource
        text: root.text
        textColor: root.textColor
        textColorHovered: root.textColorHovered
        bold: root.bold
    }
}
