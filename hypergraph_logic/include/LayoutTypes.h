#pragma once

// ============================================================================
// LayoutTypes.h
//
// Shared constants and types for the graphical layout pipeline.
//
// All coordinates are in logical pixels (1 unit = 1 Qt pixel at 96 dpi).
// Real nodes are drawn with their own shape (rectangle, circle or rhombus),
// each with its own width and height (see Node::getWidth / Node::getHeight);
// dummy nodes are rendered only as a pass-through bend point on an edge.
// ============================================================================

namespace hypergraph_logic {

    // -------------------------------------------------------------------------
    // Layout constants
    // -------------------------------------------------------------------------

    // Minimum horizontal gap between the bounding boxes of two adjacent nodes
    // in the same layer, after block-width has been accounted for.
    // Formula used in BK compaction:
    //   sep(a, b) = (blockWidth(a) + blockWidth(b)) / 2 + MIN_BLOCK_SEP
    // Default of LayoutSettings::block_sep (as LAYER_GAP and HORIZONTAL_SEP
    // below are of theirs), which the layout actually uses.
    inline constexpr double MIN_BLOCK_SEP = 60.0;

    // Width assigned to a rectangular real node box.
    inline constexpr double NODE_WIDTH = 100.0;

    // Height assigned to a rectangular real node box.
    inline constexpr double NODE_HEIGHT = 50.0;

    // Corner radius of a rounded rectangular box, which has the rectangle's size.
    inline constexpr double ROUNDED_NODE_RADIUS = 12.0;

    // Circles and rhombi (a square rotated 45 degrees) are regular shapes, so
    // both are inscribed in the same square bounding box. Its side is chosen so
    // both cover the same area as a rectangular box ((50 * sqrt(2))^2 ~ 100 * 50)
    // and (pi * 40^2 ~ 100 * 50), which makes both shapes look balanced next to
    // rectangles while still leaving room for a short label in the middle.
    inline constexpr double CIRCLE_NODE_WIDTH = 80.0;
    inline constexpr double CIRCLE_NODE_HEIGHT = 80.0;

    inline constexpr double RHOMBUS_NODE_WIDTH = 90.0;
    inline constexpr double RHOMBUS_NODE_HEIGHT = 90.0;

    // Range a real box may be resized within (any shape). Narrower boxes leave
    // no room for their ports and labels; larger ones dwarf the rest of the diagram.
    inline constexpr double MIN_NODE_WIDTH = 40.0;
    inline constexpr double MAX_NODE_WIDTH = 400.0;
    inline constexpr double MIN_NODE_HEIGHT = 30.0;
    inline constexpr double MAX_NODE_HEIGHT = 300.0;

    // Dummy nodes are invisible bend-points on edges. Due to vertical overlap
    // issues, we need to assing them a width for the port assignment step.
    inline constexpr double DUMMY_NODE_WIDTH = 10.0;

    // Dummy nodes have no vertical extent: edges pass straight through them.
    inline constexpr double DUMMY_NODE_HEIGHT = 0.0;

    // Minimum vertical gap between the vertical segments of two hyperedges
    // in the same layer. This is used just as a reference. There might be 
    // cases in which this distance is not respected, but it is a reference
    // to decide when there are overlapping conflicts after port assignemnt.
	inline constexpr double MIN_VERTICAL_SEP = 10.0;

    // Vertical gap between the nodes bottom boxes and the first horizontal
	// segment of the hyperedges. Similarly, the gap between the top boxes and 
    // the last horizontal segment.
	inline constexpr double LAYER_GAP = 60.0;

    // Minimum vertical gap between two horizontal segments from different
    // hyperedges in the same layer.
	inline constexpr double HORIZONTAL_SEP = 30.0;

    // Radius of the semicircular hop arc, in Qt logical pixels.
    // an arc is created when the horizontal span of hyperedge overlaps with a
	// vertical segment of another hyperedge. The arc is drawn as a semicircle.
    static constexpr double HOP_RADIUS = 5.0;

    // On the same topic, when consecutive hops are necessary but there is not
	// enough horizontal space to draw them separately, they are merged into a
	// single smooth Bézier arch. This constant controls the height of such arches.
    static constexpr double ARCH_HEIGHT = 10.0; // Height of merged-hop Bézier arches.

