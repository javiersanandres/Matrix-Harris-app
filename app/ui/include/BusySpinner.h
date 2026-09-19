#pragma once

#include <QPropertyAnimation>
#include <QWidget>

namespace ui {

    // ============================================================================
    // BusySpinner
    //
    // A small "busy" indicator: a ring of dots that rotates continuously, each
    // one fading out along the ring behind the leading dot -- the classic
    // iOS/macOS activity-indicator look. Pure QPainter drawing, no image assets,
    // so it's trivial to recolor or resize, and reusable anywhere the app needs
    // to show "working, no progress percentage available" rather than a
    // determinate progress bar.
    //
    // Animates only while actually visible: starts on showEvent(), stops on
    // hideEvent(), so a spinner sitting in a hidden/closed dialog never wastes
    // timer ticks in the background.
    // ============================================================================
    class BusySpinner : public QWidget {
        Q_OBJECT
            Q_PROPERTY(int angle READ angle WRITE setAngle)

    public:
        explicit BusySpinner(QWidget* parent = nullptr);

        int angle() const { return angle_; }
        void setAngle(int angle);

        QSize sizeHint() const override;

    protected:
        void paintEvent(QPaintEvent* event) override;
        void showEvent(QShowEvent* event) override;
        void hideEvent(QHideEvent* event) override;

    private:
        static constexpr int kDotCount = 12;
        static constexpr int kDiameter = 48;

        int angle_ = 0;
        QPropertyAnimation* animation_;
    };

} // namespace ui