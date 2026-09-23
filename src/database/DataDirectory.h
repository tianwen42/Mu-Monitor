#pragma once

#include <QString>
#include <QStringList>

class DataDirectory
{
public:
    enum class Source {
        CommandLine,
        Environment,
        Portable,
        AppLocalData
    };

    struct Paths {
        QString root;
        QString database;
        QString backups;
        QString logs;
        QString exports;
        QString runtime;
        QString config;
        QString legacyDatabase;
        Source source = Source::AppLocalData;
    };

    static Paths resolve(
        const QStringList &arguments,
        const QString &applicationDirectory,
        QString *errorMessage = nullptr);
    static bool ensureLayout(const Paths &paths, QString *errorMessage = nullptr);
    static QString sourceName(Source source);

private:
    static bool makePaths(const QString &root, Source source,
                          const QString &legacyDatabase,
                          Paths *paths, QString *errorMessage);
};