    // -------------------------------------------------------------------------
    // LayoutSettings
    //
    // The distances of the drawing the user may change. Each graph has its own
    // (GraphicalHypergraph::getLayoutSettings); a default-constructed one holds
    // MIN_BLOCK_SEP, LAYER_GAP and HORIZONTAL_SEP above, which is what every
    // layout used before they could be changed.
    //
    // MIN_VERTICAL_SEP, HOP_RADIUS, ARCH_HEIGHT and the dummy sizes are not
    // settings: the layout's guarantees rest on them.
    // -------------------------------------------------------------------------
    // -------------------------------------------------------------------------
    // BoxSizes
    //
    // The default size of a box of each shape: a box without a size of its own
    // (NodeAttributes::width / height) follows the one of its shape. Rounded
    // rectangles use the rectangle's. Each graph hands its own to its boxes
    // (Node::setBoxSizes), as part of its LayoutSettings.
    // -------------------------------------------------------------------------
    struct BoxSize {
        double width = 0.0;
        double height = 0.0;
        bool operator==(const BoxSize& other) const noexcept { return width == other.width && height == other.height; }
        bool operator!=(const BoxSize& other) const noexcept { return !(*this == other); }
    };

    struct BoxSizes {
        BoxSize rectangle{ NODE_WIDTH, NODE_HEIGHT };
        BoxSize circle{ CIRCLE_NODE_WIDTH, CIRCLE_NODE_HEIGHT };
        BoxSize rhombus{ RHOMBUS_NODE_WIDTH, RHOMBUS_NODE_HEIGHT };

        bool operator==(const BoxSizes& other) const noexcept {
            return rectangle == other.rectangle && circle == other.circle && rhombus == other.rhombus;
        }
        bool operator!=(const BoxSizes& other) const noexcept { return !(*this == other); }
    };

    struct LayoutSettings {
        // Gap between the boxes of two neighbours in a layer (see MIN_BLOCK_SEP).
        double block_sep = MIN_BLOCK_SEP;
        // Gap between a layer's boxes and the nearest horizontal segment of the
        // connections above or below them (see LAYER_GAP).
        double layer_gap = LAYER_GAP;
        // Gap between two horizontal segments stacked in the same layer gap
        // (see HORIZONTAL_SEP).
        double horizontal_sep = HORIZONTAL_SEP;
        // Default size of each shape's boxes, within [MIN_NODE_WIDTH, MAX_NODE_WIDTH]
        // x [MIN_NODE_HEIGHT, MAX_NODE_HEIGHT], like a box's own size.
        BoxSizes box_sizes;

        // ── Ranges ───────────────────────────────────────────────────────────
        // The gap between boxes must leave room for a vertical segment's
        // MIN_VERTICAL_SEP on both sides of each box: the vertical overlap
        // detection relies on the port spans of a layer staying that far apart.
        static constexpr double MIN_BLOCK_SEP_SETTING = 2.0 * MIN_VERTICAL_SEP;
        static constexpr double MAX_BLOCK_SEP_SETTING = 300.0;
        // Below this, the segments would touch the boxes' outlines.
        static constexpr double MIN_LAYER_GAP_SETTING = 20.0;
        static constexpr double MAX_LAYER_GAP_SETTING = 300.0;
        // A segment's merged hops rise ARCH_HEIGHT over it: they must not reach
        // the segment stacked above.
        static constexpr double MIN_HORIZONTAL_SEP_SETTING = ARCH_HEIGHT + 5.0;
        static constexpr double MAX_HORIZONTAL_SEP_SETTING = 120.0;

        // The same settings with every value brought inside its range.
        LayoutSettings clamped() const {
            auto clamp = [](double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); };
            LayoutSettings s;
            s.block_sep = clamp(block_sep, MIN_BLOCK_SEP_SETTING, MAX_BLOCK_SEP_SETTING);
            s.layer_gap = clamp(layer_gap, MIN_LAYER_GAP_SETTING, MAX_LAYER_GAP_SETTING);
            s.horizontal_sep = clamp(horizontal_sep, MIN_HORIZONTAL_SEP_SETTING, MAX_HORIZONTAL_SEP_SETTING);
            auto clampBox = [&](const BoxSize& b) {
                return BoxSize{ clamp(b.width, MIN_NODE_WIDTH, MAX_NODE_WIDTH), clamp(b.height, MIN_NODE_HEIGHT, MAX_NODE_HEIGHT) };
            };
            s.box_sizes.rectangle = clampBox(box_sizes.rectangle);
            s.box_sizes.circle = clampBox(box_sizes.circle);
            s.box_sizes.rhombus = clampBox(box_sizes.rhombus);
            return s;
        }

        bool operator==(const LayoutSettings& other) const noexcept {
            return block_sep == other.block_sep && layer_gap == other.layer_gap
                && horizontal_sep == other.horizontal_sep && box_sizes == other.box_sizes;
        }
        bool operator!=(const LayoutSettings& other) const noexcept { return !(*this == other); }
    };
} // namespace hypergraph_logic
