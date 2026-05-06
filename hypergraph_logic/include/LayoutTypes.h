#pragma once

// ============================================================================
// LayoutTypes.h
//
// Shared constants and types for the graphical layout pipeline.
//
// All coordinates are in logical pixels (1 unit = 1 Qt pixel at 96 dpi).
// Node boxes are drawn as rectangles; dummy nodes have zero visual width and
// are rendered only as a pass-through bend point on an edge.
// ============================================================================

namespace hypergraph_logic {

    // -------------------------------------------------------------------------
    // Layout constants
    // -------------------------------------------------------------------------

    // Minimum horizontal gap between the bounding boxes of two adjacent nodes
    // in the same layer, after block-width has been accounted for.
    // Formula used in BK compaction:
    //   sep(a, b) = (blockWidth(a) + blockWidth(b)) / 2 + MIN_BLOCK_SEP
    inline constexpr double MIN_BLOCK_SEP = 60.0;

    // Default width assigned to a real node box.
    inline constexpr double NODE_WIDTH = 100.0;

    // Default height assigned to a real node box.
    inline constexpr double NODE_HEIGHT = 50.0;

    // Dummy nodes are invisible bend-points on edges. Due to vertical overlap
    // issues, we need to assing them a width for the port assignment step.
    inline constexpr double DUMMY_NODE_WIDTH = 10.0;

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
} // namespace hypergraph_logic
