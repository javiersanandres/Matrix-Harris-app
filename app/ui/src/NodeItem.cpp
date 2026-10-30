#include "NodeItem.h"
#include "DiagramScene.h"
#include "NodeVisuals.h"
#include "LayoutTypes.h"
#include "UiStyle.h"

#include <QFontMetricsF>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneWheelEvent>

#include <QPainter>
#include <QCursor>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <algorithm>
#include <cmath>

using namespace hypergraph_logic;

namespace ui {

    NodeItem::NodeItem(Node* node, const QRectF& rect, QGraphicsItem* parent)
        : QGraphicsRectItem(rect, parent)
        , node_(node)
    {
        setPen(QPen(Qt::black, 1.5));
        setAcceptHoverEvents(true);
        setZValue(1.0); // nodes above edges
        setToolTip(QString::fromStdString(node->getName()));
    }

    QPainterPath NodeItem::shape() const {
        // The band along the edges lets a circle or a rhombus be grabbed by the
        // edges of its box too, where its handles are drawn.
        const QRectF r = rect();
        QPainterPath band;
        band.addRect(r.adjusted(-HANDLE_REACH, -HANDLE_REACH, HANDLE_REACH, HANDLE_REACH));
        QPainterPath inside;
        inside.addRect(r.adjusted(HANDLE_REACH, HANDLE_REACH, -HANDLE_REACH, -HANDLE_REACH));
        return node_visuals::shapePath(node_->getShape(), r).united(band.subtracted(inside));
    }

    QRectF NodeItem::boundingRect() const {
        const QRectF glow = rect().adjusted(-GLOW_MARGIN, -GLOW_MARGIN, GLOW_MARGIN, GLOW_MARGIN);
        return resizing_ ? glow.adjusted(-GLOW_MARGIN, -SIZE_CHIP_ROOM, GLOW_MARGIN, 0) : glow;
    }

    bool NodeItem::canResize() const {
        if (mark_ != SelectionMark::None || dragging_) return false;
        auto* ds = qobject_cast<DiagramScene*>(scene());
        return ds && ds->canResizeBoxes();
    }

    int NodeItem::edgesAt(const QPointF& pos) const {
        if (!canResize()) return 0;
        // The same reach on screen at any zoom, but never so much that the
        // middle of the box (which moves it) disappears.
        double zoom = 1.0;
        if (scene() && !scene()->views().isEmpty())
            zoom = std::max(0.05, std::abs(scene()->views().first()->transform().m11()));
        const double reach = std::min(HANDLE_REACH / zoom, std::min(rect().width(), rect().height()) / 4.0);
        return node_visuals::resizeEdgesAt(rect(), pos, std::max(HANDLE_REACH, reach));
    }

    void NodeItem::showResizeCursor(int edges) {
        if (mark_ != SelectionMark::None) return; // the mark decides the cursor
        if (edges) setCursor(node_visuals::resizeCursor(edges));
        else       unsetCursor();
    }

    void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
        Q_UNUSED(option);
        Q_UNUSED(widget);
        painter->setRenderHint(QPainter::Antialiasing);

        const bool candidate = mark_ == SelectionMark::Candidate;
        const bool origin = mark_ == SelectionMark::Origin;
        const QPainterPath outline = node_visuals::shapePath(node_->getShape(), rect());

        if (candidate || origin) {
            // Soft glow under the node: a few wide, translucent strokes of its own
            // outline. Candidates breathe with the scene's pulse and flare up
            // under the mouse; the origin glows steadily in amber.
            const QColor base = origin ? style::palette::amber : style::palette::accent;
            double pulse = 0.0;
            if (candidate)
                if (auto* ds = qobject_cast<DiagramScene*>(scene())) pulse = ds->selectionPulse();
            const double strength = origin ? 0.8 : (hovered_ ? 1.0 : 0.45 + 0.35 * pulse);
            const double reach = origin ? 7.0 : (hovered_ ? 11.0 : 6.0 + 4.0 * pulse);
            painter->setBrush(Qt::NoBrush);
            for (int ring = 3; ring >= 1; --ring) {
                QColor c = base;
                c.setAlphaF(static_cast<float>(strength * (0.10 + 0.08 * (3 - ring))));
                painter->setPen(QPen(c, reach * ring / 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter->drawPath(outline);
            }
        }

        node_visuals::PaintOptions opts;
        opts.outline = pen();
        opts.label_scroll = label_scroll_;
        opts.show_scroll_indicator = hovered_;
        node_visuals::paintNode(painter, node_->getAttributes(), rect(), opts);

        if (candidate || origin) {
            // Crisp ring on top, so the node's own outline reads as selected.
            const QColor ring = origin ? style::palette::amber : style::palette::accent;
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(ring, (hovered_ && candidate) ? 3.2 : 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->drawPath(outline);
        }

        if (resizing_ || (hovered_ && canResize())) {
            // Zoomed in, the handles keep their size on screen instead of growing
            // with the box; zoomed out, they shrink along with it, so they never
            // dwarf it. The size chip keeps its size on screen, to stay readable.
            const double screen_unit = 1.0 / std::max(0.05, std::abs(painter->worldTransform().m11()));
            node_visuals::paintResizeFrame(painter, rect(), resizing_ ? resizing_ : hover_edges_,
                std::min(screen_unit, 1.0));
            if (resizing_) node_visuals::paintSizeChip(painter, rect(), screen_unit);
        }
    }

    void NodeItem::setSelectionMark(SelectionMark mark) {
        if (mark_ == mark) return;
        mark_ = mark;
        setOpacity(mark == SelectionMark::Unavailable ? 0.28 : 1.0);
        switch (mark) {
        case SelectionMark::Candidate:   setCursor(Qt::PointingHandCursor); break;
        case SelectionMark::Unavailable: setCursor(Qt::ForbiddenCursor); break;
        default:                         unsetCursor(); break;
        }
        update();
    }

    void NodeItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
        hovered_ = true;
        update();
        QGraphicsRectItem::hoverEnterEvent(event);
    }

    void NodeItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event) {
        const int edges = edgesAt(event->pos());
        showResizeCursor(edges);
        if (edges != hover_edges_) {
            hover_edges_ = edges;
            update();
        }
        QGraphicsRectItem::hoverMoveEvent(event);
    }

