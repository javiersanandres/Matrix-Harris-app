#include "HyperedgeItem.h"
#include "DiagramScene.h"
#include "UiStyle.h"

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

void HyperedgeItem::setSelectionRole(SelectionRole role) {
    if (role_ == role) return;
    role_ = role;
    setOpacity(role == SelectionRole::Dimmed ? 0.25 : 1.0);
    refreshPen();
}

void HyperedgeItem::refreshPen() {
    if (role_ == SelectionRole::Focus)
        setPen(QPen(style::palette::accent, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    else if (hovered_ && role_ == SelectionRole::None)
        setPen(QPen(style::palette::accent, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    else
        setPen(QPen(Qt::black, 1.5));
}

void HyperedgeItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    hovered_ = true;
    refreshPen();
    QGraphicsPathItem::hoverEnterEvent(event);
}

void HyperedgeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    hovered_ = false;
    refreshPen();
    QGraphicsPathItem::hoverLeaveEvent(event);
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
