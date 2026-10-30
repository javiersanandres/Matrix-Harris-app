#pragma once

#include "Node.h"

#include <QGraphicsPathItem>

namespace ui {

// ============================================================================
// EquivalenceItem
//
// QGraphicsPathItem for the line between two neighbours of an equivalence
// chain (see HypergraphRenderer's "Equivalence lines"): continuous when they
// are certainly equivalent, dashed otherwise. Like a connection, it is drawn
// in the accent colour while hovered, so it reads as clickable, and its hit
// area is wider than the line itself.
//
// While a two-click operation is pending, it steps back (Dimmed) unless it
// touches the box the operation started from (see SelectionRole).
//
// The item stores raw Node* pointers to both neighbours, left first. They are
// valid for the lifetime of the scene.
// ============================================================================
class EquivalenceItem : public QGraphicsPathItem {
public:
    enum class SelectionRole { None, Dimmed };

    EquivalenceItem(hypergraph_logic::Node* left, hypergraph_logic::Node* right,
                    const QPainterPath& line, bool continuous, QGraphicsItem* parent = nullptr);

    hypergraph_logic::Node* left() const { return left_; }
    hypergraph_logic::Node* right() const { return right_; }
    bool isContinuous() const { return continuous_; }
    bool touches(const hypergraph_logic::Node* node) const { return node == left_ || node == right_; }

    // Widen the hit area so the thin line is easy to click.
    QPainterPath shape() const override;

    void setSelectionRole(SelectionRole role);

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    void refreshPen();

    hypergraph_logic::Node* left_;
    hypergraph_logic::Node* right_;
    bool continuous_;
    SelectionRole role_ = SelectionRole::None;
    bool hovered_ = false;

    static constexpr double HIT_WIDTH = 8.0; // pixels either side of the line
};

} // namespace ui
