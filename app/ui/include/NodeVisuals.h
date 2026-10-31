#pragma once

#include "Node.h"

#include <QColor>
#include <QFont>
#include <QPainterPath>
#include <QPen>
#include <QRectF>

#include <optional>

class QPainter;

namespace ui::node_visuals {

    // ============================================================================
    // NodeVisuals
    //
    // Single source of truth for how a real node looks: its outline, its fill
    // (including the fire-with-ashes split), and its label. Used by the editing
    // scene (NodeItem), the static renderer, and every dialog preview, so what
    // the user sees in a dialog is exactly what lands on the canvas.
    //
    // All geometry is expressed relative to the node's bounding box `box`
    // (getWidth() x getHeight(), centred on the node).
    // ============================================================================

    // Share of the box height painted white on top of a fire with ashes; the
    // remaining bottom part (1/4) is painted black.
    inline constexpr double ASHES_WHITE_FRACTION = 3.0 / 4.0;

    // ── Colour conversion ────────────────────────────────────────────────────────
    QColor toQColor(const hypergraph_logic::Color& c);
    hypergraph_logic::Color fromQColor(const QColor& c);

    // Black or white, whichever reads better on top of `background`.
    QColor contrastingTextColour(const QColor& background);

    // ── Geometry ─────────────────────────────────────────────────────────────────

    // Size of the box a node with these attributes occupies on the canvas
    // (Node::getWidth() x Node::getHeight()), with `sizes` as its shapes'
    // default sizes (the built-in ones without them).
    QSizeF boxSize(const hypergraph_logic::NodeAttributes& attributes,
        const hypergraph_logic::BoxSizes* sizes = nullptr);

    // Outline of the shape inscribed in box.
    QPainterPath shapePath(hypergraph_logic::NodeShape shape, const QRectF& box);

    // Area where the label may be drawn: the largest comfortable rectangle
    // inside the shape, restricted to the white top part for a fire with ashes.
    QRectF labelRect(const hypergraph_logic::NodeAttributes& attributes, const QRectF& box);

    // Font used for the label (application font at the node's font size).
    QFont labelFont(const hypergraph_logic::NodeAttributes& attributes);

    // How far the label can be scrolled when its wrapped text is taller than
    // labelRect (0 when everything fits).
    double maxLabelScroll(const hypergraph_logic::NodeAttributes& attributes, const QRectF& box);

    // ── Painting ─────────────────────────────────────────────────────────────────

    struct PaintOptions {
        // Replaces the node's own fill (used for highlight / drag feedback). The
        // label then uses whichever of black/white contrasts with it.
        std::optional<QColor> fill_override;
        QPen outline = QPen(Qt::black, 1.5);
        // Vertical scroll of the label, in the box's units (0 = top).
        double label_scroll = 0.0;
        // Draw the slim scroll indicator when the label overflows (the canvas
        // shows it only while the mouse is over the node).
        bool show_scroll_indicator = false;
    };

    // Paints the whole node (fill, outline and clipped label) inside box.
    void paintNode(QPainter* painter,
        const hypergraph_logic::NodeAttributes& attributes,
        const QRectF& box,
        const PaintOptions& options = {});

    // ── Resizing ─────────────────────────────────────────────────────────────────
    //
    // A box is resized by dragging the edges (or corners) of its bounding box,
    // on the canvas and in the dialogs' preview alike. Both sides move, so the
    // box keeps its centre, which is where the layout keeps it.

    // Edges of a box, combined as flags.
    enum ResizeEdge { ResizeLeft = 1, ResizeRight = 2, ResizeTop = 4, ResizeBottom = 8 };

    // The edges of box within `reach` of pos (0 if none).
    int resizeEdgesAt(const QRectF& box, const QPointF& pos, double reach);

    // The cursor that drags those edges (Qt::ArrowCursor for none).
    Qt::CursorShape resizeCursor(int edges);

    // The size of a box that measured `start` once its `edges` are dragged by
    // `delta` (box units), within the limits of LayoutTypes.h, in whole pixels.
    // A side changes only its own dimension; a corner keeps the box's
    // proportions (as far as those limits allow).
    QSizeF resizedBox(const QSizeF& start, int edges, const QPointF& delta);

    // The box's frame with a handle on each corner and edge; the handle of
    // `active` (the edges under the mouse or being dragged) stands out. `unit`
    // is the length of a screen pixel in the painter's units, so the handles
    // keep their size whatever the zoom.
    void paintResizeFrame(QPainter* painter, const QRectF& box, int active, double unit = 1.0);

    // The box's size ("120 × 50") in a chip just above it.
    void paintSizeChip(QPainter* painter, const QRectF& box, double unit = 1.0);

} // namespace ui::node_visuals
