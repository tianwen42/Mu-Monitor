#include "ui/UserManagementDialog.h"

#include "auth/AuditRepository.h"
#include "auth/PasswordService.h"
#include "auth/UserManagementService.h"
#include "database/PasswordRepository.h"
#include "database/UserRepository.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

void setTableItem(QTableWidget *table, int row, int column,
                  const QString &text, qint64 userId = -1)
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    if (column == 0 && userId >= 0) {
        item->setData(Qt::UserRole, userId);
    }
    table->setItem(row, column, item);
}

} // namespace

UserManagementDialog::UserManagementDialog(const QString &actorUsername,
                                           Mode mode,
                                           QWidget *parent)
    : QDialog(parent)
    , m_mode(mode)
    , m_actorUsername(actorUsername.trimmed())
    , m_users(std::make_unique<UserRepository>())
    , m_passwords(std::make_unique<PasswordRepository>())
    , m_passwordService(std::make_unique<PasswordService>())
    , m_audit(std::make_unique<AuditRepository>())
{
    QString initializationError;
    if (!m_users->initialize(&initializationError)
        || !m_passwords->initialize(&initializationError)
        || !m_audit->initialize(&initializationError)) {
        showError(initializationError);
    }

    m_service = std::make_unique<UserManagementService>(
        *m_users, *m_passwords, *m_passwordService, *m_audit, m_actorUsername);
    m_canManageUsers = m_service->can(Auth::Permission::ManageUsers);
    if (m_mode == Mode::Management && !m_canManageUsers) {
        m_mode = Mode::PasswordOnly;
    }

    setupUi();
    if (m_mode == Mode::Management) {
        refreshUsers();
    }
    updateButtonState();
}

UserManagementDialog::~UserManagementDialog() = default;

