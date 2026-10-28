#include "steamidleoverrideadaptors.h"

#include "steamidleoverridehelper.h"

#include <QDBusMessage>

ScreenSaverInhibitObject::ScreenSaverInhibitObject(InhibitBroker *broker, QObject *parent)
    : QObject(parent), m_broker(broker) {}

uint ScreenSaverInhibitObject::Inhibit(const QString &application, const QString &reason)
{
    return m_broker->onInhibit(QStringLiteral("org.freedesktop.ScreenSaver"),
                               message().service(), application, reason);
}

void ScreenSaverInhibitObject::UnInhibit(uint cookie)
{
    m_broker->onUnInhibit(cookie);
}

void ScreenSaverInhibitObject::Uninhibit(uint cookie)
{
    m_broker->onUnInhibit(cookie);
}

PowerManagementInhibitObject::PowerManagementInhibitObject(InhibitBroker *broker, QObject *parent)
    : QObject(parent), m_broker(broker) {}

uint PowerManagementInhibitObject::Inhibit(const QString &application, const QString &reason)
{
    return m_broker->onInhibit(QStringLiteral("org.freedesktop.PowerManagement.Inhibit"),
                               message().service(), application, reason);
}

void PowerManagementInhibitObject::UnInhibit(uint cookie)
{
    m_broker->onUnInhibit(cookie);
}

GnomeSessionManagerObject::GnomeSessionManagerObject(InhibitBroker *broker, QObject *parent)
    : QObject(parent), m_broker(broker) {}

uint GnomeSessionManagerObject::Inhibit(const QString &application, uint /*xid*/,
                                        const QString &reason, uint /*flags*/)
{
    return m_broker->onInhibit(QStringLiteral("org.gnome.SessionManager"),
                               message().service(), application, reason);
}

void GnomeSessionManagerObject::Uninhibit(uint cookie)
{
    m_broker->onUnInhibit(cookie);
}
