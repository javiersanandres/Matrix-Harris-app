#include "HyperedgeItem.h"
#include "DiagramScene.h"
#include "UiStyle.h"
#include "HypergraphRenderer.h"

#include <QPainter>
#include <QPen>
#include <QGraphicsSceneContextMenuEvent>
#include <QPainterPath>
#include <QPainterPathStroker>

namespace ui {

namespace {
    QPainterPath combined(const QPainterPath& a, const QPainterPath& b) {
        QPainterPath both = a;
        both.addPath(b);
        return both;
    }
}

HyperedgeItem::HyperedgeItem(hypergraph_logic::Hyperedge* edge,
                             const QPainterPath& solid,
                             const QPainterPath& dashed,
                             QGraphicsItem* parent)
    : QGraphicsPathItem(combined(solid, dashed), parent)
    , edge_(edge)
    , solid_(solid)
    , dashed_(dashed)
{
    refreshPen();
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
    // Only colour and width depend on the state; paint() draws each part of
    // the connection with its own line style.
    if (role_ == SelectionRole::Focus)
        setPen(HypergraphRenderer::connectionPen(true, style::palette::accent, 3.0));
    else if (hovered_ && role_ == SelectionRole::None)
        setPen(HypergraphRenderer::connectionPen(true, style::palette::accent, 2.4));
    else
        setPen(HypergraphRenderer::connectionPen(true));
}

void HyperedgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);
    const QColor colour = pen().color();
    const qreal width = pen().widthF();
    painter->setPen(HypergraphRenderer::connectionPen(true, colour, width));
    painter->drawPath(solid_);
    painter->setPen(HypergraphRenderer::connectionPen(false, colour, width));
    painter->drawPath(dashed_);
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
