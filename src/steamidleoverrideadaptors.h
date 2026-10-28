#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QObject>
#include <QString>

class InhibitBroker;

// Decky-equivalent: org.freedesktop.ScreenSaver on paths /ScreenSaver and /org/freedesktop/ScreenSaver.
class ScreenSaverInhibitObject : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.ScreenSaver")
public:
    explicit ScreenSaverInhibitObject(InhibitBroker *broker, QObject *parent = nullptr);

public slots:
    Q_SCRIPTABLE uint Inhibit(const QString &application, const QString &reason);
    Q_SCRIPTABLE void UnInhibit(uint cookie);
    // Some clients use Uninhibit (lowercase 'i') — accept it too.
    Q_SCRIPTABLE void Uninhibit(uint cookie);

private:
    InhibitBroker *m_broker;
};

// Decky-equivalent: org.freedesktop.PowerManagement.Inhibit on /org/freedesktop/PowerManagement/Inhibit.
class PowerManagementInhibitObject : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.PowerManagement.Inhibit")
public:
    explicit PowerManagementInhibitObject(InhibitBroker *broker, QObject *parent = nullptr);

public slots:
    Q_SCRIPTABLE uint Inhibit(const QString &application, const QString &reason);
    Q_SCRIPTABLE void UnInhibit(uint cookie);

private:
    InhibitBroker *m_broker;
};

// Decky-equivalent: org.gnome.SessionManager on /org/gnome/SessionManager.
class GnomeSessionManagerObject : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.gnome.SessionManager")
public:
    explicit GnomeSessionManagerObject(InhibitBroker *broker, QObject *parent = nullptr);

public slots:
    Q_SCRIPTABLE uint Inhibit(const QString &application, uint xid, const QString &reason, uint flags);
    Q_SCRIPTABLE void Uninhibit(uint cookie);

private:
    InhibitBroker *m_broker;
};
