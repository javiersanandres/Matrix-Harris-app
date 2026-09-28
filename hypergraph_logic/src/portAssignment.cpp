#include "PortAssignment.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_set>

// ====================================================================================
// This module implements the port assignment logic. This is almost the final step
// of the layout process and also the last complicated part of the implementation.
// In this file, we not only assing the ports to the hyperedges, but also prevent
// overlapping of vertical segments of hyperedges in the same layer. Many ideas of
// this implementation are based on the paper below, but I have developed my own
// approach to solve the problem, since the paper throws us to the lions by leaving
// many details out of the text.
// 
//	Fridman, G., Vasiliev, Y., Puhkalo, V., & Ryzhov, V. (2021).
//	"A Mixed-Integer Program for Drawing Orthogonal Hyperedges
//	in a Hierarchical Hypergraph." 
//	In: Mathematics 9, no. 16: 1903.
//	DOI: 10.3390/math9161903
// 
// ====================================================================================

namespace port_assignment_internal {

    using namespace hypergraph_logic;

    // ── PortAssigner: construction ────────────────────────────────────────────────

    PortAssigner::PortAssigner(int layer,
        const std::map<int, LayerData>& layers,
        std::unordered_map<Node*, NodeLayout>& node_layout)
        : layer_(layer)
        , upper_(layers.at(layer))
        , lower_(layers.at(layer + 1))
        , node_layout_(node_layout)
    {
        buildEdgeLookups();
        for (int i = 0; i < static_cast<int>(upper_.nodes.size()); ++i)
            upper_pos_[upper_.nodes[i].get()] = i;
        for (int i = 0; i < static_cast<int>(lower_.nodes.size()); ++i)
            lower_pos_[lower_.nodes[i].get()] = i;
    }

    // ── Lookup-table construction ─────────────────────────────────────────────────
    //
    // For each outgoing edge in the upper layer we record:
    //   - the leftmost and rightmost incident node(s) by x-coordinate, and
    //   - a rank index ordered by descending y-coordinate (lower index = higher).
    //
    // Both maps allow ties: if two nodes share the same x, both appear in the list.
    // This is because a target and a source can be perfectly aligned, so both are
    // lefmost and rightmost at the same time.
    void PortAssigner::buildEdgeLookups() {
        int counter = 0;
        for (const auto& edge : upper_.outgoing_edges) {
            Hyperedge* e = edge.get();
            leftmost_nodes_[e] = {};
            rightmost_nodes_[e] = {};
            for (const auto& src : edge->getSources())
                updateExtremesForNode(e, src.get());
            for (const auto& tgt : edge->getTargets())
                updateExtremesForNode(e, tgt.get());
            hyperedge_order_[e] = counter++;
        }
    }

    void PortAssigner::updateExtremesForNode(Hyperedge* edge, Node* node) {
        double x = node_layout_.at(node).x;

        auto& lv = leftmost_nodes_[edge];
        if (lv.empty() || x < node_layout_.at(lv[0]).x) lv = { node };
        else if (x == node_layout_.at(lv[0]).x)  lv.push_back(node);

        auto& rv = rightmost_nodes_[edge];
        if (rv.empty() || x > node_layout_.at(rv[0]).x) rv = { node };
        else if (x == node_layout_.at(rv[0]).x)  rv.push_back(node);
    }

    // pos(node, edge) function in the aforementioned paper.
    //   0 -> leftmost  (minimum x among all sources and targets)
    //   2 -> rightmost (maximum x among all sources and targets)
    //   1 -> anywhere in between
    int PortAssigner::nodePositionInEdge(Hyperedge* edge, Node* node) const {
        const auto& lv = leftmost_nodes_.at(edge);
        if (std::find(lv.begin(), lv.end(), node) != lv.end()) return 0;
        const auto& rv = rightmost_nodes_.at(edge);
        if (std::find(rv.begin(), rv.end(), node) != rv.end()) return 2;
        return 1;
    }

    // ── Node-position lookup ─────────────────────────────────────────────────────
    //
    // O(1) index of 'node' within its own layer's node list, backed by the
    // tables built once at construction (upper_pos_/lower_pos_) instead of a
    // linear scan over upper_.nodes/lower_.nodes.
    int PortAssigner::positionInLayer(Node* node, bool is_upper) const {
        const auto& pos_map = is_upper ? upper_pos_ : lower_pos_;
        auto it = pos_map.find(node);
        return it != pos_map.end() ? it->second : -1;
    }

    bool PortAssigner::edgeOrderedBefore(Hyperedge* a, Hyperedge* b) const {
        return hyperedge_order_.at(a) < hyperedge_order_.at(b);
    }


    bool PortAssigner::isLeftMost(Hyperedge* a, Node* n) const {
        auto it = leftmost_nodes_.find(a);
        if (it == leftmost_nodes_.end()) return false;
        for (const auto& node : it->second) {
            if (node == n) return true;
        }
        return false;
    }

    bool PortAssigner::isRightMost(Hyperedge* a, Node* n) const {
        auto it = rightmost_nodes_.find(a);
        if (it == rightmost_nodes_.end()) return false;
        for (const auto& node : it->second) {
            if (node == n) return true;
        }
        return false;
    }


    // ── Cross-boundary port search ────────────────────────────────────────────────
    //
    // Finds the port matching 'edge' on the named side of this pair -- used by
    // a dummy chain to find whoever owns the port immediately above (source,
    // on upper_) or below (target, on lower_) one of its own boundary ports.
    PortAssigner::PortLookup PortAssigner::findSourceFor(Hyperedge* edge) const {
        for (const auto& node : upper_.nodes)
            for (const Port& p : node_layout_.at(node.get()).source_ports)
                if (p.edge == edge) return { node.get(), p.x, true };
        return {};
    }

    PortAssigner::PortLookup PortAssigner::findTargetFor(Hyperedge* edge) const {
        for (const auto& node : lower_.nodes)
            for (const Port& p : node_layout_.at(node.get()).target_ports)
                if (p.edge == edge) return { node.get(), p.x, true };
        return {};
    }

    // ── Port ordering ─────────────────────────────────────────────────────────────
    //
    // Port ordering policy (crossing minimisation):
    //
    // Hyperedges are ranked by descending y-coordinate, so a lower hyperedge_order
    // index means the hyperedge sits higher on the canvas.
    //
    // For a generating node u, pos(u, e) encodes where u sits within the horizontal
    // span of hyperedge e:
    //   pos = 0  ->  u is the leftmost  node of e  (min X among sources ∪ targets)
    //   pos = 2  ->  u is the rightmost node of e  (max X among sources ∪ targets)
    //   pos = 1  ->  u is in between
    //
    //  When pos(u, e1) != pos(u, e2):
    //   A node that is the rightmost of its hyperedge receives the leftmost port
    //   on the generating node, and vice-versa. This minimises crossings. So, the
    //   ordering of ports will be performed by descending pos(u, e) value.
    //
    // Tie-breaking when pos(u, e1) == pos(u, e2):
    //
    //   pos = 2 (both edges reach their rightmost node here — ports cluster left):
    //     Source ports: higher hyperedge (lower index) goes first (leftmost port).
    //     Target ports: lower  hyperedge (higher index) goes first.
    //
    //   pos = 1 (middle nodes, order has little impact):
    //     Same rule as pos = 2.
    //
    //   pos = 0 (both edges reach their leftmost node here — ports cluster right):
    //     Source ports: higher hyperedge (lower index) goes last  (rightmost port).
    //     Target ports: lower  hyperedge (higher index) goes last.
    //
    // This is actually the port ordering policy described in the paper.

    void PortAssigner::orderPorts(Node* node, std::vector<Port>& ports, bool source) const {
        if (ports.empty()) return;
        std::sort(ports.begin(), ports.end(),
            [this, node, source](const Port& a, const Port& b) {
                int pos_a = nodePositionInEdge(a.edge, node);
                int pos_b = nodePositionInEdge(b.edge, node);
                if (pos_a != pos_b) return pos_a > pos_b;

                size_t size_a = 0;
                size_t size_b = 0;
                switch (pos_a) {
                case 2:
                    // If they both draw in position, it might be the case that one of the
                    // edges has two nodes as rightmost and the other has only one, so we
                    // break the tie giving priority to the one with fewer rightmost nodes,
                    // which is more likely to cause crossings if placed in the middle.
                    size_a = rightmost_nodes_.at(a.edge).size();
                    size_b = rightmost_nodes_.at(b.edge).size();
                    if (size_a != size_b) {
                        return size_a < size_b;
                    }
                    // If they have the same number, the tie is broken by the hyperedge order, as
                    // executed bellow for pos = 1.
                case 1:
                    return source ? hyperedge_order_.at(a.edge) < hyperedge_order_.at(b.edge)
                        : hyperedge_order_.at(a.edge) > hyperedge_order_.at(b.edge);
                case 0:
                    // The same works for leftmost nodes, but in reverse: the one with fewer leftmost
                    // nodes is more likely to cause crossings if placed in the middle.
                    size_a = leftmost_nodes_.at(a.edge).size();
                    size_b = leftmost_nodes_.at(b.edge).size();
                    if (size_a != size_b) {
                        return size_a > size_b;
                    }
                    return source ? hyperedge_order_.at(a.edge) > hyperedge_order_.at(b.edge)
                        : hyperedge_order_.at(a.edge) < hyperedge_order_.at(b.edge);
                default: return false;
                }
            });
    }


    // ── Port spacing ──────────────────────────────────────────────────────────────
    //
    // Ports are distributed evenly across the node's horizontal span.
    // With n ports, spacing = node_width / (n + 1), so the i-th port sits at
    //   min_x + spacing * (i + 1) and no port ever coincides with a node boundary.
    // Returns MIN_VERTICAL_SEP when there are no ports or the spacing of the node.

    double PortAssigner::arrangeSymmetrically(Node* node, std::vector<Port>& ports) const {
        int n = static_cast<int>(ports.size());
        if (n == 0) return MIN_VERTICAL_SEP;
        double node_width = node->isDummy() ? DUMMY_NODE_WIDTH : NODE_WIDTH;
        double node_x = node_layout_.at(node).x;
        double spacing = node_width / (n + 1);
        double min_x = node_x - node_width / 2.0;
        for (int i = 0; i < n; ++i)
            ports[i].x = min_x + spacing * (i + 1);
        if (node->isDummy()) return MIN_VERTICAL_SEP;
        return spacing;
    }

    // True iff moving this node's port toward 'other_x' would have to leapfrog
    // its own same-node neighbour in that direction -- i.e. that neighbour is
    // already sitting between us and where we'd be aligning to.
    static bool wouldCrossNeighbour(Node* node, const std::vector<Port>& ports, int idx,
        double node_x, double other_x, bool moving_right)
    {
        if (node->isDummy()) return false; // a dummy has only one port; nothing to cross
        if (moving_right) {
            double next_port = (idx == static_cast<int>(ports.size()) - 1) ? node_x + NODE_WIDTH / 2.0 : ports[idx + 1].x;
            return next_port < other_x;
        }
        double prev_port = (idx == 0) ? node_x - NODE_WIDTH / 2.0 : ports[idx - 1].x;
        return prev_port > other_x;
    }

