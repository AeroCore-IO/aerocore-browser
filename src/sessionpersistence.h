#pragma once

#include <QList>
#include <QString>

#include "sessionmanager.h"

///
class SessionPersistence
{
public:
    static void save(const QList<BrowserSession> &sessions, const QString &activeSessionId);

    static QList<BrowserSession> load(QString *activeSessionId = nullptr);
};
