#pragma once

#include <QSqlDatabase>
#include <QString>

class DatabaseManager
{
public:
    static DatabaseManager &instance();

    bool initialize(QString *errorMessage = nullptr);
    void shutdown();
    bool validateUser(const QString &username, const QString &password);
    QString roleForUser(const QString &username);
    QString databasePath() const;
    QString lastError() const;

    DatabaseManager(const DatabaseManager &) = delete;
    DatabaseManager &operator=(const DatabaseManager &) = delete;

private:
    DatabaseManager();
    ~DatabaseManager();

    bool createTables(QString *errorMessage);
    bool ensureColumn(const QString &table, const QString &column,
                      const QString &definition, QString *errorMessage);
    bool ensureDefaultUser(QString *errorMessage);
    QByteArray passwordHash(const QString &password, const QByteArray &salt) const;
    QString generateSaltHex() const;

    QString m_connectionName;
    QString m_databasePath;
    QString m_lastError;
    QSqlDatabase m_database;
    bool m_initialized = false;
};
