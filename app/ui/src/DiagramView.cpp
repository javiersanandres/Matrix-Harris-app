#include "DiagramView.h"
#include "DiagramScene.h"

#include <QScrollBar>
#include <QMouseEvent>

#include <QCoreApplication>
#include <QGraphicsSceneWheelEvent>

#include <cmath>

namespace ui {

    DiagramView::DiagramView(QGraphicsScene* scene, QWidget* parent)
        : QGraphicsView(scene, parent)
    {
        setRenderHint(QPainter::Antialiasing);
        setDragMode(QGraphicsView::NoDrag);
        // AnchorUnderMouse makes zoom centre on the cursor position.
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorUnderMouse);
        // Dragging pans; scrollbars would only clutter the view.
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }

    void DiagramView::zoomIn() {
        applyZoom(ZOOM_STEP);
    }

    void DiagramView::zoomOut() {
        applyZoom(1.0 / ZOOM_STEP);
    }

    void DiagramView::zoomAtCentre(bool in) {
        const ViewportAnchor anchor = transformationAnchor();
        setTransformationAnchor(QGraphicsView::AnchorViewCenter);
        applyZoom(in ? ZOOM_STEP : 1.0 / ZOOM_STEP);
        setTransformationAnchor(anchor);
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
        // The scene first: a box whose long name can still scroll takes the
        // plain wheel (thumbnails do not edit, so there it always zooms).
        // Sent by hand: QGraphicsView's own handler would scroll when the
        // scene leaves it, hidden scrollbars or not.
        if (scene() && !thumbnail_ && !(event->modifiers() & Qt::ControlModifier)) {
            QGraphicsSceneWheelEvent wheel(QEvent::GraphicsSceneWheel);
            wheel.setWidget(viewport());
            wheel.setScenePos(mapToScene(event->position().toPoint()));
            wheel.setScreenPos(event->globalPosition().toPoint());
            wheel.setButtons(event->buttons());
            wheel.setModifiers(event->modifiers());
            wheel.setDelta(event->angleDelta().y());
            wheel.setOrientation(Qt::Vertical);
            wheel.setPixelDelta(event->pixelDelta());
            wheel.setPhase(event->phase());
            wheel.setInverted(event->isInverted());
            wheel.setAccepted(false);
            QCoreApplication::sendEvent(scene(), &wheel);
            if (wheel.isAccepted()) {
                event->accept();
                return;
            }
        }
        // One step per notch; finer wheels and touchpads zoom in proportion.
        const int notches = event->angleDelta().y();
        if (notches != 0)
            applyZoom(std::pow(ZOOM_STEP, notches / 120.0));
        // Over a thumbnail the wheel belongs to it, even at the zoom's limit:
        // it must not slide the whole tab strip instead.
        event->accept();
    }

    void DiagramView::applyZoom(double factor) {
        double new_zoom = current_zoom_ * factor;
        if (new_zoom < ZOOM_MIN || new_zoom > ZOOM_MAX) return;
        scale(factor, factor);
        current_zoom_ = new_zoom;
        emit zoomChanged(current_zoom_);
    }

} // namespace ui