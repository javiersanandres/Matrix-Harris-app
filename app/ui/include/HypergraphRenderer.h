#pragma once

#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"

#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QRectF>

#include <functional>
#include <map>
#include <vector>
#include <unordered_map>

// Forward declarations so the header does not depend on NodeItem/HyperedgeItem
// headers (which themselves include this header).
namespace ui {
    class NodeItem;
    class HyperedgeItem;
}

namespace ui {

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
    // EdgeInfo
    //
    // This is the data structure built by buildPortMaps() for each segment
    // edge during the layer-by-layer sweep. It collects all source and target
	// ports of the segment and tracks the horizontal extent of the edge's bar.
    // ============================================================================
    struct EdgeInfo {
        double x_min = std::numeric_limits<double>::max();
        double x_max = std::numeric_limits<double>::lowest();
		std::vector<PortInfo> src_ports;
		std::vector<PortInfo> tgt_ports;
    };

    // ============================================================================
    // HypergraphRenderer
    //
    // Converts the layout data of a GraphicalHypergraph into QGraphicsItems
    // placed on a QGraphicsScene.
    //
    // Two overloads of render() are provided:
    //
    //   Static overload  — creates raw QGraphicsRectItem / QGraphicsPathItem.
    //                      Intended for non-interactive contexts (export, print).
    //
    //   Factory overload — accepts factory lambdas that produce NodeItem and
    //                      HyperedgeItem directly. Used by DiagramScene::rebuild()
    //                      so no intermediate raw items are created and deleted.
    //
    // All geometry helpers (buildPortMaps, drawVerticalSegments, drawHorizontalBar)
    // are shared between both overloads and only write into QPainterPath objects,
    // with no dependency on the item type.
    //
    // Coordinate convention
    // ─────────────────────
    // The layout engine uses: layer 0 at y=0, deeper layers at more negative y.
    // Qt uses the opposite convention (y increases downward), so all logical y
    // values are negated before being passed to QPainterPath / QGraphicsItem.
    // ============================================================================
    class HypergraphRenderer {
    public:

        // Factory types for the interactive overload.
        using NodeItemFactory = std::function<NodeItem* (Node*, const QRectF&)>;
        using EdgeItemFactory = std::function<HyperedgeItem* (Hyperedge*, const QPainterPath&)>;

        // ── Static overload ───────────────────────────────────────────────────────
        //
        // Clears the scene and rebuilds it using raw Qt items. node_items is
        // populated with QGraphicsRectItem* per real node; edge_items with
        // QGraphicsPathItem* per original hyperedge.
        //
        // Intended for non-interactive rendering (export, static thumbnails that
        // do not share a scene with the editing view).
        //
        static void render(
            const GraphicalHypergraph& graph,
            QGraphicsScene* scene,
            std::unordered_map<Node*, QGraphicsRectItem*>& node_items,
            std::unordered_map<Hyperedge*, QGraphicsPathItem*>& edge_items);

        // ── Factory overload ──────────────────────────────────────────────────────
        //
        // Clears the scene and rebuilds it using items produced by the supplied
        // factory lambdas. node_items is populated with NodeItem* per real node;
        // edge_items with HyperedgeItem* per original hyperedge.
        //
        // The factories receive the raw Node*/Hyperedge* pointer and the computed
        // geometry (QRectF for nodes, QPainterPath for edges) and are responsible
        // for allocating and returning the item. The renderer adds each returned
        // item to the scene via scene->addItem().
        //
        // Used by DiagramScene::rebuild() to create interactive items in a single
        // pass without any intermediate allocation or deletion.
        //
        static void render(
            const GraphicalHypergraph& graph,
            QGraphicsScene* scene,
            std::unordered_map<Node*, NodeItem*>& node_items,
            std::unordered_map<Hyperedge*, HyperedgeItem*>& edge_items,
            const NodeItemFactory& make_node,
            const EdgeItemFactory& make_edge);

    private:

        // ── computeNodeRect ───────────────────────────────────────────────────────
        //
        // Returns the axis-aligned bounding rectangle for a node box given its
        // layout x coordinate and its layer's y coordinate (in layout space).
        // The result is already in Qt coordinates (y negated).
        //
        static QRectF computeNodeRect(const NodeLayout& layout, double layer_y);

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
            EdgeInfo& edge_info);

        // ── drawVerticalSegments ──────────────────────────────────────────────────
        //
        // Draws all vertical segments for one hyperedge in the current gap
        // (layer L-1 → layer L) and records the occupied y ranges in
        // vertical_occupancy.
        //
        // Source ports (from nodes in layer L-1):
        //   Real node  -> segment from (x, layer_y_prev - NODE_HEIGHT/2) to (x, bar_y).
        //   Dummy node -> segment from (x, layer_y_prev) to (x, bar_y).
        //                If the dummy's own target port x disagrees, draws a jog
        //                at y = layer_y_prev.
        //
        // Target ports (from nodes in layer L):
        //   Real node  -> segment from (x, bar_y) to (x, layer_y + NODE_HEIGHT/2).
        //   Dummy node -> segment from (x, bar_y) to (x, layer_y).
        //                No horizontal jog for dummy targets.
        //
        static void drawVerticalSegments(
            const std::vector<PortInfo>& src_ports,
            const std::vector<PortInfo>& tgt_ports,
            double bar_y,
            double layer_y_prev,
            double layer_y,
            bool is_trivial,
            const std::unordered_map<Node*, NodeLayout>& node_layout,
            std::map<double, std::vector<VerticalRange>>& vertical_occupancy,
            QPainterPath& path);

        // ── drawHorizontalBar ─────────────────────────────────────────────────────
        //
        // Draws the horizontal bar from x_min to x_max at bar_y, inserting an
        // upward semicircular hop (radius HOP_RADIUS) wherever the bar strictly
        // crosses an already-occupied vertical segment. Consecutive hops that
        // would collide are merged into a single cubic Bézier arch (ARCH_HEIGHT).
        //
        static void drawHorizontalBar(
            double x_min,
            double x_max,
            double bar_y,
            const std::map<double, std::vector<VerticalRange>>& vertical_occupancy,
            QPainterPath& path);

        // ── Core sweep ────────────────────────────────────────────────────────────
        //
        // Shared layer-by-layer sweep used by both render() overloads.
        // Populates edge_paths (one QPainterPath per original edge) and calls
        // place_node for each real node encountered.
        //
        // place_node(Node*, QRectF) — called once per real node box.
        // commit_edge(Hyperedge*, QPainterPath&) — called once per original edge
        //   after all its segments have been processed.
        //
        static void coreSweep(
            const GraphicalHypergraph& graph,
            const std::function<void(Node*, const QRectF&)>& place_node,
            const std::function<void(Hyperedge*, QPainterPath&)>& commit_edge);

        static constexpr double HOP_RADIUS = 5.0;
        static constexpr double ARCH_HEIGHT = 10.0;
    };

} // namespace ui