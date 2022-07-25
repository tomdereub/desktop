import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

import Style 1.0

Item {
    id: errorBox

    signal closeButtonClicked
    
    property string text: ""

    property color color: Style.errorBoxTextColor
    property color backgroundColor: Style.errorBoxBackgroundColor
    property color borderColor: Style.errorBoxBorderColor
    property bool showCloseButton: false
    
    implicitHeight: errorMessage.implicitHeight + 2 * 8

    Rectangle {
        anchors.fill: parent
        color: errorBox.backgroundColor
        border.color: errorBox.borderColor
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8

        Label {
            id: errorMessage

            Layout.fillWidth: true
            Layout.fillHeight: true
            color: errorBox.color
            wrapMode: Text.WordWrap
            text: errorBox.text
            textFormat: Text.PlainText
        }

        Button {
            Layout.fillHeight: true

            background: null
            icon.color: Style.ncTextColor
            icon.source: "qrc:///client/theme/close.svg"

            visible: errorBox.showCloseButton
            enabled: visible

            onClicked: errorBox.closeButtonClicked()
        }
    }
}