    // ── Reduce horizontal jogs ──────────────────────────────────────────────────────────────
    //
    // For hyperedges consisting of just one source and one target or with two nodes sharing the
    // rightmost or leftmost position, which ports can be aligned without creating a conflict with 
    // the neighbour ports, it is desirable to align them to prevent from creating an unnecessary 
    // horizontal jog. This gives the hypergraph a cleaner look.
    double PortAssigner::reduceHorizontalJogs() const {
        double min_spacing = MIN_VERTICAL_SEP;

        // The following lambda tries to align one specific source port and one specific
        // target port of the same edge by moving both to the midpoint of the intersection of
        // their corresponding spans. The span of a port is the horizontal interval where it
        // can be moved without creating a conflict with its neighbour ports.
        auto alignPort = [&](Node* src_node, Node* tgt_node,
            std::vector<Port>& src_ports, int si,
            std::vector<Port>& tgt_ports, int ti) -> bool {
                double src_x = node_layout_.at(src_node).x;
                double tgt_x = node_layout_.at(tgt_node).x;

                if (src_node->isDummy() && tgt_node->isDummy()) return false;

                // Let's see if there is any port in between the two. If there is
                // we don't align them but if there isn't we will align them.
                if (src_ports[si].x != tgt_ports[ti].x) {
                    bool src_moving_right = src_ports[si].x < tgt_ports[ti].x;
                    if (wouldCrossNeighbour(src_node, src_ports, si, src_x, tgt_ports[ti].x, src_moving_right)) return false;
                    if (wouldCrossNeighbour(tgt_node, tgt_ports, ti, tgt_x, src_ports[si].x, !src_moving_right)) return false;
                }
                else {
                    return true; // Already aligned
                }

                std::pair<double, double> src_span, tgt_span;

                if (src_node->isDummy()) {
                    src_span = { src_ports[si].x, src_ports[si].x };
                }
                else {
                    src_span.first = (si == 0)
                        ? src_x - NODE_WIDTH / 2.0 : src_ports[si - 1].x;
                    src_span.second = (si == static_cast<int>(src_ports.size()) - 1)
                        ? src_x + NODE_WIDTH / 2.0 : src_ports[si + 1].x;
                }

                if (tgt_node->isDummy()) {
                    tgt_span = { tgt_ports[ti].x, tgt_ports[ti].x };
                }
                else {
                    tgt_span.first = (ti == 0)
                        ? tgt_x - NODE_WIDTH / 2.0 : tgt_ports[ti - 1].x;
                    tgt_span.second = (ti == static_cast<int>(tgt_ports.size()) - 1)
                        ? tgt_x + NODE_WIDTH / 2.0 : tgt_ports[ti + 1].x;
                }

                double inter_lo = std::max(src_span.first, tgt_span.first);
                double inter_hi = std::min(src_span.second, tgt_span.second);
                if (inter_lo > inter_hi) return false;

                double new_x = (inter_lo + inter_hi) / 2.0;
                src_ports[si].x = new_x;
                tgt_ports[ti].x = new_x;

                return true;
            };

        auto findPortIndex = [](const std::vector<Port>& ports, const Hyperedge* edge) {
            return static_cast<int>(std::find_if(ports.begin(), ports.end(),
                [edge](const Port& p) { return p.edge == edge; }) - ports.begin());
            };

        // We loop over the edges, there are three cases to consider:
        //  1) The edge has only one source or one target, so we can try some alignment.
        //  2) The edge has two leftmost nodes, one source and one target, so we can align those two ports.
        //  3) The edge has two rightmost nodes, one source and one target, so we can align those two ports.
        // 
        // We will also keep track of the adjusted ports to arrange them symmetrically at the end.
        std::unordered_map<Node*, std::unordered_set<Port*>> adjusted_src;
        std::unordered_map<Node*, std::unordered_set<Port*>> adjusted_tgt;
        for (const auto& edge : upper_.outgoing_edges) {
            Hyperedge* e = edge.get();

            const auto& lv = leftmost_nodes_.at(e);
            const auto& rv = rightmost_nodes_.at(e);

            // Case 1: one source, one target.
            if (edge->getSources().size() == 1 || edge->getTargets().size() == 1) {
                Node* src_node = nullptr, * tgt_node = nullptr;
                std::vector<Port>* src_ports = nullptr, * tgt_ports = nullptr;
                int si = 0, ti = 0;

                if (edge->getSources().size() == 1) {
                    src_node = edge->getSources()[0].get();
                    src_ports = &node_layout_.at(src_node).source_ports;
                    si = findPortIndex(*src_ports, e);
                }
                if (edge->getTargets().size() == 1) {
                    tgt_node = edge->getTargets()[0].get();
                    tgt_ports = &node_layout_.at(tgt_node).target_ports;
                    ti = findPortIndex(*tgt_ports, e);
                }
                if (!tgt_node && src_ports->size() == 1) {
                    for (const auto& t : edge->getTargets()) {
                        if (std::abs(node_layout_.at(t.get()).x - src_ports->front().x) >= MIN_BLOCK_SEP * 0.5) continue;
                        tgt_node = t.get();
                        tgt_ports = &node_layout_.at(tgt_node).target_ports;
                        ti = findPortIndex(*tgt_ports, e);
                        break; // Only one target can meet the distance requirement
                    }
                }
                if (!src_node && tgt_ports->size() == 1) {
                    for (const auto& s : edge->getSources()) {
                        if (std::abs(node_layout_.at(s.get()).x - tgt_ports->front().x) >= 3 * MIN_BLOCK_SEP * 0.5) continue;
                        src_node = s.get();
                        src_ports = &node_layout_.at(src_node).source_ports;
                        si = findPortIndex(*src_ports, e);
                        break; // Only one target can meet the distance requirement
                    }
                }

                if (src_node && tgt_node) {
                    if (alignPort(src_node, tgt_node, *src_ports, si, *tgt_ports, ti)) {
                        adjusted_src[src_node].insert(&(*src_ports)[si]);
                        adjusted_tgt[tgt_node].insert(&(*tgt_ports)[ti]);
                    }
                    continue;
                }
            }

            // Case 2: two leftmost nodes.
            if (lv.size() == 2) {
                bool front_is_src = edge->containsSource(lv.front()->shared_from_this());
                Node* src_node = front_is_src ? lv.front() : lv.back();
                Node* tgt_node = front_is_src ? lv.back() : lv.front();
                auto& src_ports = node_layout_.at(src_node).source_ports;
                auto& tgt_ports = node_layout_.at(tgt_node).target_ports;
                int si = findPortIndex(src_ports, e);
                int ti = findPortIndex(tgt_ports, e);
                if (alignPort(src_node, tgt_node, src_ports, si, tgt_ports, ti)) {
                    adjusted_src[src_node].insert(&src_ports[si]);
                    adjusted_tgt[tgt_node].insert(&tgt_ports[ti]);
                }
            }

            // Case 3: two rightmost nodes.
            if (rv.size() == 2) {
                bool front_is_src = edge->containsSource(rv.front()->shared_from_this());
                Node* src_node = front_is_src ? rv.front() : rv.back();
                Node* tgt_node = front_is_src ? rv.back() : rv.front();
                auto& src_ports = node_layout_.at(src_node).source_ports;
                auto& tgt_ports = node_layout_.at(tgt_node).target_ports;
                int si = findPortIndex(src_ports, e);
                int ti = findPortIndex(tgt_ports, e);
                if (alignPort(src_node, tgt_node, src_ports, si, tgt_ports, ti)) {
                    adjusted_src[src_node].insert(&src_ports[si]);
                    adjusted_tgt[tgt_node].insert(&tgt_ports[ti]);
                }
            }
        }

        // Redistribute symetrically the free ports.
        for (auto& [node, fixed] : adjusted_src)
            min_spacing = std::min(min_spacing,
                redistributePorts(node_layout_.at(node).source_ports, fixed,
                    node_layout_.at(node).x));
        for (auto& [node, fixed] : adjusted_tgt)
            min_spacing = std::min(min_spacing,
                redistributePorts(node_layout_.at(node).target_ports, fixed,
                    node_layout_.at(node).x));

        return min_spacing;
    }

    double PortAssigner::redistributePorts(std::vector<Port>& ports,
        const std::unordered_set<Port*>& fixed,
        double node_x) const
    {
        if (static_cast<int>(ports.size()) <= 1) return MIN_VERTICAL_SEP;

        double left_boundary = node_x - NODE_WIDTH / 2.0;
        double right_boundary = node_x + NODE_WIDTH / 2.0;
        double min_spacing = MIN_VERTICAL_SEP;

        // Collect the x coordinate of the anchors: fixed ports and node boundaries.
        // The spacing between the gaps defined by these anchors will be used to 
        // redistribute the free ports evenly.
        std::vector<double> anchors;
        anchors.push_back(left_boundary);
        for (Port& p : ports)
            if (fixed.count(&p)) anchors.push_back(p.x);
        anchors.push_back(right_boundary);

        int anchor_idx = 0;
        std::vector<Port*> current_gap;

        auto populateGap = [&]() {
            if (current_gap.empty()) return;
            double lo = anchors[anchor_idx];
            double hi = anchors[anchor_idx + 1];
            int    k = static_cast<int>(current_gap.size());
            double spacing = (hi - lo) / (k + 1);
            min_spacing = std::min(min_spacing, spacing);
            for (int idx = 0; idx < k; ++idx)
                current_gap[idx]->x = lo + spacing * (idx + 1);
            current_gap.clear();
            };

        // Ports are already sorted by ascending x coordinate, so we can just loop over
        // them and collect the free ports in the current gap until we hit a fixed port,
        // at which point we populate the gap.
        for (Port& p : ports) {
            if (fixed.count(&p)) {
                populateGap();
                ++anchor_idx;
            }
            else {
                current_gap.push_back(&p);
            }
        }
        populateGap(); // handle any trailing free ports after the last fixed anchor
        return min_spacing;
    }


    // ── buildPorts ────────────────────────────────────────────────────────────────
    //
    // Main entry point for port assignment on a single layer pair.
    // Steps:
    //   1. Register a placeholder port (x = 0) on every incident node.
    //   2. Order ports according to the crossing-minimisation policy.
    //   3. Assign actual x coordinates evenly across each node's span.
    //
    // Returns the minimum port spacing produced across all nodes in the pair,
    // which determines the minimum spacing required between the upper and lower 
    // layers to consider vertical overlapping conflicts.

    double PortAssigner::buildPorts() {
        for (const auto& edge : upper_.outgoing_edges) {
            for (const auto& src : edge->getSources())
                node_layout_[src.get()].source_ports.push_back({ edge.get(), 0.0 });
            for (const auto& tgt : edge->getTargets())
                node_layout_[tgt.get()].target_ports.push_back({ edge.get(), 0.0 });
        }

        for (const auto& node : upper_.nodes)
            orderPorts(node.get(), node_layout_[node.get()].source_ports, true);
        for (const auto& node : lower_.nodes)
            orderPorts(node.get(), node_layout_[node.get()].target_ports, false);

        double min_spacing = MIN_VERTICAL_SEP;
        for (const auto& node : upper_.nodes)
            min_spacing = std::min(min_spacing, arrangeSymmetrically(node.get(), node_layout_[node.get()].source_ports));
        for (const auto& node : lower_.nodes)
            min_spacing = std::min(min_spacing, arrangeSymmetrically(node.get(), node_layout_[node.get()].target_ports));

        return min_spacing;
    }

    // ── Conflict detection ────────────────────────────────────────────────────────
    //
    // We sweep two pointers over the upper and lower node lists (both sorted by
    // ascending x) and flag pairs whose boundary ports are closer than
    // min_vertical_sep. This is a necessary (but not sufficient) condition for a
    // vertical-segment overlap. The actual check happens in solveConflict.
    //
    // Three cases per step:
    //   xi.x < yj.x -> the rightmost source port of xi might be too close to the
    //                  leftmost target port of yj. It could also happen that the
    //                  ports span of xi and yj overlap.                    
    //   xi.x > yj.x -> the leftmost source port of xi might be too close to the
    //                  rightmost target port of yj. It could also happen that
    // 				    the ports span of xi and yj overlap.
    //   xi.x == yj.x -> always a potential conflict. Advance the narrower node
    //                   (or both if widths are equal).

    std::vector<std::pair<Node*, Node*>>
        PortAssigner::detectConflicts(double min_vertical_sep) const {
        std::vector<std::pair<Node*, Node*>> conflicts;
        int i = 0, j = 0;
        int nu = static_cast<int>(upper_.nodes.size());
        int nl = static_cast<int>(lower_.nodes.size());

        while (i < nu && j < nl) {
            const NodeLayout& xi = node_layout_.at(upper_.nodes[i].get());
            const NodeLayout& yj = node_layout_.at(lower_.nodes[j].get());

            if (xi.x < yj.x) {
                if (!xi.source_ports.empty() && !yj.target_ports.empty())
                    if (yj.target_ports.front().x - xi.source_ports.back().x < min_vertical_sep)
                        conflicts.emplace_back(upper_.nodes[i].get(), lower_.nodes[j].get());
                i++;
            }
            else if (xi.x > yj.x) {
                if (!xi.source_ports.empty() && !yj.target_ports.empty())
                    if (xi.source_ports.front().x - yj.target_ports.back().x < min_vertical_sep)
                        conflicts.emplace_back(upper_.nodes[i].get(), lower_.nodes[j].get());
                j++;
            }
            else {
                if (!xi.source_ports.empty() && !yj.target_ports.empty())
                    conflicts.emplace_back(upper_.nodes[i].get(), lower_.nodes[j].get());
                double wi = upper_.nodes[i]->isDummy() ? DUMMY_NODE_WIDTH : NODE_WIDTH;
                double wj = lower_.nodes[j]->isDummy() ? DUMMY_NODE_WIDTH : NODE_WIDTH;
                if (wi < wj) i++;
                else if (wi > wj) j++;
                else { i++; j++; }
            }
        }
        return conflicts;
    }


    // ── shiftWithFixedPort ────────────────────────────────────────────────────────
    //
    // Shifts 'moving_port' left (left=true) or right (left=false) relative to
    // 'fixed_x' using the barycentric ratio:
    //
    //   new_x = fixed_x - min(gap_to_left_neighbour / 3, min_sep)   [left case]
    //   new_x = fixed_x + min(gap_to_right_neighbour / 3, min_sep)  [right case]
    //
    // The neighbour is the adjacent port in the direction of movement, or the node
    // boundary when moving_port is the outermost port on that side.
    //
    // When the node has only one port there is no neighbour, so the port is placed
    // at the midpoint between fixed_x and the corresponding node boundary. We move
    // the barycentric or mid point depending on the number of brother ports to avoid
    // having very unbalanced spacings between the many ports.
    //
    // NOTE: This function is only called when the moving node is a real node, so
    // NODE_WIDTH is always the correct width to use here.

    void PortAssigner::shiftWithFixedPort(double fixed_x, Node* moving_node,
        Port& moving_port, int port_index,
        const std::vector<Port>& ports,
        double min_sep, bool left) const
    {
        double node_x = node_layout_.at(moving_node).x;

        if (left) {
            double neighbour_x = (port_index == 0)
                ? node_x - NODE_WIDTH / 2.0
                : ports[port_index - 1].x;
            moving_port.x = (ports.size() == 1)
                ? (neighbour_x + fixed_x) / 2.0
                : fixed_x - std::min((fixed_x - neighbour_x) / 3.0, min_sep);
        }
        else {
            double neighbour_x = (port_index == static_cast<int>(ports.size()) - 1)
                ? node_x + NODE_WIDTH / 2.0
                : ports[port_index + 1].x;
            moving_port.x = (ports.size() == 1)
                ? (fixed_x + neighbour_x) / 2.0
                : fixed_x + std::min((neighbour_x - fixed_x) / 3.0, min_sep);
        }
    }


