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
#include <tuple>

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

    namespace {

        // ========================================================================
        // StaticNodeItem
        //
        // Non-interactive node item for the static overload: a path item (so callers
        // keep a QGraphicsPathItem*) that paints itself exactly like NodeItem does.
        // ========================================================================
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

        // ========================================================================
        // ConnectionTree
        //
        // The drawing of one original connection, kept as a tree while the sweep
        // builds it: vertices are the ports and the points where stubs meet a bar,
        // and every piece of line (stub, stretch of bar, jog, trivial vertical)
        // joins two of them. The ports on real nodes are the leaves. Once complete,
        // split() decides which pieces are drawn discontinuous.
        // ========================================================================
        enum VertexKind { kSourcePort = 0, kTargetPort = 1, kBarPoint = 2 };

        struct VertexKey {
            const void* owner;  // the port's node, or the bar's segment
            int kind;           // VertexKind
            long long position; // bar points: x, quantised
            bool operator<(const VertexKey& o) const {
                return std::tie(owner, kind, position) < std::tie(o.owner, o.kind, o.position);
            }
        };

        struct ConnectionTree {
            struct Piece { int a; int b; QPainterPath geometry; };

            std::map<VertexKey, int> ids;
            std::vector<int> leaf;   // per vertex: -1 not a leaf, 0 certain port, 1 uncertain port
            std::vector<Piece> pieces;

            int vertex(const VertexKey& key) {
                auto [it, inserted] = ids.emplace(key, static_cast<int>(leaf.size()));
                if (inserted) leaf.push_back(-1);
                return it->second;
            }

            int port(const PortInfo& p, bool source) {
                const int v = vertex({ p.generating_node, source ? kSourcePort : kTargetPort, 0 });
                if (!p.generating_node->isDummy()) leaf[v] = p.uncertain ? 1 : 0;
                return v;
            }

            int barPoint(const Hyperedge* segment, double x) {
                return vertex({ segment, kBarPoint, std::llround(x * 1024.0) });
            }

            void add(int a, int b, QPainterPath geometry) {
                pieces.push_back({ a, b, std::move(geometry) });
            }

            // Sends each piece to solid or dashed. A piece is dashed when every
            // leaf on one of its two sides is uncertain (it only leads to doubted
            // ports); everything is dashed when the connection as a whole is.
            void split(bool whole_dashed, QPainterPath& solid, QPainterPath& dashed) const {
                if (whole_dashed) {
                    for (const auto& piece : pieces) dashed.addPath(piece.geometry);
                    return;
                }

                const int n = static_cast<int>(leaf.size());
                std::vector<std::vector<std::pair<int, int>>> adjacent(n); // (vertex, piece)
                for (int i = 0; i < static_cast<int>(pieces.size()); ++i) {
                    adjacent[pieces[i].a].push_back({ pieces[i].b, i });
                    adjacent[pieces[i].b].push_back({ pieces[i].a, i });
                }

                // Depth-first order, parent piece and component of every vertex.
                std::vector<int> order, parent_piece(n, -1), parent(n, -1), component(n, -1);
                std::vector<char> in_tree(pieces.size(), 0);
                std::vector<int> roots;
                for (int root = 0; root < n; ++root) {
                    if (component[root] >= 0) continue;
                    const int c = static_cast<int>(roots.size());
                    roots.push_back(root);
                    std::vector<int> stack{ root };
                    component[root] = c;
                    while (!stack.empty()) {
                        const int v = stack.back();
                        stack.pop_back();
                        order.push_back(v);
                        for (const auto& [w, piece] : adjacent[v]) {
                            if (component[w] >= 0) continue;
                            component[w] = c;
                            parent[w] = v;
                            parent_piece[w] = piece;
                            in_tree[piece] = 1;
                            stack.push_back(w);
                        }
                    }
                }

                // Leaves below every vertex (all of them, and the uncertain ones).
                std::vector<int> below(n, 0), below_uncertain(n, 0);
                for (auto it = order.rbegin(); it != order.rend(); ++it) {
                    const int v = *it;
                    if (leaf[v] >= 0) { ++below[v]; below_uncertain[v] += leaf[v]; }
                    if (parent[v] >= 0) {
                        below[parent[v]] += below[v];
                        below_uncertain[parent[v]] += below_uncertain[v];
                    }
                }

                auto onlyUncertain = [](int total, int uncertain) { return total > 0 && uncertain == total; };
                std::vector<char> is_dashed(pieces.size(), 0);
                for (int v = 0; v < n; ++v) {
                    if (parent_piece[v] < 0) continue;
                    const int root = roots[component[v]];
                    const int rest = below[root] - below[v];
                    const int rest_uncertain = below_uncertain[root] - below_uncertain[v];
                    is_dashed[parent_piece[v]] =
                        onlyUncertain(below[v], below_uncertain[v]) || onlyUncertain(rest, rest_uncertain);
                }
                // A piece closing a loop (not expected in a drawing) follows its
                // component: dashed only if every port there is doubted.
                for (int i = 0; i < static_cast<int>(pieces.size()); ++i) {
                    if (in_tree[i]) continue;
                    const int root = roots[component[pieces[i].a]];
                    is_dashed[i] = onlyUncertain(below[root], below_uncertain[root]);
                }

                for (int i = 0; i < static_cast<int>(pieces.size()); ++i)
                    (is_dashed[i] ? dashed : solid).addPath(pieces[i].geometry);
            }
        };

        // ========================================================================
        // Bar hops
        //
        // Where a bar strictly crosses an already-drawn vertical, it hops over it
        // with a semicircle; crossings too close together share one arch.
        // ========================================================================
        struct HopGroup {
            double start;  // where the hop leaves the bar
            double end;    // where it lands back on it
            bool single;   // one crossing: semicircle; several: one arch
            double centre() const { return (start + end) / 2.0; }
        };

        std::vector<HopGroup> hopGroups(double x_min, double x_max, double bar_y,
            const std::map<double, std::vector<VerticalRange>>& vertical_occupancy,
            double hop_radius)
        {
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

            std::vector<HopGroup> groups;
            double first = 0.0, last = 0.0;
            bool open = false;
            auto close = [&] {
                if (open) groups.push_back({ first - hop_radius, last + hop_radius, first == last });
            };
            for (double hx : hops) {
                if (open && (hx - last) <= 3.0 * hop_radius) { last = hx; continue; }
                close();
                first = last = hx;
                open = true;
            }
            close();
            return groups;
        }

        // The stretch of bar from `from` to `to`, drawing the hops whose centre
        // lies in it (the last stretch of a bar also takes a hop centred on its end).
        QPainterPath barStretch(double from, double to, bool is_last, double bar_y,
            const std::vector<HopGroup>& groups, double arch_height)
        {
            QPainterPath path;
            if (to <= from) return path;
            const double qt_y = -bar_y;
            double cur_x = from;
            path.moveTo(cur_x, qt_y);
            for (const auto& g : groups) {
                const double c = g.centre();
                if (c < from || c > to || (c == to && !is_last)) continue;
                if (g.start > cur_x) path.lineTo(g.start, qt_y);
                if (g.single) {
                    // Full circle of diameter end-start centred on the bar: sweeping
                    // from its left point over the top lands on its right point.
                    const double r = (g.end - g.start) / 2.0;
                    path.arcTo(QRectF(g.start, qt_y - r, 2.0 * r, 2.0 * r), 180.0, -180.0);
                }
                else {
                    const double ctrl_y = qt_y - arch_height;
                    path.cubicTo(c, ctrl_y, c, ctrl_y, g.end, qt_y);
                }
                cur_x = g.end;
            }
            if (cur_x < to) path.lineTo(to, qt_y);
            return path;
        }

        QPainterPath verticalLine(double x, double y_from, double y_to) {
            QPainterPath path;
            path.moveTo(x, -y_from);
            path.lineTo(x, -y_to);
            return path;
        }

    } // namespace

    // ============================================================================
    // coreSweep
    //
    // Shared layer-by-layer sweep. Calls place_node for every real node and
    // commit_edge for every original edge once its full drawing is assembled.
    // Both render() overloads delegate here, supplying different lambdas.
    // ============================================================================
    void HypergraphRenderer::coreSweep(
        const GraphicalHypergraph& graph,
        const std::function<void(Node*, const QRectF&)>& place_node,
        const std::function<void(Hyperedge*, QPainterPath& solid, QPainterPath& dashed)>& commit_edge)
    {
        if (graph.getLayers().empty()) return;

        const auto& node_layout = graph.getNodeLayout();
        const auto& edge_layout = graph.getEdgeLayout();
        const auto& layer_layout = graph.getLayerLayout();

        std::unordered_map<Hyperedge*, ConnectionTree> trees;

        auto get_original = [&](const HyperedgePtr& e) -> Hyperedge* {
            if (!e->isSegment()) return e.get();
            auto locked = e->getOrigin().lock();
            return locked ? locked.get() : nullptr;
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

            // ── Step 4: vertical pieces ───────────────────────────────────────────
            //
            // All verticals of the gap first, so that every bar drawn afterwards
            // hops over each one it crosses.
            for (const auto& edge : incoming_edges) {
                Hyperedge* orig = get_original(edge);
                if (!orig) continue;
                const EdgeInfo& info = edge_info_cache.at(edge.get());
                if (info.src_ports.empty() || info.tgt_ports.empty()) continue;
                ConnectionTree& tree = trees[orig];

                // A dummy source whose incoming x differs from its outgoing x
                // turns once, at its own layer: the jog links its two ports.
                for (const auto& pi : info.src_ports) {
                    if (!pi.generating_node->isDummy()) continue;
                    QPainterPath jog;
                    auto layout_it = node_layout.find(pi.generating_node);
                    if (layout_it != node_layout.end() && !layout_it->second.target_ports.empty()) {
                        const double in_x = layout_it->second.target_ports.front().x;
                        if (std::abs(in_x - pi.x) > 1e-9) {
                            jog.moveTo(std::min(in_x, pi.x), -layer_y_prev);
                            jog.lineTo(std::max(in_x, pi.x), -layer_y_prev);
                        }
                    }
                    tree.add(tree.vertex({ pi.generating_node, kTargetPort, 0 }), tree.port(pi, true), jog);
                }

                if (trivial_edges.count(edge.get())) {
                    const auto& src = info.src_ports.front();
                    const auto& tgt = info.tgt_ports.front();
                    vertical_occupancy[src.x].push_back({ tgt.y, src.y });
                    tree.add(tree.port(src, true), tree.port(tgt, false), verticalLine(src.x, src.y, tgt.y));
                    continue;
                }

                auto it_bar = edge_layout.find(edge.get());
                const double bar_y = (it_bar != edge_layout.end()) ? it_bar->second : layer_y_prev;
                for (const auto& pi : info.src_ports) {
                    vertical_occupancy[pi.x].push_back({ bar_y, pi.y });
                    tree.add(tree.port(pi, true), tree.barPoint(edge.get(), pi.x), verticalLine(pi.x, pi.y, bar_y));
                }
                for (const auto& pi : info.tgt_ports) {
                    vertical_occupancy[pi.x].push_back({ pi.y, bar_y });
                    tree.add(tree.barPoint(edge.get(), pi.x), tree.port(pi, false), verticalLine(pi.x, bar_y, pi.y));
                }
            }

            // ── Step 5: horizontal bars, one stretch between consecutive stubs ───
            for (const auto& edge : incoming_edges) {
                Hyperedge* orig = get_original(edge);
                if (!orig) continue;
                if (trivial_edges.count(edge.get())) continue;
                const EdgeInfo& info = edge_info_cache.at(edge.get());
                if (info.src_ports.empty() || info.tgt_ports.empty()) continue;
                ConnectionTree& tree = trees[orig];
                const double bar_y = edge_layout.at(edge.get());

                std::vector<double> stops;
                for (const auto& pi : info.src_ports) stops.push_back(pi.x);
                for (const auto& pi : info.tgt_ports) stops.push_back(pi.x);
                std::sort(stops.begin(), stops.end());
                stops.erase(std::unique(stops.begin(), stops.end()), stops.end());

                const auto groups = hopGroups(info.x_min, info.x_max, bar_y, vertical_occupancy, HOP_RADIUS);

                // A hop cannot be cut in two: a stub landing inside one moves the
                // cut to the nearer end of that hop.
                std::vector<double> cuts = stops;
                for (size_t i = 1; i + 1 < cuts.size(); ++i)
                    for (const auto& g : groups)
                        if (cuts[i] > g.start && cuts[i] < g.end)
                            cuts[i] = (cuts[i] - g.start < g.end - cuts[i]) ? g.start : g.end;

                for (size_t i = 0; i + 1 < stops.size(); ++i) {
                    tree.add(tree.barPoint(edge.get(), stops[i]), tree.barPoint(edge.get(), stops[i + 1]),
                        barStretch(cuts[i], cuts[i + 1], i + 2 == stops.size(), bar_y, groups, ARCH_HEIGHT));
                }
            }

            incoming_edges = layer_data.outgoing_edges;
            nodes_in_prev_layer = layer_data.nodes;
        }

        // ── Step 6: commit each original edge, split into solid and dashed ─────
        for (auto& [raw, tree] : trees) {
            QPainterPath solid, dashed;
            tree.split(raw->allEndsUncertain(), solid, dashed);
            commit_edge(raw, solid, dashed);
        }
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

        coreSweep(graph,
            // place_node
            [&](Node* node, const QRectF& rect) {
                auto* item = new StaticNodeItem(node, rect);
                scene->addItem(item);
                node_items[node] = item;
            },
            // commit_edge: the continuous part, with the discontinuous part as a
            // child so the edge still has a single item.
            [&](Hyperedge* orig, QPainterPath& solid, QPainterPath& dashed) {
                QGraphicsPathItem* item = scene->addPath(solid, connectionPen(true));
                if (!dashed.isEmpty()) {
                    auto* dashes = new QGraphicsPathItem(dashed, item);
                    dashes->setPen(connectionPen(false));
                }
                edge_items[orig] = item;
            });
    }

    // ============================================================================
    // connectionPen
    // ============================================================================
    QPen HypergraphRenderer::connectionPen(bool continuous, const QColor& colour, qreal width) {
        QPen pen(colour, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        if (!continuous) {
            // Qt measures dash patterns in pen widths: divide to keep 7px dashes
            // and 5px gaps. Flat caps keep the dashes crisp.
            constexpr qreal DASH = 7.0, GAP = 5.0;
            pen.setCapStyle(Qt::FlatCap);
            pen.setDashPattern({ DASH / width, GAP / width });
        }
        return pen;
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
            [&](Hyperedge* orig, QPainterPath& solid, QPainterPath& dashed) {
                HyperedgeItem* ei = make_edge(orig, solid, dashed);
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
                    edge_info.src_ports.push_back({ port.x, port.y, src_node.get(), port.uncertain });
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
                    edge_info.tgt_ports.push_back({ port.x, port.y, tgt_node.get(), port.uncertain });
                    edge_info.x_min = std::min(edge_info.x_min, port.x);
                    edge_info.x_max = std::max(edge_info.x_max, port.x);
                    break;
                }
            }
        }
    }

} // namespace ui
