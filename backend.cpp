// SPDX-License-Identifier: GPL-2.0-or-later

#include "backend.h"

#include <algorithm>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

#include <KApplicationTrader>
#include <KConfigGroup>
#include <KSharedConfig>

namespace
{
// Applications shipped as always-alive when the file does not exist yet. Both
// were measured to be worth it: the dialer is not resident at all (nothing runs
// until a call arrives) and takes seconds to show a window.
const QStringList defaultApps = {QStringLiteral("org.kde.plasma.dialer"), QStringLiteral("org.kde.spacebar")};

// Every window rule this owns is named with it, so rules written elsewhere are
// never rewritten or deleted.
const QLatin1String rulePrefix{"keepalive-"};

const QLatin1String configFile{"keepaliverc"};

// Opens the applications that were just switched on, minimized.
//
// It used to be looked for under $HOME/.local/bin, where an installer script
// put it. Since it ships in a package it lives in /usr/bin, and on a phone
// installed from the image that $HOME path does not exist at all: marking an
// application simply did nothing, with the reason only in a qWarning nobody
// reads. Looked up on PATH first -- a KCM does inherit the session's PATH, and
// /usr/bin is on it -- with the packaged path as the fallback that does not
// depend on the environment being sane.
const QLatin1String launcherName{"keepalive-launch"};
const QLatin1String launcherPath{"/usr/bin/keepalive-launch"};

// How long to wait after the last toggle before writing. Long enough that
// flipping several switches in a row costs one write and one switcher reload,
// short enough that leaving the module straight after a tap still lands.
constexpr int saveDelayMs = 400;
}

KeepAliveBackend::KeepAliveBackend(QObject *parent)
    : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(saveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &KeepAliveBackend::save);

    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &KeepAliveBackend::configChangedOnDisk);

    load();
}

// A pending write must not die with the window. Backing out of the module
// within the delay used to be a way to lose the change without being told.
KeepAliveBackend::~KeepAliveBackend()
{
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();

        // Silently: dirtyChanged reaches the KCM, and by the time a child is
        // destroyed its parent is already half gone.
        const QSignalBlocker blocker(this);
        save();
    }
}

// The list can change under an open window: the Settings module and the
// application edit the same file, and either can sit in the background for
// hours. Whoever had been open longest used to win, writing a snapshot taken
// before the other one's changes and bringing back entries that had been
// removed. Seen twice on the device.
void KeepAliveBackend::configChangedOnDisk()
{
    // Rewritten rather than modified in place -- KConfig replaces the file --
    // so the watch is on a path that no longer exists and has to be re-armed.
    rearmWatch();

    // A tap of ours is still on its way to disk, and the file therefore still
    // holds the old state. Reloading now would undo it.
    if (m_saveTimer.isActive()) {
        return;
    }

    // KConfig hands out one cached object per file per process and does not
    // notice the file being replaced, so it has to be told to look again.
    KSharedConfig::openConfig(configFile)->reparseConfiguration();

    const QSet<QString> kept = readList(QStringLiteral("apps"), defaultApps);
    const QSet<QString> hidden = readList(QStringLiteral("ocultas"), {});

    // Compared instead of timed: our own write also fires this, and the signal
    // arrives well after save() has returned, so no flag set around the write
    // would still be true by now.
    if (kept == m_kept && hidden == m_hidden) {
        return;
    }

    qInfo() << "keepalive: the list changed elsewhere; reloading";
    m_kept = kept;
    m_hidden = hidden;
    rebuild();
    setDirty(false);
}

void KeepAliveBackend::rearmWatch()
{
    const QString path = configPath();
    if (!m_watcher.files().contains(path) && QFileInfo::exists(path)) {
        m_watcher.addPath(path);
    }
}

QVariantList KeepAliveBackend::apps() const
{
    return m_apps;
}

bool KeepAliveBackend::dirty() const
{
    return m_dirty;
}

void KeepAliveBackend::setDirty(bool dirty)
{
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    Q_EMIT dirtyChanged();
}

void KeepAliveBackend::load()
{
    m_saveTimer.stop();

    // KConfig hands out one cached object per file per process, and it does not
    // notice the file being replaced from outside. Without this the reload
    // would faithfully re-read what was already in memory.
    KSharedConfig::openConfig(configFile)->reparseConfiguration();

    m_kept = readList(QStringLiteral("apps"), defaultApps);
    m_hidden = readList(QStringLiteral("ocultas"), {});
    rebuild();
    setDirty(false);
    rearmWatch();
}

void KeepAliveBackend::save()
{
    // Sorted, because a QSet has no stable order: without this the file would
    // be rewritten in a different order every time and any diff of it would be
    // noise.
    QStringList kept = m_kept.values();
    kept.sort();
    QStringList hidden = m_hidden.values();
    hidden.sort();

    KConfigGroup group(KSharedConfig::openConfig(configFile), QStringLiteral("General"));
    group.writeEntry("apps", kept);
    group.writeEntry("ocultas", hidden);
    group.sync();

    writeSkipSwitcherRules(hidden);
    reloadTaskSwitcher();
    setDirty(false);
    Q_EMIT saved();

    rearmWatch();

    // After the write, never before: the launcher reads the list from the file.
    launchNewlyKept();
}

