#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class TrendChartWidget : public QWidget
{
public:
    explicit TrendChartWidget(QWidget *parent = nullptr);

    void setSamples(const QList<double> &temperatures,
                    const QList<double> &pressures,
                    const QString &deviceLabel = QString());
    void addSample(double temperature, double pressure);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static constexpr int MaxPoints = 120;
    QList<double> m_temperature;
    QList<double> m_pressure;
    QString m_deviceLabel;
};