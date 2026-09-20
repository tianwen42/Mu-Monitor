#include "ui/LoginDialog.h"

#include "database/DatabaseManager.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
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
    rootLayout->addWidget(m_showPasswordCheck);
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

    accept();
}
