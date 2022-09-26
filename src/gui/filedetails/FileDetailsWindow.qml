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
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

import com.nextcloud.desktopclient 1.0
import Style 1.0

ApplicationWindow {
    id: root

    property var accountState
    property string localPath: ""

    flags: Qt.Dialog
    width: 400
    height: 500

    title: qsTr("File details of %1 · %2").arg(fileDetailsPage.fileDetails.name).arg(Systray.windowTitle)

    FileDetailsPage {
        id: fileDetailsPage
        anchors.fill: parent
        accountState: root.accountState
        localPath: root.localPath
    }
}
