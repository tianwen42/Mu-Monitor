#pragma once

#include <QDialog>

class QTabWidget;

class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);

private:
    QString buildInformation() const;
    void copyBuildInformation();

    QTabWidget *m_tabs = nullptr;
};
