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
import QtQuick.Layouts 1.2
import QtQuick.Controls 2.15

import com.nextcloud.desktopclient 1.0
import Style 1.0
import "../tray"
import "../"

ColumnLayout {
    id: root

    property string localPath: ""
    property var accountState: ({})
    property FileDetails fileDetails: FileDetails {}
    property int horizontalPadding: 0
    property int iconSize: 32

    readonly property bool sharingPossible: shareModel && shareModel.canShare && shareModel.sharingEnabled
    readonly property bool userGroupSharingPossible: sharingPossible && shareModel.userGroupSharingEnabled
    readonly property bool publicLinkSharingPossible: sharingPossible && shareModel.publicLinkSharesEnabled

    readonly property bool loading: sharingPossible && (!shareModel ||
                                                        shareModel.fetchOngoing ||
                                                        !shareModel.hasInitialShareFetchCompleted ||
                                                        waitingForSharesToChange)
    property bool waitingForSharesToChange: true // Gets changed to false when listview count changes
    property bool stopWaitingForSharesToChangeOnPasswordError: false

    readonly property ShareModel shareModel: ShareModel {
        accountState: root.accountState
        localPath: root.localPath

        onServerError: {
            if(errorBox.text === "") {
                errorBox.text = message;
            } else {
                errorBox.text += "\n\n" + message
            }

            errorBox.visible = true;
        }

        onPasswordSetError: if(root.stopWaitingForSharesToChangeOnPasswordError) {
            root.waitingForSharesToChange = false;
            root.stopWaitingForSharesToChangeOnPasswordError = false;
        }

        onRequestPasswordForLinkShare: shareRequiresPasswordDialog.open()
        onRequestPasswordForEmailSharee: {
            shareRequiresPasswordDialog.sharee = sharee;
            shareRequiresPasswordDialog.open();
        }
    }

    Dialog {
        id: shareRequiresPasswordDialog

        property var sharee

        function discardDialog() {
            sharee = undefined;
            root.waitingForSharesToChange = false;
            close();
        }

        anchors.centerIn: parent
        width: parent.width * 0.8

        title: qsTr("Password required for new share")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        closePolicy: Popup.NoAutoClose

        // TODO: Rather than setting all these palette colours manually,
        // create a custom style and do it for all components globally
        palette {
            text: Style.ncTextColor
            windowText: Style.ncTextColor
            buttonText: Style.ncTextColor
            light: Style.lightHover
            midlight: Style.lightHover
            mid: Style.ncSecondaryTextColor
            dark: Style.menuBorder
            button: Style.menuBorder
            window: Style.backgroundColor
            base: Style.backgroundColor
        }

        visible: false

        onAccepted: {
            if(sharee) {
                root.shareModel.createNewUserGroupShareWithPasswordFromQml(sharee, dialogPasswordField.text);
                sharee = undefined;
            } else {
                root.shareModel.createNewLinkShareWithPassword(dialogPasswordField.text);
            }

            root.stopWaitingForSharesToChangeOnPasswordError = true;
            dialogPasswordField.text = "";
        }
        onDiscarded: discardDialog()
        onRejected: discardDialog()

        NCInputTextField {
            id: dialogPasswordField

            anchors.left: parent.left
            anchors.right: parent.right

            placeholderText: qsTr("Share password")
            onAccepted: shareRequiresPasswordDialog.accept()
        }
    }

    ErrorBox {
        id: errorBox

        Layout.fillWidth: true
        Layout.leftMargin: root.horizontalPadding
        Layout.rightMargin: root.horizontalPadding

        showCloseButton: true
        visible: false

        onCloseButtonClicked: {
            text = "";
            visible = false;
        }
    }

    ShareeSearchField {
        Layout.fillWidth: true
        Layout.leftMargin: root.horizontalPadding
        Layout.rightMargin: root.horizontalPadding

        visible: root.userGroupSharingPossible
        enabled: visible && !root.loading

        accountState: root.accountState
        shareItemIsFolder: root.fileDetails && root.fileDetails.isFolder

        onShareeSelected: {
            root.waitingForSharesToChange = true;
            root.shareModel.createNewUserGroupShareFromQml(sharee)
        }
    }

    Loader {
        id: sharesViewLoader

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: root.horizontalPadding
        Layout.rightMargin: root.horizontalPadding

        active: root.sharingPossible

        sourceComponent: ScrollView {
            id: scrollView
            anchors.fill: parent

            contentWidth: availableWidth
            clip: true
            enabled: root.sharingPossible

            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            data: WheelHandler {
                target: scrollView.contentItem
            }

            ListView {
                id: shareLinksListView

                enabled: !root.loading
                model: SortedShareModel {
                    shareModel: root.shareModel
                }
                onCountChanged: root.waitingForSharesToChange = false;

                delegate: ShareDelegate {
                    id: shareDelegate

                    Connections {
                        target: root.shareModel
                        // Though we try to handle this internally by listening to onPasswordChanged,
                        // with passwords we will get the same value from the model data when a
                        // password set has failed, meaning we won't be able to easily tell when we
                        // have had a response from the server in QML. So we listen to this signal
                        // directly from the model and do the reset of the password field manually.
                        function onPasswordSetError(shareId, errorCode, errorMessage) {
                            if(shareId !== model.shareId) {
                                return;
                            }
                            shareDelegate.resetPasswordField();
                            shareDelegate.showPasswordSetError(errorMessage);
                        }
                    }

                    iconSize: root.iconSize
                    canCreateLinkShares: root.publicLinkSharingPossible

                    onCreateNewLinkShare: {
                        shareModel.createNewLinkShare();
                        root.waitingForSharesToChange = true;
                    }
                    onDeleteShare: {
                        shareModel.deleteShareFromQml(model.share);
                        root.waitingForSharesToChange = true;
                    }

                    onToggleAllowEditing: shareModel.toggleShareAllowEditingFromQml(model.share, enable)
                    onToggleAllowResharing: shareModel.toggleShareAllowResharingFromQml(model.share, enable)
                    onTogglePasswordProtect: shareModel.toggleSharePasswordProtectFromQml(model.share, enable)
                    onToggleExpirationDate: shareModel.toggleShareExpirationDateFromQml(model.share, enable)
                    onToggleNoteToRecipient: shareModel.toggleShareNoteToRecipientFromQml(model.share, enable)

                    onSetLinkShareLabel: shareModel.setLinkShareLabelFromQml(model.share, label)
                    onSetExpireDate: shareModel.setShareExpireDateFromQml(model.share, milliseconds)
                    onSetPassword: shareModel.setSharePasswordFromQml(model.share, password)
                    onSetNote: shareModel.setShareNoteFromQml(model.share, note)
                }

                Loader {
                    id: sharesFetchingLoader
                    anchors.fill: parent
                    active: root.loading
                    z: Infinity

                    sourceComponent: Rectangle {
                        color: Style.backgroundColor
                        opacity: 0.5

                        NCBusyIndicator {
                            anchors.centerIn: parent
                            color: Style.ncSecondaryTextColor
                        }
                    }
                }
            }
        }
    }

    Loader {
        id: sharingNotPossibleView

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: root.horizontalPadding
        Layout.rightMargin: root.horizontalPadding

        active: !root.sharingPossible

        sourceComponent: Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter

            Label {
                id: sharingDisabledLabel
                width: parent.width
                text: qsTr("Sharing is disabled")
                color: Style.ncSecondaryTextColor
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            Label {
                width: parent.width
                text: qsTr("This item cannot be shared.")
                color: Style.ncSecondaryTextColor
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                visible: !root.shareModel.canShare
            }
            Label {
                width: parent.width
                text: qsTr("Sharing is disabled.")
                color: Style.ncSecondaryTextColor
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                visible: !root.shareModel.sharingEnabled
            }
        }
    }
}