    // ── Edge-topology helpers ─────────────────────────────────────────────────────

    static bool isLeftmost(Node* node, Hyperedge* edge,
        const std::unordered_map<Hyperedge*, std::vector<Node*>>& leftmost_nodes)
    {
        const auto& v = leftmost_nodes.at(edge);
        return std::find(v.begin(), v.end(), node) != v.end();
    }

    static bool isRightmost(Node* node, Hyperedge* edge,
        const std::unordered_map<Hyperedge*, std::vector<Node*>>& rightmost_nodes)
    {
        const auto& v = rightmost_nodes.at(edge);
        return std::find(v.begin(), v.end(), node) != v.end();
    }


    // ── rearrangeConflictingPorts ─────────────────────────────────────────────────
    //
    // Given the contiguous ranges of conflicting ports in the upper and lower port
    // lists, redistributes them to eliminate the overlap. We avoid moving dummy ports
    // to avoid having horizontal jogs in the dummy chains, which would not be very 
    // aesthetic. Three cases (a fourth, both dummy, is handled elsewhere -- see
    // below):
    //
    //   Upper dummy -> upper port is fixed. Move the one or two conflicting
    //                  lower ports using shiftWithFixedPort.
    //
    //   Lower dummy -> symmetric: lower port is fixed, move upper port(s).
    //
    //   Neither dummy -> merge all conflicting ports from both nodes into a
    //                    single sorted list using edge-topology tie-breaking,
    //                    then redistribute them evenly over a computed interval.

    void PortAssigner::rearrangeConflictingPorts(
        Node* upper_node, Node* lower_node,
        std::vector<Port>& upper_ports, std::vector<Port>& lower_ports,
        std::pair<int, int> upper_range, std::pair<int, int> lower_range,
        double min_sep)
    {
        // ── Both dummy ────────────────────────────────────────────────────────────
        // Dummy-to-dummy conflicts are fully resolved by straightenDummyChains(), 
        // which runs before this pass and moves entire chains as rigid units.
        if (upper_node->isDummy() && lower_node->isDummy()) return;

        // ── Upper dummy: keep upper port fixed, move lower port(s) ───────────────
        // The dummy part can only have one port, so upper_range is useless.
        if (upper_node->isDummy()) {
            double fixed_x = upper_ports[0].x;
            if (lower_range.first == lower_range.second) {
                int idy = lower_range.first;
                Port& lp = lower_ports[idy];
                if (fixed_x < lp.x) shiftWithFixedPort(fixed_x, lower_node, lp, idy, lower_ports, min_sep, false);
                else if (fixed_x > lp.x) shiftWithFixedPort(fixed_x, lower_node, lp, idy, lower_ports, min_sep, true);
                else {
                    // Use edge topology to pick direction when exactly coincident.
                    Hyperedge* le = lp.edge;
                    bool go_left = true;
                    if (isLeftmost(lower_node, le, leftmost_nodes_)) go_left = false;
                    else if (!isRightmost(lower_node, le, rightmost_nodes_)) {
                        Hyperedge* ue = upper_ports[0].edge;
                        go_left = isRightmost(upper_node, ue, rightmost_nodes_);
                    }
                    shiftWithFixedPort(fixed_x, lower_node, lp, idy, lower_ports, min_sep, go_left);
                }
            }
            else {
                // Upper port is strictly between two conflicting lower ports.
                shiftWithFixedPort(fixed_x, lower_node, lower_ports[lower_range.first], lower_range.first, lower_ports, min_sep, true);
                shiftWithFixedPort(fixed_x, lower_node, lower_ports[lower_range.second], lower_range.second, lower_ports, min_sep, false);
            }
            return;
        }

        // ── Lower dummy: symmetric — keep lower port fixed, move upper port(s) ───
        // The dummy part can only have one port, so lower_range is useless.
        if (lower_node->isDummy()) {
            double fixed_x = lower_ports[0].x;
            if (upper_range.first == upper_range.second) {
                int idx = upper_range.first;
                Port& up = upper_ports[idx];
                if (fixed_x < up.x) shiftWithFixedPort(fixed_x, upper_node, up, idx, upper_ports, min_sep, false);
                else if (fixed_x > up.x) shiftWithFixedPort(fixed_x, upper_node, up, idx, upper_ports, min_sep, true);
                else {
                    Hyperedge* le = lower_ports[0].edge;
                    bool go_left = true;
                    if (isLeftmost(lower_node, le, leftmost_nodes_)) go_left = false;
                    else if (!isRightmost(lower_node, le, rightmost_nodes_)) {
                        Hyperedge* ue = up.edge;
                        go_left = isRightmost(upper_node, ue, rightmost_nodes_);
                    }
                    shiftWithFixedPort(fixed_x, upper_node, up, idx, upper_ports, min_sep, go_left);
                }

            }
            else {
                shiftWithFixedPort(fixed_x, upper_node, upper_ports[upper_range.first], upper_range.first, upper_ports, min_sep, true);
                shiftWithFixedPort(fixed_x, upper_node, upper_ports[upper_range.second], upper_range.second, upper_ports, min_sep, false);
            }
            return;
        }

        // ── Neither dummy: merge and redistribute ─────────────────────────────────
        //
        // We merge the conflicting port sub-ranges from both nodes into a single
        // list sorted by x. Ties are broken using edge topology so that the
        // relative order that minimises crossings is preserved.
        // The merged list is then redistributed evenly over the interval
        // [leftBound, rightBound], where each bound is placed 1/3 of the way
        // into the gap between the outermost conflicting port and its neighbour
        // (or the node boundary if there is no neighbour).
        std::vector<std::pair<Port*, bool>> merged; // The boolean determines whether the port is upper (true) or lower (false).

        int i = upper_range.first, j = lower_range.first;
        while (i <= upper_range.second || j <= lower_range.second) {
            bool exhausted_upper = (i > upper_range.second);
            bool exhausted_lower = (j > lower_range.second);

            if (exhausted_upper && exhausted_lower) break; // Both ranges exhausted, we finish.
            else  if (!exhausted_upper && (exhausted_lower || upper_ports[i].x < lower_ports[j].x)) {
                merged.push_back({ &upper_ports[i++], true });
            }
            else if (!exhausted_lower && (exhausted_upper || lower_ports[j].x < upper_ports[i].x)) {
                merged.push_back({ &lower_ports[j++], false });
            }
            else {
                // Tied x: resolve by edge topology.
                Hyperedge* le = lower_ports[j].edge;
                if (isLeftmost(lower_node, le, leftmost_nodes_)) {
                    merged.push_back({ &upper_ports[i++], true });
                    merged.push_back({ &lower_ports[j++], false });
                }
                else if (isRightmost(lower_node, le, rightmost_nodes_)) {
                    merged.push_back({ &lower_ports[j++], false });
                    merged.push_back({ &upper_ports[i++], true });
                }
                else {
                    Hyperedge* ue = upper_ports[i].edge;
                    if (isLeftmost(upper_node, ue, leftmost_nodes_)) {
                        merged.push_back({ &lower_ports[j++], false });
                        merged.push_back({ &upper_ports[i++], true });
                    }
                    else {
                        merged.push_back({ &upper_ports[i++], true });
                        merged.push_back({ &lower_ports[j++], false });
                    }
                }
            }
        }
        double min_x = leftBound(upper_node, lower_node, upper_ports[upper_range.first], lower_ports[lower_range.first],
            upper_ports, lower_ports, upper_range.first, lower_range.first, min_sep, merged);

        double max_x = rightBound(upper_node, lower_node, upper_ports[upper_range.second], lower_ports[lower_range.second],
            upper_ports, lower_ports, upper_range.second, lower_range.second, min_sep, merged);

        int count = static_cast<int>(merged.size());
        if (count == 0) return; // No ports left to locate, we finish.
        else if (count == 1) {
            merged[0].first->x = (min_x + max_x) / 2.0;
            return;
        }
        double spacing = (max_x - min_x) / static_cast<double>((count - 1));
        for (int k = 0; k < count; ++k)
            merged[k].first->x = min_x + k * spacing;
    }

    // Let x_o be the leftmost conflicting upper port and y_o its analogue. In this sense,
    // let x_-1 be the previous upper port or node boundary and y_-1 the previous lower port.
    // Then,
    // - If x_-1 < y_-1, then there are two cases:
    //  1) If pos(x_o) < pos(y_o), we locate x_-1 independently at x_o - (x_o - x_-1) / 3 if no new
    //     conflict is created, otherwise set the left merge bound (min_x) at x_o - (x_o - y_-1) / 3.
    //  2) If pos(x_o) > pos(y_o), the left merge bound is located at y_o - (y_o - y_-1) / 3.
    // - If x_-1 > y_-1, there are also two cases:
    //  1) If pos(x_o) > pos(y_o), we locate y_-1 independently at y_o - (y_o - y_-1) / 3 if no new
    //      conflict is created, otherwise set the left merge bound (min_x) at y_o - (y_o - x_-1) / 3.
    //  2) If pos(x_o) < pos(y_o), the left merge bound is located at x_o - (x_o - x_-1) / 3.
    // - If x_-1 == y_-1, we set the left merge bound at min{x_o - (x_o - x_-1) / 3, y_o - (y_o - y_-1) / 3}.
    // Here, pos(x_o) and pos(y_o) are the positions in the merged list, different to pos(node, edge).

    double PortAssigner::leftBound(Node* upper_node, Node* lower_node, Port& x_o, Port& y_o,
        std::vector<Port>& upper_ports, std::vector<Port>& lower_ports,
        int idx, int idy, double min_sep, std::vector<std::pair<Port*, bool>>& merged) {
        double left_upper_bound = (idx == 0) ? node_layout_.at(upper_node).x - NODE_WIDTH / 2.0 : upper_ports[idx - 1].x;
        double left_lower_bound = (idy == 0) ? node_layout_.at(lower_node).x - NODE_WIDTH / 2.0 : lower_ports[idy - 1].x;
        if (left_upper_bound < left_lower_bound) { // x_-1 < y_-1
            if (merged.front().second) { // pos(x_o) < pos(y_o)
                double desired_x = x_o.x - std::min((x_o.x - left_upper_bound) / 3.0, min_sep);
                if ((idy == 0) || (std::abs(desired_x - lower_ports[idy - 1].x) >= min_sep) ||
                    (hyperedge_order_.at(x_o.edge) <= hyperedge_order_.at(lower_ports[idy - 1].edge))) {
                    x_o.x = desired_x;
                    merged.erase(merged.begin()); // We have already placed the leftmost upper port, so we remove it from the list.
                }
                else {
                    return (2.0 * x_o.x + left_lower_bound) / 3.0; // It is the case that x_o must be in between.
                }
            }
            return (2.0 * y_o.x + left_lower_bound) / 3.0;
        }
        else if (left_upper_bound > left_lower_bound) {
            if (!merged.front().second) { // pos(x_o) > pos(y_o)
                double desired_y = y_o.x - std::min((y_o.x - left_lower_bound) / 3.0, min_sep);
                if ((idx == 0) || (std::abs(desired_y - upper_ports[idx - 1].x) >= min_sep) ||
                    (hyperedge_order_.at(y_o.edge) >= hyperedge_order_.at(upper_ports[idx - 1].edge))) {
                    y_o.x = desired_y;
                    merged.erase(merged.begin()); // We have already placed the leftmost lower port, so we remove it from the list.
                }
                else {
                    return (2.0 * y_o.x + left_upper_bound) / 3.0; // It is the case that y_o must be in between.
                }
            }
            return (2.0 * x_o.x + left_upper_bound) / 3.0;
        }
        return std::min((2.0 * x_o.x + left_upper_bound) / 3.0, (2.0 * y_o.x + left_lower_bound) / 3.0);
    }

    // Similarly for the right bound, but looking at the next ports x_n+1 and y_m+1 instead of the previous ones.
    double PortAssigner::rightBound(Node* upper_node, Node* lower_node, Port& x_n, Port& y_m,
        std::vector<Port>& upper_ports, std::vector<Port>& lower_ports,
        int idx, int idy, double min_sep, std::vector<std::pair<Port*, bool>>& merged) {
        int n = static_cast<int>(upper_ports.size()) - 1;
        int m = static_cast<int>(lower_ports.size()) - 1;
        double right_upper_bound = (idx == n) ? node_layout_.at(upper_node).x + NODE_WIDTH / 2.0 : upper_ports[idx + 1].x;
        double right_lower_bound = (idy == m) ? node_layout_.at(lower_node).x + NODE_WIDTH / 2.0 : lower_ports[idy + 1].x;

        if (right_upper_bound < right_lower_bound) { // x_n+1 < y_m+1
            if (!merged.back().second) { // pos(x_n) < pos(y_m)
                double desired_y = y_m.x + std::min((right_lower_bound - y_m.x) / 3.0, min_sep);
                if ((idx == n) || (std::abs(desired_y - upper_ports[idx + 1].x) >= min_sep) ||
                    (hyperedge_order_.at(y_m.edge) >= hyperedge_order_.at(upper_ports[idx + 1].edge))) {
                    y_m.x = desired_y;
                    merged.pop_back(); // We have already placed the rightmost lower port, so we remove it from the list.
                }
                else {
                    return (2.0 * y_m.x + right_upper_bound) / 3.0; // It is the case that y_m is in between
                }
            }
            return (2.0 * x_n.x + right_upper_bound) / 3.0;
        }
        else if (right_upper_bound > right_lower_bound) {
            if (merged.back().second) { // pos(x_n) > pos(y_m)
                double desired_x = x_n.x + std::min((right_upper_bound - x_n.x) / 3.0, min_sep);
                if ((idy == m) || (std::abs(desired_x - lower_ports[idy + 1].x) >= min_sep) ||
                    (hyperedge_order_.at(x_n.edge) <= hyperedge_order_.at(lower_ports[idy + 1].edge))) {
                    x_n.x = desired_x;
                    merged.pop_back(); // We have already placed the rightmost upper port, so we remove it from the list.
                }
                else {
                    return (2.0 * x_n.x + right_lower_bound) / 3.0; // It is the case that x_n is in between.
                }
            }
            return (2.0 * y_m.x + right_lower_bound) / 3.0;
        }
        return std::max((2.0 * x_n.x + right_upper_bound) / 3.0, (2.0 * y_m.x + right_lower_bound) / 3.0);
    }



