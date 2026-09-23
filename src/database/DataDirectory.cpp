#include "database/DataDirectory.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
bool fail(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

QString cleanAbsolutePath(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

bool samePath(const QString &left, const QString &right)
{
    const Qt::CaseSensitivity sensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    return QString::compare(cleanAbsolutePath(left), cleanAbsolutePath(right), sensitivity) == 0;
}

bool isValidExplicitPath(const QString &path, const QString &source,
                         QString *errorMessage)
{
    if (path.isEmpty()) {
        return fail(errorMessage, QStringLiteral("%1未提供数据目录").arg(source));
    }
    if (!QDir::isAbsolutePath(path)) {
        return fail(errorMessage,
                    QStringLiteral("%1必须是绝对路径：%2").arg(source, path));
    }
    return true;
}
}

DataDirectory::Paths DataDirectory::resolve(
    const QStringList &arguments,
    const QString &applicationDirectory,
    QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    QString commandLinePath;
    bool commandLineSpecified = false;
    for (int index = 1; index < arguments.size(); ++index) {
        const QString &argument = arguments.at(index);
        if (argument == QStringLiteral("--data-dir")) {
            if (index + 1 >= arguments.size()) {
                fail(errorMessage, QStringLiteral("--data-dir 缺少目录参数"));
                return {};
            }
            commandLinePath = arguments.at(++index);
            commandLineSpecified = true;
        } else if (argument.startsWith(QStringLiteral("--data-dir="))) {
            commandLinePath = argument.mid(QStringLiteral("--data-dir=").size());
            commandLineSpecified = true;
        }
    }

    if (commandLineSpecified) {
        if (!isValidExplicitPath(commandLinePath, QStringLiteral("命令行 --data-dir"),
                                 errorMessage)) {
            return {};
        }
        Paths paths;
        const QString root = cleanAbsolutePath(commandLinePath);
        if (!makePaths(root, Source::CommandLine, QString(), &paths, errorMessage)) {
            return {};
        }
        return paths;
    }

    const QString environmentPath = qEnvironmentVariable("MU_MONITOR_DATA_DIR");
    if (!environmentPath.isEmpty()) {
        if (!isValidExplicitPath(environmentPath, QStringLiteral("环境变量 MU_MONITOR_DATA_DIR"),
                                 errorMessage)) {
            return {};
        }
        Paths paths;
        const QString root = cleanAbsolutePath(environmentPath);
        if (!makePaths(root, Source::Environment, QString(), &paths, errorMessage)) {
            return {};
        }
        return paths;
    }

    if (!applicationDirectory.isEmpty()
        && QFileInfo(QDir(applicationDirectory).filePath(QStringLiteral("portable.flag"))).isFile()) {
        Paths paths;
        const QString root = cleanAbsolutePath(
            QDir(applicationDirectory).filePath(QStringLiteral("data")));
        if (!makePaths(root, Source::Portable, QString(), &paths, errorMessage)) {
            return {};
        }
        return paths;
    }

    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty()) {
        fail(errorMessage, QStringLiteral("无法获取 AppLocalDataLocation"));
        return {};
    }

    Paths paths;
    if (!makePaths(cleanAbsolutePath(root), Source::AppLocalData, QString(), &paths,
                   errorMessage)) {
        return {};
    }
    return paths;
}

bool DataDirectory::ensureLayout(const Paths &paths, QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    if (paths.root.isEmpty() || !QDir::isAbsolutePath(paths.root)) {
        return fail(errorMessage, QStringLiteral("数据目录必须是绝对路径"));
    }

    const QFileInfo rootInfo(paths.root);
    if (rootInfo.exists() && !rootInfo.isDir()) {
        return fail(errorMessage, QStringLiteral("数据目录路径不是目录：%1").arg(paths.root));
    }

    const QStringList directories = {
        paths.root,
        paths.database,
        paths.backups,
        paths.logs,
        paths.exports,
        paths.runtime,
        paths.config,
    };

    for (const QString &directory : directories) {
        if (directory.isEmpty()) {
            return fail(errorMessage, QStringLiteral("数据目录布局包含空路径"));
        }
        const QFileInfo info(directory);
        if (info.exists() && !info.isDir()) {
            return fail(errorMessage, QStringLiteral("数据目录路径不是目录：%1").arg(directory));
        }
        if (!QDir().mkpath(directory)) {
            return fail(errorMessage, QStringLiteral("无法创建数据目录：%1").arg(directory));
        }
        if (!QFileInfo(directory).isWritable()) {
            return fail(errorMessage, QStringLiteral("数据目录不可写：%1").arg(directory));
        }
    }

    return true;
}

QString DataDirectory::sourceName(Source source)
{
    switch (source) {
    case Source::CommandLine:
        return QStringLiteral("command-line");
    case Source::Environment:
        return QStringLiteral("environment");
    case Source::Portable:
        return QStringLiteral("portable");
    case Source::AppLocalData:
        return QStringLiteral("app-local-data");
    }
    return QStringLiteral("unknown");
}

bool DataDirectory::makePaths(const QString &root, Source source,
                              const QString &legacyDatabase,
                              Paths *paths, QString *errorMessage)
{
    if (!paths) {
        return fail(errorMessage, QStringLiteral("数据目录输出参数为空"));
    }
    if (root.isEmpty() || !QDir::isAbsolutePath(root)) {
        return fail(errorMessage, QStringLiteral("数据目录必须是绝对路径"));
    }

    const QDir rootDirectory(root);
    paths->root = cleanAbsolutePath(root);
    paths->database = rootDirectory.filePath(QStringLiteral("database"));
    paths->backups = rootDirectory.filePath(QStringLiteral("database/backups"));
    paths->logs = rootDirectory.filePath(QStringLiteral("logs"));
    paths->exports = rootDirectory.filePath(QStringLiteral("exports"));
    paths->runtime = rootDirectory.filePath(QStringLiteral("runtime"));
    paths->config = rootDirectory.filePath(QStringLiteral("config"));
    paths->source = source;

    QString legacy = legacyDatabase;
    if (legacy.isEmpty()) {
        const QString legacyRoot =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!legacyRoot.isEmpty()) {
            legacy = QDir(legacyRoot).filePath(QStringLiteral("mu-monitor.db"));
        }
    }
    const QString newDatabase = rootDirectory.filePath(QStringLiteral("database/mu-monitor.db"));
    if (!legacy.isEmpty() && !samePath(legacy, newDatabase)) {
        paths->legacyDatabase = cleanAbsolutePath(legacy);
    }
    return true;
}
