#include "auth/AuditRepository.h"
#include "auth/AuthenticationService.h"
#include "auth/PasswordService.h"
#include "auth/UserManagementService.h"
#include "database/DatabaseManager.h"
#include "database/PasswordRepository.h"
#include "database/UserRepository.h"
#include "utils/TimeUtils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QtTest>

class UserManagementTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void rejectsDuplicateUser();
    void editsUser();
    void enablesAndDisablesUser();
    void protectsLastEnabledAdmin();
    void preventsSelfDisable();
    void enforcesRolePermissions();
    void upgradesLegacyPasswordOnLogin();
    void resetPasswordRevokesSessions();
    void changesOwnPasswordAndRevokesOtherSessions();
    void writesAuditTrail();
    void doesNotPersistSensitiveInformation();

private:
    bool initializeDatabase(QString *errorMessage = nullptr);
    bool createUser(UserManagementService &service,
                    const QString &username,
                    const QString &role,
                    const QString &password,
                    qint64 *userId = nullptr);
    bool insertSession(const QString &username, const QString &rawToken);
    int sessionCount(const QString &username);
    bool removeTestDataDirectory() const;

    QString m_dataDirectory;
};

void UserManagementTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("Mu-MonitorTests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("Mu-MonitorUserManagementTest-%1")
            .arg(QCoreApplication::applicationPid()));
    m_dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QVERIFY(!m_dataDirectory.isEmpty());
}

void UserManagementTest::init()
{
    DatabaseManager::instance().shutdown();
    removeTestDataDirectory();
    QString errorMessage;
    QVERIFY2(initializeDatabase(&errorMessage), qPrintable(errorMessage));
}

void UserManagementTest::cleanup()
{
    DatabaseManager::instance().shutdown();
    removeTestDataDirectory();
}

bool UserManagementTest::initializeDatabase(QString *errorMessage)
{
    QString message;
    const bool initialized = DatabaseManager::instance().initialize(&message);
    if (errorMessage) *errorMessage = message;
    return initialized;
}

bool UserManagementTest::removeTestDataDirectory() const
{
    if (m_dataDirectory.isEmpty()) {
        return false;
    }

    const QString normalized = QDir::fromNativeSeparators(m_dataDirectory).toLower();
    if (!normalized.contains(QStringLiteral("qttest"))
        && !normalized.contains(QStringLiteral("mu-monitorusermanagementtest"))) {
        return false;
    }

    return QDir(m_dataDirectory).removeRecursively()
        || !QFileInfo::exists(m_dataDirectory);
}

bool UserManagementTest::createUser(UserManagementService &service,
                                    const QString &username,
                                    const QString &role,
                                    const QString &password,
                                    qint64 *userId)
{
    QString errorMessage;
    if (!service.createUser(username, username, role, password, true,
                            userId, &errorMessage)) {
        return false;
    }
    return true;
}

bool UserManagementTest::insertSession(const QString &username,
                                       const QString &rawToken)
{
    QSqlQuery query(QSqlDatabase::database(QStringLiteral("mu_monitor_sqlite")));
    query.prepare(QStringLiteral(
        "INSERT INTO sessions "
        "(username, token_hash, expires_at, created_at, last_used_at) "
        "VALUES (?, ?, ?, ?, ?)"));
    query.addBindValue(username);
    query.addBindValue(QString::fromLatin1(
        QCryptographicHash::hash(rawToken.toUtf8(),
                                 QCryptographicHash::Sha256).toHex()));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    query.addBindValue(TimeUtils::toUtcIso8601(now.addDays(1)));
    query.addBindValue(TimeUtils::toUtcIso8601(now));
    query.addBindValue(TimeUtils::toUtcIso8601(now));
    return query.exec();
}