    // ── solveConflict ─────────────────────────────────────────────────────────────
    //
    // Identifies the precise conflicting port index ranges within one upper/lower
    // node pair and delegates to rearrangeConflictingPorts.
    //
    // A conflict between port i (upper) and port j (lower) exists when:
    //  - their x-coordinates are closer than min_sep, and
    //  - the hyperedge order implies a crossing (upper order index > lower order index).
    //
    // The sweep advances the pointer on the side with the smaller port x, to visit every 
    // relevant (i, j) pair exactly once.

    void PortAssigner::solveConflict(Node* upper_node, Node* lower_node, double min_sep) {
        std::vector<Port>& upper_ports = node_layout_[upper_node].source_ports;
        std::vector<Port>& lower_ports = node_layout_[lower_node].target_ports;

        int s = static_cast<int>(upper_ports.size());
        int r = static_cast<int>(lower_ports.size());

        int min_ui = INT_MAX, max_ui = INT_MIN;
        int min_li = INT_MAX, max_li = INT_MIN;

        int i = 0, j = 0;
        while (i < s && j < r) {
            if (std::abs(upper_ports[i].x - lower_ports[j].x) < min_sep &&
                hyperedge_order_.at(upper_ports[i].edge) > hyperedge_order_.at(lower_ports[j].edge)) {
                min_ui = std::min(min_ui, i); max_ui = std::max(max_ui, i);
                min_li = std::min(min_li, j); max_li = std::max(max_li, j);
            }

            if (upper_ports[i].x < lower_ports[j].x) i++;
            else if (upper_ports[i].x > lower_ports[j].x) j++;
            else { i++; j++; }
        }

        if (min_ui == INT_MAX) return; // No conflict found.

        rearrangeConflictingPorts(upper_node, lower_node,
            upper_ports, lower_ports,
            { min_ui, max_ui }, { min_li, max_li },
            min_sep);
    }


    // ── solveVerticalOverlaps ─────────────────────────────────────────────────────

    void PortAssigner::solveVerticalOverlaps(double min_vertical_sep) {
        if (upper_.outgoing_edges.empty()) return;
        for (auto& [upper_node, lower_node] : detectConflicts(min_vertical_sep))
            solveConflict(upper_node, lower_node, min_vertical_sep);
    }


    // ============================================================================
    // Dummy chain straightening
    // ============================================================================

    // ── DummyChain ────────────────────────────────────────────────────────────

    struct DummyChain {
        std::vector<Node*> members;   // top (shallowest) -> bottom (deepest)
        int top_layer = -1;
        int bottom_layer = -1;
    };

    static bool isChainLink(Node* a, Node* b) {
        if (!a->isDummy() || !b->isDummy()) return false;
        auto a_children = a->getChildren();
        if (a_children.size() != 1 || a_children[0].get() != b) return false;
        auto b_parents = b->getParents();
        if (b_parents.size() != 1 || b_parents[0].get() != a) return false;
        return true;
    }

    // A dummy with more than one child (a branch point) or more than one
    // parent (a merge point) can never be a chain-link's continuation, so it
    // always ends up as, respectively, the bottom or the top of its own chain.
    static std::vector<DummyChain> findDummyChains(const std::map<int, LayerData>& layers) {
        std::vector<DummyChain> chains;
        std::unordered_set<Node*> visited;

        for (const auto& [layer, data] : layers) {
            for (const auto& node_ptr : data.nodes) {
                Node* d = node_ptr.get();
                if (!d->isDummy() || visited.count(d)) continue;

                // We found the start of a new chain
                DummyChain chain;
                Node* current = d;
                while (true) {
                    chain.members.push_back(current);
                    visited.insert(current);
                    auto children = current->getChildren();
                    if (children.size() != 1 || !isChainLink(current, children[0].get())) break;
                    current = children[0].get();
                }
                chain.top_layer = chain.members.front()->getLayer();
                chain.bottom_layer = chain.members.back()->getLayer();
                chains.push_back(std::move(chain));
            }
        }
        return chains;
    }

    // ── Interval helpers ──────────────────────────────────────────────────────
    //
    // A chain's feasible x-range is a sorted list of disjoint [lo, hi] pieces:
    // the node-separation window with every "conflicting region" (a real port's
    // [-min_sep, +min_sep] buffer) punched out of it.

    using Interval = std::pair<double, double>;

    static std::vector<Interval> normalize(std::vector<Interval> v, bool closed) {
        v.erase(std::remove_if(v.begin(), v.end(),
            [closed](const Interval& x) { return closed ? x.first > x.second : x.first >= x.second; }), v.end());
        std::sort(v.begin(), v.end());
        std::vector<Interval> out;
        for (const auto& x : v)
            if (!out.empty() && (closed ? x.first <= out.back().second : x.first < out.back().second))
                out.back().second = std::max(out.back().second, x.second);
            else
                out.push_back(x);
        return out;
    }

    static std::vector<Interval> subtractRegions(Interval window, std::vector<Interval> forbidden) {
        std::vector<Interval> result;
        if (window.first > window.second) return result;
        double cursor = window.first;
        for (const auto& [flo, fhi] : normalize(std::move(forbidden), false)) {
            if (fhi <= window.first) continue;
            if (flo >= window.second) break;
            if (flo >= cursor) result.push_back({ cursor, flo });
            cursor = fhi;
        }
        if (cursor <= window.second) result.push_back({ cursor, window.second });
        return result;
    }

    static bool insideAny(const std::vector<Interval>& intervals, double x, bool closed = true) {
        for (auto& [lo, hi] : intervals)
            if (closed ? (x >= lo && x <= hi) : (x > lo && x < hi)) return true;
        return false;
    }

    static double nearestInAny(const std::vector<Interval>& intervals, double x, double tie_break) {
        double best = x, best_d = std::numeric_limits<double>::max();
        for (auto& [lo, hi] : intervals) {
            double cand = std::clamp(x, lo, hi);
            double d = std::abs(cand - x);
            if (d < best_d) { best_d = d; best = cand; }
            else if (d == best_d) {
                if (std::abs(cand - tie_break) < std::abs(best - tie_break)) best = cand;
            }
        }
        return best;
    }

    static std::vector<Interval> intersectWithInterval(const std::vector<Interval>& regions, Interval bound) {
        std::vector<Interval> result;
        for (const auto& [lo, hi] : regions) {
            double clo = std::max(lo, bound.first);
            double chi = std::min(hi, bound.second);
            if (clo <= chi) result.push_back({ clo, chi });
        }
        return result;
    }

    static std::vector<Interval> intersectIntervalFamilies(const std::vector<Interval>& family_1, const std::vector<Interval>& family_2) {
        std::vector<Interval> result;

        std::vector<Interval> f1 = normalize(family_1, true);
        std::vector<Interval> f2 = normalize(family_2, true);

        size_t k = 0, l = 0;
        while (k < f1.size() && l < f2.size()) {
            double lo = std::max(f1[k].first, f2[l].first);
            double hi = std::min(f1[k].second, f2[l].second);
            if (lo <= hi) result.push_back({ lo, hi }); // closed intervals: touching at a point still counts

            // Whichever piece ends first can't overlap anything further along in
            // the other family either, so it's the one that advances.
            if (f1[k].second < f2[l].second) ++k;
            else ++l;
        }

        return result;
    }

    // ── Per-chain geometry ────────────────────────────────────────────────────

    // The assigner that treats 'layer' as its upper_ side, if any, else the
    // one that treats it as its lower_ side.
    static std::pair<PortAssigner*, bool> assignerForLayer(int layer,
        const std::vector<PortAssigner*>& assigners)
    {
        if (layer < static_cast<int>(assigners.size()) && assigners[layer])
            return { assigners[layer], true };
        if (layer > 0 && assigners[layer - 1])
            return { assigners[layer - 1], false };
        return { nullptr, false };
    }

    static Interval memberLayerWindow(const LayerData& data, int idx,
        std::unordered_map<Node*, NodeLayout>& node_layout)
    {
        const auto& nodes = data.nodes;
        double low = std::numeric_limits<double>::lowest();
        double high = std::numeric_limits<double>::max();
        if (idx > 0) {
            Node* l = nodes[idx - 1].get();
            double width_l = l->isDummy() ? DUMMY_NODE_WIDTH : NODE_WIDTH;
            low = node_layout.at(l).x + width_l * 0.5 + MIN_BLOCK_SEP;
        }
        if (idx < static_cast<int>(nodes.size()) - 1) {
            Node* r = nodes[idx + 1].get();
            double width_r = r->isDummy() ? DUMMY_NODE_WIDTH : NODE_WIDTH;
            high = node_layout.at(r).x - width_r * 0.5 - MIN_BLOCK_SEP;
        }
        return { low, high };
    }

    // Intersection across every layer the chain crosses.
    static Interval chainSeparationWindow(const DummyChain& chain,
        const std::map<int, LayerData>& layers,
        std::unordered_map<Node*, NodeLayout>& node_layout,
        const std::vector<PortAssigner*>& assigners)
    {
        double low = std::numeric_limits<double>::lowest();
        double high = std::numeric_limits<double>::max();
        for (Node* member : chain.members) {
            int layer = member->getLayer();
            auto [assigner, is_upper] = assignerForLayer(layer, assigners);
            int idx = assigner ? assigner->positionInLayer(member, is_upper) : -1;
            Interval w = memberLayerWindow(layers.at(layer), idx, node_layout);
            low = std::max(low, w.first);
            high = std::min(high, w.second);
        }
        return { low, high };
    }

    // ── Forbidden regions for a dummy-chain boundary port ─────────────────────────
    std::vector<std::pair<double, double>> PortAssigner::forbiddenRegionsAsUpper(
        Node* upper_node, double min_sep, Node* skip) const
    {
        // Since the node is a dummy, just one source port can be found.
        Port& upper_port = node_layout_.at(upper_node).source_ports.front();
        std::vector<std::pair<double, double>> regions;
        for (const auto& node : lower_.nodes) {
            if (node.get() == skip) continue;

            for (const Port& p : node_layout_.at(node.get()).target_ports) {
                if (hyperedge_order_.at(upper_port.edge) > hyperedge_order_.at(p.edge)) {
                    regions.push_back({ p.x - min_sep, p.x + min_sep });
                }
            }
        }
        return regions;
    }

    std::vector<std::pair<double, double>> PortAssigner::forbiddenRegionsAsLower(
        Node* lower_node, double min_sep, Node* skip) const
    {
        // Since the node is a dummy, just one target port can be found.
        Port& lower_port = node_layout_.at(lower_node).target_ports.front();
        std::vector<std::pair<double, double>> regions;
        for (const auto& node : upper_.nodes) {
            if (node.get() == skip) continue;
            for (const Port& p : node_layout_.at(node.get()).source_ports) {
                if (hyperedge_order_.at(p.edge) > hyperedge_order_.at(lower_port.edge)) {
                    regions.push_back({ p.x - min_sep, p.x + min_sep });
                }
            }
        }
        return regions;
    }

    static std::vector<Interval> topForbiddenRegions(const DummyChain& chain,
        double min_sep, const std::vector<PortAssigner*>& assigners)
    {
        int origin = chain.top_layer - 1;
        if (origin < 0 || origin >= static_cast<int>(assigners.size()) || !assigners[origin]) return {};
        return assigners[origin]->forbiddenRegionsAsLower(chain.members.front(), min_sep); // our port plays the lower role here
    }

    static std::vector<Interval> bottomForbiddenRegions(const DummyChain& chain,
        double min_sep, const std::vector<PortAssigner*>& assigners)
    {
        int origin = chain.bottom_layer;
        if (origin >= static_cast<int>(assigners.size()) || !assigners[origin]) return {};
        return assigners[origin]->forbiddenRegionsAsUpper(chain.members.back(), min_sep); // our port plays the upper role here
    }

    static std::vector<Interval> forbiddenRegions(const DummyChain& chain,
        std::vector<double> min_spacing, const std::vector<PortAssigner*>& assigners) {
        std::vector<Interval> result;

        result = topForbiddenRegions(chain, min_spacing[chain.top_layer - 1], assigners);
        for (const Interval& i : bottomForbiddenRegions(chain, min_spacing[chain.bottom_layer], assigners)) {
            result.push_back(i);
        }

        return normalize(result, false);
    }

