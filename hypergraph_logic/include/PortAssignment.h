#pragma once
#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"

#include <unordered_map>
#include <utility>
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
		// Order and space ports on every node for this layer pair.
		// Returns the minimum port spacing produced.
		double buildPorts();

		// ── Jog reduction ─────────────────────────────────────────────────────────
		//
		// Tries to align, on every edge, the source and target ports that are nearly
		// vertical to each other (closer than MIN_BLOCK_SEP / 2) and those of a source
		// and a target sharing the edge's leftmost or rightmost x, closest pairs first,
		// so that the connecting vertical segment has no horizontal jog. A root or a
		// leaf whose only port is the one being aligned moves its whole box instead,
		// within the room its neighbours leave.
		// Returns the minimum port spacing remaining after adjustments.
		double reduceHorizontalJogs() const;

		// Redistributes free ports evenly within each gap defined by fixed anchor
		// ports and the node boundaries. Called by reduceHorizontalJogs after all
		// alignments are done to restore symmetry on affected nodes. The node boundaries
		// are node_x -/+ node_width / 2.
		// Returns the minimum spacing produced between adjacent ports after redistribution.
		double redistributePorts(std::vector<Port>& ports,
			const std::unordered_set<Port*>& fixed,
			double node_x, double node_width) const;

		// Detect and resolve vertical-segment overlaps between this layer pair.
		// min_vertical_sep: the minimum required x-gap between any two vertical segments.
		void solveVerticalOverlaps(double min_vertical_sep);

		// ── Dummy-chain support ────────────────────────────────────────────────────
		//
		// Used by GraphicalHypergraph::straightenDummyChains(), which settles a
		// dummy chain's final x before reduceHorizontalJogs()/solveVerticalOverlaps()
		// ever run. A chain can span several layer pairs, so that pass needs to
		// query specific pairs directly rather than duplicating any of the lookup
		// tables below -- these methods are that entry point.

		// O(1) index of 'node' within its own layer's node list (upper_ if
		// is_upper, otherwise lower_), backed by a table built once at
		// construction instead of a linear scan.
		int positionInLayer(Node* node, bool is_upper) const;

		// Relative rank of two edges of this pair (see hyperedge_order_): true
		// iff 'a' is ranked before 'b' (a's bar sits closer to the upper row).
		bool edgeOrderedBefore(Hyperedge* a, Hyperedge* b) const;

		bool isLeftMost(Hyperedge* a, Node* n) const;

		bool isRightMost(Hyperedge* a, Node* n) const;

		// Result of searching for the port matching a given edge on one side
		// of this pair; 'found' is false if no such port exists here.
		struct PortLookup {
			Node* node = nullptr;
			double x = 0.0;
			bool found = false;
		};

		// edge's source port, found among upper_'s nodes.
		PortLookup findSourceFor(Hyperedge* edge) const;
		// edge's target port, found among lower_'s nodes.
		PortLookup findTargetFor(Hyperedge* edge) const;

		// Forbidden x-regions for a boundary port belonging to boundary_edge.
		// "AsUpper": boundary_edge plays the upper role here (a source port on
		// upper_); every target port on lower_ whose bar sits above it
		// (ranked before it) is a genuine obstacle. "AsLower" is the mirror:
		// boundary_edge plays the lower role (a target port on lower_); every
		// source port on upper_ ranked after it is the obstacle. See the .cpp
		// for the geometric argument (which vertical runs can actually share
		// a height range).
		std::vector<std::pair<double, double>> forbiddenRegionsAsUpper(Node* upper_node, double min_sep, Node* skip = nullptr) const;
		std::vector<std::pair<double, double>> forbiddenRegionsAsLower(Node* lower_node, double min_sep, Node* skip = nullptr) const;

	private:
		int layer_;
		const LayerData& upper_;
		const LayerData& lower_;
		std::unordered_map<Node*, NodeLayout>& node_layout_;

		std::unordered_map<Hyperedge*, std::vector<Node*>> leftmost_nodes_;
		std::unordered_map<Hyperedge*, std::vector<Node*>> rightmost_nodes_;
		std::unordered_map<Hyperedge*, int> hyperedge_order_;

		// Index of each node within its own layer's node list
		std::unordered_map<Node*, int> upper_pos_;
		std::unordered_map<Node*, int> lower_pos_;

		// ── Lookup-table helpers ───────────────────────────────────────────────────
		void buildEdgeLookups();
		void updateExtremesForNode(Hyperedge* edge, Node* node);

		// pos(node, edge): 0 = leftmost, 1 = middle, 2 = rightmost.
		int nodePositionInEdge(Hyperedge* edge, Node* node) const;

		// ── Port ordering and spacing ──────────────────────────────────────────────
		void orderPorts(Node* node, std::vector<Port>& ports, bool source) const;
		double arrangeSymmetrically(Node* node, std::vector<Port>& ports) const;

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