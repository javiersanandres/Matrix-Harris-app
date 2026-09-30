#include "BusySpinner.h"
#include "UiStyle.h"

#include <QConicalGradient>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace ui {

    namespace {
        constexpr double kTurnSeconds = 1.6;   // one full turn of the whole ring
        constexpr double kCycleSeconds = 1.3;  // one stretch-and-shrink of the arc
        constexpr double kStretch = 250.0;     // degrees the arc's head runs ahead
        constexpr double kMinSpan = 18.0;      // the arc never vanishes completely
        constexpr double kPenWidth = 4.5;

        // Smooth start and end (smoothstep), clamped to [0, 1].
        double ease(double x) {
            x = std::clamp(x, 0.0, 1.0);
            return x * x * (3.0 - 2.0 * x);
        }
    }

    BusySpinner::BusySpinner(QWidget* parent)
        : QWidget(parent)
    {
        setFixedSize(kDiameter, kDiameter);
        setAttribute(Qt::WA_TranslucentBackground);

        frame_timer_ = new QTimer(this);
        frame_timer_->setInterval(16); // ~60 frames per second
        connect(frame_timer_, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    }

    QSize BusySpinner::sizeHint() const {
        return QSize(kDiameter, kDiameter);
    }

    void BusySpinner::paintEvent(QPaintEvent*) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const double inset = kPenWidth / 2.0 + 3.0;
        const QRectF ring = QRectF(rect()).adjusted(inset, inset, -inset, -inset);

        // The track: a faint full ring.
        p.setPen(QPen(style::palette::accent_soft, kPenWidth));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(ring);

        // The arc. Each cycle its head runs kStretch degrees ahead of the tail
        // (first half), then the tail catches up (second half); every cycle
        // starts where the previous one ended, and the whole ring turns too.
        const double t = clock_.isValid() ? clock_.elapsed() / 1000.0 : 0.0;
        const double cycles = t / kCycleSeconds;
        const double cycle = std::floor(cycles);
        const double phase = cycles - cycle;
        const double head = kStretch * ease(phase * 2.0);
        const double tail = kStretch * ease(phase * 2.0 - 1.0);
        const double start = std::fmod(360.0 * t / kTurnSeconds + cycle * kStretch + tail, 360.0);
        const double span = head - tail + kMinSpan;

        // Clockwise on screen: Qt's angles run counter-clockwise, so both are
        // negated. The gradient runs from the head (violet) back to the tail
        // (indigo), fading slightly towards the tail.
        const double head_angle = -(start + span);
        QConicalGradient gradient(ring.center(), head_angle);
        QColor tail_colour = style::palette::accent;
        tail_colour.setAlphaF(0.55);
        gradient.setColorAt(0.0, style::palette::violet);
        gradient.setColorAt(std::min(span / 360.0, 1.0), tail_colour);
        gradient.setColorAt(1.0, tail_colour);

        p.setPen(QPen(QBrush(gradient), kPenWidth, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(ring, static_cast<int>(-start * 16.0), static_cast<int>(-span * 16.0));
    }

    void BusySpinner::showEvent(QShowEvent* event) {
        QWidget::showEvent(event);
        clock_.start();
        frame_timer_->start();
    }

    void BusySpinner::hideEvent(QHideEvent* event) {
        frame_timer_->stop();
        QWidget::hideEvent(event);
    }

} // namespace ui