    // ── Per-boundary desire ───────────────────────────────────────────────────
    enum class Kind { ConflictFull, ConflictPartial, ConflictForbiddenMovement, AlignFull, AlignPartial, AlignForbiddenMovement, None, NotVisited };

    // ============================================================================
    // Dummy chain placement
    //
    // Every function below is part of one pass: decide, for every chain end, a
    // Kind and a desired_x range (resolveChainEnd and its helpers), then unify
    // each chain's two ends into one final x (unifyChain and its helpers). They
    // all read and write the same dozen-odd pieces of per-run state, so rather
    // than have each be a lambda capturing the whole local scope by reference,
    // they're ordinary free functions taking one shared context.
    // ============================================================================

    struct ChainPlacementContext {
        const std::map<int, LayerData>& layers;
        std::unordered_map<Node*, NodeLayout>& node_layout;
        const std::vector<PortAssigner*>& assigners;
        std::vector<double> min_spacing;

        std::vector<DummyChain> chains;
        std::unordered_map<Node*, int> node_to_chain;

        std::vector<Interval> sep_window;
        std::vector<std::vector<Interval>> forbidden_regions;
        std::vector<std::pair<Kind, Kind>> settled;                                     // {top, bottom}
        std::vector<std::pair<std::vector<Interval>, std::vector<Interval>>> desired_x; // {top, bottom}
        std::vector<std::pair<double, double>> tie_break_x;                             // {top, bottom}

        // Records which real node/port a chain's end actually achieved
        // AlignFull against, so unifyChain can, when BOTH ends are AlignFull,
        // try pulling both counterparts together too.
        // Left at {nullptr, nullptr} for every other Kind.
        struct AlignPartner { Node* node = nullptr; Port* port = nullptr; };
        std::vector<std::pair<AlignPartner, AlignPartner>> align_partner;               // {top, bottom}
    };

    // ── Slot accessors ─────────────────────────────────────────────────────────
    static Kind& settledSlot(ChainPlacementContext& ctx, int i, bool top) { return top ? ctx.settled[i].first : ctx.settled[i].second; }
    static std::vector<Interval>& desiredSlot(ChainPlacementContext& ctx, int i, bool top) { return top ? ctx.desired_x[i].first : ctx.desired_x[i].second; }
    static double& tieBreakSlot(ChainPlacementContext& ctx, int i, bool top) { return top ? ctx.tie_break_x[i].first : ctx.tie_break_x[i].second; }

    static std::vector<Interval> forbiddenFor(ChainPlacementContext& ctx, int i, bool is_top) {
        return is_top
            ? topForbiddenRegions(ctx.chains[i], ctx.min_spacing[ctx.chains[i].top_layer - 1], ctx.assigners) // A dummy chain cannot start at level 0
            : bottomForbiddenRegions(ctx.chains[i], ctx.min_spacing[ctx.chains[i].bottom_layer], ctx.assigners);
    }

    // A ForbiddenMovement end is already a forced, final position -- it never
    // yields further, so it jogs against anything rather than being overridden
    // (see unifyChain).
    static bool isForbiddenMovement(Kind k) {
        return k == Kind::ConflictForbiddenMovement || k == Kind::AlignForbiddenMovement;
    }

    // Used when an end has neither an alignment nor a conflict need. Scans the
    // neighbouring layer's own ports for the nearest port to our left and to our
    // right, then either centres in that gap (if it's already tight) or offers
    // the sub-range that keeps a comfortable 1.5*MIN_VERTICAL_SEP clearance from
    // each neighbour (if the gap can spare it). Seeding the bounds from
    // sep_window[idx] (rather than +/-infinity) means the port scan can only
    // tighten them further, never loosen them past the box-separation window
    // every other Kind is already built from.
    static void pushForClarity(ChainPlacementContext& ctx, int idx, bool i_is_top) {
        int scan_layer = i_is_top ? ctx.chains[idx].top_layer - 1 : ctx.chains[idx].bottom_layer + 1;
        NodeLayout& nl = ctx.node_layout.at(i_is_top ? ctx.chains[idx].members.front() : ctx.chains[idx].members.back());
        double chain_x = nl.x;
        Hyperedge* edge = i_is_top ? nl.target_ports.front().edge : nl.source_ports.front().edge;

        double left_bound = ctx.sep_window[idx].first;
        double right_bound = ctx.sep_window[idx].second;
        for (const auto& n : ctx.layers.at(scan_layer).nodes) {
            auto& ports = i_is_top ? ctx.node_layout.at(n.get()).source_ports : ctx.node_layout.at(n.get()).target_ports;
            for (const Port& p : ports) {
                if (i_is_top ? !ctx.assigners[edge->getLayer()]->edgeOrderedBefore(edge, p.edge) :
                               !ctx.assigners[edge->getLayer()]->edgeOrderedBefore(p.edge, edge)) continue;
                if (p.x < chain_x) left_bound = std::max(left_bound, p.x);
                else if (p.x > chain_x) right_bound = std::min(right_bound, p.x);
            }
        }

        if (right_bound - left_bound < 4.0 * MIN_VERTICAL_SEP) {
            double mid = (left_bound + right_bound) * 0.5;
            desiredSlot(ctx, idx, i_is_top) = { { mid, mid } };
            tieBreakSlot(ctx, idx, i_is_top) = mid;
        }
        else {
            Interval shrunk = { left_bound + 2.0 * MIN_VERTICAL_SEP, right_bound - 2.0 * MIN_VERTICAL_SEP };
            desiredSlot(ctx, idx, i_is_top) = { shrunk };
            tieBreakSlot(ctx, idx, i_is_top) = nearestInAny({ shrunk }, chain_x, chain_x);
        }
    }

    // Tier 5 for the "both ends None" case: a single combined scan across both
    // neighbouring layers at once (rather than the two independent pushForClarity
    // results), since both ends are purely cosmetic here and a single shared x
    // is always preferable to jogging between two arbitrary cosmetic picks.
    static double solveBothEndsNone(ChainPlacementContext& ctx, int idx) {
        int scan_layers[2] = { ctx.chains[idx].top_layer - 1, ctx.chains[idx].bottom_layer + 1 };
        double chain_x = ctx.node_layout.at(ctx.chains[idx].members.front()).x;

        double left_bound = ctx.sep_window[idx].first;
        double right_bound = ctx.sep_window[idx].second;
        for (int side = 0; side < 2; ++side) {
            bool is_top_side = (side == 0);
            for (const auto& n : ctx.layers.at(scan_layers[side]).nodes) {
                auto& ports = is_top_side ? ctx.node_layout.at(n.get()).source_ports : ctx.node_layout.at(n.get()).target_ports;
                for (const Port& p : ports) {
                    if (p.x < chain_x) left_bound = std::max(left_bound, p.x);
                    else if (p.x > chain_x) right_bound = std::min(right_bound, p.x);
                }
            }
        }

        if (right_bound - left_bound < 4.0 * MIN_VERTICAL_SEP)
            return (left_bound + right_bound) * 0.5;
        return nearestInAny({ { left_bound + 2.0 * MIN_VERTICAL_SEP, right_bound - 2.0 * MIN_VERTICAL_SEP } }, chain_x, chain_x);
    }

    // Settles one side of a chain-vs-chain conflict (see resolveChainEnd):
    // chain_idx tries to move into 'interval', clamped against its own
    // separation window and combined (top+bottom) forbidden regions.
    static void resolveHalfOfDummyConflict(ChainPlacementContext& ctx, int chain_idx, Interval interval, bool is_top) {
        auto allowed_space = subtractRegions(ctx.sep_window[chain_idx], ctx.forbidden_regions[chain_idx]);
        auto intersection = intersectWithInterval(allowed_space, interval);
        if (intersection.empty()) {
            desiredSlot(ctx, chain_idx, is_top) = { { interval.second, interval.second } };
            settledSlot(ctx, chain_idx, is_top) = Kind::ConflictForbiddenMovement;
        }
        else {
            desiredSlot(ctx, chain_idx, is_top) = intersection;
            tieBreakSlot(ctx, chain_idx, is_top) = nearestInAny(intersection, interval.second, interval.second);
            settledSlot(ctx, chain_idx, is_top) = Kind::ConflictFull;
        }
    }


    // Checks whether the vertical corridor [lo, hi] is genuinely clear across
    // every layer from top_layer to bottom_layer (inclusive): no real node's
    // box and no real node's port (other than the two specific ports being
    // aligned) falls inside it. 
    static bool corridorIsClear(ChainPlacementContext& ctx, double lo, double hi,
        int top_layer, int bottom_layer)
    {
        for (int layer = top_layer; layer <= bottom_layer; ++layer) {
            auto it = ctx.layers.find(layer);
            if (it == ctx.layers.end()) continue;
            for (const auto& node_ptr : it->second.nodes) {
                Node* n = node_ptr.get();
                const NodeLayout& nl = ctx.node_layout.at(n);

                if (!n->isDummy()) {
                    if (nl.x + NODE_WIDTH * 0.5 > lo && nl.x + NODE_WIDTH * 0.5 < hi) return false;
                    if (nl.x - NODE_WIDTH * 0.5 > lo && nl.x - NODE_WIDTH * 0.5 < hi) return false;
                }
                for (const Port& p : nl.source_ports) {
                    if (p.x > lo && p.x < hi) return false;
                }
                for (const Port& p : nl.target_ports) {
                    if (p.x > lo && p.x < hi) return false;
                }
            }
        }
        return true;
    }

    // ── Alignment candidates for one end of one chain ────────────────────────
    //
    // near: counterpart ports within NODE_WIDTH*0.5 of the chain, sorted by
    //       distance to it. These are aligned to directly (see resolveChainEnd).
    // far : corridors [lo, hi] between a counterpart port that is too far to
    //       align to directly and the closest coordinate where the ORIGINAL
    //       (pre-split) hyperedge lands on the opposite side of the chain,
    //       kept only when nothing lies inside the corridor. The chain is
    //       meant to sit in the middle of such a corridor (see
    //       resolveFarAlignment).
    struct FarPair {
        double lo, hi;
        Node* top_node;    Port* top_port;     // source port of the real node above the chain
        Node* bottom_node; Port* bottom_port;  // target port of the real node below the chain
        double mid() const { return (lo + hi) * 0.5; }
    };

    struct AlignCoord { double x; Node* node; Port* port; };

    struct EndAlignmentInfo {
        std::vector<std::pair<Node*, Port*>> near;
        std::vector<FarPair> far;
    };

    static EndAlignmentInfo gatherAlignmentInfo(ChainPlacementContext& ctx, int idx, bool i_is_top) {
        EndAlignmentInfo info;
        Node* dummy = i_is_top ? ctx.chains[idx].members.front() : ctx.chains[idx].members.back();
        NodeLayout& dummy_layout = ctx.node_layout.at(dummy);
        Hyperedge* edge = i_is_top ? dummy_layout.target_ports[0].edge : dummy_layout.source_ports[0].edge;

        std::vector<Node*> counterpart_candidates;
        std::vector<AlignCoord> alignment_coords;
        HyperedgePtr original = edge->getOrigin().lock();
        if (i_is_top) {
            for (const auto& src : edge->getSources()) counterpart_candidates.push_back(src.get());
            if (original) {
                for (const auto& tgt : original->getTargets()) {
                    if (tgt->getLayer() != ctx.chains[idx].bottom_layer + 1) continue;
                    for (Port& p : ctx.node_layout.at(tgt.get()).target_ports)
                        if (p.edge->getOrigin().lock() == original) alignment_coords.push_back({ p.x, tgt.get(), &p });
                }
            }
        }
        else {
            for (const auto& tgt : edge->getTargets()) counterpart_candidates.push_back(tgt.get());
            if (original) {
                for (const auto& src : original->getSources()) {
                    if (src->getLayer() != ctx.chains[idx].top_layer - 1) continue;
                    for (Port& p : ctx.node_layout.at(src.get()).source_ports)
                        if (p.edge->getOrigin().lock() == original) alignment_coords.push_back({ p.x, src.get(), &p });
                }
            }
        }

        for (Node* other : counterpart_candidates) {
            auto& other_ports = i_is_top ? ctx.node_layout.at(other).source_ports : ctx.node_layout.at(other).target_ports;
            for (Port& p : other_ports) {
                if (p.edge != edge) continue;
                if (std::abs(dummy_layout.x - p.x) <= NODE_WIDTH * 0.5) {
                    info.near.push_back({ other, &p });
                }
                else {
                    if (other->isDummy() || alignment_coords.empty()) continue;
                    const AlignCoord* closest = &alignment_coords.front();
                    for (const AlignCoord& c : alignment_coords)
                        if (std::abs(c.x - p.x) < std::abs(closest->x - p.x)) closest = &c;

                    double lo = std::min(closest->x, p.x);
                    double hi = std::max(closest->x, p.x);
                    if (!corridorIsClear(ctx, lo, hi, ctx.chains[idx].top_layer - 1, ctx.chains[idx].bottom_layer + 1)) continue;

                    // Either way the pair is (source port above, target port below).
                    if (i_is_top) info.far.push_back({ lo, hi, other, &p, closest->node, closest->port });
                    else          info.far.push_back({ lo, hi, closest->node, closest->port, other, &p });
                }
            }
        }
        std::sort(info.near.begin(), info.near.end(), [&](const auto& a, const auto& b) {
            return std::abs(dummy_layout.x - a.second->x) < std::abs(dummy_layout.x - b.second->x);
            });
        return info;
    }

