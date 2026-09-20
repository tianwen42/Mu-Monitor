#include "ui/TrendChartWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QtMath>

TrendChartWidget::TrendChartWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void TrendChartWidget::addSample(double temperature, double pressure)
{
    m_temperature.append(temperature);
    m_pressure.append(pressure);

    if (m_temperature.size() > MaxPoints) {
        m_temperature.removeFirst();
    }
    if (m_pressure.size() > MaxPoints) {
        m_pressure.removeFirst();
    }

    update();
}

void TrendChartWidget::clear()
{
    m_temperature.clear();
    m_pressure.clear();
    update();
}

void TrendChartWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF plot = rect().adjusted(46, 28, -16, -28);
    painter.fillRect(rect(), Qt::transparent);

    painter.setPen(QPen(QColor(QStringLiteral("#334155")), 1));
    for (int i = 0; i <= 4; ++i) {
        const qreal y = plot.top() + plot.height() * i / 4.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }

    painter.setPen(QColor(QStringLiteral("#64748b")));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
    for (int i = 0; i <= 4; ++i) {
        const int value = 100 - i * 25;
        const qreal y = plot.top() + plot.height() * i / 4.0;
        painter.drawText(QRectF(0, y - 8, 40, 16), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(value));
    }

    painter.setPen(QColor(QStringLiteral("#64748b")));
    painter.drawText(QRectF(plot.left(), 2, 200, 18), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("温度 ℃  /  压力 MPa"));

    if (m_temperature.size() < 2) {
        painter.setPen(QColor(QStringLiteral("#64748b")));
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("等待模拟数据..."));
        return;
    }

    auto drawSeries = [&](const QList<double> &values, double maxValue, const QColor &color) {
        QPainterPath path;
        for (int i = 0; i < values.size(); ++i) {
            const qreal x = plot.left() + plot.width() * i / qMax(1, MaxPoints - 1);
            const double bounded = qBound(0.0, values.at(i), maxValue);
            const qreal y = plot.bottom() - plot.height() * bounded / maxValue;
            if (i == 0) {
                path.moveTo(x, y);
            } else {
                path.lineTo(x, y);
            }
        }
        painter.setPen(QPen(color, 2));
        painter.drawPath(path);
    };

    drawSeries(m_temperature, 100.0, QColor(QStringLiteral("#38bdf8")));
    drawSeries(m_pressure, 3.0, QColor(QStringLiteral("#f59e0b")));

    painter.setPen(QPen(QColor(QStringLiteral("#38bdf8")), 3));
    painter.drawLine(QPointF(plot.right() - 150, 11), QPointF(plot.right() - 128, 11));
    painter.setPen(QColor(QStringLiteral("#cbd5e1")));
    painter.drawText(QPointF(plot.right() - 122, 15), QStringLiteral("温度"));

    painter.setPen(QPen(QColor(QStringLiteral("#f59e0b")), 3));
    painter.drawLine(QPointF(plot.right() - 70, 11), QPointF(plot.right() - 48, 11));
    painter.setPen(QColor(QStringLiteral("#cbd5e1")));
    painter.drawText(QPointF(plot.right() - 42, 15), QStringLiteral("压力"));
}
