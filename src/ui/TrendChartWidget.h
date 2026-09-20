#pragma once

#include <QList>
#include <QWidget>

class TrendChartWidget : public QWidget
{
public:
    explicit TrendChartWidget(QWidget *parent = nullptr);

    void addSample(double temperature, double pressure);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static constexpr int MaxPoints = 60;

    QList<double> m_temperature;
    QList<double> m_pressure;
};
