// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Settings module. All the logic is in KeepAliveBackend, shared with the
// standalone application; this only wires it to the KCM frame so that both
// front-ends cannot drift apart.

#include "backend.h"

#include <KPluginFactory>
#include <KQuickConfigModule>

class KCMKeepAlive : public KQuickConfigModule
{
    Q_OBJECT

    Q_PROPERTY(KeepAliveBackend *backend READ backend CONSTANT)

public:
    KCMKeepAlive(QObject *parent, const KPluginMetaData &data)
        : KQuickConfigModule(parent, data)
        , m_backend(new KeepAliveBackend(this))
    {
        // The Apply button belongs to the KCM frame, so the backend's dirty
        // flag has to drive it.
        connect(m_backend, &KeepAliveBackend::dirtyChanged, this, [this] {
            setNeedsSave(m_backend->dirty());
        });
        setNeedsSave(m_backend->dirty());
    }

    KeepAliveBackend *backend() const
    {
        return m_backend;
    }

    void load() override
    {
        m_backend->load();
    }

    void save() override
    {
        m_backend->save();
    }

    void defaults() override
    {
        m_backend->restoreDefaults();
    }

private:
    KeepAliveBackend *const m_backend;
};

K_PLUGIN_CLASS_WITH_JSON(KCMKeepAlive, "kcm_keepalive.json")

#include "kcm.moc"
