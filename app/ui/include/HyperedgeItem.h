#pragma once

#include "Hyperedge.h"

#include <QGraphicsPathItem>
#include <QGraphicsSceneMouseEvent>

namespace ui {

// ============================================================================
// HyperedgeItem
//
// QGraphicsPathItem representing the full polyline of one original hyperedge.
// Handles:
//   - Single click → context menu with all hyperedge operations.
//   - Hover        → drawn in the accent colour, so it reads as clickable.
//
// While a two-click operation is pending, every edge is dimmed except the one
// the operation works on, which is emphasised (see SelectionRole).
//
// The drawing comes in two parts: what is drawn with a continuous line and what
// is drawn discontinuous (the whole connection, or the branches that lead only
// to uncertain ports -- see HypergraphRenderer). Both take the same colour and
// width; path() holds them together for hit-testing.
//
// The item stores a raw Hyperedge* pointing to the original edge (never a
// segment). The pointer is valid for the lifetime of the scene.
// ============================================================================
class HyperedgeItem : public QGraphicsPathItem {
public:
    enum class SelectionRole { None, Dimmed, Focus };

    explicit HyperedgeItem(hypergraph_logic::Hyperedge* edge,
                           const QPainterPath& solid,
                           const QPainterPath& dashed,
                           QGraphicsItem* parent = nullptr);

    hypergraph_logic::Hyperedge* edge() const { return edge_; }

    // Widen the hit area so thin lines are easy to click.
    QPainterPath shape() const override;

    void setSelectionRole(SelectionRole role);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget = nullptr) override;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    void refreshPen();

    hypergraph_logic::Hyperedge* edge_;
    QPainterPath solid_;
    QPainterPath dashed_;
    SelectionRole role_ = SelectionRole::None;
    bool hovered_ = false;

    static constexpr double HIT_WIDTH = 8.0; // pixels either side of the path
};

} // namespace ui
