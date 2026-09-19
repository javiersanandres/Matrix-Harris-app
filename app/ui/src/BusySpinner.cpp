#include "BusySpinner.h"

#include <QPainter>
#include <QPropertyAnimation>
#include <QtMath>

namespace ui {

    BusySpinner::BusySpinner(QWidget* parent)
        : QWidget(parent)
    {
        setFixedSize(kDiameter, kDiameter);

        animation_ = new QPropertyAnimation(this, "angle", this);
        animation_->setStartValue(0);
        animation_->setEndValue(360);
        animation_->setDuration(1000);
        animation_->setLoopCount(-1); // spin forever while visible
        animation_->setEasingCurve(QEasingCurve::Linear);
    }

    void BusySpinner::setAngle(int angle) {
        angle_ = angle % 360;
        update();
    }

    QSize BusySpinner::sizeHint() const {
        return QSize(kDiameter, kDiameter);
    }

    void BusySpinner::paintEvent(QPaintEvent*) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const double radius = qMin(width(), height()) / 2.0 - 4.0;
        const QPointF center(width() / 2.0, height() / 2.0);
        const double dot_radius = radius * 0.14;

        QColor base = palette().color(QPalette::Highlight);

        for (int i = 0; i < kDotCount; ++i) {
            double dot_angle_deg = angle_ - i * (360.0 / kDotCount);
            double dot_angle_rad = qDegreesToRadians(dot_angle_deg);
            QPointF pos = center + QPointF(radius * qCos(dot_angle_rad), radius * qSin(dot_angle_rad));

            // Trailing fade: the leading dot (i == 0) is fully opaque, each one
            // behind it a bit dimmer, down to a faint floor rather than fully
            // invisible so the full ring shape stays legible.
            double fade = 1.0 - static_cast<double>(i) / kDotCount;
            QColor color = base;
            color.setAlphaF(qBound(0.15, fade, 1.0));

            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(pos, dot_radius, dot_radius);
        }
    }

    void BusySpinner::showEvent(QShowEvent* event) {
        QWidget::showEvent(event);
        animation_->start();
    }

    void BusySpinner::hideEvent(QHideEvent* event) {
        animation_->stop();
        QWidget::hideEvent(event);
    }

} // namespace ui
