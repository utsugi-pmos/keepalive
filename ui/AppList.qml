// SPDX-License-Identifier: GPL-2.0-or-later
//
// The list itself, used by both front-ends: the Settings module and the
// standalone app. Everything it needs arrives through `backend`, so it knows
// nothing about which of the two is showing it.
//
// Nothing here asks to be applied. The module has no Apply button inside
// plasma-settings -- checked on the device, the frame simply does not draw one
// -- so a change that waited to be applied was a change thrown away. The
// backend writes on its own; this file's job is to make that visible.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

ListView {
    id: appList

    // An KeepAliveBackend. Not typed, so this file does not have to be part of
    // the same QML module as the C++ type.
    required property QtObject backend

    model: backend.apps
    currentIndex: -1

    // The cost is real and easy to forget once the switches are flipped, so it
    // is stated up front rather than buried in a tooltip. The figure is
    // measured (PSS of dialer + messages), not guessed.
    header: Kirigami.InlineMessage {
        width: appList.width
        visible: true
        position: Kirigami.InlineMessage.Position.Header
        type: Kirigami.MessageType.Information
        text: i18n("These applications do not close when you swipe their card up: they minimise, so they reopen instantly. In exchange they stay in memory (the dialer and messages take about 125 MB between them).")
    }

    // Switching an app on moves its row up into the other group. Scrolled to,
    // because a row that vanishes from where you tapped it reads as a setting
    // that did not take -- which is exactly how this list used to feel.
    Connections {
        target: appList.backend

        function onAppMoved(index) {
            if (index >= 0) {
                Qt.callLater(() => appList.positionViewAtIndex(index, ListView.Contain));
            }
        }
    }

    delegate: Column {
        id: row

        required property var modelData

        width: ListView.view.width

        // The heading is drawn here, from the model's own firstOfGroup flag,
        // instead of by ListView.section: with a section delegate the two
        // headings came out swapped after a toggle, so the apps that were kept
        // alive sat under "They close normally". Seen on the device.
        Kirigami.ListSectionHeader {
            width: parent.width
            visible: row.modelData.firstOfGroup
            height: visible ? implicitHeight : 0
            text: row.modelData.group ? i18n("Always alive") : i18n("They close normally")
        }

        // Not an ItemDelegate. A delegate is a button, and a button in a
        // scrolling list fires on a scroll that starts on it: dragging the list
        // switched applications on and off by itself. The taps below are
        // TapHandlers with DragThreshold, which cancel the moment the finger
        // moves -- the list scrolls and nothing is toggled -- and act on
        // release, never on touch.
        Item {
            id: delegate

            width: parent.width
            height: content.implicitHeight + Kirigami.Units.largeSpacing * 2

            ColumnLayout {
                id: content

                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    // The whole row is the target: on a phone a bare switch is
                    // a small thing to hit. Cancelled by any drag.
                    TapHandler {
                        gesturePolicy: TapHandler.DragThreshold
                        onTapped: appList.backend.setKeptAlive(row.modelData.appId, !row.modelData.keptAlive)
                    }

                    Kirigami.Icon {
                        source: row.modelData.icon
                        Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                        Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: row.modelData.name
                            elide: Text.ElideRight
                        }

                        // The identifier is shown because it is what actually
                        // gets matched, and it is not always guessable from the
                        // name: PocoNav identifies itself as plain "poconav".
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: row.modelData.appId
                            elide: Text.ElideRight
                            font: Kirigami.Theme.smallFont
                            opacity: 0.6
                        }
                    }

                    QQC2.Switch {
                        id: appSwitch

                        checked: row.modelData.keptAlive

                        // Toggling a Switch assigns to `checked`, which throws
                        // away the binding above. The binding is put back, or
                        // the switch would keep showing whatever the finger
                        // left there rather than what the backend decided.
                        onToggled: {
                            appList.backend.setKeptAlive(row.modelData.appId, checked);
                            checked = Qt.binding(() => row.modelData.keptAlive);
                        }
                    }
                }

                // Second setting, shown only once the app is kept alive: an app
                // that closes normally already leaves the list by itself, so
                // offering to hide it would be offering nothing.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.iconSizes.medium + Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.largeSpacing
                    visible: row.modelData.keptAlive

                    // Its own handler, so a tap down here does not reach the
                    // row above it and switch the application off.
                    TapHandler {
                        gesturePolicy: TapHandler.DragThreshold
                        onTapped: appList.backend.setHidden(row.modelData.appId, !row.modelData.hidden)
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Hide it from the list of open apps")
                        elide: Text.ElideRight
                        font: Kirigami.Theme.smallFont
                        opacity: 0.8
                    }

                    QQC2.Switch {
                        id: hiddenSwitch

                        checked: row.modelData.hidden
                        onToggled: {
                            appList.backend.setHidden(row.modelData.appId, checked);
                            checked = Qt.binding(() => row.modelData.hidden);
                        }
                    }
                }
            }
        }
    }

    // Pinned to the view, not to the scrolling content -- hence the explicit
    // parent. Says out loud that the change is on disk: this list looked broken
    // for exactly as long as nothing ever confirmed that.
    Kirigami.InlineMessage {
        id: savedMessage

        parent: appList
        z: 10
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: Kirigami.Units.largeSpacing
        }

        type: Kirigami.MessageType.Positive
        text: i18n("Saved")
        visible: false

        Timer {
            id: savedTimer
            interval: 2500
            onTriggered: savedMessage.visible = false
        }

        Connections {
            target: appList.backend

            function onSaved() {
                savedMessage.visible = true;
                savedTimer.restart();
            }
        }
    }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: appList.count === 0
        text: i18n("No installed application was found")
    }
}
