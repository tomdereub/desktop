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

TextField {
    id: root

    signal shareeSelected(var sharee)

    property var accountState: ({})
    property bool shareItemIsFolder: false
    property ShareeModel shareeModel: ShareeModel {
        accountState: root.accountState
        shareItemIsFolder: root.shareItemIsFolder
        searchString: root.text
    }

    readonly property int horizontalPaddingOffset: Style.trayHorizontalMargin
    readonly property color placeholderColor: Style.menuBorder
    readonly property double iconsScaleFactor: 0.6

    function triggerSuggestionsVisibility() {
        shareeListView.count > 0 && text !== "" ? suggestionsPopup.open() : suggestionsPopup.close();
    }

    placeholderText: qsTr("Search for users or groups…")
    placeholderTextColor: placeholderColor
    color: Style.ncTextColor
    enabled: !shareeModel.fetchOngoing

    onActiveFocusChanged: triggerSuggestionsVisibility()
    onTextChanged: triggerSuggestionsVisibility()
    Keys.onPressed: {
        if(suggestionsPopup.visible) {
            switch(event.key) {
            case Qt.Key_Escape:
                suggestionsPopup.close();
                shareeListView.currentIndex = -1;
                event.accepted = true;
                break;

            case Qt.Key_Up:
                shareeListView.decrementCurrentIndex();
                event.accepted = true;
                break;

            case Qt.Key_Down:
                shareeListView.incrementCurrentIndex();
                event.accepted = true;
                break;

            case Qt.Key_Enter:
            case Qt.Key_Return:
                if(shareeListView.currentIndex > -1) {
                    shareeListView.itemAtIndex(shareeListView.currentIndex).selectSharee();
                    event.accepted = true;
                    break;
                }
            }
        } else {
            switch(event.key) {
            case Qt.Key_Down:
                triggerSuggestionsVisibility();
                event.accepted = true;
                break;
            }
        }
    }

    leftPadding: searchIcon.width + searchIcon.anchors.leftMargin + horizontalPaddingOffset
    rightPadding: clearTextButton.width + clearTextButton.anchors.rightMargin + horizontalPaddingOffset

    background: Rectangle {
        radius: 5
        border.color: parent.activeFocus ? UserModel.currentUser.accentColor : Style.menuBorder
        border.width: 1
        color: Style.backgroundColor
    }

    Image {
        id: searchIcon
        anchors {
            top: parent.top
            left: parent.left
            bottom: parent.bottom
            margins: 4
        }

        width: height

        smooth: true
        antialiasing: true
        mipmap: true
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignLeft

        source: "image://svgimage-custom-color/search.svg" + "/" + root.placeholderColor
        sourceSize: Qt.size(parent.height * root.iconsScaleFactor, parent.height * root.iconsScaleFactor)

        visible: !root.shareeModel.fetchOngoing
    }

    NCBusyIndicator {
        id: busyIndicator

        anchors {
            top: parent.top
            left: parent.left
            bottom: parent.bottom
        }

        width: height
        color: root.placeholderColor
        visible: root.shareeModel.fetchOngoing
        running: visible
    }

    Image {
        id: clearTextButton

        anchors {
            top: parent.top
            right: parent.right
            bottom: parent.bottom
            margins: 4
        }

        width: height

        smooth: true
        antialiasing: true
        mipmap: true
        fillMode: Image.PreserveAspectFit

        source: "image://svgimage-custom-color/clear.svg" + "/" + root.placeholderColor
        sourceSize: Qt.size(parent.height * root.iconsScaleFactor, parent.height * root.iconsScaleFactor)

        visible: root.text

        MouseArea {
            id: clearTextButtonMouseArea
            anchors.fill: parent
            onClicked: root.clear()
        }
    }

    Popup {
        id: suggestionsPopup

        width: root.width
        height: 100
        y: root.height

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

        contentItem: ScrollView {
            id: suggestionsScrollView

            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ListView {
                id: shareeListView

                spacing: 0
                currentIndex: -1
                interactive: true

                highlight: Rectangle {
                    width: shareeListView.currentItem.width
                    height: shareeListView.currentItem.height
                    color: Style.lightHover
                }
                highlightFollowsCurrentItem: true
                highlightMoveDuration: 0
                highlightResizeDuration: 0
                highlightRangeMode: ListView.ApplyRange
                preferredHighlightBegin: 0
                preferredHighlightEnd: suggestionsScrollView.height

                onCountChanged: root.triggerSuggestionsVisibility()

                model: root.shareeModel
                delegate: ShareeDelegate {
                    anchors.left: parent.left
                    anchors.right: parent.right

                    function selectSharee() {
                        root.shareeSelected(model.sharee);
                        suggestionsPopup.close();

                        root.clear();
                    }

                    onHoveredChanged: if (hovered) {
                        // When we set the currentIndex the list view will scroll...
                        // unless we tamper with the preferred highlight points to stop this.
                        const savedPreferredHighlightBegin = shareeListView.preferredHighlightBegin;
                        const savedPreferredHighlightEnd = shareeListView.preferredHighlightEnd;
                        // Set overkill values to make sure no scroll happens when we hover with mouse
                        shareeListView.preferredHighlightBegin = -suggestionsScrollView.height;
                        shareeListView.preferredHighlightEnd = suggestionsScrollView.height * 2;

                        shareeListView.currentIndex = index

                        // Reset original values so keyboard navigation makes list view scroll
                        shareeListView.preferredHighlightBegin = savedPreferredHighlightBegin;
                        shareeListView.preferredHighlightEnd = savedPreferredHighlightEnd;
                    }
                    onClicked: selectSharee()
                }
            }
        }
    }
}