void KeepAliveBackend::launchNewlyKept()
{
    if (m_toLaunch.isEmpty()) {
        return;
    }

    QStringList apps = m_toLaunch.values();
    apps.sort();
    m_toLaunch.clear();

    QString program = QStandardPaths::findExecutable(launcherName);
    if (program.isEmpty() && QFileInfo::exists(launcherPath)) {
        program = launcherPath;
    }
    if (program.isEmpty()) {
        qWarning() << "keepalive:" << launcherName << "is not installed"
                   << "-- reinstall the keepalive package; these stay closed until opened by hand:" << apps;
        return;
    }

    // Detached because it outlives this dialog: it holds a window rule in place
    // until the windows have appeared, which takes seconds.
    if (!QProcess::startDetached(program, apps)) {
        qWarning() << "keepalive: could not run" << program << "for" << apps;
        return;
    }

    qInfo() << "keepalive: opening minimized:" << apps;
}

// Settings on a phone are expected to hold as soon as they are flipped. This
// module could not rely on being asked to save: plasma-settings shows no Apply
// button for it -- verified on the device, the footer is simply not there -- so
// every change made here was thrown away on the way out.
void KeepAliveBackend::scheduleSave()
{
    m_saveTimer.start();
}

QString KeepAliveBackend::configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1Char('/') + configFile;
}

int KeepAliveBackend::indexOf(const QString &appId) const
{
    for (int i = 0; i < m_apps.size(); ++i) {
        if (m_apps.at(i).toMap()[QStringLiteral("appId")].toString() == appId) {
            return i;
        }
    }
    return -1;
}

void KeepAliveBackend::restoreDefaults()
{
    m_kept = QSet<QString>(defaultApps.begin(), defaultApps.end());
    m_hidden.clear();
    rebuild();
    setDirty(true);
    scheduleSave();
}

void KeepAliveBackend::setKeptAlive(const QString &appId, bool keep)
{
    if (keep == m_kept.contains(appId)) {
        return;
    }

    if (keep) {
        m_kept.insert(appId);
        // Opened once the write lands. Switching an app off does not close it:
        // that would throw away whatever the user had on screen there.
        m_toLaunch.insert(appId);
    } else {
        m_kept.remove(appId);
        m_toLaunch.remove(appId);
    }

    // An app that closes normally leaves the switcher on its own, so hiding it
    // would only be confusing: dropping the flag keeps the two settings from
    // drifting into a state the interface cannot show.
    if (!keep) {
        m_hidden.remove(appId);
    }

    // Re-sorted, so the row lands under the heading that now describes it. The
    // previous version updated the row in place to keep it under the finger,
    // and the cost was the whole point of the module: you switched an app on
    // and it stayed sitting under "these close normally", looking ignored.
    // The list scrolls to follow it instead -- see appMoved.
    rebuild();
    setDirty(true);
    scheduleSave();
    Q_EMIT appMoved(indexOf(appId));
}

void KeepAliveBackend::setHidden(const QString &appId, bool hide)
{
    // Hiding only makes sense for an app that stays alive: one that closes
    // normally leaves the list on its own. The QML hides the switch, and this
    // guard makes it a rule instead of a detail of the interface.
    if (hide && !m_kept.contains(appId)) {
        return;
    }
    if (hide == m_hidden.contains(appId)) {
        return;
    }

    if (hide) {
        m_hidden.insert(appId);
    } else {
        m_hidden.remove(appId);
    }

    // No appMoved: hiding does not change the grouping, so the row stays put.
    rebuild();
    setDirty(true);
    scheduleSave();
}

QSet<QString> KeepAliveBackend::readList(const QString &key, const QStringList &fallback) const
{
    KConfigGroup group(KSharedConfig::openConfig(configFile), QStringLiteral("General"));
    const QStringList list = group.readEntry(key, fallback);
    return QSet<QString>(list.begin(), list.end());
}