    void NodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
        hovered_ = false;
        hover_edges_ = 0;
        if (mark_ == SelectionMark::None) unsetCursor();
        update();
        QGraphicsRectItem::hoverLeaveEvent(event);
    }

    void NodeItem::wheelEvent(QGraphicsSceneWheelEvent* event) {
        // Scroll the label only when it overflows its box and can still move in
        // the requested direction; otherwise let the view handle the wheel.
        const double max_scroll = node_visuals::maxLabelScroll(node_->getAttributes(), rect());
        if (max_scroll <= 0.0 || (event->modifiers() & Qt::ControlModifier)) {
            event->ignore();
            return;
        }
        const double step = QFontMetricsF(node_visuals::labelFont(node_->getAttributes())).lineSpacing();
        const double next = std::clamp(label_scroll_ - (event->delta() / 120.0) * step, 0.0, max_scroll);
        if (next == label_scroll_) {
            event->ignore();
            return;
        }
        label_scroll_ = next;
        update();
        event->accept();
    }

    void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
        if (event->button() == Qt::LeftButton) {
            press_pos_ = event->scenePos();
            drag_start_x_ = event->scenePos().x();
            drag_current_x_ = drag_start_x_;
            drag_start_y_ = event->scenePos().y();       
            drag_current_y_ = drag_start_y_;
            dragging_ = false;
            const int edges = edgesAt(event->pos());
            if (edges) {
                prepareGeometryChange(); // room for the size chip
                resizing_ = edges;
                press_rect_ = rect();
                update();
            }
        }
        // Do NOT call base — we handle selection ourselves.
    }

    void NodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
        if (!(event->buttons() & Qt::LeftButton)) return;

        if (resizing_) {
            // Both sides move, so the centre stays where the layout keeps it.
            const QSizeF size = node_visuals::resizedBox(press_rect_.size(), resizing_, event->scenePos() - press_pos_);
            const QPointF c = press_rect_.center();
            setRect(QRectF(c - QPointF(size.width(), size.height()) / 2.0, size));
            return;
        }

        double dx = std::abs(event->scenePos().x() - press_pos_.x());
        double dy = std::abs(event->scenePos().y() - press_pos_.y());

        DiagramScene* ds = qobject_cast<DiagramScene*>(scene());
        if (!dragging_ && (dx > DRAG_THRESHOLD || dy > DRAG_THRESHOLD)) {
            // Any movement past the threshold, in either direction, starts a
            // drag: the scene lifts the box (shadow, slight zoom).
            dragging_ = true;
            if (ds) ds->beginBoxDrag(this);
        }

        if (dragging_) {
            // Follow the cursor freely in 2D, and let the scene show where the
            // box would land.
            drag_current_x_ = event->scenePos().x();
            drag_current_y_ = event->scenePos().y();
            setPos(event->scenePos() - press_pos_);
            if (ds) ds->updateBoxDrag(this);
        }
    }

    void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
        if (event->button() == Qt::LeftButton && resizing_) {
            Qt::Orientations sides;
            if (resizing_ & (node_visuals::ResizeLeft | node_visuals::ResizeRight)) sides |= Qt::Horizontal;
            if (resizing_ & (node_visuals::ResizeTop | node_visuals::ResizeBottom)) sides |= Qt::Vertical;
            const QSizeF size = rect().size();
            prepareGeometryChange();
            resizing_ = 0;
            update();
            // Queued: the scene rebuilds every item, this one included.
            if (DiagramScene* ds = qobject_cast<DiagramScene*>(scene()))
                QMetaObject::invokeMethod(ds, [ds, node = node_, size, sides]() {
                    ds->resizeNode(node, size, sides);
                    }, Qt::QueuedConnection);
            return;
        }

        if (event->button() == Qt::LeftButton && dragging_) {
            dragging_ = false;

            // Notify the scene — it will call relocateNode.
            DiagramScene* ds = qobject_cast<DiagramScene*>(scene());
            if (ds) {
                ds->endBoxDrag();
                // Pass the scene-space centre of where the box was dropped.
                const QPointF dropped = mapToScene(rect().center());
                double new_x = dropped.x();
                double new_y = dropped.y();
                // The scene will handle calling the editor and then rebuild().
                QMetaObject::invokeMethod(ds, [ds, node = node_, new_x, new_y]() {
                    // Find the NodeItem again after potential rebuild — but since
                    // we have the raw Node* we emit a signal instead.
                    // DiagramScene listens via a connection set up in its ctor.
                    emit ds->nodeRelocated(node, new_x, new_y);
                    }, Qt::QueuedConnection);
            }
            return;
        }

        if (event->button() == Qt::LeftButton && !dragging_) {
            // Single click — handled by DiagramScene::mousePressEvent which
            // intercepts item clicks before forwarding here. Nothing to do.
        }
    }

    void NodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
        Q_UNUSED(event);
        DiagramScene* ds = qobject_cast<DiagramScene*>(scene());
        if (ds) ds->showNodeProperties(this);
    }

    void NodeItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
        DiagramScene* ds = qobject_cast<DiagramScene*>(scene());
        if (ds) ds->showNodeContextMenu(this, event->scenePos());
    }

} // namespace ui