int UserManagementTest::sessionCount(const QString &username)
{
    QSqlQuery query(QSqlDatabase::database(QStringLiteral("mu_monitor_sqlite")));
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM sessions WHERE username = ?"));
    query.addBindValue(username);
    if (!query.exec() || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

void UserManagementTest::rejectsDuplicateUser()
{
    UserRepository users;
    PasswordRepository passwords;
    PasswordService passwordService;
    AuditRepository audit;
    UserManagementService service(users, passwords, passwordService, audit,
                                  QStringLiteral("admin"));

    QVERIFY(createUser(service, QStringLiteral("operator1"),
                       QStringLiteral("operator"),
                       QStringLiteral("Operator123")));
    QString errorMessage;
    QVERIFY(!service.createUser(QStringLiteral("operator1"),
                                QStringLiteral("重复用户"),
                                QStringLiteral("viewer"),
                                QStringLiteral("Viewer12345"), true,
                                nullptr, &errorMessage));
    QVERIFY(!errorMessage.isEmpty());
}

void UserManagementTest::editsUser()
{
    UserRepository users;
    PasswordRepository passwords;
    PasswordService passwordService;
    AuditRepository audit;
    UserManagementService service(users, passwords, passwordService, audit,
                                  QStringLiteral("admin"));

    qint64 userId = -1;
    QVERIFY(createUser(service, QStringLiteral("edit_user"),
                       QStringLiteral("viewer"),
                       QStringLiteral("Viewer123"), &userId));
    QString errorMessage;
    QVERIFY2(service.updateUser(userId, QStringLiteral("edited_user"),
                                QStringLiteral("编辑后用户"),
                                QStringLiteral("operator"), true,
                                &errorMessage),
             qPrintable(errorMessage));

    Auth::UserRecord updated;
    QVERIFY(users.findById(userId, &updated, &errorMessage));
    QCOMPARE(updated.username, QStringLiteral("edited_user"));
    QCOMPARE(updated.displayName, QStringLiteral("编辑后用户"));
    QCOMPARE(updated.role, QStringLiteral("operator"));
}

void UserManagementTest::enablesAndDisablesUser()
{
    UserRepository users;
    PasswordRepository passwords;
    PasswordService passwordService;
    AuditRepository audit;
    UserManagementService service(users, passwords, passwordService, audit,
                                  QStringLiteral("admin"));

    qint64 userId = -1;
    QVERIFY(createUser(service, QStringLiteral("toggle_user"),
                       QStringLiteral("operator"),
                       QStringLiteral("Operator123"), &userId));
    QVERIFY(insertSession(QStringLiteral("toggle_user"), QStringLiteral("toggle-token")));
    QCOMPARE(sessionCount(QStringLiteral("toggle_user")), 1);

    QString errorMessage;
    QVERIFY2(service.setUserEnabled(userId, false, &errorMessage),
             qPrintable(errorMessage));
    Auth::UserRecord user;
    QVERIFY(users.findById(userId, &user, &errorMessage));
    QVERIFY(!user.enabled);
    QCOMPARE(sessionCount(QStringLiteral("toggle_user")), 0);

    QVERIFY2(service.setUserEnabled(userId, true, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(users.findById(userId, &user, &errorMessage));
    QVERIFY(user.enabled);
}

void UserManagementTest::protectsLastEnabledAdmin()
{
    UserRepository users;
    PasswordRepository passwords;
    PasswordService passwordService;
    AuditRepository audit;
    UserManagementService admin1(users, passwords, passwordService, audit,
                                 QStringLiteral("admin"));

    qint64 admin2Id = -1;
    QVERIFY(createUser(admin1, QStringLiteral("admin2"),
                       QStringLiteral("admin"),
                       QStringLiteral("AdminTwo123"), &admin2Id));

    UserManagementService admin2(users, passwords, passwordService, audit,
                                 QStringLiteral("admin2"));
    QString errorMessage;
    QVERIFY2(admin2.setUserEnabled(1, false, &errorMessage),
             qPrintable(errorMessage));
    QVERIFY(!admin2.setUserRole(admin2Id, QStringLiteral("operator"),
                                &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("最后一个启用的管理员")));
}

void UserManagementTest::preventsSelfDisable()
{
    UserRepository users;
    PasswordRepository passwords;
    PasswordService passwordService;
    AuditRepository audit;
    UserManagementService service(users, passwords, passwordService, audit,
                                  QStringLiteral("admin"));

    QString errorMessage;
    QVERIFY(!service.setUserEnabled(1, false, &errorMessage));
    QVERIFY(errorMessage.contains(QStringLiteral("不能禁用自己")));
}

void UserManagementTest::enforcesRolePermissions()
{
    UserRepository users;
    PasswordRepository passwords;
    PasswordService passwordService;
    AuditRepository audit;
    UserManagementService admin(users, passwords, passwordService, audit,
                                QStringLiteral("admin"));
    QVERIFY(createUser(admin, QStringLiteral("operator_role"),
                       QStringLiteral("operator"),
                       QStringLiteral("Operator123")));
    QVERIFY(createUser(admin, QStringLiteral("viewer_role"),
                       QStringLiteral("viewer"),
                       QStringLiteral("Viewer12345")));

    UserManagementService operatorService(users, passwords, passwordService, audit,
                                          QStringLiteral("operator_role"));
    QVERIFY(!operatorService.can(Auth::Permission::ManageUsers));
    QVERIFY(!operatorService.can(Auth::Permission::ModifySettings));
    QVERIFY(operatorService.can(Auth::Permission::AcknowledgeAlarm));
    QVERIFY(operatorService.can(Auth::Permission::ViewHistory));
    QVERIFY(operatorService.can(Auth::Permission::ControlCollection));

    QList<Auth::UserRecord> listed;
    QString errorMessage;
    QVERIFY(!operatorService.listUsers(&listed, &errorMessage));
    QVERIFY(listed.isEmpty());
    QVERIFY(!errorMessage.isEmpty());

    UserManagementService viewerService(users, passwords, passwordService, audit,
                                        QStringLiteral("viewer_role"));
    QVERIFY(viewerService.can(Auth::Permission::ViewHistory));
    QVERIFY(!viewerService.can(Auth::Permission::AcknowledgeAlarm));
    QVERIFY(!viewerService.can(Auth::Permission::ControlCollection));
}

void UserManagementTest::upgradesLegacyPasswordOnLogin()
{
    UserRepository users;
    PasswordRepository passwords;
    PasswordService passwordService;
    AuditRepository audit;
    UserManagementService admin(users, passwords, passwordService, audit,
                                QStringLiteral("admin"));

    QString errorMessage;
    QVERIFY2(users.initialize(&errorMessage), qPrintable(errorMessage));
    Auth::PasswordCredential before;
    QVERIFY2(passwords.credentialForUser(1, &before, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(before.scheme, QStringLiteral("legacy_sha256"));

    AuthenticationService authentication(users, passwords, passwordService, audit);
    Auth::UserRecord authenticated;
    QVERIFY2(authentication.authenticate(QStringLiteral("admin"),
                                         QStringLiteral("123456"),
                                         &authenticated, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(authenticated.username, QStringLiteral("admin"));

    Auth::PasswordCredential after;
    QVERIFY(passwords.credentialForUser(1, &after, &errorMessage));
    QCOMPARE(after.scheme, QStringLiteral("pbkdf2_sha256"));
    QCOMPARE(after.iterations, Auth::kPbkdf2Iterations);
    QVERIFY(passwordService.verifyPassword(QStringLiteral("123456"), after, nullptr));
}

void UserManagementTest::resetPasswordRevokesSessions()
{
    UserRepository users;
    PasswordRepository passwords;
    PasswordService passwordService;
    AuditRepository audit;
    UserManagementService service(users, passwords, passwordService, audit,
                                  QStringLiteral("admin"));

    qint64 userId = -1;
    QVERIFY(createUser(service, QStringLiteral("reset_user"),
                       QStringLiteral("operator"),
                       QStringLiteral("OldPass123"), &userId));
    QVERIFY(insertSession(QStringLiteral("reset_user"), QStringLiteral("token-one")));
    QVERIFY(insertSession(QStringLiteral("reset_user"), QStringLiteral("token-two")));
    QCOMPARE(sessionCount(QStringLiteral("reset_user")), 2);

    QString errorMessage;
    QVERIFY2(service.resetPassword(userId, QStringLiteral("NewPass123"),
                                   &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(sessionCount(QStringLiteral("reset_user")), 0);

    AuthenticationService authentication(users, passwords, passwordService, audit);
    QVERIFY(authentication.authenticate(QStringLiteral("reset_user"),
                                        QStringLiteral("NewPass123"), nullptr,
                                        &errorMessage));
    QVERIFY(!authentication.authenticate(QStringLiteral("reset_user"),
                                         QStringLiteral("OldPass123"), nullptr));
}

void UserManagementTest::changesOwnPasswordAndRevokesOtherSessions()
{
    UserRepository users;
    PasswordRepository passwords;
    PasswordService passwordService;
    AuditRepository audit;
    UserManagementService admin(users, passwords, passwordService, audit,
                                QStringLiteral("admin"));
    QVERIFY(createUser(admin, QStringLiteral("self_change"),
                       QStringLiteral("operator"),
                       QStringLiteral("OldPass123")));
    QVERIFY(insertSession(QStringLiteral("self_change"), QStringLiteral("keep-token")));
    QVERIFY(insertSession(QStringLiteral("self_change"), QStringLiteral("revoke-token")));

    UserManagementService ownService(users, passwords, passwordService, audit,
                                     QStringLiteral("self_change"));
    QString errorMessage;
    QVERIFY2(ownService.changeOwnPassword(QStringLiteral("OldPass123"),
                                          QStringLiteral("NewPass123"),
                                          QStringLiteral("keep-token"),
                                          &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(sessionCount(QStringLiteral("self_change")), 1);

    QString username;
    QVERIFY(DatabaseManager::instance().validateRememberSession(
        QStringLiteral("keep-token"), &username, &errorMessage));
    QCOMPARE(username, QStringLiteral("self_change"));
    QVERIFY(!DatabaseManager::instance().validateRememberSession(
        QStringLiteral("revoke-token"), &username));
}

void UserManagementTest::writesAuditTrail()
{
    UserRepository users;
    PasswordRepository passwords;
    PasswordService passwordService;
    AuditRepository audit;
    UserManagementService service(users, passwords, passwordService, audit,
                                  QStringLiteral("admin"));

    AuthenticationService authentication(users, passwords, passwordService, audit);
    QString errorMessage;
    QVERIFY(authentication.authenticate(QStringLiteral("admin"),
                                        QStringLiteral("123456"), nullptr,
                                        &errorMessage));
    QVERIFY(!authentication.authenticate(QStringLiteral("admin"),
                                         QStringLiteral("wrong-password")));

    qint64 userId = -1;
    QVERIFY(createUser(service, QStringLiteral("audit_user"),
                       QStringLiteral("viewer"),
                       QStringLiteral("Viewer123"), &userId));
    QVERIFY(service.updateUser(userId, QStringLiteral("audit_user"),
                               QStringLiteral("审计用户"),
                               QStringLiteral("operator"), true,
                               &errorMessage));
    QVERIFY(service.setUserEnabled(userId, false, &errorMessage));
    QVERIFY(service.setUserEnabled(userId, true, &errorMessage));
    QVERIFY(service.setUserRole(userId, QStringLiteral("viewer"), &errorMessage));
    QVERIFY(service.resetPassword(userId, QStringLiteral("NewViewer123"),
                                  &errorMessage));
    UserManagementService ownService(users, passwords, passwordService, audit,
                                     QStringLiteral("admin"));
    QVERIFY(ownService.changeOwnPassword(QStringLiteral("123456"),
                                         QStringLiteral("AdminNew123"),
                                         QString(), &errorMessage));

    const QList<Auth::AuditRecord> entries = audit.recentEntries(100, &errorMessage);
    QVERIFY2(!entries.isEmpty(), qPrintable(errorMessage));
    QStringList eventTypes;
    for (const Auth::AuditRecord &entry : entries) {
        eventTypes.append(entry.eventType);
        QVERIFY(!entry.actorUsername.isEmpty());
        QVERIFY(entry.occurredAt.isValid());
    }
    for (const QString &eventType : {
             QStringLiteral("login_success"),
             QStringLiteral("login_failure"),
             QStringLiteral("user_create"),
             QStringLiteral("user_edit"),
             QStringLiteral("user_disable"),
             QStringLiteral("user_enable"),
             QStringLiteral("role_change"),
             QStringLiteral("password_reset"),
             QStringLiteral("password_change")}) {
        QVERIFY2(eventTypes.contains(eventType), qPrintable(eventType));
    }
}

void UserManagementTest::doesNotPersistSensitiveInformation()
{
    UserRepository users;
    PasswordRepository passwords;
    PasswordService passwordService;
    AuditRepository audit;
    UserManagementService service(users, passwords, passwordService, audit,
                                  QStringLiteral("admin"));

    const QString plainPassword = QStringLiteral("UniqueSecret987!");
    QString errorMessage;
    QVERIFY(createUser(service, QStringLiteral("secret_user"),
                       QStringLiteral("viewer"), plainPassword));

    const QString unsafeContext = QStringLiteral(
        "password=ContextSecret987 token=token-should-not-persist salt=abcdef");
    QVERIFY(audit.record(QStringLiteral("admin"), QStringLiteral("secret_user"),
                         QStringLiteral("test_sensitive"), QStringLiteral("success"),
                         unsafeContext, &errorMessage));
    const QList<Auth::AuditRecord> entries = audit.recentEntries(10, &errorMessage);
    QVERIFY(!entries.isEmpty());
    for (const Auth::AuditRecord &entry : entries) {
        QVERIFY(!entry.context.contains(QStringLiteral("ContextSecret987")));
        QVERIFY(!entry.context.contains(QStringLiteral("token-should-not-persist")));
        QVERIFY(!entry.context.contains(QStringLiteral("abcdef")));
    }

    QFile databaseFile(DatabaseManager::instance().databasePath());
    QVERIFY(databaseFile.open(QIODevice::ReadOnly));
    const QByteArray databaseBytes = databaseFile.readAll();
    QVERIFY(!databaseBytes.contains(plainPassword.toUtf8()));
}

QTEST_MAIN(UserManagementTest)

#include "UserManagementTest.moc"


