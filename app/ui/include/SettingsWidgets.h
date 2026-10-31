#pragma once

#include "LayoutTypes.h"
#include "Node.h"

#include <QWidget>

#include <array>

class QVariantAnimation;

namespace ui {

    // ============================================================================
    // DistanceSlider
    //
    // A value picked by dragging a point along a track, as in GeoGebra:
    //
    //      ━━━━━━━━━━━━━━━━●──────────────────────
    //      (filled up to the point)
    //
    // Clicking the track brings the point there; the arrow keys move it by one,
    // Page Up / Page Down by ten. While it is dragged it reports every value
    // (moved); a value set from outside (setValue) glides the point there.
    // ============================================================================
    class DistanceSlider : public QWidget {
        Q_OBJECT
    public:
        explicit DistanceSlider(int min, int max, QWidget* parent = nullptr);

        int value() const { return value_; }
        void setValue(int value);
        bool isDragging() const { return dragging_; }

        QSize sizeHint() const override { return QSize(220, 28); }

    signals:
        void moved(int value);     // by the user
        void draggingChanged(bool dragging);

    protected:
        void paintEvent(QPaintEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;
        void enterEvent(QEnterEvent* event) override;
        void leaveEvent(QEvent* event) override;

    private:
        double trackLeft() const;
        double trackRight() const;
        double xOf(double value) const;
        int valueAt(double x) const;
        void pick(int value); // by the user: set and report

        int min_, max_, value_;
        double shown_;              // where the point is drawn (gliding towards value_)
        QVariantAnimation* glide_;
        bool dragging_ = false;
        bool hovered_ = false;
    };

    // ============================================================================
    // ShapeSizesPreview
    //
    // The default size of each shape, drawn side by side at the same scale so
    // they can be compared, centred on one line as boxes of a level are:
    //
    //      ┌──────────┐     ╭────╮       ◇
    //      │          │    │      │     ◇   ◇
    //      └──────────┘     ╰────╯       ◇
    //       Rectángulo      Círculo     Rombo
    //        100 × 50       80 × 80    90 × 90
    //
    // Each one is resized by dragging its edges, as a box in the box dialog's
    // preview (both sides move); the others keep their place meanwhile, and
    // the drawing makes room once it is let go. Sizes set from outside glide.
    // ============================================================================
    class ShapeSizesPreview : public QWidget {
        Q_OBJECT
    public:
        explicit ShapeSizesPreview(QWidget* parent = nullptr);

        void setSizes(const hypergraph_logic::BoxSizes& sizes);

        QSize sizeHint() const override { return QSize(460, 220); }

    signals:
        // The user dragged a shape to this size (rectangle stands for the rounded one too).
        void sizeEdited(hypergraph_logic::NodeShape shape, double width, double height);

    protected:
        void paintEvent(QPaintEvent* event) override;
        void resizeEvent(QResizeEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void leaveEvent(QEvent* event) override;

    private:
        static constexpr int COUNT = 3; // rectangle, circle, rhombus

        QRectF stage() const;
        double fitScale() const;
        std::array<QSizeF, COUNT> shownSizes() const;
        // Each shape's box, in widget coordinates.
        std::array<QRectF, COUNT> boxes() const;
        // Where the shapes' centres go (widget x) at this scale, side by side.
        std::array<double, COUNT> layOutCentres(double scale) const;
        void zoomToFit();
        void hoverAt(const QPointF& pos);

        std::array<QSizeF, COUNT> sizes_, from_;
        double progress_ = 1.0;
        QVariantAnimation* glide_;
        double scale_ = 0.0; // 0 until laid out
        // Gliding to the scale that fits (zoomToFit), from zoom_from_ to
        // zoom_to_; centres held during a drag glide back to their places too.
        QVariantAnimation* zoom_;
        double zoom_from_ = 0.0, zoom_to_ = 0.0, zoom_t_ = 1.0;
        bool releasing_ = false;

        int hover_shape_ = -1, hover_edges_ = 0;
        int drag_shape_ = -1, drag_edges_ = 0;
        QPointF press_pos_;
        QSizeF press_size_;
        std::array<double, COUNT> held_centres_{}; // while dragging
    };

} // namespace ui
