#pragma once
#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"

#include <unordered_map>
#include <vector> 


namespace port_assignment_internal {
	using namespace hypergraph_logic;

	// ── PortAssigner ──────────────────────────────────────────────────────────────
	//
	// Internal helper that encapsulates all shared lookup tables and algorithms
	// for one pair of adjacent layers (upper at 'layer', lower at 'layer + 1').
	//
	// The three lookup tables built at construction time:
	//   - leftmost_nodes_  : for each edge, the node(s) with the minimum x.
	//   - rightmost_nodes_ : for each edge, the node(s) with the maximum x.
	//   - hyperedge_order_ : rank of each edge by descending y-coordinate
	//                        (lower index = higher on the canvas).
	//
	// These tables are used by every sub-algorithm, so owning them here avoids
	// passing them through every function call.
	struct PortAssigner {
		PortAssigner(int layer,
			const std::map<int, LayerData>& layers,
			std::unordered_map<Node*, NodeLayout>& node_layout);

		// ── Public operations ─────────────────────────────────────────────────────
		//
		// Call in order:
		//   1. buildPorts(): order and space ports on every node.
		//   2. solveVerticalOverlaps(): nudge ports to remove segment crossings.

		// Order and space ports on every node for this layer pair.
		// Returns the minimum port spacing produced.
		double buildPorts();

		// Detect and resolve vertical-segment overlaps between this layer pair.
		// min_vertical_sep: the minimum required x-gap between any two vertical segments.
		void solveVerticalOverlaps(double min_vertical_sep);

	private:
		int layer_;
		const LayerData& upper_;
		const LayerData& lower_;
		std::unordered_map<Node*, NodeLayout>& node_layout_;

		std::unordered_map<Hyperedge*, std::vector<Node*>> leftmost_nodes_;
		std::unordered_map<Hyperedge*, std::vector<Node*>> rightmost_nodes_;
		std::unordered_map<Hyperedge*, int> hyperedge_order_;

		// ── Lookup-table helpers ───────────────────────────────────────────────────
		void buildEdgeLookups();
		void updateExtremesForNode(Hyperedge* edge, Node* node);

		// pos(node, edge): 0 = leftmost, 1 = middle, 2 = rightmost.
		int nodePositionInEdge(Hyperedge* edge, Node* node) const;

		// ── Port ordering and spacing ──────────────────────────────────────────────
		void orderPorts(Node* node, std::vector<Port>& ports, bool source) const;
		double arrangeSymmetrically(Node* node, std::vector<Port>& ports) const;
		double reduceHorizontalJogs() const;

		// ── Conflict detection ─────────────────────────────────────────────────────
		std::vector<std::pair<Node*, Node*>> detectConflicts(double min_vertical_sep) const;

		// ── Conflict resolution ────────────────────────────────────────────────────
		// Shift a single port left (left=true) or right (left=false) by the
		// 2:1 ratio relative to its neighbour, clamped to the node bounds.
		// Only called when the moving node is a real node.
		void shiftWithFixedPort(double fixed_x, Node* moving_node,
			Port& moving_port, int port_index,
			const std::vector<Port>& ports,
			double min_sep, bool left) const;

		// Rearrange the conflicting port subsets of one upper/lower node pair.
		// upper_range / lower_range: {first_index, last_index} of conflicting ports.
		void rearrangeConflictingPorts(
			Node* upper_node, Node* lower_node,
			std::vector<Port>& upper_ports, std::vector<Port>& lower_ports,
			std::pair<int, int> upper_range, std::pair<int, int> lower_range,
			double min_sep);

		// These are private helpers for rearrangeConflictingPorts that compute the left and right
		// bounds for the merged list of conflicting ports when we have two real nodes colliding.
		double leftBound(Node* upper_node, Node* lower_node, Port& x_o, Port& y_o,
			std::vector<Port>& upper_ports, std::vector<Port>& lower_ports,
			int idx, int idy, double min_sep, std::vector<std::pair<Port*, bool>>& merged);
		double rightBound(Node* upper_node, Node* lower_node, Port& x_n, Port& y_m,
			std::vector<Port>& upper_ports, std::vector<Port>& lower_ports,
			int idx, int idy, double min_sep, std::vector<std::pair<Port*, bool>>& merged);

		// Solve one conflict between two nodes by rearranging the conflicting ports.
		void solveConflict(Node* upper_node, Node* lower_node, double min_sep);
	};

} // namespace port_assignment_internal