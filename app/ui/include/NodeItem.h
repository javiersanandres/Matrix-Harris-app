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
//   - Single click       → context menu with all node operations.
//   - Double click       → inline rename (QGraphicsTextItem becomes editable).
//   - Mouse press + drag → horizontal-only drag (grey out, snap on release
//                          calls relocateNodeInLayer via the scene).
//
// The item stores a raw Node* for identification. The pointer is valid for the
// lifetime of the scene because the scene is rebuilt from scratch after every
// editor mutation and the underlying graph owns the nodes.
// ============================================================================
class NodeItem : public QGraphicsRectItem {
public:
    explicit NodeItem(hypergraph_logic::Node* node,
                      const QRectF& rect,
                      QGraphicsItem* parent = nullptr);

    hypergraph_logic::Node* node() const { return node_; }

    // Highlight this item as a valid selection target during a pending
    // two-click operation (addConnection, fuseNodes, etc.).
    void setHighlighted(bool on);

    // Called by DiagramScene after a rename is committed to refresh the label.
    void updateLabel(const QString& text);

    // rect() is the node's bounding box; the node itself is drawn and hit-tested
    // with its own shape (rectangle, circle or rhombus) inscribed in that box.
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget = nullptr) override;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    hypergraph_logic::Node* node_;
    QGraphicsSimpleTextItem* label_;

    // Drag state
    bool    dragging_       = false;
    double  drag_start_x_   = 0.0;   // scene x at press
    double  drag_current_x_ = 0.0;   // current scene x during drag
    double drag_start_y_ = 0.0;
    double drag_current_y_ = 0.0;
    QPointF press_pos_;               // scene position at press

    static constexpr double DRAG_THRESHOLD = 5.0; // pixels before drag activates
};

} // namespace ui
