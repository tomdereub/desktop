/*
 * Copyright (C) 2022 by Claudio Cambra <claudio.cambra@nextcloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import com.nextcloud.desktopclient 1.0
import Style 1.0

TextField {
    id: root

    property color accentColor: Style.ncBlue
    property color secondaryColor: Style.menuBorder
    property alias submitButton: submitButton

    implicitHeight: Style.talkReplyTextFieldPreferredHeight
    color: Style.ncTextColor
    placeholderTextColor: secondaryColor

    rightPadding: submitButton.width

    selectByMouse: true

    background: Rectangle {
        id: textFieldBorder
        radius: Style.slightlyRoundedButtonRadius
        border.width: Style.normalBorderWidth
        border.color: root.activeFocus ? root.accentColor : root.secondaryColor
        color: Style.backgroundColor
    }

    Button {
        id: submitButton

        anchors.top: root.top
        anchors.right: root.right
        anchors.margins: 1

        width: height
        height: parent.height

        background: null
        flat: true
        icon.source: "image://svgimage-custom-color/confirm.svg" + "/" + root.secondaryColor
        icon.color: hovered && enabled ? UserModel.currentUser.accentColor : root.secondaryColor

        enabled: root.text !== ""

        onClicked: root.accepted()
    }
}

