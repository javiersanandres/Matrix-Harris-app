#include "HypergraphRenderer.h"
#include "NodeItem.h"
#include "HyperedgeItem.h"
#include "NodeVisuals.h"

#include <QPainter>
#include <QPen>
#include <QBrush>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ui {

    // ============================================================================
    // computeNodeRect
    // ============================================================================
    QRectF HypergraphRenderer::computeNodeRect(const Node* node, const NodeLayout& layout, double layer_y) {
        double cx = layout.x;
        double cy = -layer_y;   // negate: layout y=0 → Qt top, deeper → Qt down
        double w = node->getWidth();
        double h = node->getHeight();
        return QRectF(cx - w / 2.0, cy - h / 2.0, w, h);
    }

    // ============================================================================
    // nodeShapePath
    // ============================================================================
    QPainterPath HypergraphRenderer::nodeShapePath(const Node* node, const QRectF& rect) {
        return node_visuals::shapePath(node->getShape(), rect);
    }

    // ============================================================================
    // StaticNodeItem
    //
    // Non-interactive node item for the static overload: a path item (so callers
    // keep a QGraphicsPathItem*) that paints itself exactly like NodeItem does.
    // ============================================================================
    namespace {
        class StaticNodeItem : public QGraphicsPathItem {
        public:
            StaticNodeItem(const Node* node, const QRectF& box)
                : QGraphicsPathItem(node_visuals::shapePath(node->getShape(), box))
                , attributes_(node->getAttributes())
                , box_(box)
            {
                setPen(QPen(Qt::black, 1.5));
            }

            void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
                node_visuals::PaintOptions opts;
                opts.outline = pen();
                node_visuals::paintNode(painter, attributes_, box_, opts);
            }

        private:
            NodeAttributes attributes_;
            QRectF box_;
        };
    } // namespace

    // ============================================================================
    // coreSweep
    //
    // Shared layer-by-layer sweep. Calls place_node for every real node and
    // commit_edge for every original edge once its full path is assembled.
    // Both render() overloads delegate here, supplying different lambdas.
    // ============================================================================
    void HypergraphRenderer::coreSweep(
        const GraphicalHypergraph& graph,
        const std::function<void(Node*, const QRectF&)>& place_node,
        const std::function<void(Hyperedge*, QPainterPath&)>& commit_edge)
    {
        if (graph.getLayers().empty()) return;

        const auto& node_layout = graph.getNodeLayout();
        const auto& edge_layout = graph.getEdgeLayout();
        const auto& layer_layout = graph.getLayerLayout();

        std::unordered_map<Hyperedge*, QPainterPath> edge_paths;

        auto get_original = [&](const HyperedgePtr& e) -> Hyperedge* {
            if (!e->isSegment()) return e.get();
            auto locked = e->getOrigin().lock();
            return locked ? locked.get() : nullptr;
            };

        auto ensure_path = [&](Hyperedge* orig) -> QPainterPath& {
            return edge_paths.emplace(orig, QPainterPath()).first->second;
            };

        std::vector<HyperedgePtr> incoming_edges;
        std::vector<NodePtr>      nodes_in_prev_layer;

        for (const auto& [layer_idx, layer_data] : graph.getLayers()) {

            double layer_y = layer_layout.at(layer_idx);

            // ── Step 1: place real node boxes ─────────────────────────────────────
            for (const auto& node : layer_data.nodes) {
                if (node->isDummy()) continue;
                const NodeLayout& nl = node_layout.at(node.get());
                place_node(node.get(), computeNodeRect(node.get(), nl, layer_y));
            }

            // ── Step 2: initialise on first layer ─────────────────────────────────
            if (incoming_edges.empty() || nodes_in_prev_layer.empty()) {
                incoming_edges = layer_data.outgoing_edges;
                nodes_in_prev_layer = layer_data.nodes;
                continue;
            }

            double layer_y_prev = layer_layout.at(layer_idx - 1);

            // ── Step 3: gather the ports of each edge ─────────────────────────────
            //
            // An edge is trivial when all its ports share one x (the same rule
            // assignYCoordinates uses): it needs no horizontal bar, just a single
            // vertical segment from its source port down to its target port.
            std::unordered_map<Hyperedge*, EdgeInfo> edge_info_cache;
            std::unordered_set<Hyperedge*> trivial_edges; // for quick lookup when drawing horizontal bars
            for (const auto& edge : incoming_edges) {
                EdgeInfo& info = edge_info_cache[edge.get()];
                buildPortMaps(edge, node_layout, info);
                if (info.x_min == info.x_max) trivial_edges.insert(edge.get());
            }

            std::stable_partition(incoming_edges.begin(), incoming_edges.end(),
                [&](const HyperedgePtr& e) { return trivial_edges.count(e.get()) > 0; });

            std::map<double, std::vector<VerticalRange>> vertical_occupancy;

            // ── Step 4: process each edge ─────────────────────────────────────────

            // First, draw the vertical segments. Then, if the bar is non-trivial, draw th
            // horizontal bar. This is the only way to ensure that the horizontal bar hops
            // over all vertical segments that it crosses.
            for (const auto& edge : incoming_edges) {
                Hyperedge* orig = get_original(edge);
                if (!orig) continue;
                const EdgeInfo& info = edge_info_cache.at(edge.get());
                if (info.src_ports.empty() || info.tgt_ports.empty()) continue;

                bool is_trivial = trivial_edges.count(edge.get()) > 0;
                auto it_bar = edge_layout.find(edge.get());
                double bar_y = (it_bar != edge_layout.end()) ? it_bar->second : layer_y_prev;

                QPainterPath& path = ensure_path(orig);

                drawVerticalSegments(
                    info.src_ports, info.tgt_ports,
                    bar_y, layer_y_prev,
                    is_trivial,
                    node_layout, vertical_occupancy, path);
            }

            // Second, draw the horizontal bars for non-trivial edges.
            for (const auto& edge : incoming_edges) {
                Hyperedge* orig = get_original(edge);
                if (!orig) continue;
                if (trivial_edges.count(edge.get())) continue; // already drawn as trivial in the previous loop
                double bar_y = edge_layout.at(edge.get());
                QPainterPath& path = ensure_path(orig);
                EdgeInfo& info = edge_info_cache.at(edge.get());
                drawHorizontalBar(info.x_min, info.x_max, bar_y, vertical_occupancy, path);
            }

            incoming_edges = layer_data.outgoing_edges;
            nodes_in_prev_layer = layer_data.nodes;
        }

        // ── Step 5: commit one path per original edge ─────────────────────────────
        for (auto& [raw, path] : edge_paths)
            commit_edge(raw, path);
    }

    // ============================================================================
    // render — static overload (raw Qt items)
    // ============================================================================
    void HypergraphRenderer::render(
        const GraphicalHypergraph& graph,
        QGraphicsScene* scene,
        std::unordered_map<Node*, QGraphicsPathItem*>& node_items,
        std::unordered_map<Hyperedge*, QGraphicsPathItem*>& edge_items)
    {
        scene->clear();
        node_items.clear();
        edge_items.clear();

        QPen edge_pen(Qt::black, 1.5);

        coreSweep(graph,
            // place_node
            [&](Node* node, const QRectF& rect) {
                auto* item = new StaticNodeItem(node, rect);
                scene->addItem(item);
                node_items[node] = item;
            },
            // commit_edge
            [&](Hyperedge* orig, QPainterPath& path) {
                edge_items[orig] = scene->addPath(path, edge_pen);
            });
    }

    // ============================================================================
    // render — factory overload (interactive NodeItem / HyperedgeItem)
    // ============================================================================
    void HypergraphRenderer::render(
        const GraphicalHypergraph& graph,
        QGraphicsScene* scene,
        std::unordered_map<Node*, NodeItem*>& node_items,
        std::unordered_map<Hyperedge*, HyperedgeItem*>& edge_items,
        const NodeItemFactory& make_node,
        const EdgeItemFactory& make_edge)
    {
        scene->clear();
        node_items.clear();
        edge_items.clear();

        coreSweep(graph,
            // place_node
            [&](Node* node, const QRectF& rect) {
                NodeItem* ni = make_node(node, rect);
                scene->addItem(ni);
                node_items[node] = ni;
            },
            // commit_edge
            [&](Hyperedge* orig, QPainterPath& path) {
                HyperedgeItem* ei = make_edge(orig, path);
                scene->addItem(ei);
                edge_items[orig] = ei;
            });
    }

    // ============================================================================
    // buildPortMaps
    // ============================================================================
    void HypergraphRenderer::buildPortMaps(
        const HyperedgePtr& segment,
        const std::unordered_map<Node*, NodeLayout>& node_layout,
        EdgeInfo& edge_info)
    {
        for (const auto& src_node : segment->getSources()) {
            auto it = node_layout.find(src_node.get());
            if (it == node_layout.end()) continue;
            for (const auto& port : it->second.source_ports) {
                if (port.edge == segment.get()) {
                    edge_info.src_ports.push_back({ port.x, port.y, src_node.get() });
                    edge_info.x_min = std::min(edge_info.x_min, port.x);
                    edge_info.x_max = std::max(edge_info.x_max, port.x);
                    break;
                }
            }
        }
        for (const auto& tgt_node : segment->getTargets()) {
            auto it = node_layout.find(tgt_node.get());
            if (it == node_layout.end()) continue;
            for (const auto& port : it->second.target_ports) {
                if (port.edge == segment.get()) {
                    edge_info.tgt_ports.push_back({ port.x, port.y, tgt_node.get() });
                    edge_info.x_min = std::min(edge_info.x_min, port.x);
                    edge_info.x_max = std::max(edge_info.x_max, port.x);
                    break;
                }
            }
        }
    }

    // ============================================================================
    // drawVerticalSegments
    // ============================================================================
    void HypergraphRenderer::drawVerticalSegments(
        const std::vector<PortInfo>& src_ports,
        const std::vector<PortInfo>& tgt_ports,
        double                                        bar_y,
        double                                        layer_y_prev,
        bool                                         is_trivial,
        const std::unordered_map<Node*, NodeLayout>& node_layout,
        std::map<double, std::vector<VerticalRange>>& vertical_occupancy,
        QPainterPath& path)
    {
        if (is_trivial) {
            const auto& src_port = src_ports.front();
            const auto& tgt_port = tgt_ports.front();
            bool is_dummy_src = src_port.generating_node->isDummy();
            double y_top = src_port.y;
            double y_bottom = tgt_port.y;

            vertical_occupancy[src_port.x].push_back({ y_bottom, y_top });
            path.moveTo(src_port.x, -y_top);
            path.lineTo(src_port.x, -y_bottom);

            if (is_dummy_src) {
                auto layout_it = node_layout.find(src_port.generating_node);
                if (layout_it != node_layout.end()) {
                    const NodeLayout& dl = layout_it->second;
                    if (!dl.target_ports.empty()) {
                        double tgt_x = dl.target_ports.front().x;
                        if (std::abs(tgt_x - src_port.x) > 1e-9) {
                            path.moveTo(std::min(tgt_x, src_port.x), -layer_y_prev);
                            path.lineTo(std::max(tgt_x, src_port.x), -layer_y_prev);
                        }
                    }
                }
            }
            return;
        }

        // ── Source ports ──────────────────────────────────────────────────────────
        for (const auto& pi : src_ports) {
            double x = pi.x;
            bool is_dummy = pi.generating_node->isDummy();
            double y_top = pi.y;
            double y_bot = bar_y;

            path.moveTo(x, -y_top);
            path.lineTo(x, -y_bot);
            vertical_occupancy[x].push_back({ y_bot, y_top });

            if (is_dummy) {
                auto layout_it = node_layout.find(pi.generating_node);
                if (layout_it != node_layout.end()) {
                    const NodeLayout& dl = layout_it->second;
                    if (!dl.target_ports.empty()) {
                        double tgt_x = dl.target_ports.front().x;
                        if (std::abs(tgt_x - x) > 1e-9) {
                            path.moveTo(std::min(tgt_x, x), -layer_y_prev);
                            path.lineTo(std::max(tgt_x, x), -layer_y_prev);
                        }
                    }
                }
            }
        }

        // ── Target ports ──────────────────────────────────────────────────────────
        for (const auto& pi : tgt_ports) {
            double x = pi.x;
            double y_top = bar_y;
            double y_bot = pi.y;

            path.moveTo(x, -y_top);
            path.lineTo(x, -y_bot);
            vertical_occupancy[x].push_back({ y_bot, y_top });
        }
    }

    // ============================================================================
    // drawHorizontalBar
    // ============================================================================
    void HypergraphRenderer::drawHorizontalBar(
        double x_min,
        double x_max,
        double bar_y,
        const std::map<double, std::vector<VerticalRange>>& vertical_occupancy,
        QPainterPath& path)
    {
        if (x_min >= x_max) return;

        std::vector<double> hops;
        for (const auto& [x, ranges] : vertical_occupancy) {
            if (x <= x_min || x >= x_max) continue;
            for (const auto& r : ranges) {
                if (bar_y > r.y_min && bar_y < r.y_max) {
                    hops.push_back(x);
                    break;
                }
            }
        }
        std::sort(hops.begin(), hops.end());

        struct HopGroup { double x_first, x_last; };
        std::vector<HopGroup> groups;
        for (double hx : hops) {
            if (!groups.empty() && (hx - groups.back().x_last) <= 3.0 * HOP_RADIUS)
                groups.back().x_last = hx;
            else
                groups.push_back({ hx, hx });
        }

        double qt_y = -bar_y;
        double cur_x = x_min;
        path.moveTo(cur_x, qt_y);

        for (const auto& g : groups) {
            double start_x = g.x_first - HOP_RADIUS;
            double end_x = g.x_last + HOP_RADIUS;

            if (start_x > cur_x) path.lineTo(start_x, qt_y);

            if (g.x_first == g.x_last) {
                // The bounding rect must be a full circle of diameter 2*HOP_RADIUS
                // centred at (hx, qt_y) so that the arc endpoints land exactly
                // on the bar line. Top = qt_y - 2*HOP_RADIUS, height = 2*HOP_RADIUS.
                // arcTo(180, -180) sweeps clockwise from left midpoint (on the bar)
                // through the top of the circle and back down to the right midpoint.
                QRectF arc_rect(start_x,
                    qt_y - HOP_RADIUS,
                    2.0 * HOP_RADIUS,
                    2.0 * HOP_RADIUS);
                path.arcTo(arc_rect, 180.0, -180.0);
            }
            else {
                double mid_x = (start_x + end_x) / 2.0;
                double ctrl_y = qt_y - ARCH_HEIGHT;
                path.cubicTo(mid_x, ctrl_y, mid_x, ctrl_y, end_x, qt_y);
            }

            cur_x = end_x;
        }

        if (cur_x < x_max) path.lineTo(x_max, qt_y);
    }

} // namespace ui