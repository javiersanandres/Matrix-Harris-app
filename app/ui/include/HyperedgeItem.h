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
//
// The item stores a raw Hyperedge* pointing to the original edge (never a
// segment). The pointer is valid for the lifetime of the scene.
// ============================================================================
class HyperedgeItem : public QGraphicsPathItem {
public:
    explicit HyperedgeItem(hypergraph_logic::Hyperedge* edge,
                           const QPainterPath& path,
                           QGraphicsItem* parent = nullptr);

    hypergraph_logic::Hyperedge* edge() const { return edge_; }

    // Widen the hit area so thin lines are easy to click.
    QPainterPath shape() const override;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    hypergraph_logic::Hyperedge* edge_;

    static constexpr double HIT_WIDTH = 8.0; // pixels either side of the path
};

} // namespace ui
