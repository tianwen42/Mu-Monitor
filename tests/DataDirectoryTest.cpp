#include "database/DataDirectory.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
class EnvironmentVariableGuard
{
public:
    explicit EnvironmentVariableGuard(const QByteArray &name)
        : m_name(name)
    {
        m_previousValue = qgetenv(name.constData());
        m_hadValue = !m_previousValue.isNull();
    }

    ~EnvironmentVariableGuard()
    {
        if (m_hadValue) {
            qputenv(m_name.constData(), m_previousValue);
        } else {
            qunsetenv(m_name.constData());
        }
    }

private:
    QByteArray m_name;
    QByteArray m_previousValue;
    bool m_hadValue = false;
};

bool createPortableFlag(const QString &applicationDirectory, QString *errorMessage)
{
    QFile flag(QDir(applicationDirectory).filePath(QStringLiteral("portable.flag")));
    if (!flag.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = flag.errorString();
        }
        return false;
    }
    return true;
}
}

class DataDirectoryTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void commandLineOverridesEnvironmentAndPortable();
    void environmentOverridesPortable();
    void portableFlagUsesExecutableDataDirectory();
    void fallsBackToAppLocalDataLocation();
    void rejectsRelativeExplicitPath();
    void createsStandardLayout();
};

void DataDirectoryTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("Mu-MonitorTests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("Mu-MonitorDataDirectoryTest-%1")
            .arg(QCoreApplication::applicationPid()));
}

void DataDirectoryTest::commandLineOverridesEnvironmentAndPortable()
{
    QTemporaryDir applicationDirectory;
    QTemporaryDir environmentDirectory;
    QTemporaryDir commandLineDirectory;
    QVERIFY(applicationDirectory.isValid());
    QVERIFY(environmentDirectory.isValid());
    QVERIFY(commandLineDirectory.isValid());

    QString error;
    QVERIFY2(createPortableFlag(applicationDirectory.path(), &error), qPrintable(error));
    EnvironmentVariableGuard guard("MU_MONITOR_DATA_DIR");
    qputenv("MU_MONITOR_DATA_DIR", environmentDirectory.path().toUtf8());

    const QString commandLinePath = QDir(commandLineDirectory.path()).filePath(
        QStringLiteral("explicit-data"));
    const DataDirectory::Paths paths = DataDirectory::resolve(
        {QStringLiteral("Mu-MonitorTest"), QStringLiteral("--data-dir"), commandLinePath},
        applicationDirectory.path(), &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(paths.source, DataDirectory::Source::CommandLine);
    QCOMPARE(paths.root, QDir::cleanPath(commandLinePath));
    QCOMPARE(paths.database, QDir(paths.root).filePath(QStringLiteral("database")));
}

void DataDirectoryTest::environmentOverridesPortable()
{
    QTemporaryDir applicationDirectory;
    QTemporaryDir environmentDirectory;
    QVERIFY(applicationDirectory.isValid());
    QVERIFY(environmentDirectory.isValid());

    QString error;
    QVERIFY2(createPortableFlag(applicationDirectory.path(), &error), qPrintable(error));
    EnvironmentVariableGuard guard("MU_MONITOR_DATA_DIR");
    qputenv("MU_MONITOR_DATA_DIR", environmentDirectory.path().toUtf8());

    const DataDirectory::Paths paths = DataDirectory::resolve(
        {QStringLiteral("Mu-MonitorTest")}, applicationDirectory.path(), &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(paths.source, DataDirectory::Source::Environment);
    QCOMPARE(paths.root, QDir::cleanPath(environmentDirectory.path()));
}

void DataDirectoryTest::portableFlagUsesExecutableDataDirectory()
{
    QTemporaryDir applicationDirectory;
    QVERIFY(applicationDirectory.isValid());

    QString error;
    QVERIFY2(createPortableFlag(applicationDirectory.path(), &error), qPrintable(error));
    EnvironmentVariableGuard guard("MU_MONITOR_DATA_DIR");
    qunsetenv("MU_MONITOR_DATA_DIR");

    const DataDirectory::Paths paths = DataDirectory::resolve(
        {QStringLiteral("Mu-MonitorTest")}, applicationDirectory.path(), &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(paths.source, DataDirectory::Source::Portable);
    QCOMPARE(paths.root,
             QDir::cleanPath(QDir(applicationDirectory.path()).filePath(QStringLiteral("data"))));
}

void DataDirectoryTest::fallsBackToAppLocalDataLocation()
{
    QTemporaryDir applicationDirectory;
    QVERIFY(applicationDirectory.isValid());

    EnvironmentVariableGuard guard("MU_MONITOR_DATA_DIR");
    qunsetenv("MU_MONITOR_DATA_DIR");
    QString error;

    const DataDirectory::Paths paths = DataDirectory::resolve(
        {QStringLiteral("Mu-MonitorTest")}, applicationDirectory.path(), &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(paths.source, DataDirectory::Source::AppLocalData);
    QCOMPARE(paths.root,
             QDir::cleanPath(QStandardPaths::writableLocation(
                 QStandardPaths::AppLocalDataLocation)));
    QCOMPARE(paths.legacyDatabase,
             QDir::cleanPath(QDir(QStandardPaths::writableLocation(
                 QStandardPaths::AppDataLocation)).filePath(QStringLiteral("mu-monitor.db"))));
}

void DataDirectoryTest::rejectsRelativeExplicitPath()
{
    EnvironmentVariableGuard guard("MU_MONITOR_DATA_DIR");
    qunsetenv("MU_MONITOR_DATA_DIR");

    QString error;
    const DataDirectory::Paths paths = DataDirectory::resolve(
        {QStringLiteral("Mu-MonitorTest"), QStringLiteral("--data-dir"),
         QStringLiteral("relative-data")},
        QCoreApplication::applicationDirPath(), &error);

    QVERIFY(paths.root.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("绝对路径")));
}

void DataDirectoryTest::createsStandardLayout()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    const QString root = QDir(temporaryDirectory.path()).filePath(QStringLiteral("data"));
    const DataDirectory::Paths paths = DataDirectory::resolve(
        {QStringLiteral("Mu-MonitorTest"), QStringLiteral("--data-dir=") + root},
        QCoreApplication::applicationDirPath(), nullptr);

    QString error;
    QVERIFY2(DataDirectory::ensureLayout(paths, &error), qPrintable(error));
    const QStringList expectedDirectories = {
        paths.root,
        paths.database,
        paths.backups,
        paths.logs,
        paths.exports,
        paths.runtime,
        paths.config,
    };
    for (const QString &directory : expectedDirectories) {
        QVERIFY2(QFileInfo(directory).isDir(), qPrintable(directory));
    }
}

QTEST_MAIN(DataDirectoryTest)

#include "DataDirectoryTest.moc"
