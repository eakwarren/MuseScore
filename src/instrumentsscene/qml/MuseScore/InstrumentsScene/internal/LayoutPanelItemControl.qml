/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
import QtQuick 2.15
import QtQuick.Layouts 1.15

import Muse.Ui 1.0
import Muse.UiComponents 1.0

ListItemBlank {
    id: root

    property int sideMargin: 12

    normalColor: ui.theme.textFieldColor
    navigation.column: 0
    navigation.accessible.name: titleLabel.text

    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined

    height: parent ? parent.height : implicitHeight
    width: parent ? parent.width : implicitWidth

    implicitHeight: 34
    implicitWidth: 248

    background.border.width: 0

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: sideMargin + 68 // + 12 * styleData.depth
        spacing: 8

        FlatButton {
            id: addButton

            // override larger min width default from Muse properties
            buttonType: FlatButton.Custom

            Layout.preferredHeight: 24

            // 25 = 3 x 8 margins + 1 to balance space in + icon
            implicitWidth: plusIcon.implicitWidth + titleLabel.implicitWidth + 25


            Layout.alignment: Qt.AlignVCenter

            Item {

                anchors.fill: parent
                anchors.leftMargin: 8

                StyledIconLabel {
                    id: plusIcon

                    iconCode: IconCode.PLUS
                    anchors.verticalCenter: parent.verticalCenter
                }

                StyledTextLabel {
                    id: titleLabel

                    text: model ? model.itemRole.title : ""

                    anchors.left: plusIcon.right
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter

                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                }
            }

            onClicked: root.clicked(null)
        }
    }

    SeparatorLine { anchors.bottom: parent.bottom; }
}
