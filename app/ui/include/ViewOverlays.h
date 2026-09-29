#pragma once

#include <QFrame>
#include <QPoint>

class QHBoxLayout;
class QLabel;
class QPropertyAnimation;
class QToolButton;

namespace ui {

    // ============================================================================
    // FloatingPanel
    //
    // A small card that floats over its parent widget (the diagram view's
    // container) instead of taking room from it. Its size always matches its
    // contents. The user moves it by dragging its grip (the dotted strip on the
    // left) or any bare part of the card; it never leaves the parent's area.
    //
    // It remembers which corner it is closest to, so when the window is resized
    // it keeps its distance to that corner (a panel parked top-right stays
    // top-right).
    // ============================================================================
    class FloatingPanel : public QFrame {
        Q_OBJECT
    public:
        explicit FloatingPanel(QWidget* parent);

        // Where the panel's widgets go, left to right after the grip.
        QHBoxLayout* contentLayout() const { return content_; }

        // Parks the panel in the given corner of the parent, `margin` px inside it.
        void placeInCorner(Qt::Corner corner, int margin = 14);

    protected:
        void paintEvent(QPaintEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        bool eventFilter(QObject* watched, QEvent* event) override;
        void resizeEvent(QResizeEvent* event) override;

    private:
        // Moves to pos, clamped inside the parent, and records the nearest corner.
        void moveClamped(const QPoint& pos);
        // Re-applies the recorded corner distances (after the parent resizes).
        void reanchor();

        QHBoxLayout* content_;
        bool dragging_ = false;
        QPoint drag_offset_;

        bool anchor_right_ = true;
        bool anchor_bottom_ = false;
        QPoint corner_distance_{ 14, 14 };

        static constexpr int GRIP_WIDTH = 16;
        static constexpr int EDGE_GAP = 6; // closest the panel gets to the parent's border
    };

    // ============================================================================
    // HintBanner
    //
    // Dark pill centred at the top of its parent that tells the user what a
    // pending operation expects ("Elige la caja…"), with a "Cancelar" button and
    // an Esc reminder. It fades in and out.
    // ============================================================================
    class HintBanner : public QFrame {
        Q_OBJECT
    public:
        explicit HintBanner(QWidget* parent);

        // Shows the text, or hides the banner when it is empty.
        void setHint(const QString& text);

    signals:
        void cancelRequested();

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override;

    private:
        void recentre();

        QLabel* text_;
        QToolButton* cancel_;
        QPropertyAnimation* fade_;
    };

} // namespace ui