    // True iff the per-end logic of resolveChainEnd would settle this end on one
    // of its near candidates without falling back to a partial alignment: a
    // dummy candidate (settles immediately) or a real one inside allowed_space.
    static bool endResolvesNearby(ChainPlacementContext& ctx, int idx, bool i_is_top, const EndAlignmentInfo& info) {
        if (info.near.empty()) return false;
        std::vector<Interval> allowed_space = subtractRegions(ctx.sep_window[idx], forbiddenFor(ctx, idx, !i_is_top));
        for (const auto& [other, port] : info.near)
            if (other->isDummy() || insideAny(allowed_space, port->x, true)) return true;
        return false;
    }

    // Recomputes everything derived from real node/port positions for one chain.
    static void refreshChainInfo(ChainPlacementContext& ctx, int i) {
        ctx.sep_window[i] = chainSeparationWindow(ctx.chains[i], ctx.layers, ctx.node_layout, ctx.assigners);
        ctx.forbidden_regions[i] = forbiddenRegions(ctx.chains[i], ctx.min_spacing, ctx.assigners);
    }

    // ── Step 1 (pre-pass): align a whole chain through far corridors ─────────
    //
    // Both ends are decided at once. Only tried when neither end can settle on
    // a near candidate (near ones always have priority) and at least one far
    // corridor exists. Each corridor is scored by how far its middle point is
    // from the chain; the closest one(s) win. When several are tied within an
    // epsilon, the target is the middle point of their middle points (for a
    // single winner that is just its own middle point). The whole chain goes
    // to that target -- both ends AlignFull -- if it lies in the chain's
    // allowed space; otherwise nothing is done and both ends are marked None.
    static void resolveFarAlignment(ChainPlacementContext& ctx, int idx,
        std::unordered_map<Node*, std::unordered_set<Port*>>& adjusted_src,
        std::unordered_map<Node*, std::unordered_set<Port*>>& adjusted_tgt)
    {
        if (ctx.settled[idx].first != Kind::NotVisited || ctx.settled[idx].second != Kind::NotVisited) return;

        // Earlier far alignments of this same pass may already have moved real ports.
        refreshChainInfo(ctx, idx);

        EndAlignmentInfo top = gatherAlignmentInfo(ctx, idx, true);
        EndAlignmentInfo bottom = gatherAlignmentInfo(ctx, idx, false);
        if (endResolvesNearby(ctx, idx, true, top) || endResolvesNearby(ctx, idx, false, bottom)) return;

        std::vector<FarPair> pairs = top.far;
        pairs.insert(pairs.end(), bottom.far.begin(), bottom.far.end());
        if (pairs.empty()) return;

        constexpr double EPS = 1e-6;
        double chain_x = ctx.node_layout.at(ctx.chains[idx].members.front()).x;

        double best = std::numeric_limits<double>::max();
        for (const FarPair& f : pairs) best = std::min(best, std::abs(f.mid() - chain_x));

        double lowest_tied = std::numeric_limits<double>::max();
        double highest_tied = std::numeric_limits<double>::lowest();
        for (const FarPair& f : pairs) {
            if (std::abs(f.mid() - chain_x) > best + EPS) continue;
            lowest_tied = std::min(lowest_tied, f.mid());
            highest_tied = std::max(highest_tied, f.mid());
        }
        double target = (lowest_tied + highest_tied) * 0.5;

        std::vector<Interval> allowed_space = subtractRegions(ctx.sep_window[idx], ctx.forbidden_regions[idx]);
        if (!insideAny(allowed_space, target, true)) {
            settledSlot(ctx, idx, true) = Kind::None;
            settledSlot(ctx, idx, false) = Kind::None;
            return;
        }

        for (bool is_top : { true, false }) {
            desiredSlot(ctx, idx, is_top) = { { target, target } };
            tieBreakSlot(ctx, idx, is_top) = target;
            settledSlot(ctx, idx, is_top) = Kind::AlignFull;
        }

        // Bring the real ports of every pair whose verified corridor contains the
        // target to it, and note them so the rest of their node's ports can be
        // rearranged afterwards (same bookkeeping as reduceHorizontalJogs).
        for (const FarPair& f : pairs) {
            if (target < f.lo - EPS || target > f.hi + EPS) continue;
            f.top_port->x = target;
            f.bottom_port->x = target;
            adjusted_src[f.top_node].insert(f.top_port);
            adjusted_tgt[f.bottom_node].insert(f.bottom_port);
        }
    }

    // ── Step 1: resolve one end of one chain ─────────────────────────────────
    //
    // Settles settledSlot(ctx, idx, i_is_top) and, in every case except a
    // dummy-vs-dummy alignment miss (see below), desiredSlot/tieBreakSlot too.
    // A plain recursive free function now rather than a std::function lambda --
    // it captures nothing, so ordinary self-recursion by name is all it needs.
    static void resolveChainEnd(ChainPlacementContext& ctx, int idx, bool i_is_top) {
        Kind& settled_own = settledSlot(ctx, idx, i_is_top);
        if (settled_own != Kind::NotVisited) return; // End was already visited

        Node* dummy = i_is_top ? ctx.chains[idx].members.front() : ctx.chains[idx].members.back();
        NodeLayout& dummy_layout = ctx.node_layout.at(dummy);
        Hyperedge* edge = i_is_top ? dummy_layout.target_ports[0].edge : dummy_layout.source_ports[0].edge;

        // ── Vertical Alignment logic ─────────────────────────────────────────
        //
        // Alignment must still respect the OPPOSITE end's forbidden regions,
        // since both ends share one final x for the whole chain -- a position
        // already doomed to conflict at the other end isn't a real alignment
        // candidate here either. It does NOT need to respect THIS end's own
        // forbidden regions: if the partner is a real node, either
        // reduceHorizontalJogs() will force the alignment or solveConflicts()
        // will handle the conflicting regions; if the partner is a dummy, we do
        // nothing (see below).
        {
            EndAlignmentInfo info = gatherAlignmentInfo(ctx, idx, i_is_top);
            std::vector<std::pair<Node*, Port*>>& candidates = info.near;

            if (!candidates.empty()) {
                std::vector<Interval> allowed_space = subtractRegions(ctx.sep_window[idx], forbiddenFor(ctx, idx, !i_is_top));

                // Walk the candidates closest-first, looking for 
                // the first one that achieves full alignment.
                for (const auto& [other, port_ptr] : candidates) {
                    Port& p = *port_ptr;

                    if (other->isDummy()) {
                        // We won't be aligning dummy nodes because they don't fall under any
                        // of the needed premises. We align ports when:
                        // - Either we have a binary edge and the nodes are sufficiently close
                        // - We have two leftmost or rightmost nodes
                        // None of those conditions are met, because if they did then both
                        // dummies would already be aligned.
                        if (std::abs(dummy_layout.x - p.x) < 1e-6) {
                            // Already aligned, keep the end fixed.
                            desiredSlot(ctx, idx, i_is_top) = { {p.x, p.x} };
                            settled_own = Kind::AlignFull;
                        }
                        else {
                            pushForClarity(ctx, idx, i_is_top);
                            settled_own = Kind::None;
                        }
                        return;
                    }

                    if (insideAny(allowed_space, p.x)) {
                        // Full alignment is possible with this candidate. Before taking
                        // it, look for ties: other real candidates that are also inside
                        // allowed_space and at the same distance from the chain (the list
                        // is sorted by distance, so the scan stops at the first farther one).
                        constexpr double TIE_EPS = 1e-6;
                        double best_dist = std::abs(dummy_layout.x - p.x);
                        std::vector<std::pair<Node*, Port*>> tied;
                        for (const auto& [o, op] : candidates) {
                            if (std::abs(dummy_layout.x - op->x) > best_dist + TIE_EPS) break;
                            if (o->isDummy()) continue;
                            if (insideAny(allowed_space, op->x)) tied.push_back({ o, op });
                        }

                        Node* chosen_node = other;
                        Port* chosen_port = &p;

                        double lowest = std::numeric_limits<double>::max();
                        double highest = std::numeric_limits<double>::lowest();
                        for (const auto& t : tied) {
                            lowest = std::min(lowest, t.second->x);
                            highest = std::max(highest, t.second->x);
                        }

                        if (tied.size() > 1 && highest - lowest > TIE_EPS) {
                            // Genuine tie between different positions: align to the
                            // middle point of the tied alignments if it is allowed.
                            double mid = (lowest + highest) * 0.5;
                            if (insideAny(allowed_space, mid)) {
                                // The chain sits at neither port, so no align_partner is
                                // recorded (unifyChain's counterpart-pulling must not fire).
                                desiredSlot(ctx, idx, i_is_top) = { {mid, mid} };
                                settled_own = Kind::AlignFull;
                                return;
                            }

                            // The middle point is not allowed, so prefer one of the tied
                            // candidates: the one closest to the allowed point nearest to
                            // the middle point (final tie -> the leftmost, to stay deterministic).
                            double q = nearestInAny(allowed_space, mid, dummy_layout.x);
                            for (const auto& t : tied) {
                                double d_new = std::abs(t.second->x - q);
                                double d_cur = std::abs(chosen_port->x - q);
                                if (d_new < d_cur - TIE_EPS ||
                                    (std::abs(d_new - d_cur) <= TIE_EPS && t.second->x < chosen_port->x)) {
                                    chosen_node = t.first;
                                    chosen_port = t.second;
                                }
                            }
                        }

                        desiredSlot(ctx, idx, i_is_top) = { {chosen_port->x, chosen_port->x} };
                        settled_own = Kind::AlignFull;
                        (i_is_top ? ctx.align_partner[idx].first : ctx.align_partner[idx].second) = { chosen_node, chosen_port };
                        return;
                    }
                    // Otherwise: keep looking for a candidate that CAN achieve full
                    // alignment; if none do, fall back to the closest one below.
                }

                // No candidate achieved full alignment: fall back to a partial
                // alignment with the closest one. However, we will first ask the
                // other end of its situation. If it were another partial then it
                // makes no sense to have both ends in a partial align and add a
                // jog when we could have both full alignments and a jog.
                Port& p = *candidates.front().second;

                settled_own = Kind::AlignPartial;
                Kind& settled_other = settledSlot(ctx, idx, !i_is_top);
                if (settled_other == Kind::NotVisited) {
                    resolveChainEnd(ctx, idx, !i_is_top);
                    if (settled_other == Kind::AlignForbiddenMovement || settled_other == Kind::ConflictForbiddenMovement) {
                        // After calling the other end, it has been placed in a forbidden position.
                        // So both ends work independently and we are free to do whatever we please.
                        desiredSlot(ctx, idx, i_is_top) = { {p.x, p.x} };
                        settled_own = Kind::AlignForbiddenMovement;
                        return;
                    }
                }
                else if (settled_other == Kind::AlignPartial) {
                    // The other end (which had to call) also has partial alignment
                    // and since this would lead to two partial alignments and a jog
                    // we will instead replace ours for a full forbidden alignment.
                    // After returning, the other end will update its own kind.
                    desiredSlot(ctx, idx, i_is_top) = { {p.x, p.x} };
                    settled_own = Kind::AlignForbiddenMovement;
                    return;
                }

                double new_x = nearestInAny(allowed_space, p.x, dummy_layout.x);
                desiredSlot(ctx, idx, i_is_top) = { {new_x, new_x} };
                return;
            }
        }

        // ── Conflict Resolution logic ────────────────────────────────────────
        {
            int pair_layer = i_is_top ? ctx.chains[idx].top_layer - 1 : ctx.chains[idx].bottom_layer;
            int scan_layer = i_is_top ? ctx.chains[idx].top_layer - 1 : ctx.chains[idx].bottom_layer + 1;
            PortAssigner* assigner = ctx.assigners[pair_layer];

            std::vector<double> conflicts_x;
            Node* dummy_conflict = nullptr;
            Hyperedge* dummy_edge = nullptr;
            for (const auto& n : ctx.layers.at(scan_layer).nodes) {
                auto& candidate_ports = i_is_top ? ctx.node_layout.at(n.get()).source_ports : ctx.node_layout.at(n.get()).target_ports;
                for (const Port& p : candidate_ports) {
                    bool order_ok = i_is_top ? assigner->edgeOrderedBefore(edge, p.edge)
                        : assigner->edgeOrderedBefore(p.edge, edge);
                    if (std::abs(dummy_layout.x - p.x) < ctx.min_spacing[pair_layer] && order_ok) {
                        if (n->isDummy()) { dummy_conflict = n.get(); dummy_edge = p.edge; }
                        conflicts_x.push_back(p.x);
                    }
                }
                if (dummy_conflict) break; // No more conflicts can occur, so we stop the search
            }

            if (!conflicts_x.empty()) {
                if (dummy_conflict) {
                    // We will move both ports so that the whole conflict is avoided. Some will have to
                    // move right and the other will have to move left. That will be determined by first
                    // the abscisas and then rightmost or leftmost.
                    Node* left; Node* right;
                    if (dummy_layout.x < conflicts_x.front()) { left = dummy; right = dummy_conflict; }
                    else if (dummy_layout.x > conflicts_x.front()) { left = dummy_conflict; right = dummy; }
                    else {
                        // Both share the same x-coordinate, hyperedges will decide the draw
                        if (assigner->isLeftMost(edge, dummy)) { left = dummy_conflict; right = dummy; }
                        else if (assigner->isRightMost(edge, dummy)) { left = dummy; right = dummy_conflict; }
                        else if (assigner->isLeftMost(dummy_edge, dummy_conflict)) { left = dummy; right = dummy_conflict; }
                        else { left = dummy_conflict; right = dummy; }
                    }

                    // Moving both chains. Let m be the middle point of both, the left chain tries
                    // to move to (-infty, m - MIN_VERTICAL_SEP/2] whilst the right chain tries the
                    // analogous [m + MIN_VERTICAL_SEP/2, +infty). dummy_conflict was found via the
                    // OPPOSITE port kind to ours, so it's addressed at its opposite boundary.
                    constexpr double INF = std::numeric_limits<double>::infinity();
                    double m = (dummy_layout.x + conflicts_x.front()) * 0.5;
                    int other_idx = ctx.node_to_chain.at(dummy_conflict);
                    if (left == dummy) {
                        resolveHalfOfDummyConflict(ctx, idx, { -INF, m - MIN_VERTICAL_SEP * 0.5 }, i_is_top);
                        resolveHalfOfDummyConflict(ctx, other_idx, { m + MIN_VERTICAL_SEP * 0.5, INF }, !i_is_top);
                    }
                    else {
                        resolveHalfOfDummyConflict(ctx, other_idx, { -INF, m - MIN_VERTICAL_SEP * 0.5 }, !i_is_top);
                        resolveHalfOfDummyConflict(ctx, idx, { m + MIN_VERTICAL_SEP * 0.5, INF }, i_is_top);
                    }
                    return;
                }

                // No dummy conflict whatsoever, but we have to fix the conflicts with the real nodes.
                auto allowed_whole_space = subtractRegions(ctx.sep_window[idx], ctx.forbidden_regions[idx]);
                if (!allowed_whole_space.empty()) {
                    desiredSlot(ctx, idx, i_is_top) = allowed_whole_space;
                    settled_own = Kind::ConflictFull;

                    // Prefer points keeping an extra MIN_VERTICAL_SEP from both ends of their piece.
                    std::vector<Interval> comfortable;
                    for (const Interval& I : allowed_whole_space)
                        if (I.second - I.first >= 4 * MIN_VERTICAL_SEP)
                            comfortable.push_back({ I.first + 2 * MIN_VERTICAL_SEP, I.second - 2 * MIN_VERTICAL_SEP });

                    if (!comfortable.empty()) {
                        tieBreakSlot(ctx, idx, i_is_top) = nearestInAny(comfortable, dummy_layout.x, dummy_layout.x);
                    }
                    else {
                        // Every piece is narrow: aim for the middle of the piece nearest the chain,
                        // which is the point with the most clearance there.
                        double edge_point = nearestInAny(allowed_whole_space, dummy_layout.x, dummy_layout.x);
                        for (const Interval& I : allowed_whole_space) {
                            if (edge_point >= I.first && edge_point <= I.second) {
                                tieBreakSlot(ctx, idx, i_is_top) = (I.first + I.second) * 0.5;
                                break;
                            }
                        }
                    }
                    return;
                }

                auto allowed_own_space = subtractRegions(ctx.sep_window[idx], forbiddenFor(ctx, idx, i_is_top));
                if (!allowed_own_space.empty()) {
                    // There is allowed space to move our dummy end to and avoid all conflicts.
                    // In any case, we will try to move the dummy to the closest point possible
                    // that respects the spacing.
                    double new_x = nearestInAny(allowed_own_space, dummy_layout.x, dummy_layout.x);
                    desiredSlot(ctx, idx, i_is_top) = { { new_x, new_x } };
                    settled_own = Kind::ConflictForbiddenMovement;
                    return;
                }

                // There is no space that wouldn't cause any conflict, so we will place
                // the dummy port more symmetrically to its current conflicts.
                if (conflicts_x.size() == 2) {
                    double middle = (conflicts_x.front() + conflicts_x.back()) * 0.5;
                    desiredSlot(ctx, idx, i_is_top) = { { middle, middle } };
                    settled_own = insideAny(forbiddenFor(ctx, idx, !i_is_top), middle, false)
                        ? Kind::ConflictForbiddenMovement : Kind::ConflictPartial;
                }
                // else: just one conflict (not possible) because allowed_own_space is empty.
                return;
            }
        }

        // If no vertical alignments or conflicts, then push for clarity.
        pushForClarity(ctx, idx, i_is_top);
        settled_own = Kind::None;
    }