void UserManagementDialog::setupUi()
{
    setObjectName(QStringLiteral("userManagementDialog"));
    setWindowTitle(m_mode == Mode::Management
                       ? QStringLiteral("用户管理")
                       : QStringLiteral("修改我的密码"));
    setMinimumSize(m_mode == Mode::Management ? QSize(920, 560) : QSize(460, 250));
    resize(m_mode == Mode::Management ? QSize(980, 620) : QSize(480, 270));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 16);
    rootLayout->setSpacing(12);

    auto *title = new QLabel(m_mode == Mode::Management
                                 ? QStringLiteral("用户、角色与访问状态")
                                 : QStringLiteral("修改当前账号密码"),
                             this);
    title->setObjectName(QStringLiteral("userManagementTitle"));
    rootLayout->addWidget(title);

    if (m_mode == Mode::Management) {
        m_userTable = new QTableWidget(0, 6, this);
        m_userTable->setObjectName(QStringLiteral("userTable"));
        m_userTable->setHorizontalHeaderLabels({
            QStringLiteral("用户名"),
            QStringLiteral("显示名"),
            QStringLiteral("角色"),
            QStringLiteral("状态"),
            QStringLiteral("创建时间"),
            QStringLiteral("更新时间"),
        });
        m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_userTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_userTable->setAlternatingRowColors(true);
        m_userTable->setShowGrid(false);
        m_userTable->verticalHeader()->setVisible(false);
        m_userTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_userTable->horizontalHeader()->setMinimumSectionSize(110);
        rootLayout->addWidget(m_userTable, 1);

        auto *buttonLayout = new QHBoxLayout;
        buttonLayout->setSpacing(8);

        m_addButton = new QPushButton(QStringLiteral("新增用户"), this);
        m_addButton->setObjectName(QStringLiteral("addUserButton"));
        m_editButton = new QPushButton(QStringLiteral("编辑用户"), this);
        m_editButton->setObjectName(QStringLiteral("editUserButton"));
        m_enableButton = new QPushButton(QStringLiteral("禁用用户"), this);
        m_enableButton->setObjectName(QStringLiteral("toggleUserButton"));
        m_resetPasswordButton = new QPushButton(QStringLiteral("重置密码"), this);
        m_resetPasswordButton->setObjectName(QStringLiteral("resetPasswordButton"));
        m_changeOwnPasswordButton = new QPushButton(QStringLiteral("修改我的密码"), this);
        m_changeOwnPasswordButton->setObjectName(QStringLiteral("changeOwnPasswordButton"));

        buttonLayout->addWidget(m_addButton);
        buttonLayout->addWidget(m_editButton);
        buttonLayout->addWidget(m_enableButton);
        buttonLayout->addWidget(m_resetPasswordButton);
        buttonLayout->addStretch();
        buttonLayout->addWidget(m_changeOwnPasswordButton);
        rootLayout->addLayout(buttonLayout);

        connect(m_userTable, &QTableWidget::itemSelectionChanged,
                this, &UserManagementDialog::updateButtonState);
        connect(m_addButton, &QPushButton::clicked, this, [this]() {
            QString username;
            QString displayName;
            QString role;
            bool enabled = true;
            QString password;
            if (!collectUser(true, &username, &displayName,
                             &role, &enabled, &password)) {
                return;
            }

            QString errorMessage;
            if (!m_service->createUser(username, displayName, role, password,
                                       enabled, nullptr, &errorMessage)) {
                showError(errorMessage);
                return;
            }
            password.fill(QLatin1Char('\0'));
            password.clear();
            refreshUsers();
        });
        connect(m_editButton, &QPushButton::clicked, this, [this]() {
            const qint64 userId = selectedUserId();
            if (userId < 0) {
                return;
            }

            const auto rows = m_userTable->selectionModel()->selectedRows();
            if (rows.isEmpty()) {
                return;
            }
            const int row = rows.constFirst().row();
            QString username = m_userTable->item(row, 0)->text();
            QString displayName = m_userTable->item(row, 1)->text();
            const QString currentRole = m_userTable->item(row, 0)
                ? m_userTable->item(row, 0)->data(Qt::UserRole + 1).toString()
                : QString();
            QString role = currentRole;
            bool enabled = m_userTable->item(row, 3)->text() == QStringLiteral("已启用");
            QString unusedPassword;
            if (!collectUser(false, &username, &displayName,
                             &role, &enabled, &unusedPassword)) {
                return;
            }

            QString errorMessage;
            if (!m_service->updateUser(userId, username, displayName,
                                       role, enabled, &errorMessage)) {
                showError(errorMessage);
                return;
            }
            refreshUsers();
        });
        connect(m_enableButton, &QPushButton::clicked, this, [this]() {
            const qint64 userId = selectedUserId();
            if (userId < 0) {
                return;
            }
            const bool currentlyEnabled =
                m_enableButton->text() == QStringLiteral("禁用用户");
            const QString action = currentlyEnabled
                ? QStringLiteral("禁用")
                : QStringLiteral("启用");
            if (QMessageBox::question(
                    this, QStringLiteral("确认%1用户").arg(action),
                    QStringLiteral("确定要%1所选用户吗？").arg(action))
                != QMessageBox::Yes) {
                return;
            }

            QString errorMessage;
            if (!m_service->setUserEnabled(userId, !currentlyEnabled,
                                           &errorMessage)) {
                showError(errorMessage);
                return;
            }
            refreshUsers();
        });
        connect(m_resetPasswordButton, &QPushButton::clicked,
                this, &UserManagementDialog::changeSelectedPassword);
    } else {
        auto *hint = new QLabel(
            QStringLiteral("修改密码前需要验证旧密码；成功后仅保留当前免登录会话。"),
            this);
        hint->setWordWrap(true);
        rootLayout->addWidget(hint);
        m_changeOwnPasswordButton = new QPushButton(QStringLiteral("修改密码"), this);
        m_changeOwnPasswordButton->setObjectName(QStringLiteral("changeOwnPasswordButton"));
        rootLayout->addWidget(m_changeOwnPasswordButton, 0, Qt::AlignRight);
    }

    rootLayout->addStretch();
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_closeButton = buttonBox->button(QDialogButtonBox::Close);
    m_closeButton->setObjectName(QStringLiteral("closeUserManagementButton"));
    rootLayout->addWidget(buttonBox);

    connect(m_changeOwnPasswordButton, &QPushButton::clicked,
            this, &UserManagementDialog::changeOwnPassword);
    connect(buttonBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
}

