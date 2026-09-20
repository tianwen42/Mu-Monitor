#include "ui/LoginDialog.h"

#include "database/DatabaseManager.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
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
    setMinimumSize(480, 330);
    resize(480, 330);
    setSizeGripEnabled(false);
    setModal(true);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(32, 26, 32, 24);
    rootLayout->setSpacing(10);

    auto *titleLabel = new QLabel(QStringLiteral("Mu-Monitor"), this);
    titleLabel->setObjectName(QStringLiteral("loginTitle"));
    titleLabel->setAlignment(Qt::AlignCenter);

    auto *subtitleLabel = new QLabel(QStringLiteral("工业设备监控与告警平台"), this);
    subtitleLabel->setObjectName(QStringLiteral("loginSubtitle"));
    subtitleLabel->setAlignment(Qt::AlignCenter);

    rootLayout->addWidget(titleLabel);
    rootLayout->addWidget(subtitleLabel);
    rootLayout->addSpacing(4);

    auto *separator = new QFrame(this);
    separator->setObjectName(QStringLiteral("loginSeparator"));
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    rootLayout->addWidget(separator);
    rootLayout->addSpacing(8);

    auto *form = new QFormLayout;
    form->setContentsMargins(12, 0, 12, 0);
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(14);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_usernameEdit = new QLineEdit(QStringLiteral("admin"), this);
    m_usernameEdit->setMinimumHeight(34);
    m_usernameEdit->setClearButtonEnabled(true);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setMinimumHeight(34);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("请输入密码"));

    form->addRow(QStringLiteral("用户名"), m_usernameEdit);
    form->addRow(QStringLiteral("密码"), m_passwordEdit);
    rootLayout->addLayout(form);

    auto *optionsLayout = new QHBoxLayout;
    optionsLayout->setContentsMargins(12, 0, 12, 0);
    optionsLayout->setSpacing(16);

    m_showPasswordCheck = new QCheckBox(QStringLiteral("显示密码"), this);
    m_rememberCheck = new QCheckBox(QStringLiteral("30 天内免登录"), this);
    m_rememberCheck->setChecked(true);

    optionsLayout->addWidget(m_showPasswordCheck);
    optionsLayout->addStretch();
    optionsLayout->addWidget(m_rememberCheck);
    rootLayout->addLayout(optionsLayout);

    rootLayout->addStretch();

    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->setCenterButtons(false);

    QPushButton *loginButton = buttonBox->button(QDialogButtonBox::Ok);
    QPushButton *exitButton = buttonBox->button(QDialogButtonBox::Cancel);
    loginButton->setText(QStringLiteral("登录"));
    loginButton->setMinimumSize(96, 36);
    loginButton->setDefault(true);

    exitButton->setText(QStringLiteral("退出"));
    exitButton->setMinimumSize(96, 36);
    exitButton->setProperty("secondary", true);

    rootLayout->addWidget(buttonBox);

    connect(m_showPasswordCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_passwordEdit->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
    });
    connect(buttonBox, &QDialogButtonBox::accepted, this, &LoginDialog::attemptLogin);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setTabOrder(m_usernameEdit, m_passwordEdit);
    setTabOrder(m_passwordEdit, loginButton);
    setTabOrder(loginButton, exitButton);
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
        DatabaseManager::instance().insertLog(
            QStringLiteral("WARN"), QStringLiteral("auth"), QStringLiteral("登录失败：用户名或密码为空"));
        QMessageBox::warning(this, QStringLiteral("登录失败"), QStringLiteral("请输入用户名和密码。"));
        return;
    }

    if (!DatabaseManager::instance().validateUser(name, password)) {
        DatabaseManager::instance().insertLog(
            QStringLiteral("WARN"), QStringLiteral("auth"),
            QStringLiteral("登录失败：用户名或密码错误，用户名 %1").arg(name));
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
            DatabaseManager::instance().insertLog(
                QStringLiteral("ERROR"), QStringLiteral("auth"),
                QStringLiteral("创建免登录会话失败：%1").arg(errorMessage));
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

    DatabaseManager::instance().insertLog(
        QStringLiteral("INFO"), QStringLiteral("auth"),
        QStringLiteral("用户登录成功：%1").arg(name));
    accept();
}
