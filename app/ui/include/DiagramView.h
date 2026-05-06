#pragma once

#include <QGraphicsView>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPoint>

namespace ui {

    // ============================================================================
    // DiagramView
    //
    // QGraphicsView subclass used for both the central editing area and the
    // miniature tab thumbnails. Supports:
    //   - Zoom via Ctrl+scroll wheel, centred on the mouse cursor position.
    //   - Zoom via zoomIn() / zoomOut() from the Edit menu (centres on viewport).
    //   - Pan by pressing and dragging (left button on background or middle button).
    //   - fitWithMargin(): fits the scene with a fixed pixel margin on all sides,
    //     used when switching tabs to avoid filling the whole viewport with a
    //     single node.
    //   - setInteractive(false) makes it a read-only miniature view for the tab bar.
    //
    // Each DiagramView tracks its own zoom level so that switching tabs restores
    // the previous zoom for that diagram.
    // ============================================================================
    class DiagramView : public QGraphicsView {
        Q_OBJECT

    public:
        explicit DiagramView(QGraphicsScene* scene, QWidget* parent = nullptr);

        // Zoom in / out by a fixed factor, centred on the current cursor position.
        void zoomIn();
        void zoomOut();

        // Reset zoom to 100%.
        void resetZoom();

        // Fit the supplied scene rect into the view with a fixed pixel margin on
        // all sides. Use this instead of fitInView() to avoid the "single giant
        // node" effect when a diagram contains only a few items.
        void fitWithMargin(const QRectF& scene_rect);

        double currentZoom() const { return current_zoom_; }

        // Apply an absolute zoom factor (used when restoring saved zoom on tab switch).
        // Sets the view transform so that current_zoom_ == factor.
        void applyZoomFactor(double factor) {
            if (factor <= 0.0) return;
            resetTransform();
            current_zoom_ = 1.0;
            applyZoom(factor);
        }

    protected:
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void wheelEvent(QWheelEvent* event) override;

    private:
        void applyZoom(double factor);

        double  current_zoom_ = 1.0;
        bool    panning_ = false;
        QPoint  pan_start_;

        static constexpr double ZOOM_STEP = 1.15;
        static constexpr double ZOOM_MIN = 0.05;
        static constexpr double ZOOM_MAX = 10.0;
        static constexpr double MARGIN = 40.0; // pixels of padding around scene
    };

} // namespace ui
