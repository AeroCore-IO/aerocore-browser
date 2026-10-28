#pragma once

#include <QString>
#include <QSqlDatabase>

class DatabaseManager
{
public:
    static bool initialize(const QString &dbFilePath = {});
    static bool isInitialized();
    static QSqlDatabase database();
    static void close();

private:
    static bool createSchema(QSqlDatabase &db);

    static inline const QString kConnectionName = QStringLiteral("aerocore_browser");
    static QSqlDatabase s_connection;
};