    // ── Step 2: unify one chain's two ends into a single final position ─────
    //
    // The priority between the two ends is entirely captured by Kind's
    // declared enum order (ConflictFull > ConflictPartial > ... > None):
    // whichever end has the lower ordinal wins whenever they can't share
    // a position. The only two exceptions, both symmetric by nature:
    //   - A ForbiddenMovement end is already a forced, final position -- it
    //     never yields further, so it jogs against anything rather than
    //     being overridden.
    //   - Two ends of the identical Kind are peers, neither of which should
    //     be discarded in favour of the other, so they jog too -- except
    //     None vs None, whose "desire" is purely cosmetic (Tier 5) on both
    //     sides, so there's no reason to force a jog between two cosmetic
    //     preferences that simply don't overlap.
    static void repositionChain(ChainPlacementContext& ctx, int idx, double top_x, double bottom_x) {
        int size = static_cast<int>(ctx.chains[idx].members.size());
        for (int i = 0; i < size; i++) {
            NodeLayout& nl = ctx.node_layout.at(ctx.chains[idx].members[i]);
            if (i < size / 2) {
                nl.source_ports[0].x = top_x;
                nl.target_ports[0].x = top_x;
                nl.x = top_x;
            }
            else if (i > size / 2) {
                nl.source_ports[0].x = bottom_x;
                nl.target_ports[0].x = bottom_x;
                nl.x = bottom_x;
            }
            else {
                nl.source_ports[0].x = bottom_x;
                nl.target_ports[0].x = top_x;
                nl.x = (bottom_x + top_x) * 0.5;
            }
        }
    }

    // Range of x over which a real node's box can slide without breaking
    // MIN_BLOCK_SEP against its immediate neighbours in the layer.
    static Interval realNodeMovableRange(ChainPlacementContext& ctx, int layer, int pos) {
        const auto& nodes = ctx.layers.at(layer).nodes;
        int k = static_cast<int>(nodes.size());

        double low = std::numeric_limits<double>::lowest();
        double high = std::numeric_limits<double>::max();
        if (pos > 0) {
            Node* l = nodes[pos - 1].get();
            double wl = l->isDummy() ? DUMMY_NODE_WIDTH : NODE_WIDTH;
            low = ctx.node_layout.at(l).x + (wl + NODE_WIDTH) * 0.5 + MIN_BLOCK_SEP;
        }
        if (pos < k - 1) {
            Node* r = nodes[pos + 1].get();
            double wr = r->isDummy() ? DUMMY_NODE_WIDTH : NODE_WIDTH;
            high = ctx.node_layout.at(r).x - (wr + NODE_WIDTH) * 0.5 - MIN_BLOCK_SEP;
        }
        return { low, high };
    }

    // Both ends of the chain are AlignFull against different real nodes. If those
    // nodes are free to move (top: no parents and a single source port; bottom: no
    // children and a single target port), try to move their boxes so that both line
    // up with the chain on one x. Returns false when nothing could be done.
    static bool tryMoveAlignedNodes(ChainPlacementContext& ctx, int i, double top_x, double bottom_x) {
        Node* top_node = ctx.align_partner[i].first.node;
        Node* bottom_node = ctx.align_partner[i].second.node;
        NodeLayout& top_nl = ctx.node_layout.at(top_node);
        NodeLayout& bottom_nl = ctx.node_layout.at(bottom_node);

        int top_layer = ctx.chains[i].top_layer - 1;
        int bottom_layer = ctx.chains[i].bottom_layer + 1;
        int top_pos = ctx.assigners[top_layer]->positionInLayer(top_node, true);
        int bottom_pos = ctx.assigners[ctx.chains[i].bottom_layer]->positionInLayer(bottom_node, false);

        bool top_movable = top_pos >= 0 && top_node->getParents().empty() && top_nl.source_ports.size() == 1;
        bool bottom_movable = bottom_pos >= 0 && bottom_node->getChildren().empty() && bottom_nl.target_ports.size() == 1;
        if (!top_movable && !bottom_movable) return false;

        // An end that cannot move stays pinned where it is, so if only one end is
        // movable the only feasible position is the one the other end dictates.
        Interval top_range = top_movable ? realNodeMovableRange(ctx, top_layer, top_pos) : Interval{ top_x, top_x };
        Interval bottom_range = bottom_movable ? realNodeMovableRange(ctx, bottom_layer, bottom_pos) : Interval{ bottom_x, bottom_x };

        std::vector<Interval> feasible = subtractRegions(ctx.sep_window[i], ctx.forbidden_regions[i]);
        feasible = intersectWithInterval(feasible, top_range);
        feasible = intersectWithInterval(feasible, bottom_range);
        feasible = intersectWithInterval(feasible, { std::min(top_x, bottom_x), std::max(top_x, bottom_x) });
        if (feasible.empty()) return false;

        // Closest feasible point to the middle: when it can't be reached exactly,
        // one box simply ends up moving more than the other.
        double mid = (top_x + bottom_x) * 0.5;
        double target = nearestInAny(feasible, mid, mid);

        if (top_movable) { top_nl.x = target; top_nl.source_ports[0].x = target; }
        if (bottom_movable) { bottom_nl.x = target; bottom_nl.target_ports[0].x = target; }

        repositionChain(ctx, i, target, target);
        return true;
    }

    // Rearranges the free ports of every node that had ports moved (the moved ones
    // stay fixed) and folds the resulting spacing into ctx.min_spacing.
    static void redistributeAdjustedPorts(ChainPlacementContext& ctx,
        std::unordered_map<Node*, std::unordered_set<Port*>>& adjusted_src,
        std::unordered_map<Node*, std::unordered_set<Port*>>& adjusted_tgt)
    {
        for (auto& [node, fixed] : adjusted_src) {
            NodeLayout& nl = ctx.node_layout.at(node);
            int pair_layer = node->getLayer();
            double spacing = ctx.assigners[pair_layer]->redistributePorts(nl.source_ports, fixed, nl.x);
            ctx.min_spacing[pair_layer] = std::min(ctx.min_spacing[pair_layer], spacing);
        }
        for (auto& [node, fixed] : adjusted_tgt) {
            NodeLayout& nl = ctx.node_layout.at(node);
            int pair_layer = node->getLayer() - 1;
            double spacing = ctx.assigners[pair_layer]->redistributePorts(nl.target_ports, fixed, nl.x);
            ctx.min_spacing[pair_layer] = std::min(ctx.min_spacing[pair_layer], spacing);
        }
    }

    static void unifyChain(ChainPlacementContext& ctx, int i,
        std::unordered_map<Node*, std::unordered_set<Port*>>& adjusted_src,
        std::unordered_map<Node*, std::unordered_set<Port*>>& adjusted_tgt) {
        double chain_x = ctx.node_layout.at(ctx.chains[i].members.front()).x;
        Kind top = ctx.settled[i].first, bottom = ctx.settled[i].second;

        if (top == Kind::None && bottom == Kind::None) {
            double target = solveBothEndsNone(ctx, i);
            repositionChain(ctx, i, target, target);
            return;
        }

        auto intersection = intersectIntervalFamilies(ctx.desired_x[i].first, ctx.desired_x[i].second);
        if (!intersection.empty()) {
            double target = nearestInAny(intersection, (ctx.tie_break_x[i].first + ctx.tie_break_x[i].second) * 0.5, chain_x);
            repositionChain(ctx, i, target, target);
            return;
        }

        // Both ends achieved full alignment individually, but with different
        // targets, so the chain would otherwise have to jog between them.
        // Before accepting that, check whether the wider corridor between
        // the two counterparts is genuinely empty -- if so we can do better
        // than the chain alone: pull BOTH counterpart ports together to
        // their midpoint too, straightening the whole assembly across
        // non-adjacent layers, not just the chain's own interior.
        if (top == Kind::AlignFull && bottom == Kind::AlignFull &&
            ctx.align_partner[i].first.node && ctx.align_partner[i].second.node)
        {
            // Full alignments desired_x are just single-point degenerate intervals.
            double top_x = ctx.desired_x[i].first.front().first;
            double bottom_x = ctx.desired_x[i].second.front().first;
            double lo = std::min(top_x, bottom_x);
            double hi = std::max(top_x, bottom_x);

            int top_neighbor_layer = ctx.chains[i].top_layer - 1;
            int bottom_neighbor_layer = ctx.chains[i].bottom_layer + 1;

            Node* top_node = ctx.align_partner[i].first.node;
            Node* bottom_node = ctx.align_partner[i].second.node;
            Port* top_port = ctx.align_partner[i].first.port;
            Port* bottom_port = ctx.align_partner[i].second.port;

            // First choice: move the real boxes themselves when they are free to.
            if (tryMoveAlignedNodes(ctx, i, top_x, bottom_x)) return;

            if (corridorIsClear(ctx, lo, hi, top_neighbor_layer, bottom_neighbor_layer))
            {
                // Otherwise: current behaviour, pull both ports to the midpoint. They are now
                // fully aligned, so they are recorded as fixed; the rest of their nodes' ports
                // are rearranged once, at the very end of placeDummyChains.
                double mid = (top_x + bottom_x) * 0.5;
                top_port->x = mid;
                bottom_port->x = mid;
                adjusted_src[top_node].insert(top_port);
                adjusted_tgt[bottom_node].insert(bottom_port);

                repositionChain(ctx, i, mid, mid);
                return;
            }
        }

        if (isForbiddenMovement(top) || isForbiddenMovement(bottom) || top == bottom) {
            double top_x = nearestInAny(ctx.desired_x[i].first, ctx.tie_break_x[i].first, ctx.tie_break_x[i].first);
            double bottom_x = nearestInAny(ctx.desired_x[i].second, ctx.tie_break_x[i].second, ctx.tie_break_x[i].second);
            repositionChain(ctx, i, top_x, bottom_x);
        }
        else {
            bool top_wins = static_cast<int>(top) < static_cast<int>(bottom);
            double target = top_wins ? nearestInAny(ctx.desired_x[i].first, ctx.tie_break_x[i].first, chain_x)
                : nearestInAny(ctx.desired_x[i].second, ctx.tie_break_x[i].second, chain_x);
            repositionChain(ctx, i, target, target);
        }
    }

} // namespace port_assignment_internal

