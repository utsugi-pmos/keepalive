// SPDX-License-Identifier: GPL-2.0-or-later
//
// The standalone application. Same list as the Settings module, in a plain
// Kirigami window: it depends only on Qt and Kirigami, so it keeps working if a
// Plasma update ever leaves the KCM unable to load.
//
// There is no Apply button any more, in either front-end. The module never had
// one inside plasma-settings -- the frame does not draw it -- so having one
// here only taught that changes needed applying, and every change made in the
// module was silently lost. The backend writes on its own now.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    title: i18n("Always-alive apps")

    width: Kirigami.Units.gridUnit * 26
    height: Kirigami.Units.gridUnit * 45

    // Without this the page's actions are filed away in the contextual drawer
    // and the header shows nothing but the title -- checked on the device, the
    // Close button was simply not there. ToolBar puts it next to the title,
    // where the module inside Settings has its back arrow.
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.ToolBar

    pageStack.initialPage: Kirigami.ScrollablePage {
        id: page

        title: i18n("Always-alive apps")

        // Kirigami.ScrollablePage takes a single flickable child and scrolls it.
        AppList {
            backend: keepAliveBackend
        }

        // A window with no frame and no back arrow has no other way out: on
        // this phone the page header is all there is.
        actions: [
            Kirigami.Action {
                text: i18n("Close")
                icon.name: "window-close"
                displayHint: Kirigami.DisplayHint.KeepVisible
                onTriggered: root.close()
            }
        ]
    }
}
