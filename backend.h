// SPDX-License-Identifier: GPL-2.0-or-later
//
// Shared brain of both front-ends: the Settings module (kcm.cpp) and the
// standalone application (main.cpp). They differ only in their frame -- one is
// a KCM plugin, the other a window with an Apply button -- so all the logic
// lives here and neither owns a copy of it.
//
// The standalone app exists because a KCM is a plugin loaded into
// plasma-settings and therefore tied to the KF6 ABI: an update can leave it
// failing to load. kcm_lookandfeel.so on this very device is already in that
// state ("symbol not found" in the session log). The app only needs Qt and
// Kirigami, so the settings stay reachable when that happens.

#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>

class KeepAliveBackend : public QObject
{
    Q_OBJECT

    // One entry per installed application:
    //   {appId, name, icon, keptAlive, hidden, group, firstOfGroup}
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)

    // Whether there are changes not yet written to disk. Short-lived: a toggle
    // is written on its own a moment later. The KCM maps this onto
    // setNeedsSave() so a frame that does offer an Apply button still works.
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

public:
    explicit KeepAliveBackend(QObject *parent = nullptr);
    ~KeepAliveBackend() override;

    QVariantList apps() const;
    bool dirty() const;

    Q_INVOKABLE void setKeptAlive(const QString &appId, bool keep);
    Q_INVOKABLE void setHidden(const QString &appId, bool hide);

    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void restoreDefaults();

Q_SIGNALS:
    void appsChanged();
    void dirtyChanged();

    // The list was re-sorted and the app that was just toggled is now at this
    // row. The list scrolls to it, because a row that silently moves to the
    // other end reads as "it did not take".
    void appMoved(int index);

    // Written to disk. The interface says so: this module used to look broken
    // precisely because nothing ever confirmed a change had been kept.
    void saved();

private:
    QSet<QString> readList(const QString &key, const QStringList &fallback) const;
    QSet<QString> readHidden() const;
    void writeSkipSwitcherRules(const QStringList &hidden);
    void reloadTaskSwitcher();
    void rebuild();
    void setDirty(bool dirty);
    void scheduleSave();
    void launchNewlyKept();
    void configChangedOnDisk();
    void rearmWatch();
    static QString configPath();
    int indexOf(const QString &appId) const;

    QVariantList m_apps;
    QSet<QString> m_kept;
    QSet<QString> m_hidden;
    bool m_dirty = false;

    // Toggles are written on their own, a moment after the last one, so a
    // burst of taps costs a single write and a single switcher reload.
    QTimer m_saveTimer;

    // Applications switched on since the last write, to be opened once it
    // lands. Marking an app "always alive" while it is not running used to do
    // nothing at all until the next time it was opened by hand.
    QSet<QString> m_toLaunch;

    // Both front-ends can be open at once, and either can sit in the background
    // for hours. Without this, the one that had been open longest wrote its own
    // stale snapshot on top and settings came back from the dead.
    QFileSystemWatcher m_watcher;
};
