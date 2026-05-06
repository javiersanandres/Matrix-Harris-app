#include "NodeItem.h"
#include "DiagramScene.h"
#include "LayoutTypes.h"

#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsScene>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <cmath>

using namespace hypergraph_logic;

namespace ui {

    NodeItem::NodeItem(Node* node, const QRectF& rect, QGraphicsItem* parent)
        : QGraphicsRectItem(rect, parent)
        , node_(node)
    {
        setPen(QPen(Qt::black, 1.5));
        setBrush(QBrush(QColor(255, 255, 200)));  // light yellow
        setAcceptHoverEvents(true);
        setZValue(1.0); // nodes above edges

        label_ = new QGraphicsSimpleTextItem(
            QString::fromStdString(node->getName()), this);
        QRectF lb = label_->boundingRect();
        label_->setPos(
            rect.left() + (rect.width() - lb.width()) / 2.0,
            rect.top() + (rect.height() - lb.height()) / 2.0);
    }

    void NodeItem::setHighlighted(bool on) {
        setBrush(on ? QBrush(QColor(200, 230, 255)) : QBrush(QColor(255, 255, 200)));
    }

    void NodeItem::updateLabel(const QString& text) {
        label_->setText(text);
        QRectF r = rect();
        QRectF lb = label_->boundingRect();
        label_->setPos(
            r.left() + (r.width() - lb.width()) / 2.0,
            r.top() + (r.height() - lb.height()) / 2.0);
    }

    void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
        if (event->button() == Qt::LeftButton) {
            press_pos_ = event->scenePos();
            drag_start_x_ = event->scenePos().x();
            drag_current_x_ = drag_start_x_;
            dragging_ = false;
        }
        // Do NOT call base — we handle selection ourselves.
    }

    void NodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
        if (!(event->buttons() & Qt::LeftButton)) return;

        double dx = std::abs(event->scenePos().x() - press_pos_.x());
        double dy = std::abs(event->scenePos().y() - press_pos_.y());

        if (!dragging_ && (dx > DRAG_THRESHOLD || dy > DRAG_THRESHOLD)) {
            // Only start a drag if horizontal movement dominates.
            if (dx >= dy) {
                dragging_ = true;
                setBrush(QBrush(QColor(180, 180, 180, 160))); // grey out
            }
        }

        if (dragging_) {
            // Constrain to horizontal: only update x, keep y fixed.
            drag_current_x_ = event->scenePos().x();
            double delta_x = drag_current_x_ - drag_start_x_;
            setRect(rect().translated(delta_x, 0.0));
            // Move the label with the rect so the text follows the box visually.
            label_->setPos(label_->pos() + QPointF(delta_x, 0.0));
            drag_start_x_ = drag_current_x_;
        }
    }

    void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
        if (event->button() == Qt::LeftButton && dragging_) {
            dragging_ = false;
            setBrush(QBrush(QColor(255, 255, 200)));  // light yellow

            // Notify the scene — it will call relocateNodeInLayer.
            DiagramScene* ds = qobject_cast<DiagramScene*>(scene());
            if (ds) {
                // Pass the scene-space centre x of the item's current rect.
                double new_x = rect().center().x();
                // The scene will handle calling the editor and then rebuild().
                QMetaObject::invokeMethod(ds, [ds, node = node_, new_x]() {
                    // Find the NodeItem again after potential rebuild — but since
                    // we have the raw Node* we emit a signal instead.
                    // DiagramScene listens via a connection set up in its ctor.
                    emit ds->nodeRelocated(node, new_x);
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
        if (ds) ds->startInlineRename(this);
    }

    void NodeItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
        DiagramScene* ds = qobject_cast<DiagramScene*>(scene());
        if (ds) ds->showNodeContextMenu(this, event->scenePos());
    }

} // namespace ui