// A window is kept out of the task switcher by KWin itself: the mobile
// switcher's TaskFilterModel::filterAcceptsRow() ends with
//
//     if (window->skipSwitcher()) return false;
//
// so a plain window rule does it and no QML needs patching. Verified on the
// device: with the rule in place the dialer reports skipSwitcher=true while an
// app without one still reports false.
//
// Only groups named "keepalive-*" are touched, so rules written by hand or from
// the Window Rules module survive.
void KeepAliveBackend::writeSkipSwitcherRules(const QStringList &hidden)
{
    KSharedConfig::Ptr rules = KSharedConfig::openConfig(QStringLiteral("kwinrulesrc"));
    KConfigGroup general(rules, QStringLiteral("General"));

    QStringList groups = general.readEntry("rules", QStringList());

    for (const QString &group : std::as_const(groups)) {
        if (group.startsWith(rulePrefix)) {
            rules->deleteGroup(group);
        }
    }
    groups.removeIf([](const QString &group) {
        return group.startsWith(rulePrefix);
    });

    for (const QString &appId : hidden) {
        const QString name = rulePrefix + appId;
        groups.append(name);

        // Built with arg() rather than '+': with QStringBuilder enabled the
        // concatenation is a QStringBuilder, not a QString, and
        // KConfigGroup::writeEntry() refuses it at compile time.
        const QString description = QStringLiteral("Always-alive apps: hide %1").arg(appId);

        KConfigGroup rule(rules, name);
        rule.writeEntry("Description", description);
        rule.writeEntry("skipswitcher", true);
        rule.writeEntry("skipswitcherrule", 2); // 2 = Force
        rule.writeEntry("wmclass", appId);
        rule.writeEntry("wmclasscomplete", false);
        rule.writeEntry("wmclassmatch", 1); // 1 = ExactMatch
    }

    general.writeEntry("rules", groups);
    general.writeEntry("count", groups.size());
    rules->sync();

    QDBusInterface kwin(QStringLiteral("org.kde.KWin"),
                        QStringLiteral("/KWin"),
                        QStringLiteral("org.kde.KWin"),
                        QDBusConnection::sessionBus());
    if (kwin.isValid()) {
        kwin.call(QStringLiteral("reconfigure"));
    }
}

void KeepAliveBackend::rebuild()
{
    // The identifier is the desktop file name without its extension, which is
    // what a well-behaved Wayland client sets as its app_id -- and app_id is
    // what KWin reports as desktopFileName to the task switcher.
    //
    // It is not always the reverse-domain name: poconav identifies itself as
    // plain "poconav". Measured on the device, so the list must hold whatever
    // the .desktop is actually called and nothing prettier.
    const KService::List services = KApplicationTrader::query([](const KService::Ptr &service) {
        return !service->noDisplay() && !service->desktopEntryName().isEmpty();
    });

    QVariantList apps;
    apps.reserve(services.size());

    for (const KService::Ptr &service : services) {
        const QString appId = service->desktopEntryName();
        const bool kept = m_kept.contains(appId);
        apps.append(QVariantMap{
            {QStringLiteral("appId"), appId},
            {QStringLiteral("name"), service->name()},
            {QStringLiteral("icon"), service->icon()},
            {QStringLiteral("keptAlive"), kept},
            {QStringLiteral("hidden"), m_hidden.contains(appId)},
            {QStringLiteral("group"), kept},
            // Filled in below, once the list is in its final order.
            {QStringLiteral("firstOfGroup"), false},
        });
    }

    // Kept-alive first, then by name: what the user came to check is at the top
    // instead of buried among every installed application.
    std::sort(apps.begin(), apps.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap left = a.toMap();
        const QVariantMap right = b.toMap();

        const bool leftKept = left[QStringLiteral("group")].toBool();
        const bool rightKept = right[QStringLiteral("group")].toBool();
        if (leftKept != rightKept) {
            return leftKept;
        }

        return left[QStringLiteral("name")].toString().localeAwareCompare(right[QStringLiteral("name")].toString()) < 0;
    });

    // Which rows carry a heading. Computed here rather than left to
    // ListView.section: with a section delegate the two headings came out
    // swapped after a toggle -- "Se cierran normalmente" printed above the apps
    // that were kept alive -- which made a working list look broken. Seen on
    // the device. Marking the first row of each group is ours to get right.
    bool seenKept = false;
    bool seenOther = false;
    for (QVariant &entry : apps) {
        QVariantMap app = entry.toMap();
        const bool kept = app[QStringLiteral("group")].toBool();
        bool &seen = kept ? seenKept : seenOther;
        if (!seen) {
            seen = true;
            app[QStringLiteral("firstOfGroup")] = true;
            entry = app;
        }
    }

    m_apps = apps;
    Q_EMIT appsChanged();
}

// Writing the file is not enough. The task switcher reads its config once and
// caches it: rewriting the file underneath it kept serving the old value for
// 13 s in a direct measurement. Re-reading on every close is not an option
// either -- a synchronous XMLHttpRequest against file:// hangs, which inside
// KWin would freeze the compositor. So the effect is reloaded.
void KeepAliveBackend::reloadTaskSwitcher()
{
    QDBusInterface effects(QStringLiteral("org.kde.KWin"),
                           QStringLiteral("/Effects"),
                           QStringLiteral("org.kde.kwin.Effects"),
                           QDBusConnection::sessionBus());

    if (!effects.isValid()) {
        return;
    }

    const QString effect = QStringLiteral("mobiletaskswitcher");
    effects.call(QStringLiteral("unloadEffect"), effect);

    // If the reload fails the switcher would be left unloaded, which makes the
    // phone awkward to use, so the result is not ignored: on failure load it
    // again, which brings back whichever copy KWin can parse.
    const QDBusReply<bool> loaded = effects.call(QStringLiteral("loadEffect"), effect);
    if (!loaded.isValid() || !loaded.value()) {
        effects.call(QStringLiteral("loadEffect"), effect);
    }
}
