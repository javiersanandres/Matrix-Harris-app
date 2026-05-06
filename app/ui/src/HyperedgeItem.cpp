#include "HyperedgeItem.h"
#include "DiagramScene.h"

#include <QPen>
#include <QGraphicsSceneContextMenuEvent>
#include <QPainterPath>
#include <QPainterPathStroker>

namespace ui {

HyperedgeItem::HyperedgeItem(hypergraph_logic::Hyperedge* edge,
                             const QPainterPath& path,
                             QGraphicsItem* parent)
    : QGraphicsPathItem(path, parent)
    , edge_(edge)
{
    setPen(QPen(Qt::black, 1.5));
    setZValue(0.0); // edges behind nodes
    setAcceptHoverEvents(true);
}

QPainterPath HyperedgeItem::shape() const {
    // Widen the clickable area around the path so thin lines are easy to hit.
    QPainterPathStroker stroker;
    stroker.setWidth(HIT_WIDTH * 2.0);
    return stroker.createStroke(path());
}

void HyperedgeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        DiagramScene* ds = qobject_cast<DiagramScene*>(scene());
        if (ds) ds->showEdgeContextMenu(this, event->scenePos());
    }
}

void HyperedgeItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    DiagramScene* ds = qobject_cast<DiagramScene*>(scene());
    if (ds) ds->showEdgeContextMenu(this, event->scenePos());
}

} // namespace ui
