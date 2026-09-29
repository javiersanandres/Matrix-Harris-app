#pragma once

#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"

#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPen>
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
    // Associates a port's coordinates with the Node* that generated it. y is
    // where the edge meets the node's boundary (see hypergraph_logic::Port).
    // Storing the generating node pointer allows the renderer to determine:
    //   - Whether the port comes from a real or dummy node (node->isDummy()).
    //   - For dummy nodes: the node's own source_ports and target_ports, needed
    //     to detect and draw horizontal jogs.
    // ============================================================================
    struct PortInfo {
        double x;
        double y;
        Node* generating_node;
        bool uncertain = false; // Port::uncertain: the connection is doubted at this node
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
    //   Static overload  — creates raw QGraphicsPathItems for nodes and edges.
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
        // The edge factory gets the connection's drawing split in two: the part
        // drawn with a continuous line and the part drawn discontinuous (see
        // "Discontinuous parts" below).
        using EdgeItemFactory = std::function<HyperedgeItem* (Hyperedge*, const QPainterPath& solid,
            const QPainterPath& dashed)>;

        // Pen for drawing (part of) a connection: solid, or dashed when
        // continuous is false. Dashes keep the same length on screen whatever
        // the width (a hovered or emphasised connection is thicker).
        static QPen connectionPen(bool continuous, const QColor& colour = Qt::black, qreal width = 1.5);

        // ── Discontinuous parts ───────────────────────────────────────────────────
        //
        // A connection is drawn as a tree: stubs from every port to a bar, the
        // bars, and, across several layers, the chains through dummy nodes with
        // their jogs. The ports on real nodes are its leaves. A piece of that tree
        // is drawn discontinuous when everything it leads to on one of its sides
        // is uncertain ports (Port::uncertain): it only exists to reach nodes the
        // user doubts. So an uncertain port dashes its own branch -- its stub,
        // the stretch of bar that only it needs, and any bend or multi-layer run
        // built to reach it -- up to where it meets the rest of the connection;
        // in a one-to-one connection that is the whole line. A connection that
        // is not continuous as a whole is drawn discontinuous entirely.

        // ── Static overload ───────────────────────────────────────────────────────
        //
        // Clears the scene and rebuilds it using raw Qt items. node_items is
        // populated with a QGraphicsPathItem* (outlining the node's shape) per
        // real node; edge_items with
        // QGraphicsPathItem* per original hyperedge.
        //
        // Intended for non-interactive rendering (export, static thumbnails that
        // do not share a scene with the editing view).
        //
        static void render(
            const GraphicalHypergraph& graph,
            QGraphicsScene* scene,
            std::unordered_map<Node*, QGraphicsPathItem*>& node_items,
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

        // ── nodeShapePath ─────────────────────────────────────────────────────────
        //
        // Returns the outline of the node's shape inscribed in rect: the rect
        // itself, the ellipse inscribed in it, or the rhombus joining the
        // midpoints of its sides. Shared with NodeItem for painting/hit-testing.
        //
        static QPainterPath nodeShapePath(const Node* node, const QRectF& rect);

    private:

        // ── computeNodeRect ───────────────────────────────────────────────────────
        //
        // Returns the axis-aligned bounding box of the node's shape (its
        // getWidth() x getHeight()) given its layout x coordinate and its layer's
        // y coordinate (in layout space), so the shape's centre lies on the layer.
        // The result is already in Qt coordinates (y negated).
        //
        static QRectF computeNodeRect(const Node* node, const NodeLayout& layout, double layer_y);

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

        // ── Core sweep ────────────────────────────────────────────────────────────
        //
        // Shared layer-by-layer sweep used by both render() overloads. Builds
        // each original edge's drawing (see "Discontinuous parts") and calls
        // place_node for each real node encountered.
        //
        // In every gap (layer L-1 -> layer L), each segment contributes vertical
        // stubs between its ports' own y (where they meet the node's boundary,
        // or the layer's y for a dummy) and its bar, a jog at the dummy's layer
        // where a dummy's incoming and outgoing x differ, and its horizontal
        // bar. Trivial segments (single x) are one vertical line from source
        // port to target port. Bars are drawn after every vertical of the gap, so
        // they hop (radius HOP_RADIUS) over each vertical they strictly cross;
        // hops too close together merge into one arch (ARCH_HEIGHT).
        //
        // place_node(Node*, QRectF) — called once per real node box.
        // commit_edge(Hyperedge*, solid, dashed) — called once per original edge
        //   after all its segments have been processed.
        //
        static void coreSweep(
            const GraphicalHypergraph& graph,
            const std::function<void(Node*, const QRectF&)>& place_node,
            const std::function<void(Hyperedge*, QPainterPath& solid, QPainterPath& dashed)>& commit_edge);

        static constexpr double HOP_RADIUS = 5.0;
        static constexpr double ARCH_HEIGHT = 10.0;
    };

} // namespace ui