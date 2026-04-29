#include "PortAssignment.h"

#include <algorithm>
#include <climits>
#include <limits>

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

                switch (pos_a) {
                case 2: case 1:
                    return source ? hyperedge_order_.at(a.edge) < hyperedge_order_.at(b.edge)
                        : hyperedge_order_.at(a.edge) > hyperedge_order_.at(b.edge);
                case 0:
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
        return spacing;
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
    // aesthetic. Four cases:
    //
    //   Both dummy -> each has exactly one port. Move the upper port only
    //                 (to avoid disturbing the start of a dummy chain below).
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
        // They have only one port each, so upper_range and lower_range are useless.
        if (upper_node->isDummy() && lower_node->isDummy()) {
            double sep = std::abs(lower_ports[0].x - upper_ports[0].x);
            double deficit = min_sep - sep;
            if (upper_ports[0].x < lower_ports[0].x) {
                upper_ports[0].x -= deficit;
            }
            else if (upper_ports[0].x > lower_ports[0].x) {
                upper_ports[0].x += deficit;
            }
            else {
                // Exactly coincident: use edge topology to pick direction.
                Hyperedge* le = lower_ports[0].edge;
                if (isLeftmost(lower_node, le, leftmost_nodes_))  upper_ports[0].x -= deficit;
                else if (isRightmost(lower_node, le, rightmost_nodes_)) upper_ports[0].x += deficit;
                else {
                    Hyperedge* ue = upper_ports[0].edge;
                    upper_ports[0].x += isLeftmost(upper_node, ue, leftmost_nodes_) ? deficit : -deficit;
                }
            }
            return;
        }

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
} // namespace port_assignment_internal

// ============================================================================
// GraphicalHypergraph::assignPorts
// 
// Iterates over every consecutive layer pair in the graph, running the full
// port-assignment pipeline for each:
//   1. buildPorts()            — order and space ports on both layers.
//   2. solveVerticalOverlaps() — nudge ports to eliminate segment crossings,
//                                using the minimum spacing from step 1 as the
//                                required separation threshold.
// ============================================================================
namespace hypergraph_logic {
	using namespace port_assignment_internal;

    void GraphicalHypergraph::assignPorts() {
        int layer_count = static_cast<int>(layers_.size());
        for (int layer = 0; layer < layer_count - 1; layer++) {
            if (layers_.at(layer).outgoing_edges.empty()) continue;
            PortAssigner assigner(layer, layers_, node_layout_);
            double min_spacing = assigner.buildPorts();
            assigner.solveVerticalOverlaps(min_spacing);
        }
    }
} // namespace hypergraph_logic