#pragma once

#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QListWidget;
class QStackedWidget;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    void setupUi();
    void setupPages();
    void addPage(const QString &title, const QIcon &icon, QWidget *page);

    QListWidget *m_categoryList = nullptr;
    QStackedWidget *m_pageStack = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
    QLabel *m_statusLabel = nullptr;
};
