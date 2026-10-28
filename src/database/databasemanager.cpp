#include "database/databasemanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

QSqlDatabase DatabaseManager::s_connection;

namespace {

void ensureSqlitePluginPaths()
{
    const QByteArray pluginPath = qgetenv("QT_PLUGIN_PATH");
    if (pluginPath.isEmpty()) {
        return;
    }

    const QList<QByteArray> entries = pluginPath.split(':');
    for (const QByteArray &entry : entries) {
        if (!entry.isEmpty()) {
            QCoreApplication::addLibraryPath(QString::fromUtf8(entry));
        }
    }
}

} // namespace

bool DatabaseManager::initialize(const QString &dbFilePath)
{
    if (!QCoreApplication::instance()) {
        return false;
    }

    ensureSqlitePluginPaths();

    QString path = dbFilePath;
    if (path.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (dir.isEmpty()) {
            return false;
        }
        QDir().mkpath(dir);
        path = dir + QStringLiteral("/aerocore-browser.db");
    } else {
        const QFileInfo fileInfo(path);
        QDir().mkpath(fileInfo.absolutePath());
    }

    close();

    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        return false;
    }

    if (!dbFilePath.isEmpty()) {
        QFile::remove(path);
        QFile::remove(path + QStringLiteral("-wal"));
        QFile::remove(path + QStringLiteral("-shm"));
    }

    s_connection = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnectionName);
    s_connection.setDatabaseName(path);
    if (!s_connection.open()) {
        close();
        return false;
    }

    if (!createSchema(s_connection)) {
        close();
        return false;
    }

    return s_connection.isOpen();
}

bool DatabaseManager::isInitialized()
{
    return s_connection.isValid() && s_connection.isOpen();
}

QSqlDatabase DatabaseManager::database()
{
    return s_connection;
}

void DatabaseManager::close()
{
    if (s_connection.isValid()) {
        if (s_connection.isOpen()) {
            s_connection.close();
        }
        s_connection = QSqlDatabase();
    }

    if (QSqlDatabase::contains(kConnectionName)) {
        QSqlDatabase::removeDatabase(kConnectionName);
    }
}

bool DatabaseManager::createSchema(QSqlDatabase &db)
{
    QSqlQuery versionQuery(db);
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version"))) {
        return false;
    }

    int version = 0;
    if (versionQuery.next()) {
        version = versionQuery.value(0).toInt();
    }

    if (version == 0) {
        QSqlQuery createQuery(db);
        const QString createTable = QStringLiteral(
            "CREATE TABLE IF NOT EXISTS websites ("
            "id TEXT PRIMARY KEY,"
            "icon TEXT NOT NULL DEFAULT '',"
            "name TEXT NOT NULL,"
            "url TEXT NOT NULL UNIQUE,"
            "created_at TEXT NOT NULL,"
            "updated_at TEXT NOT NULL"
            ")");
        if (!createQuery.exec(createTable)) {
            return false;
        }

        QSqlQuery indexQuery(db);
        if (!indexQuery.exec(QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_websites_created_at ON websites(created_at)"))) {
            return false;
        }

        QSqlQuery setVersionQuery(db);
        if (!setVersionQuery.exec(QStringLiteral("PRAGMA user_version = 1"))) {
            return false;
        }
        version = 1;
    }

    if (version < 2) {
        QSqlQuery createHistoryQuery(db);
        if (!createHistoryQuery.exec(QStringLiteral(
                "CREATE TABLE IF NOT EXISTS history ("
                "id TEXT PRIMARY KEY,"
                "website_id TEXT NOT NULL DEFAULT '',"
                "website_name TEXT NOT NULL DEFAULT '',"
                "title TEXT NOT NULL,"
                "thumbnail TEXT NOT NULL DEFAULT '',"
                "url TEXT NOT NULL UNIQUE,"
                "last_visit_time TEXT NOT NULL,"
                "visit_count INTEGER NOT NULL DEFAULT 1,"
                "created_at TEXT NOT NULL,"
                "updated_at TEXT NOT NULL"
                ")"))) {
            return false;
        }

        QSqlQuery historyIndexQuery(db);
        if (!historyIndexQuery.exec(QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_history_last_visit_time "
                "ON history(last_visit_time DESC)"))) {
            return false;
        }

        QSqlQuery websiteIdIndexQuery(db);
        if (!websiteIdIndexQuery.exec(QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_history_website_id ON history(website_id)"))) {
            return false;
        }

        QSqlQuery setVersionQuery(db);
        if (!setVersionQuery.exec(QStringLiteral("PRAGMA user_version = 2"))) {
            return false;
        }
    }

    return true;
}
