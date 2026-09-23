#pragma once

#include <QDialog>
#include <memory>

class AuditRepository;
class PasswordRepository;
class PasswordService;
class QPushButton;
class QTableWidget;
class UserManagementService;
class UserRepository;

class UserManagementDialog : public QDialog
{
public:
    enum class Mode {
        Management,
        PasswordOnly,
    };

    explicit UserManagementDialog(const QString &actorUsername,
                                  Mode mode = Mode::Management,
                                  QWidget *parent = nullptr);
    ~UserManagementDialog() override;

private:
    void setupUi();
    void refreshUsers();
    void updateButtonState();
    qint64 selectedUserId() const;
    bool collectUser(bool creating,
                     QString *username,
                     QString *displayName,
                     QString *role,
                     bool *enabled,
                     QString *password);
    bool changeSelectedPassword();
    bool changeOwnPassword();
    void showError(const QString &message);

    Mode m_mode = Mode::Management;
    QString m_actorUsername;
    bool m_canManageUsers = false;
    std::unique_ptr<UserRepository> m_users;
    std::unique_ptr<PasswordRepository> m_passwords;
    std::unique_ptr<PasswordService> m_passwordService;
    std::unique_ptr<AuditRepository> m_audit;
    std::unique_ptr<UserManagementService> m_service;
    QTableWidget *m_userTable = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_enableButton = nullptr;
    QPushButton *m_resetPasswordButton = nullptr;
    QPushButton *m_changeOwnPasswordButton = nullptr;
    QPushButton *m_closeButton = nullptr;
};
