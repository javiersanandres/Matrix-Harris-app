#include "DiagramView.h"
#include "DiagramScene.h"

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
        emit zoomChanged(current_zoom_);
    }

    void DiagramView::fitWithMargin(const QRectF& scene_rect, double margin) {
        if (scene_rect.isEmpty()) return;
        // Expand the rect by margin pixels on each side before fitting.
        QRectF padded = scene_rect.adjusted(-margin, -margin, margin, margin);
        fitInView(padded, Qt::KeepAspectRatio);
        // Sync current_zoom_ from the actual transform.
        current_zoom_ = transform().m11();
        emit zoomChanged(current_zoom_);
    }

    void DiagramView::setThumbnailMode(bool on) {
        thumbnail_ = on;
        setInteractive(!on);
        viewport()->setCursor(on ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }

    void DiagramView::mouseDoubleClickEvent(QMouseEvent* event) {
        if (thumbnail_) { event->accept(); return; } // a thumbnail is only a picture
        QGraphicsView::mouseDoubleClickEvent(event);
    }

    void DiagramView::mousePressEvent(QMouseEvent* event) {
        // While a piece of the joint diagram is being moved, left and right
        // clicks belong to the scene (drop / put back), wherever they land.
        if (auto* ds = qobject_cast<DiagramScene*>(scene());
            ds && ds->isMovingPiece() && event->button() != Qt::MiddleButton) {
            QGraphicsView::mousePressEvent(event);
            return;
        }
        // While the scene waits for the second node of an operation, a click on
        // the background cancels it rather than panning.
        if (event->button() == Qt::LeftButton && !itemAt(event->pos())) {
            if (auto* ds = qobject_cast<DiagramScene*>(scene()); ds && ds->isInteractionPending()) {
                ds->cancelInteraction();
                event->accept();
                return;
            }
        }
        if (event->button() == Qt::MiddleButton ||
            (event->button() == Qt::LeftButton &&
                (thumbnail_ || !itemAt(event->pos())))) {
            // Start pan when middle-clicking or left-clicking the background
            // (anywhere at all on a thumbnail).
            panning_ = true;
            pan_start_ = event->position().toPoint();
            viewport()->setCursor(Qt::ClosedHandCursor);
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
            viewport()->setCursor(thumbnail_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
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
        // Over a thumbnail the wheel belongs to it, even with nothing to
        // scroll: it must not slide the whole tab strip instead.
        if (thumbnail_) event->accept();
    }

    void DiagramView::applyZoom(double factor) {
        double new_zoom = current_zoom_ * factor;
        if (new_zoom < ZOOM_MIN || new_zoom > ZOOM_MAX) return;
        scale(factor, factor);
        current_zoom_ = new_zoom;
        emit zoomChanged(current_zoom_);
    }

} // namespace ui