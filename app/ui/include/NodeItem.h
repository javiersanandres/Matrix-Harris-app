#pragma once

#include "Node.h"

#include <QGraphicsRectItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QString>

namespace ui {

// ============================================================================
// NodeItem
//
// QGraphicsRectItem representing a single real node in the editing scene. The
// rect is the node's bounding box; the item paints the node's own shape in it.
// Handles:
//   - Right click        → context menu with all node operations.
//   - Double click       → "Propiedades" dialog to edit every node attribute.
//   - Mouse wheel        → scrolls the label when it overflows the node's shape
//                          (its scroll indicator only shows while hovering).
//   - Mouse press + drag → the box lifts and follows the mouse (the scene shows
//                          where it would land); on release the scene relocates
//                          it and the layout glides to its new shape.
//
// The item stores a raw Node* for identification. The pointer is valid for the
// lifetime of the scene because the scene is rebuilt from scratch after every
// editor mutation and the underlying graph owns the nodes.
// ============================================================================
class NodeItem : public QGraphicsRectItem {
public:
    // Role of the node while a two-click operation waits for its second node.
    enum class SelectionMark {
        None,         // no operation pending
        Candidate,    // can be picked: pulsing indigo glow, pointing-hand cursor
        Unavailable,  // cannot be picked: faded out
        Origin,       // the node the operation started from: steady amber ring
    };

    explicit NodeItem(hypergraph_logic::Node* node,
                      const QRectF& rect,
                      QGraphicsItem* parent = nullptr);

    hypergraph_logic::Node* node() const { return node_; }

    // Marks this item's role in a pending two-click operation (addConnection,
    // fuseNodes, etc.). SelectionMark::None restores the normal look.
    void setSelectionMark(SelectionMark mark);
    SelectionMark selectionMark() const { return mark_; }

    // rect() is the node's bounding box; the node itself is drawn and hit-tested
    // with its own shape (rectangle, circle or rhombus) inscribed in that box.
    QPainterPath shape() const override;
    // rect() plus room for the selection glow around it.
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget = nullptr) override;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void wheelEvent(QGraphicsSceneWheelEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    hypergraph_logic::Node* node_;

    SelectionMark mark_  = SelectionMark::None;
    double label_scroll_ = 0.0;    // vertical scroll of an overflowing label
    bool   hovered_      = false;  // mouse over the node: show the label's scroll indicator

    // Drag state
    bool    dragging_       = false;
    double  drag_start_x_   = 0.0;   // scene x at press
    double  drag_current_x_ = 0.0;   // current scene x during drag
    double drag_start_y_ = 0.0;
    double drag_current_y_ = 0.0;
    QPointF press_pos_;               // scene position at press

    static constexpr double DRAG_THRESHOLD = 5.0; // pixels before drag activates
    static constexpr double GLOW_MARGIN = 16.0;   // room around rect() for the glow
};

} // namespace ui
