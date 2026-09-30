#pragma once

#include <QElapsedTimer>
#include <QWidget>

class QTimer;

namespace ui {

    // ============================================================================
    // BusySpinner
    //
    // A "working, no percentage available" indicator in the application's
    // colours: a faint ring with an indigo-to-violet arc that keeps turning while
    // it stretches and shrinks, the way modern apps show activity. Pure QPainter
    // drawing, no image assets. Sized like the badges of StyledDialog (48 x 48),
    // so it can stand in for one (see StyledDialog::setEmblem).
    //
    // Animates only while actually visible: the frame timer starts on
    // showEvent() and stops on hideEvent(), so a spinner in a hidden or closed
    // dialog never wastes timer ticks in the background.
    // ============================================================================
    class BusySpinner : public QWidget {
        Q_OBJECT

    public:
        explicit BusySpinner(QWidget* parent = nullptr);

        QSize sizeHint() const override;

    protected:
        void paintEvent(QPaintEvent* event) override;
        void showEvent(QShowEvent* event) override;
        void hideEvent(QHideEvent* event) override;

    private:
        static constexpr int kDiameter = 48;

        QTimer* frame_timer_;
        QElapsedTimer clock_; // animation time since the spinner was shown
    };

} // namespace ui
