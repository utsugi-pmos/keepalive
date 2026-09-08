// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Settings module front-end. The list is AppList.qml, shared with the
// standalone app; the only thing this adds is the KCM frame, whose Apply
// button is driven by the backend's dirty flag from kcm.cpp.

import QtQuick

import org.kde.kcmutils as KCM

KCM.ScrollViewKCM {
    id: root

    view: AppList {
        backend: kcm.backend
    }
}
