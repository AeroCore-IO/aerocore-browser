#pragma once

#include <QObject>
#include <QString>

class CefSettingsService : public QObject
{
    Q_OBJECT

public:
    explicit CefSettingsService(QObject *parent = nullptr);

    Q_INVOKABLE QString cefProfilePath() const;
    Q_INVOKABLE QString cefLogPath() const;
    Q_INVOKABLE QString appVersion() const;

    Q_INVOKABLE void clearCefCookies();
    Q_INVOKABLE void clearCefCache();
};
