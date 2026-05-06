#include "DiagramView.h"

#include <QScrollBar>
#include <QMouseEvent>

namespace ui {

    DiagramView::DiagramView(QGraphicsScene* scene, QWidget* parent)
        : QGraphicsView(scene, parent)
    {
        setRenderHint(QPainter::Antialiasing);
        setDragMode(QGraphicsView::NoDrag);
        // AnchorUnderMouse makes zoom centre on the cursor position.
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorUnderMouse);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }

    void DiagramView::zoomIn() {
        applyZoom(ZOOM_STEP);
    }

    void DiagramView::zoomOut() {
        applyZoom(1.0 / ZOOM_STEP);
    }

    void DiagramView::resetZoom() {
        resetTransform();
        current_zoom_ = 1.0;
    }

    void DiagramView::fitWithMargin(const QRectF& scene_rect) {
        if (scene_rect.isEmpty()) return;
        // Expand the rect by MARGIN pixels on each side before fitting.
        QRectF padded = scene_rect.adjusted(-MARGIN, -MARGIN, MARGIN, MARGIN);
        fitInView(padded, Qt::KeepAspectRatio);
        // Sync current_zoom_ from the actual transform.
        current_zoom_ = transform().m11();
    }

    void DiagramView::mousePressEvent(QMouseEvent* event) {
        if (event->button() == Qt::MiddleButton ||
            (event->button() == Qt::LeftButton &&
                !itemAt(event->pos()))) {
            // Start pan when middle-clicking or left-clicking the background.
            panning_ = true;
            pan_start_ = event->position().toPoint();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QGraphicsView::mousePressEvent(event);
    }

    void DiagramView::mouseMoveEvent(QMouseEvent* event) {
        if (panning_) {
            QPoint delta = event->position().toPoint() - pan_start_;
            pan_start_ = event->position().toPoint();
            horizontalScrollBar()->setValue(
                horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(
                verticalScrollBar()->value() - delta.y());
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }

    void DiagramView::mouseReleaseEvent(QMouseEvent* event) {
        if (panning_ && (event->button() == Qt::MiddleButton ||
            event->button() == Qt::LeftButton)) {
            panning_ = false;
            setCursor(Qt::ArrowCursor);
            event->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }

    void DiagramView::wheelEvent(QWheelEvent* event) {
        if (event->modifiers() & Qt::ControlModifier) {
            double factor = (event->angleDelta().y() > 0) ? ZOOM_STEP : 1.0 / ZOOM_STEP;
            applyZoom(factor);
            event->accept();
        }
        else {
            QGraphicsView::wheelEvent(event);
        }
    }

    void DiagramView::applyZoom(double factor) {
        double new_zoom = current_zoom_ * factor;
        if (new_zoom < ZOOM_MIN || new_zoom > ZOOM_MAX) return;
        scale(factor, factor);
        current_zoom_ = new_zoom;
    }

} // namespace ui