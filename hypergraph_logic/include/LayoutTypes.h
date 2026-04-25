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
    inline constexpr double MIN_BLOCK_SEP = 25.0;

    // Default width assigned to a real node box.
    inline constexpr double NODE_WIDTH = 80.0;

    // Default height assigned to a real node box.
    inline constexpr double NODE_HEIGHT = 40.0;

    // Dummy nodes are invisible bend-points on edges. Their layout width is
    // zero so they never contribute to block width or separation.
    inline constexpr double DUMMY_NODE_WIDTH = 0.0;
} // namespace hypergraph_logic
