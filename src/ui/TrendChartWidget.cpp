#include "ui/TrendChartWidget.h"

#include <QPair>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QtMath>

namespace {
QPair<double, double> paddedRange(const QList<double> &values,
                                  double fallbackMin, double fallbackMax,
                                  double minimumPadding)
{
    if (values.isEmpty()) return {fallbackMin, fallbackMax};
    double minimum = values.first();
    double maximum = values.first();
    for (double value : values) {
        minimum = qMin(minimum, value);
        maximum = qMax(maximum, value);
    }
    if (qFuzzyCompare(minimum, maximum)) {
        minimum -= minimumPadding;
        maximum += minimumPadding;
    } else {
        const double padding = qMax(minimumPadding, (maximum - minimum) * 0.12);
        minimum -= padding;
        maximum += padding;
    }
    return {minimum, maximum};
}
}

TrendChartWidget::TrendChartWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(260);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void TrendChartWidget::setSamples(const QList<double> &temperatures,
                                  const QList<double> &pressures,
                                  const QString &deviceLabel)
{
    const int count = qMin(temperatures.size(), pressures.size());
    const int start = qMax(0, count - MaxPoints);
    m_temperature = temperatures.mid(start, count - start);
    m_pressure = pressures.mid(start, count - start);
    m_deviceLabel = deviceLabel;
    update();
}

void TrendChartWidget::addSample(double temperature, double pressure)
{
    m_temperature.append(temperature);
    m_pressure.append(pressure);
    while (m_temperature.size() > MaxPoints) m_temperature.removeFirst();
    while (m_pressure.size() > MaxPoints) m_pressure.removeFirst();
    update();
}

void TrendChartWidget::clear()
{
    m_temperature.clear();
    m_pressure.clear();
    m_deviceLabel.clear();
    update();
}

void TrendChartWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF plot = rect().adjusted(58, 34, -62, -34);
    const int count = qMin(m_temperature.size(), m_pressure.size());

    painter.setPen(QColor(QStringLiteral("#64748b")));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
    painter.drawText(QRectF(plot.left(), 2, plot.width(), 20),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     m_deviceLabel.isEmpty() ? QStringLiteral("实时趋势") : m_deviceLabel);

    if (count < 2) {
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("等待实时数据..."));
        return;
    }

    const auto temperatureRange = paddedRange(m_temperature, 0.0, 100.0, 1.0);
    const auto pressureRange = paddedRange(m_pressure, 0.0, 3.0, 0.1);
    constexpr int divisions = 4;

    painter.setPen(QPen(QColor(QStringLiteral("#334155")), 1));
    for (int i = 0; i <= divisions; ++i) {
        const qreal y = plot.top() + plot.height() * i / divisions;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    painter.setFont(QFont(QStringLiteral("Consolas"), 8));
    painter.setPen(QColor(QStringLiteral("#38bdf8")));
    for (int i = 0; i <= divisions; ++i) {
        const qreal y = plot.top() + plot.height() * i / divisions;
        const double value = temperatureRange.second - (temperatureRange.second - temperatureRange.first) * i / divisions;
        painter.drawText(QRectF(0, y - 9, plot.left() - 7, 18), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(value, 'f', 1));
    }

    painter.setPen(QColor(QStringLiteral("#f59e0b")));
    for (int i = 0; i <= divisions; ++i) {
        const qreal y = plot.top() + plot.height() * i / divisions;
        const double value = pressureRange.second - (pressureRange.second - pressureRange.first) * i / divisions;
        painter.drawText(QRectF(plot.right() + 7, y - 9, 54, 18), Qt::AlignLeft | Qt::AlignVCenter,
                         QString::number(value, 'f', 2));
    }

    auto drawSeries = [&](const QList<double> &values, const QPair<double, double> &range,
                          const QColor &color) {
        QPainterPath path;
        const int denominator = qMax(1, values.size() - 1);
        for (int i = 0; i < values.size(); ++i) {
            const qreal x = plot.left() + plot.width() * i / denominator;
            const double bounded = qBound(range.first, values.at(i), range.second);
            const qreal ratio = (bounded - range.first) / (range.second - range.first);
            const qreal y = plot.bottom() - plot.height() * ratio;
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        painter.setPen(QPen(color, 2));
        painter.drawPath(path);
    };

    drawSeries(m_temperature, temperatureRange, QColor(QStringLiteral("#38bdf8")));
    drawSeries(m_pressure, pressureRange, QColor(QStringLiteral("#f59e0b")));

    painter.setPen(QColor(QStringLiteral("#64748b")));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 8, plot.width(), 18), Qt::AlignCenter,
                     QStringLiteral("最近 %1 个采样点").arg(count));
}