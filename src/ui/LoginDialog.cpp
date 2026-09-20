#include "ui/LoginDialog.h"

#include "database/DatabaseManager.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Mu-Monitor 登录"));
    setObjectName(QStringLiteral("loginDialog"));
    setMinimumSize(420, 240);
    resize(440, 260);
    setModal(true);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(28, 24, 28, 20);
    rootLayout->setSpacing(16);

    auto *titleLabel = new QLabel(QStringLiteral("Mu-Monitor"), this);
    titleLabel->setObjectName(QStringLiteral("loginTitle"));
    auto *subtitleLabel = new QLabel(QStringLiteral("工业设备监控与告警平台"), this);
    subtitleLabel->setObjectName(QStringLiteral("loginSubtitle"));
    rootLayout->addWidget(titleLabel);
    rootLayout->addWidget(subtitleLabel);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(12);

    m_usernameEdit = new QLineEdit(QStringLiteral("admin"), this);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("请输入密码"));

    form->addRow(QStringLiteral("用户名"), m_usernameEdit);
    form->addRow(QStringLiteral("密码"), m_passwordEdit);
    rootLayout->addLayout(form);

    m_showPasswordCheck = new QCheckBox(QStringLiteral("显示密码"), this);
    m_rememberCheck = new QCheckBox(QStringLiteral("30 天内记住登录状态"), this);
    m_rememberCheck->setChecked(true);
    rootLayout->addWidget(m_showPasswordCheck);
    rootLayout->addWidget(m_rememberCheck);
    connect(m_showPasswordCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_passwordEdit->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
    });

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("登录"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("退出"));
    rootLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &LoginDialog::attemptLogin);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_passwordEdit->setFocus();
}

QString LoginDialog::username() const
{
    return m_usernameEdit->text().trimmed();
}

void LoginDialog::attemptLogin()
{
    const QString name = username();
    const QString password = m_passwordEdit->text();

    if (name.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("登录失败"), QStringLiteral("请输入用户名和密码。"));
        return;
    }

    if (!DatabaseManager::instance().validateUser(name, password)) {
        QMessageBox::warning(this, QStringLiteral("登录失败"), QStringLiteral("用户名或密码错误。"));
        m_passwordEdit->clear();
        m_passwordEdit->setFocus();
        return;
    }

    QSettings settings;
    if (m_rememberCheck->isChecked()) {
        QString token;
        QString errorMessage;
        if (DatabaseManager::instance().createRememberSession(name, &token, &errorMessage)) {
            settings.setValue(QStringLiteral("auth/rememberToken"), token);
            settings.setValue(QStringLiteral("auth/rememberUsername"), name);
        } else {
            settings.remove(QStringLiteral("auth/rememberToken"));
            settings.remove(QStringLiteral("auth/rememberUsername"));
            QMessageBox::warning(
                this,
                QStringLiteral("登录成功"),
                QStringLiteral("登录成功，但未能创建免登录会话：%1").arg(errorMessage));
        }
    } else {
        const QString oldToken = settings.value(QStringLiteral("auth/rememberToken")).toString();
        DatabaseManager::instance().revokeRememberSession(oldToken);
        settings.remove(QStringLiteral("auth/rememberToken"));
        settings.remove(QStringLiteral("auth/rememberUsername"));
    }

    accept();
}
