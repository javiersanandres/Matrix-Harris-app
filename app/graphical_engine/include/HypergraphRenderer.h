#pragma once

#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"

#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsPathItem>
#include <QPainterPath>

#include <map>
#include <vector>
#include <unordered_map>

namespace graphical_engine {

	using namespace hypergraph_logic;

	// ============================================================================
	// VerticalRange
	//
	// Represents the y interval [y_min, y_max] occupied by a vertical segment
	// at a specific x coordinate. y_min is the more negative (deeper) value and
	// y_max is the less negative (shallower) value, following the layout
	// convention where layer 0 is at y=0 and deeper layers have negative y.
	//
	// Used to detect conflicts when drawing horizontal bars so that semicircular
	// hops can be inserted wherever a bar crosses an already-drawn vertical
	// segment.
	// ============================================================================
	struct VerticalRange {
		double y_min; // more negative end (deeper in the diagram)
		double y_max; // less negative end (shallower in the diagram)
	};

	// ============================================================================
	// PortInfo
	//
	// Associates a port's x coordinate with the Node* that generated it.
	// Storing the generating node pointer allows the renderer to determine:
	//   - Whether the port comes from a real or dummy node (node->isDummy()).
	//   - For dummy nodes: the node's own source_ports and target_ports, needed
	//     to detect and draw horizontal jogs.
	// ============================================================================
	struct PortInfo {
		double x;
		Node* generating_node;
	};

	// ============================================================================
	// HypergraphRenderer
	//
	// Converts the layout data of a GraphicalHypergraph (or any subclass, since
	// both GraphicalHypergraph and JointGraphicalHypergraph expose the same
	// layout interface) into QGraphicsItems placed on a QGraphicsScene.
	//
	// Rendering algorithm
	// ───────────────────
	// The renderer performs a layer-by-layer sweep, mirroring the structure of
	// assignYCoordinates():
	//
	//   For each layer L (starting from 0):
	//     1. Draw all real node boxes for layer L.
	//     2. For the gap between layer L-1 and layer L, process incoming_edges:
	//        a. Stable-partition: trivial edges (bar_y == layer_y[L-1] -
	//           NODE_HEIGHT/2) go first — they have no horizontal bar.
	//        b. For each segment edge, build source-port and target-port maps
	//           (PortInfo vectors) from node_layout_.
	//        c. Draw vertical segments from source nodes (layer L-1) downward
	//           to the bar y-coordinate, or from target nodes (layer L) upward.
	//           Record the occupied y ranges in a per-gap vertical_occupancy map.
	//        d. Draw the horizontal bar from x_min to x_max at bar_y, inserting
	//           a small upward semicircular hop (radius HOP_RADIUS = 5.0) wherever
	//           the bar strictly crosses an already-occupied vertical segment.
	//
	// Coordinate convention
	// ─────────────────────
	// The layout engine uses: layer 0 at y=0, deeper layers at more negative y.
	// Qt uses the opposite convention (y increases downward), so all logical y
	// values are negated before being passed to QPainterPath / QGraphicsItem.
	//
	// Usage
	// ─────
	//   QGraphicsScene* scene = new QGraphicsScene;
	//   std::unordered_map<Node*, QGraphicsRectItem*>      node_items;
	//   std::unordered_map<Hyperedge*, QGraphicsPathItem*> edge_items;
	//   HypergraphRenderer::render(graph, scene, node_items, edge_items);
	// ============================================================================
	class HypergraphRenderer {
	public:

		// ── render ────────────────────────────────────────────────────────────────
		//
		// Clears the given scene and rebuilds it entirely from the layout data of
		// the supplied graph. computeLayout() must have been called on the graph
		// at least once before this function is invoked.
		//
		// node_items is populated with one entry per real node, mapping the Node*
		// to its QGraphicsRectItem. The caller attaches interaction handlers to
		// these items (clicks, drags, context menus).
		//
		// edge_items is populated with one entry per original hyperedge, mapping
		// the Hyperedge* to its QGraphicsPathItem. Segment edges do not appear
		// here — all segments of one original edge are drawn into a single path.
		//
		static void render(
			const GraphicalHypergraph& graph,
			QGraphicsScene* scene,
			std::unordered_map<Node*, QGraphicsRectItem*>& node_items,
			std::unordered_map<Hyperedge*, QGraphicsPathItem*>& edge_items);