void UserManagementDialog::refreshUsers()
{
    if (!m_userTable || !m_service) {
        return;
    }

    QList<Auth::UserRecord> users;
    QString errorMessage;
    if (!m_service->listUsers(&users, &errorMessage)) {
        showError(errorMessage);
        return;
    }

    m_userTable->setUpdatesEnabled(false);
    m_userTable->setRowCount(users.size());
    for (int row = 0; row < users.size(); ++row) {
        const Auth::UserRecord &user = users.at(row);
        setTableItem(m_userTable, row, 0, user.username, user.id);
        if (m_userTable->item(row, 0)) {
            m_userTable->item(row, 0)->setData(Qt::UserRole + 1, user.role);
        }
        setTableItem(m_userTable, row, 1, user.displayName, user.id);
        setTableItem(m_userTable, row, 2, Auth::roleDisplayName(user.role), user.id);
        setTableItem(m_userTable, row, 3,
                     user.enabled ? QStringLiteral("已启用") : QStringLiteral("已禁用"),
                     user.id);
        setTableItem(m_userTable, row, 4,
                     user.createdAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                     user.id);
        setTableItem(m_userTable, row, 5,
                     user.updatedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                     user.id);
    }
    m_userTable->setUpdatesEnabled(true);
    updateButtonState();
}

void UserManagementDialog::updateButtonState()
{
    const bool hasSelection = selectedUserId() >= 0;
    if (m_addButton) {
        m_addButton->setEnabled(m_canManageUsers);
    }
    if (m_editButton) {
        m_editButton->setEnabled(m_canManageUsers && hasSelection);
    }
    if (m_resetPasswordButton) {
        m_resetPasswordButton->setEnabled(m_canManageUsers && hasSelection);
    }
    if (m_changeOwnPasswordButton) {
        m_changeOwnPasswordButton->setEnabled(true);
    }
    if (m_enableButton) {
        m_enableButton->setEnabled(m_canManageUsers && hasSelection);
        if (hasSelection && m_userTable) {
            const int row = m_userTable->currentRow();
            const bool enabled = m_userTable->item(row, 3)
                && m_userTable->item(row, 3)->text() == QStringLiteral("已启用");
            m_enableButton->setText(enabled
                                        ? QStringLiteral("禁用用户")
                                        : QStringLiteral("启用用户"));
        }
    }
}

qint64 UserManagementDialog::selectedUserId() const
{
    if (!m_userTable || !m_userTable->selectionModel()) {
        return -1;
    }
    const auto rows = m_userTable->selectionModel()->selectedRows();
    if (rows.isEmpty() || !m_userTable->item(rows.constFirst().row(), 0)) {
        return -1;
    }
    return m_userTable->item(rows.constFirst().row(), 0)
        ->data(Qt::UserRole).toLongLong();
}

