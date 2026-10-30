#include "EquivalenceItem.h"
#include "DiagramScene.h"
#include "HypergraphRenderer.h"
#include "UiStyle.h"

#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPathStroker>

namespace ui {

EquivalenceItem::EquivalenceItem(hypergraph_logic::Node* left, hypergraph_logic::Node* right,
                                 const QPainterPath& line, bool continuous, QGraphicsItem* parent)
    : QGraphicsPathItem(line, parent)
    , left_(left)
    , right_(right)
    , continuous_(continuous)
{
    refreshPen();
    setZValue(0.0); // behind the boxes, like connections
    setAcceptHoverEvents(true);
}

QPainterPath EquivalenceItem::shape() const {
    QPainterPathStroker stroker;
    stroker.setWidth(HIT_WIDTH * 2.0);
    return stroker.createStroke(path());
}

void EquivalenceItem::setSelectionRole(SelectionRole role) {
    if (role_ == role) return;
    role_ = role;
    setOpacity(role == SelectionRole::Dimmed ? 0.25 : 1.0);
    refreshPen();
}

void EquivalenceItem::refreshPen() {
    if (hovered_ && role_ == SelectionRole::None)
        setPen(HypergraphRenderer::connectionPen(continuous_, style::palette::accent, 2.4));
    else
        setPen(HypergraphRenderer::connectionPen(continuous_));
}

void EquivalenceItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    hovered_ = true;
    refreshPen();
    QGraphicsPathItem::hoverEnterEvent(event);
}

void EquivalenceItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    hovered_ = false;
    refreshPen();
    QGraphicsPathItem::hoverLeaveEvent(event);
}

void EquivalenceItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        DiagramScene* ds = qobject_cast<DiagramScene*>(scene());
        if (ds) ds->showEquivalenceContextMenu(this, event->scenePos());
    }
}

void EquivalenceItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    DiagramScene* ds = qobject_cast<DiagramScene*>(scene());
    if (ds) ds->showEquivalenceContextMenu(this, event->scenePos());
}

} // namespace ui