	private:

		// ── drawNodeBox ───────────────────────────────────────────────────────────
		//
		// Creates a labelled rectangle for the given real node and adds it to the
		// scene. The rectangle is centred at (layout.x, -layer_y) in Qt coordinates.
		// Returns the created item and records it in node_items.
		//
		static QGraphicsRectItem* drawNodeBox(
			Node* node,
			const NodeLayout& layout,
			double                                          layer_y,
			QGraphicsScene* scene,
			std::unordered_map<Node*, QGraphicsRectItem*>& node_items);

		// ── buildPortMaps ─────────────────────────────────────────────────────────
		//
		// For a given segment edge, scans the source and target nodes' port lists
		// and builds:
		//   src_ports — one PortInfo per source port of this segment.
		//   tgt_ports — one PortInfo per target port of this segment.
		//   x_min, x_max — updated in place to track the horizontal extent of the
		//                  edge's bar across all calls for the same original edge.
		//
		static void buildPortMaps(
			const HyperedgePtr& segment,
			const std::unordered_map<Node*, NodeLayout>& node_layout,
			std::vector<PortInfo>& src_ports,
			std::vector<PortInfo>& tgt_ports,
			double& x_min,
			double& x_max);

		// ── drawVerticalSegments ──────────────────────────────────────────────────
		//
		// Draws all vertical segments for one hyperedge in the current gap
		// (layer L-1 → layer L) and records the occupied y ranges in
		// vertical_occupancy.
		//
		// Source ports (from nodes in layer L-1):
		//   Real node   → segment from (x, layer_y_prev - NODE_HEIGHT/2) to (x, bar_y).
		//   Dummy node  → segment from (x, layer_y_prev) to (x, bar_y).
		//                 If the dummy's own target port x disagrees with x,
		//                 also draws a horizontal jog at y = layer_y_prev.
		//
		// Target ports (from nodes in layer L):
		//   Real node   → segment from (x, bar_y) to (x, layer_y + NODE_HEIGHT/2).
		//   Dummy node  → segment from (x, bar_y) to (x, layer_y).
		//                 No horizontal jog for dummy targets.
		//
		// For trivial edges (no horizontal bar), the vertical range added to
		// vertical_occupancy covers the full gap:
		//   [layer_y - NODE_HEIGHT/2, layer_y_prev - NODE_HEIGHT/2]
		// regardless of whether nodes are real or dummy.
		//
		static void drawVerticalSegments(
			const std::vector<PortInfo>& src_ports,
			const std::vector<PortInfo>& tgt_ports,
			double                                                 bar_y,
			double                                                 layer_y_prev,
			double                                                 layer_y,
			bool                                                   is_trivial,
			const std::unordered_map<Node*, NodeLayout>& node_layout,
			std::map<double, std::vector<VerticalRange>>& vertical_occupancy,
			QPainterPath& path);

		// ── drawHorizontalBar ─────────────────────────────────────────────────────
		//
		// Draws the horizontal bar of a hyperedge from x_min to x_max at bar_y,
		// inserting a small upward semicircular hop (radius HOP_RADIUS) wherever
		// the bar strictly crosses an already-occupied vertical segment.
		//
		// A conflict exists at x if:
		//   1. x_min < x < x_max  (strictly between the bar endpoints), AND
		//   2. bar_y is strictly inside one of the VerticalRanges at that x.
		//
		// No-ops if x_min >= x_max (trivial edge, no bar needed).
		//
		static void drawHorizontalBar(
			double                                                 x_min,
			double                                                 x_max,
			double                                                 bar_y,
			const std::map<double, std::vector<VerticalRange>>& vertical_occupancy,
			QPainterPath& path);

		// Radius of the semicircular hop arc, in Qt logical pixels.
		// The arc goes upward (toward less negative y in layout space, i.e.
		// toward layer 0), which in Qt coordinates means toward more negative
		// Qt-y (since we negate layout y).
		static constexpr double HOP_RADIUS = 5.0;
	};

} // namespace graphical_engine