bool UserManagementDialog::collectUser(bool creating,
                                       QString *username,
                                       QString *displayName,
                                       QString *role,
                                       bool *enabled,
                                       QString *password)
{
    QDialog dialog(this);
    dialog.setWindowTitle(creating ? QStringLiteral("新增用户")
                                   : QStringLiteral("编辑用户"));
    dialog.setMinimumWidth(420);

    auto *root = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(12);

    auto *usernameEdit = new QLineEdit(username ? *username : QString(), &dialog);
    usernameEdit->setPlaceholderText(QStringLiteral("3-64 个字符"));
    auto *displayNameEdit = new QLineEdit(displayName ? *displayName : QString(), &dialog);
    auto *roleCombo = new QComboBox(&dialog);
    for (const QString &roleCode : Auth::builtInRoleCodes()) {
        roleCombo->addItem(Auth::roleDisplayName(roleCode), roleCode);
    }
    if (role) {
        const int roleIndex = roleCombo->findData(*role);
        roleCombo->setCurrentIndex(roleIndex >= 0 ? roleIndex : 2);
    } else {
        roleCombo->setCurrentIndex(roleCombo->findData(QStringLiteral("viewer")));
    }
    auto *enabledCheck = new QCheckBox(QStringLiteral("启用该用户"), &dialog);
    enabledCheck->setChecked(enabled ? *enabled : true);

    auto *passwordEdit = new QLineEdit(&dialog);
    auto *confirmEdit = new QLineEdit(&dialog);
    passwordEdit->setEchoMode(QLineEdit::Password);
    confirmEdit->setEchoMode(QLineEdit::Password);

    form->addRow(QStringLiteral("用户名"), usernameEdit);
    form->addRow(QStringLiteral("显示名"), displayNameEdit);
    form->addRow(QStringLiteral("角色"), roleCombo);
    form->addRow(QString(), enabledCheck);
    if (creating) {
        form->addRow(QStringLiteral("初始密码"), passwordEdit);
        form->addRow(QStringLiteral("确认密码"), confirmEdit);
    }
    root->addLayout(form);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (creating && passwordEdit->text() != confirmEdit->text()) {
            QMessageBox::warning(&dialog, QStringLiteral("密码不一致"),
                                 QStringLiteral("两次输入的密码不一致。"));
            return;
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    if (username) *username = usernameEdit->text().trimmed();
    if (displayName) *displayName = displayNameEdit->text().trimmed();
    if (role) *role = roleCombo->currentData().toString();
    if (enabled) *enabled = enabledCheck->isChecked();
    if (password) *password = passwordEdit->text();
    return true;
}

bool UserManagementDialog::changeSelectedPassword()
{
    const qint64 userId = selectedUserId();
    if (userId < 0) {
        return false;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("重置用户密码"));
    auto *root = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *passwordEdit = new QLineEdit(&dialog);
    auto *confirmEdit = new QLineEdit(&dialog);
    passwordEdit->setEchoMode(QLineEdit::Password);
    confirmEdit->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("新密码"), passwordEdit);
    form->addRow(QStringLiteral("确认密码"), confirmEdit);
    root->addLayout(form);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (passwordEdit->text() != confirmEdit->text()) {
            QMessageBox::warning(&dialog, QStringLiteral("密码不一致"),
                                 QStringLiteral("两次输入的密码不一致。"));
            return;
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    QString errorMessage;
    if (!m_service->resetPassword(userId, passwordEdit->text(), &errorMessage)) {
        showError(errorMessage);
        return false;
    }
    QMessageBox::information(this, QStringLiteral("重置成功"),
                             QStringLiteral("密码已重置，该用户所有已保存会话均已撤销。"));
    return true;
}

bool UserManagementDialog::changeOwnPassword()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("修改我的密码"));
    auto *root = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *oldPasswordEdit = new QLineEdit(&dialog);
    auto *newPasswordEdit = new QLineEdit(&dialog);
    auto *confirmEdit = new QLineEdit(&dialog);
    for (QLineEdit *edit : {oldPasswordEdit, newPasswordEdit, confirmEdit}) {
        edit->setEchoMode(QLineEdit::Password);
    }
    form->addRow(QStringLiteral("旧密码"), oldPasswordEdit);
    form->addRow(QStringLiteral("新密码"), newPasswordEdit);
    form->addRow(QStringLiteral("确认新密码"), confirmEdit);
    root->addLayout(form);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (newPasswordEdit->text() != confirmEdit->text()) {
            QMessageBox::warning(&dialog, QStringLiteral("密码不一致"),
                                 QStringLiteral("两次输入的新密码不一致。"));
            return;
        }
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    QSettings settings;
    const QString preserveToken =
        settings.value(QStringLiteral("auth/rememberToken")).toString();
    QString errorMessage;
    if (!m_service->changeOwnPassword(oldPasswordEdit->text(),
                                      newPasswordEdit->text(),
                                      preserveToken, &errorMessage)) {
        showError(errorMessage);
        return false;
    }

    oldPasswordEdit->clear();
    newPasswordEdit->clear();
    confirmEdit->clear();
    QMessageBox::information(this, QStringLiteral("修改成功"),
                             QStringLiteral("密码已更新，该账号的其他会话已撤销。"));
    if (m_mode == Mode::PasswordOnly) {
        accept();
    }
    return true;
}

void UserManagementDialog::showError(const QString &message)
{
    QMessageBox::warning(this, QStringLiteral("操作失败"),
                         message.isEmpty() ? QStringLiteral("操作失败") : message);
}