// ============================================================================
// GraphicalHypergraph::assignPorts
// 
// Runs the port-assignment pipeline in three passes over the whole graph:
//   1. buildPorts(false)         — order and symmetrically space ports on
//                                   every layer pair, with jog-reduction
//                                   deferred (see straightenDummyChains()).
//   2. placeDummyChains()        — settle every dummy chain's to a position
//                                  where it is better placed.
//                                              
//   3. reduceHorizontalJogs() /
//      solveVerticalOverlaps()   — Solves conflicts and reduces jogs.
// ============================================================================
namespace hypergraph_logic {
    using namespace port_assignment_internal;

    void GraphicalHypergraph::assignPorts() {
        int layer_count = static_cast<int>(layers_.size());

        std::vector<std::unique_ptr<PortAssigner>> assigners(layer_count > 0 ? layer_count - 1 : 0);
        std::vector<double> min_spacing(assigners.size(), MIN_VERTICAL_SEP);
        for (int layer = 0; layer < layer_count - 1; layer++) {
            if (layers_.at(layer).outgoing_edges.empty()) continue;
            assigners[layer] = std::make_unique<PortAssigner>(layer, layers_, node_layout_);
            min_spacing[layer] = assigners[layer]->buildPorts();
        }

        std::vector<PortAssigner*> assigner_ptrs(assigners.size());
        for (size_t i = 0; i < assigners.size(); ++i) assigner_ptrs[i] = assigners[i].get();
        placeDummyChains(assigner_ptrs, min_spacing);

        for (int layer = 0; layer < layer_count - 1; layer++) {
            if (!assigners[layer]) continue;
            min_spacing[layer] = std::min(min_spacing[layer], assigners[layer]->reduceHorizontalJogs());
            assigners[layer]->solveVerticalOverlaps(min_spacing[layer]);
        }
        recentreNodesUnderPorts();
        centerSingleHyperedgeRoots(assigner_ptrs);
    }

    // ── placeDummyChains ─────────────────────────────────────────────────
    //
    // It finds a better placing for dummy chains so that vertical alignment
    // is performed or conflicts are solved before being encountered by the
    // other part.
    //
    //   Step 1: Resolve every chain's end status.
    //   Step 2: Unify every end's desire to the whole dummy chain.
    //
    void GraphicalHypergraph::placeDummyChains(std::vector<port_assignment_internal::PortAssigner*>& assigners,
        std::vector<double>& min_spacing) {
        using namespace port_assignment_internal;

        std::vector<DummyChain> chains = findDummyChains(layers_);
        if (chains.empty()) return;

        ChainPlacementContext ctx{ layers_, node_layout_, assigners, min_spacing, std::move(chains) };
        int n = static_cast<int>(ctx.chains.size());

        for (int i = 0; i < n; ++i)
            for (Node* m : ctx.chains[i].members) ctx.node_to_chain[m] = i;

        ctx.sep_window.resize(n);
        ctx.forbidden_regions.resize(n);
        ctx.settled.assign(n, { Kind::NotVisited, Kind::NotVisited }); // {top, bottom}
        ctx.desired_x.resize(n);
        ctx.tie_break_x.resize(n);
        ctx.align_partner.resize(n);

        for (int i = 0; i < n; ++i) {
            double chain_x = ctx.node_layout.at(ctx.chains[i].members.front()).x;
            ctx.desired_x[i] = { { { chain_x, chain_x } }, { { chain_x, chain_x } } }; // default: no change needed yet
            ctx.tie_break_x[i] = { chain_x, chain_x };
            refreshChainInfo(ctx, i);
        }

        // ── Step 1a: far alignments first, for every chain ────────────────
        // resolveFarAlignment already checks that neither end could settle on a
        // near alignment, so running it for all chains up front is safe.
        std::unordered_map<Node*, std::unordered_set<Port*>> adjusted_src;
        std::unordered_map<Node*, std::unordered_set<Port*>> adjusted_tgt;
        for (int i = 0; i < n; ++i)
            resolveFarAlignment(ctx, i, adjusted_src, adjusted_tgt);
        // Rearrange the free ports of every node that had ports moved, then refresh
        // everything derived from port positions before any chain end is resolved.
        redistributeAdjustedPorts(ctx, adjusted_src, adjusted_tgt);

        for (int i = 0; i < n; ++i)
            refreshChainInfo(ctx, i);

        // ── Step 1b: everything else, per chain end ───────────────────────
        // Chains settled above are skipped by resolveChainEnd's own early return.
        for (int i = 0; i < n; ++i) {
            resolveChainEnd(ctx, i, true);
            resolveChainEnd(ctx, i, false);
        }

        // ── Step 2: Unify every end's desire into the whole dummy chain ────
        for (int i = 0; i < n; ++i)
            unifyChain(ctx, i, adjusted_src, adjusted_tgt);

        // Any real port a chain end ended up exactly aligned with must stay where it is.
        for (int i = 0; i < n; ++i) {
            auto& [top_p, bottom_p] = ctx.align_partner[i];
            double top_end_x = ctx.node_layout.at(ctx.chains[i].members.front()).target_ports[0].x;
            double bottom_end_x = ctx.node_layout.at(ctx.chains[i].members.back()).source_ports[0].x;
            if (top_p.node && std::abs(top_p.port->x - top_end_x) < 1e-6)
                adjusted_src[top_p.node].insert(top_p.port);
            if (bottom_p.node && std::abs(bottom_p.port->x - bottom_end_x) < 1e-6)
                adjusted_tgt[bottom_p.node].insert(bottom_p.port);
        }
        redistributeAdjustedPorts(ctx, adjusted_src, adjusted_tgt);

        min_spacing = std::move(ctx.min_spacing);
    }

    void GraphicalHypergraph::recentreNodesUnderPorts() {
        for (const auto& [layer, data] : layers_) {
            const auto& nodes = data.nodes;
            int k = static_cast<int>(nodes.size());

            auto pass = [&](int start, int end, int step) {
                for (int i = start; i != end; i += step) {
                    Node* n = nodes[i].get();
                    NodeLayout& nl = node_layout_.at(n);

                    double min_p = std::numeric_limits<double>::max();
                    double max_p = std::numeric_limits<double>::lowest();
                    for (const Port& p : nl.source_ports) { min_p = std::min(min_p, p.x); max_p = std::max(max_p, p.x); }
                    for (const Port& p : nl.target_ports) { min_p = std::min(min_p, p.x); max_p = std::max(max_p, p.x); }
                    if (min_p > max_p) continue; // no ports at all: nothing to centre

                    double target = (min_p + max_p) * 0.5;
                    if (nl.x == target) continue;
                    double width_n = n->isDummy() ? DUMMY_NODE_WIDTH : NODE_WIDTH;

                    double low = std::numeric_limits<double>::lowest();
                    if (i > 0) {
                        Node* l = nodes[i - 1].get();
                        double width_l = l->isDummy() ? DUMMY_NODE_WIDTH : NODE_WIDTH;
                        low = node_layout_.at(l).x + (width_l + width_n) * 0.5 + MIN_BLOCK_SEP;
                    }

                    double high = std::numeric_limits<double>::max();
                    if (i < k - 1) {
                        Node* r = nodes[i + 1].get();
                        double width_r = r->isDummy() ? DUMMY_NODE_WIDTH : NODE_WIDTH;
                        high = node_layout_.at(r).x - (width_n + width_r) * 0.5 - MIN_BLOCK_SEP;
                    }

                    if (low <= high)
                        nl.x = std::clamp(target, low, high);
                    // else: siblings already at minimum spacing, no room to move safely.
                }
                };

            pass(0, k, 1);       // left  -> right
            pass(k - 1, -1, -1); // right -> left
        }
    }

    void GraphicalHypergraph::centerSingleHyperedgeRoots(std::vector<port_assignment_internal::PortAssigner*>& assigners) {
        using namespace port_assignment_internal;

        for (const auto& [layer, data] : layers_) {
            const auto& nodes = data.nodes;
            int k = static_cast<int>(nodes.size());

            auto pass = [&](int start, int end, int step) {
                for (int i = start; i != end; i += step) {
                    Node* node = nodes[i].get();
                    if (!node->getParents().empty()) continue;

                    NodeLayout& nl = node_layout_.at(node);
                    if (nl.source_ports.size() != 1) continue;

                    Hyperedge* edge = nl.source_ports[0].edge;
                    double min_x = std::numeric_limits<double>::max();
                    double max_x = std::numeric_limits<double>::lowest();
                    for (const auto& src : edge->getSources()) {
                        if (src.get() == node) continue;
                        for (const Port& p : node_layout_.at(src.get()).source_ports)
                            if (p.edge == edge) { min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x); }
                    }
                    bool aligned = false;
                    std::vector<double> tgt_ports; // Store the target ports for later steps
                    for (const auto& tgt : edge->getTargets()) {
                        for (const Port& p : node_layout_.at(tgt.get()).target_ports) {
                            if (p.edge == edge) {
                                if (p.x == nl.source_ports[0].x) aligned = true;
                                min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
                                tgt_ports.push_back(p.x);
                            }
                            if (aligned) break;
                        }
                        if (aligned) break;
                    }
                    if (aligned || min_x > max_x) continue; // Do nothing when two ports are already aligned.
                    double candidate = (min_x + max_x) * 0.5;

                    double low = (i == 0) ? std::numeric_limits<double>::lowest() :
                        nodes[i - 1]->isDummy() ? node_layout_.at(nodes[i - 1].get()).x + (DUMMY_NODE_WIDTH + NODE_WIDTH) * 0.5 + MIN_BLOCK_SEP
                        : node_layout_.at(nodes[i - 1].get()).x + NODE_WIDTH + MIN_BLOCK_SEP;

                    double high = (i == k - 1) ? std::numeric_limits<double>::max() :
                        nodes[i + 1]->isDummy() ? node_layout_.at(nodes[i + 1].get()).x - (DUMMY_NODE_WIDTH + NODE_WIDTH) * 0.5 - MIN_BLOCK_SEP
                        : node_layout_.at(nodes[i + 1].get()).x - NODE_WIDTH - MIN_BLOCK_SEP;

                    std::vector<Interval> forbidden_regions;
                    for (const auto& e : data.outgoing_edges) {
                        if (assigners[layer]->edgeOrderedBefore(e.get(), edge)) {
                            for (const auto& tgt : e->getTargets()) {
                                for (const Port& p : node_layout_.at(tgt.get()).target_ports) {
                                    if (low < p.x + 3 * MIN_VERTICAL_SEP && p.x - 3 * MIN_VERTICAL_SEP < high) {
                                        forbidden_regions.push_back({ p.x - 2 * MIN_VERTICAL_SEP, p.x + 2 * MIN_VERTICAL_SEP });
                                    }
                                }
                            }
                        }
                    }
                    auto allowed_space = subtractRegions({ low, high }, forbidden_regions);
                    if (insideAny(allowed_space, candidate)) {
                        nl.x = candidate;
                        nl.source_ports[0].x = candidate;
                    }
                    else {
                        // Try to align it to the target port which is closest to
                        // the middle point and also lives inside allowed_space.
                        double alignment_port = nl.x;
                        double best_d = std::abs(nl.x - candidate);
                        for (auto port : tgt_ports) {
                            if (insideAny(allowed_space, port)) {
                                double diff = std::abs(port - candidate);
                                if (diff < best_d) {
                                    alignment_port = port;
                                    best_d = diff;
                                }
                                else if (diff == best_d) {
                                    if (std::abs(alignment_port - nl.x) < std::abs(port - nl.x)) {
                                        alignment_port = port;
                                    }
                                }
                            }
                        }
                        if (alignment_port != nl.x) {
                            nl.x = alignment_port;
                            nl.source_ports[0].x = alignment_port;
                        }
                    }
                }
                };

            pass(0, k, 1);       // left  -> right
            pass(k - 1, -1, -1); // right -> left
        }
    }

} // namespace hypergraph_logic