#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);

    QString username() const;

private slots:
    void attemptLogin();

private:
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QCheckBox *m_showPasswordCheck = nullptr;
};
