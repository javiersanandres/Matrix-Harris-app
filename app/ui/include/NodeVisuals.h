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
    // remaining bottom part (2/5) is painted black.
    inline constexpr double ASHES_WHITE_FRACTION = 3.0 / 5.0;

    // ── Colour conversion ────────────────────────────────────────────────────────
    QColor toQColor(const hypergraph_logic::Color& c);
    hypergraph_logic::Color fromQColor(const QColor& c);

    // Black or white, whichever reads better on top of `background`.
    QColor contrastingTextColour(const QColor& background);

    // ── Geometry ─────────────────────────────────────────────────────────────────

    // Size of the box a node with these attributes occupies on the canvas
    // (Node::getWidth() x Node::getHeight() for its shape).
    QSizeF boxSize(const hypergraph_logic::NodeAttributes& attributes);

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

} // namespace ui::node_visuals
