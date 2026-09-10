#pragma once

#include "Node.h"
#include "Hyperedge.h"

#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace hypergraph_logic {

	// ============================================================================
	// LayerData
	//
	// Data associated with a single layer in the hypergraph.
	// ============================================================================
	struct LayerData {
		std::vector<NodePtr> nodes;                  // Nodes in this layer (in order)
		std::vector<HyperedgePtr> outgoing_edges;    // Hyperedges from this layer to the next
	};

	// ============================================================================
	// Hypergraph
	//
	// A directed layered hierarchical hypergraph  H = (V, E, λ).
	//
	// Layer assignment follows the depth rule:
	// layer(v) = max{ layer(p) : p is a parent of v } + 1.
	// There is no function λ explicitly stored, the fact that a node belongs
	// to a certain layer is encoded through a dictionary in this class.
	//
	// Ownership model:
	//	 - The graph owns all nodes and hyperedges. 
	// ============================================================================
	class Hypergraph {
	public:
		explicit Hypergraph(const std::string& name);

		const std::string& getName() const { return name_; }
		void setName(const std::string& name) { name_ = name; }

		// ====================================================================
		// Reporting convention used throughout this class
		// ====================================================================
		//
		// Many methods below accept one or both of:
		//
		//   int* out_min_new_layer (default nullptr, protected/internal methods only):
		//     Caller initializes to INT_MAX before the call (or reuses a running value across
		//     several calls in the same operation). Every place that places a node via
		//     addNodeToLayer — whether a brand-new dummy or an existing node being relocated —
		//     merges its layer in via std::min, never overwrites, never raises it back up. Used
		//     internally to determine where a subsequent crossing-minimization pass should start;
		//     never exposed on a public method.
		//
		//   std::set<int>* out_altered_layers (default nullptr, available on every public method
		//     and most protected ones):
		//     Caller passes a std::set<int>& (or nullptr). Every place that registers a hyperedge
		//     as newly present in a layer's outgoing_edges (whether brand new, or an existing edge
		//     settling into a layer it wasn't in before), or that changes an existing, surviving
		//     hyperedge's source or target set (gaining OR losing a member), inserts that layer
		//     number in — never clears, never removes. Plain removal of a whole hyperedge does not
		//     qualify on its own. This is the signal GraphicalHypergraph uses to know which layers'
		//     horizontal-ordering MIP needs recomputing after a structural change.
		//
		// ====================================================================
		// Node management
		// ====================================================================

		// ── getNodesAt ───────────────────────────────────────────────────────────────────────────────
		//
		// Returns the ordered list of nodes belonging to the given layer number.
		// If the layer does not exist, an empty vector is returned.
		//
		std::vector<NodePtr> getNodesAt(int layer) const;

		// ── getAllNodes ───────────────────────────────────────────────────────────────────────────────
		//
		// Returns all nodes currently owned by the graph, in insertion order.
		// This includes both real nodes and dummy nodes created during edge splitting.
		//
		std::vector<NodePtr> getAllNodes() const;

		// ============================================================================
		// Hyperedge management
		// ============================================================================

		// ── getAllHyperedges ──────────────────────────────────────────────────────────────────────────
		//
		// Returns every hyperedge owned by the graph: both original (non-segment)
		// edges and all segment edges that were created when splitting long edges.
		// The original edge always appears before its segments in the returned vector.
		//
		std::vector<HyperedgePtr> getAllHyperedges() const;

		// ====================================================================
		// Layer queries
		// ====================================================================

		// ── getLayerCount ────────────────────────────────────────────────────────────────────────────
		//
		// Returns the number of distinct layers currently present in the graph.
		// Empty layers (which are cleaned up automatically) are not counted.
		//
		int getLayerCount() const;

		// ── getLayers ────────────────────────────────────────────────────────────────────────────────
		//
		// Returns a const reference to the internal layer map, keyed by layer number.
		// Each entry holds the ordered list of nodes and outgoing hyperedges for that layer.
		// The map is ordered by layer number, so iteration proceeds from shallowest to deepest.
		//
		const std::map<int, LayerData>& getLayers() const;

		// ── getLayerData ─────────────────────────────────────────────────────────────────────────────
		//
		// Returns a const reference to the LayerData for a specific layer number.
		// If the requested layer does not exist, a reference to a static empty LayerData is returned.
		//
		const LayerData& getLayerData(int layer) const;

		// ====================================================================
		// Connection addition management
		// ====================================================================

		// ── createNode (with parent) ──────────────────────────────────────────────────────────────────
		//
		// Creates a new real node with the given label and inserts it into the graph.
		// If no parent is provided, the node is placed at layer 0 in position layer_position.
		// If a parent is provided, the node is placed at layer parent->layer + 1 and a new
		// short hyperedge from parent to the new node is created automatically.
		// layer_position controls where within the target layer the node is inserted;
		// out-of-bounds values cause the node to be appended at the end of the layer.
		//
		// When layer_position is -1 (i.e. no specific position is requested), crossing
		// minimization is applied to find the least disruptive position for the new node
		// within its layer, regardless of whether it has a parent or not.
		//
		NodePtr createNode(const std::string& label, int layer_position, const NodePtr& parent, std::set<int>* out_altered_layers = nullptr);

		// ── createParent  ────────────────────────────────────────────────────────────────────────
		//
		// Creates a new real node with the given label and inserts it as a parent of the specified
		// child node. If child is currently at layer 0 it must relocate (the new parent needs
		// layer 0 for itself); otherwise the new parent is placed directly one layer above the
		// child, carrying an explicit desired_layer override if that's deeper than its own natural
		// depth rule of 0 (i.e. whenever child->getLayer() > 1).
		//
		NodePtr createParent(const std::string& label, const NodePtr& child, std::set<int>* out_altered_layers = nullptr);

		// ── createNodeInEdge ────────────────────────────────────────────────────────────────────────
		//
		// Creates a new real node and inserts it as an intermediate node on an existing hyperedge.
		// The original edge is effectively split into two: one from the edge's sources to the new node,
		// and one from the new node to the edge's original targets, which are pushed one layer deeper.
		// This is the main mechanism for adding structure to an existing connection.
		//
		// After the structural changes are applied, a global sifting pass is run starting from the
		// shallowest layer affected by the insertion (i.e. the minimum layer among the sources + 1).
		// The number of sifting rounds is intentionally kept low (3) to minimise disruption to the
		// existing layout while still placing the new node and any new dummy nodes reasonably well.
		//
		NodePtr createNodeInEdge(const std::string& label, const HyperedgePtr& edge, std::set<int>* out_altered_layers = nullptr);

		// ── createNodeNextTo ─────────────────────────────────────────────────────────────────────────
		//
		// Creates a new, unconnected real node and inserts it into the same layer as node, immediately
		// to its left or right without creating any hyperedge.
		//
		NodePtr createNodeNextTo(const std::string& label, const NodePtr& node, bool left);

		// ── createSource ─────────────────────────────────────────────────────────────────────────────
		//
		// Creates a new real node and registers it as an additional source of an existing hyperedge.
		// The new node is placed at the deepest layer that still respects the depth rule for every
		// target of edge (one layer above the shallowest target), rather than always at layer 0.
		// Because every one of edge's existing targets already has at least one parent (edge itself
		// is guaranteed non-empty on both sides through the public API), this placement is always
		// >= 0 and never forces any target to relocate. Throws std::logic_error if edge has no
		// sources or no targets, which should be unreachable through the public API.
		//
		// Crossing minimization is applied after the structural changes:
		//   - If the edge was re-split, minimizeCrossingsForNodes is called for the new source and
		//     all new dummy nodes created by the split, over the range [0, deepest dummy layer].
		//   - If the edge remained short, minimizeCrossingsForNodes is called for the new source
		//     alone, restricted to layer 0.
		//
		NodePtr createSource(const std::string& label, int layer_position, const HyperedgePtr& edge, std::set<int>* out_altered_layers = nullptr);

		// ── createTarget ─────────────────────────────────────────────────────────────────────────────
		//
		// Creates a new real node and registers it as an additional target of an existing hyperedge.
		// The new node is placed at layer max(source layers) + 1, guaranteeing the layering invariant.
		// Parent-child relationships are wired from all existing sources to the new node.
		// If the edge was previously short and the new target placement makes it long, it is re-split.
		//
		// Crossing minimization is applied after the structural changes:
		//   - If the edge was re-split, minimizeCrossingsForNodes is called for the new target and
		//     all new dummy nodes created by the split, over the range
		//     [shallowest dummy layer, target layer].
		//   - If the edge remained short, minimizeCrossingsForNodes is called for the new target
		//     alone, restricted to its layer.
		//
		NodePtr createTarget(const std::string& label, int layer_position, const HyperedgePtr& edge, std::set<int>* out_altered_layers = nullptr);

		// ── addConnection ────────────────────────────────────────────────────────────────────────────
		//
		// Adds a new directed connection (hyperedge) from parent to child, enforcing several invariants:
		//   1. Self-loops are rejected.
		//   2. Redundant connections (where parent is already an ancestor of child) are rejected.
		//   3. Cycle-forming connections are detected and rejected via DFS before any permanent change.
		//   4. Any pre-existing connections that become transitively redundant due to the new edge are
		//      removed, preserving the Hasse-diagram property of the partial order.
		//
		// Depending on the relative layers of parent and child, three cases are handled:
		//   - parent_layer == child_layer - 1: the new edge is already short, inserted directly.
		//   - parent_layer < child_layer:      the child stays where it is, but the edge is long and
		//                                      must be split with dummy nodes through intermediate layers.
		//   - parent_layer >= child_layer:     the child must be pushed to parent_layer + 1, triggering
		//                                      applyRelocationAndPropagate for it and all its descendants.
		//
		// Crossing minimization strategy after the structural changes:
		//   - Short edge: no minimization needed, the new edge fits without creating new nodes.
		//   - Long edge, no transitive removals: minimizeCrossingsForNodes is called for the new dummy
		//     nodes only, over the intermediate layer range [parent_layer + 1, child_layer - 1].
		//   - Long edge, with transitive removals: global sifting (3 rounds) is run from the shallowest
		//     layer affected by any new dummy created during the transitive removal splits.
		//   - Relocation case: global sifting (10 rounds) is run from the shallowest layer among all
		//     of the child's parents, to account for the wider disruption caused by the propagation.
		//
		// Returns the newly created hyperedge (before any splitting).
		//
		HyperedgePtr addConnection(const NodePtr& parent, const NodePtr& child, std::set<int>* out_altered_layers = nullptr);

		// ── addSourceToEdge ───────────────────────────────────────────────────────────────────────────
		//
		// Adds an existing real node as an additional source to an existing hyperedge.
		// The function enforces that no self-connections are created (source == target) and that
		// no cycles would result from the change.
		//
		// A special grouping policy handles the case where the new source already has a connection
		// to some of the edge's targets through a different single-source hyperedge: in that case,
		// those targets are migrated into the current edge (the old edge is cleaned up). If the
		// pre-existing edge has multiple sources, the operation is rejected as ambiguous.
		//
		// After the source is added, targets may need to be relocated downward (since the new source
		// might be in a deeper layer than the existing ones), and the edge may need to be re-split.
		//
		// Crossing minimization strategy after the structural changes:
		//   - No relocation needed, edge stayed short: no minimization.
		//   - No relocation needed, edge is long, no transitive removals: minimizeCrossingsForNodes
		//     for the new dummy nodes over their layer range.
		//   - No relocation needed, edge is long, with transitive removals: the start_layer is updated
		//     to the shallowest layer affected, then global sifting (3 rounds) is run from start_layer.
		//   - Relocation needed: global sifting (10 rounds) from the shallowest source layer + 1,
		//     to account for the wider disruption caused by the propagation.
		//
		void addSourceToEdge(const HyperedgePtr& edge, const NodePtr& source, std::set<int>* out_altered_layers = nullptr);

		// ── addTargetToEdge ───────────────────────────────────────────────────────────────────────────
		//
		// Adds an existing real node as an additional target to an existing hyperedge.
		// Rejects self-connections and cycles, and checks for redundancy similarly to addSourceToEdge.
		//
		// After adding the target, if its current layer is shallower than max(source layers) + 1,
		// it must be relocated downward via applyRelocationAndPropagate; otherwise the edge is
		// re-evaluated for shortness and split if necessary.
		//
		// Crossing minimization strategy mirrors addSourceToEdge exactly:
		//   - No relocation needed, edge stayed short: no minimization.
		//   - No relocation needed, edge is long, no transitive removals: minimizeCrossingsForNodes
		//     for the new dummy nodes over their layer range.
		//   - No relocation needed, edge is long, with transitive removals: global sifting (3 rounds)
		//     from the shallowest layer affected by any split during transitive removal.
		//   - Relocation needed: global sifting (10 rounds) from the shallowest source layer + 1.
		//
		void addTargetToEdge(const HyperedgePtr& edge, const NodePtr& target, std::set<int>* out_altered_layers = nullptr);

		// ====================================================================
		// Removal management
		// ====================================================================

		// ── removeNode ───────────────────────────────────────────────────────────────────────────────
		//
		// Removes a node from the graph entirely, handling three structural cases:
		//   - Leaf node (no children): simply removed from all hyperedges where it appears as a target.
		//   - Root node (no parents): removed from all source positions; its former targets are
		//     relocated upward since they may no longer need to be as deep.
		//   - Internal node (has both parents and children): parents inherit the node's children by
		//     creating a new hyperedge from parents to children, so no structural information is lost.
		//     The new edge is split or relocated as needed.
		//
		// In all cases the node is erased from all_nodes_ and from its layer, and a cleanUp is
		// performed to remove any layers that become empty as a result.
		//
		void removeNode(const NodePtr& node, std::set<int>* out_altered_layers = nullptr);

		// ── removeConnection ─────────────────────────────────────────────────────────────────────────
		//
		// Removes the directed connection between parent and child by finding the hyperedge
		// that contains parent as a source and child as a target, and extracting child from it.
		// If parent was the only source on that edge, the edge is deleted; otherwise, the remaining
		// sources are preserved in a new edge that is re-split or re-layered as needed.
		//
		// If the child relocates upward as a result (it no longer needs to be as deep), crossing
		// minimization is applied:
		//   - If the child has no children of its own: minimizeCrossingsForNodes for any new dummy
		//     nodes created by re-splitting the new edge after relocation.
		//   - If the child has children: global sifting (10 rounds) from the shallowest parent
		//     layer + 1, since the relocation propagates through the child's descendants.
		// If the child does not relocate and the new edge is long: minimizeCrossingsForNodes for
		// the new dummy nodes over their layer range.
		//
		void removeConnection(const NodePtr& parent, const NodePtr& child, std::set<int>* out_altered_layers = nullptr);

		// ── removeSourcesFromHyperedge ────────────────────────────────────────────────────────────────
		//
		// Removes a set of source nodes from an existing original (non-segment) hyperedge and
		// propagates the structural consequences through the segment decomposition of that edge.
		//
		// The propagation works top-down: a segment becomes "dead" when all of its sources have
		// been removed or are themselves dead dummy nodes. Dead segments and their dummy nodes are
		// collected and erased from LayerData and all_nodes_ in a single pass.
		//
		// If no sources remain at all, the entire edge (including all segments) is dissolved.
		// If the remaining sources all live in the layer immediately above the targets, the edge
		// is now short and the segments can be dissolved without dummy nodes.
		//
		// The relocation flag controls whether the targets of the edge are subsequently relocated
		// upward (some of them may now be deeper than necessary if their deepest parent was removed).
		// When relocation is true and at least one node actually moves, global sifting (10 rounds)
		// is run from the shallowest new parent layer + 1 among all relocated targets.
		//
		void removeSourcesFromHyperedge(const HyperedgePtr& edge, const std::unordered_set<Node*>& sources_to_remove, bool relocation, std::set<int>* out_altered_layers = nullptr);

		// ── removeTargetsFromHyperedge ────────────────────────────────────────────────────────────────
		//
		// Symmetric counterpart to removeSourcesFromHyperedge, but propagation goes bottom-up:
		// a segment becomes "dead" when all of its targets have been removed or are dead dummies.
		// Dead dummy sources that only existed to feed those targets are collected and erased.
		//
		// If no targets remain, the entire edge is dissolved. If the edge becomes short after the
		// removal, segments are dissolved and the edge is registered at the correct layer.
		//
		// The relocation flag controls whether the remaining targets are subsequently relocated upward,
		// since some of them may have fewer parents and could legally sit in a shallower layer.
		// When relocation is true and at least one node actually moves, global sifting (10 rounds)
		// is run from the shallowest new parent layer + 1 among all relocated targets.
		//
		void removeTargetsFromHyperedge(const HyperedgePtr& original_edge, const std::unordered_set<Node*>& targets_to_remove, bool relocation, std::set<int>* out_altered_layers = nullptr);

		// ============================================================================
		// Node fusion management
		// ============================================================================

		// ── fuseNodes ────────────────────────────────────────────────────────────────────────────────
		//
		// Merges node2 into node1, transferring all of node2's parent and child relationships to node1
		// and then deleting node2. The merged node is renamed to new_label.
		//
		// The fusion is first attempted tentatively: if unifying the neighbourhoods of the two nodes
		// would introduce a cycle, the operation is rolled back and an exception is thrown.
		//
		// After the structural merge, all hyperedges that referenced node2 (as source or target,
		// in originals or segments) are updated to reference node1 instead. If this produces
		// duplicate hyperedges (same source set and target set), the duplicates are dissolved.
		//
		// Finally, node1 is relocated if its optimal layer changed as a result of inheriting
		// node2's parents; all its descendants are propagated accordingly. If relocation occurs,
		// global sifting (10 rounds) is run from the shallowest parent layer + 1 of node1 to
		// account for the full extent of the disruption.
		//
		void fuseNodes(const NodePtr& node1, const NodePtr& node2, const std::string& new_name, std::set<int>* out_altered_layers = nullptr);

		// ============================================================================
		// Node layer management
		// ============================================================================
		// 
		// ── relocateNodeToLayer ───────────────────────────────────────────────────────────────────────
		//
		// Lets the caller override the default depth rule for a single node, placing it at
		// `desired_layer` instead of the layer the rule would normally assign. This relaxes the
		// depth rule from an equality to the inequality
		//
		//     layer(v) >= max{ layer(p) : p is a parent of v } + 1        (1)
		//
		// (0 if v has no parents), i.e. the node may be pushed to any layer at or below (deeper than)
		// the one the rule would normally give it, but never above it.
		//
		// If desired_layer equals the depth-rule layer exactly, this is equivalent to clearing any
		// existing override. Otherwise the override is recorded (getDesiredLayer() == desired_layer) 
		// and will stick until a later structural change either makes the depth rule catch up with it,
		// or pushes the depth rule past it.
		//
		// Two special values of desired_layer request a brand-new shallowest or deepest layer rather
		// than a specific number:
		//   - desired_layer == -1: creates a new layer above everything else (every existing layer
		//     number is incremented by 1) and places node there. node must have no parents. Throws
		//     if node is currently the sole occupant of layer 0 (the move would be a meaningless
		//     no-op that only relabels the rest of the graph).
		//   - desired_layer > the current deepest layer: places node at exactly (deepest layer + 1),
		//     normalizing whatever literal value was passed to that number instead. node must have
		//     no children. Throws if node is currently the sole occupant of the deepest layer and has
		//     no parents (the move would strand that layer empty with nothing to backfill it).
		// Both special cases are gap-safe: any layer a node vacates that would otherwise be left
		// empty is closed up by the automatic compaction in cleanUp(), which every relocation path
		// ultimately funnels through.
		//
		void relocateNodeToLayer(const NodePtr& node, int desired_layer, std::set<int>* out_altered_layers = nullptr);

	protected:
		std::string name_;

		// Layer assignment: layer number -> LayerData (nodes + edges)
		std::map<int, LayerData> layers_;

		// All nodes owned by this graph
		std::vector<NodePtr> all_nodes_;

		struct HyperedgePtrHash {
			size_t operator()(const HyperedgePtr& e) const {
				return std::hash<Hyperedge*>()(e.get());
			}
		};

		// All hyperedges owned by this graph.
		// The key is the original (non-segment) hyperedge; the value is the list of segment
		// hyperedges created when the original edge spans more than one layer.
		// A short edge has an empty segment list.
		std::unordered_map<HyperedgePtr, std::vector<HyperedgePtr>, HyperedgePtrHash> all_hyperedges_;

		// ============================================================================
		// Node management
		// ============================================================================

		// ── addNodeToLayer ───────────────────────────────────────────────────────────────────────────
		//
		// Inserts a node into the specified layer at the given position, creating the layer entry
		// if it does not yet exist. If position is out of bounds, the node is appended at the end.
		// The node's internal layer field is updated to match. No-ops if the node is already present
		// in that layer.
		//
		void addNodeToLayer(int layer, int position, const NodePtr& node, int* out_min_new_layer = nullptr);

		// ── removeNodeFromLayer (single) ──────────────────────────────────────────────────────────────
		//
		// Removes a single node from the LayerData of the given layer. Does not update any
		// parent/child relationships or hyperedge membership — it is a pure bookkeeping operation
		// intended to be called after the structural changes have already been applied.
		//
		void removeNodeFromLayer(int layer, const NodePtr& node);

		// ── removeNodeFromLayer (batch) ───────────────────────────────────────────────────────────────
		//
		// Batch variant of removeNodeFromLayer that removes all nodes in the given set from the
		// specified layer in a single pass. More efficient than calling the single-node overload
		// repeatedly when many nodes must be removed at once (e.g. dead dummies after edge dissolution).
		//
		void removeNodeFromLayer(int layer, const std::unordered_set<Node*>& nodes);

		// ============================================================================
		// Hyperedge management
		// ============================================================================

		// ── createHyperedge (no origin) ───────────────────────────────────────────────────────────────
		//
		// Creates a new original (non-segment) hyperedge with the given sources and targets,
		// registers it in all_hyperedges_ with an empty segment list, and adds it to the specified
		// layer's outgoing_edges (if layer >= 0).
		//
		// Parent/child relationships are wired between every (source, target) pair, but only for
		// real (non-dummy) nodes, since dummy nodes are internal routing artefacts that real nodes
		// should have no knowledge of.
		//
		HyperedgePtr createHyperedge(const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets, int layer, std::set<int>* out_altered_layers = nullptr);

		// ── createHyperedge (with origin) ────────────────────────────────────────────────────────────
		//
		// Creates a segment hyperedge that belongs to an existing original edge (its origin).
		// The new segment is appended to the origin's segment list in all_hyperedges_ and added
		// to the specified layer's outgoing_edges.
		//
		// Parent/child wiring follows more nuanced rules to keep real nodes unaware of the dummy
		// routing infrastructure:
		//   - dummy → dummy: both parent and child links are established.
		//   - dummy → real:  only the dummy's child link is set (the real node already knows its parents).
		//   - real  → dummy: only the dummy's parent link is set (the real node already knows its children).
		//   - real  → real:  no links are added here; they were already established on the original edge.
		//
		HyperedgePtr createHyperedge(const WeakHyperedgePtr& origin, const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets, int layer, std::set<int>* out_altered_layers = nullptr);

		// ── addHyperedgeToLayer ───────────────────────────────────────────────────────────────────────
		//
		// Registers an existing hyperedge in the outgoing_edges list of the specified layer,
		// creating the layer entry if necessary. Updates the edge's internal layer field.
		// No-ops if the edge is already registered in that layer.
		//
		void addHyperedgeToLayer(int layer, const HyperedgePtr& edge, std::set<int>* out_altered_layers = nullptr);

		// ── removeHyperedgeFromLayer (single) ────────────────────────────────────────────────────────
		//
		// Removes a single hyperedge from the outgoing_edges list of the specified layer and
		// resets the edge's internal layer field to -1. Pure bookkeeping: structural changes
		// must be handled separately by the caller.
		//
		void removeHyperedgeFromLayer(int layer, const HyperedgePtr& edge);

		// ── removeHyperedgeFromLayer (batch) ─────────────────────────────────────────────────────────
		//
		// Batch variant that removes all hyperedges in the given set from the specified layer in
		// a single pass. All removed edges have their internal layer field reset to -1.
		// More efficient than repeated single-edge removal when cleaning up after edge dissolution.
		//
		void removeHyperedgeFromLayer(int layer, const std::unordered_set<Hyperedge*>& edges);

		// ── edgeIsShort ───────────────────────────────────────────────────────────────────────────────
		//
		// Checks whether a hyperedge is "short", meaning every (source, target) pair spans
		// exactly one layer (i.e. target_layer == source_layer + 1 for all pairs).
		//
		// Returns the source layer k if the edge is short, or -1 if the edge is long, empty, or null.
		// This is the key predicate used throughout the graph to decide whether an edge needs to
		// be split into segments with dummy nodes or can be stored directly.
		//
		int edgeIsShort(const HyperedgePtr& edge);

		// ── settleEdgePlacement ───────────────────────────────────────────────────────────────────────
		//
		// Places a freshly created (unsegmented, unlayered) hyperedge into the layer structure: if the
		// edge is short, it is registered directly at its source layer via addHyperedgeToLayer; otherwise
		// it is decomposed into segments via splitLongEdge.
		//
		// This is the "decide short vs. long and act accordingly" step that recurs at every call site
		// that creates or settles a hyperedge with no prior segment/layer state to clean up first.
		// Compare with resettleEdge, below, which additionally handles an edge that may already be
		// registered in a layer and/or already have segments from a previous split.
		//
		// Returns the value of edgeIsShort(edge): the source layer k >= 0 if the edge is short, or -1 if
		// it needed to be split.
		//
		int settleEdgePlacement(const HyperedgePtr& edge, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr);

		// ── collectSegmentDummies ─────────────────────────────────────────────────────────────────────
		//
		// Scans every segment of an already-split edge and appends each dummy source node found to
		// out_nodes, widening min_layer/max_layer to cover the layer range spanned by those sources.
		// Does not modify the edge itself; assumes splitLongEdge has already been called. min_layer/
		// max_layer are only ever widened, never narrowed, so callers should pre-populate them with
		// whatever bounds are relevant on top of which the split's dummies should be considered.
		//
		// include_real_sources controls whether the very first segment's real (non-dummy) sources also
		// count towards the bounds. Because splitLongEdge's first segment is the only one with no carry
		// dummy among its sources (see splitLongEdge), the two modes can differ by exactly one layer at
		// the shallow end: with include_real_sources = true (the default), min_layer can reach down to
		// the edge's original source layer; with false, it only reaches the layer of the first dummy,
		// one layer deeper. Pass false to reproduce a call site that historically only considered dummy
		// nodes when computing its sifting range.
		//
		void collectSegmentDummies(const HyperedgePtr& edge, std::vector<Node*>& out_nodes, int& min_layer, int& max_layer, bool include_real_sources = true);

		// ── settleEdgePlacementAndCollectDummies ─────────────────────────────────────────────────────
		//
		// Combines settleEdgePlacement with collectSegmentDummies: places the edge, and if it had to be
		// split, collects the newly created dummy nodes into seed_nodes and widens min_layer/max_layer
		// accordingly. If the edge is short, seed_nodes/min_layer/max_layer are left exactly as the
		// caller passed them in, so callers should pre-populate them with whatever nodes/bounds are
		// relevant to the short case before calling this (this is what almost every call site that
		// creates or rebuilds a hyperedge needs immediately afterwards, to know what to hand to
		// minimizeCrossingsForNodes).
		//
		// include_real_sources is forwarded to collectSegmentDummies; see its comment above.
		//
		// Returns the same value as settleEdgePlacement.
		//
		int settleEdgePlacementAndCollectDummies(const HyperedgePtr& edge, std::vector<Node*>& seed_nodes, int& min_layer, int& max_layer, bool include_real_sources = true, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr);


		// ── settleAndMinimizeIfSplit ──────────────────────────────────────────────────────────────────
		//
		// Convenience wrapper for the common case of settling a freshly created edge that has no
		// existing node to seed the minimization with: if the edge had to be split, its new dummy nodes
		// are placed via minimizeCrossingsForNodes; if it turned out short, nothing further is done,
		// since a short edge introduces no new dummy nodes to place.
		//
		void settleAndMinimizeIfSplit(const HyperedgePtr& edge, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr);

		// ── collapseToShortLayer ──────────────────────────────────────────────────────────────────────
		//
		// Used when an existing (possibly already-split) edge has just become short again after losing
		// some of its sources or targets: dissolves any existing segments and registers the edge at
		// layer k directly.
		//
		void collapseToShortLayer(const HyperedgePtr& edge, int k, std::set<int>* out_altered_layers = nullptr);

		// ── resettleEdge ──────────────────────────────────────────────────────────────────────────────
		//
		// Re-evaluates the placement of an edge that may already be registered in a layer and/or already
		// have segments (used after node relocation, where an edge's shortness may have changed as a
		// side effect). If the edge is now short, any existing segments are dissolved and the edge is
		// moved to the correct layer (a no-op if it is already there); if it is long, it is (re-)split
		// via splitLongEdge, which itself dissolves any stale segments before rebuilding them.
		//
		void resettleEdge(const HyperedgePtr& edge, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr);

		// ── minimizeCrossingsAfterRelocation ──────────────────────────────────────────────────────────
		//
		// Called right after applyRelocationAndPropagate to run the disruptive, many-rounds sifting pass
		// that such a relocation warrants. The floor of the sifting range (start_layer) is pulled up to
		// no deeper than one layer below the shallowest of reference_nodes, so that any new dummy nodes
		// introduced by the relocation are covered even if start_layer was already set from an earlier,
		// unrelated adjustment.
		//
		void minimizeCrossingsAfterRelocation(const std::vector<NodePtr>& reference_nodes, int start_layer);

		// ── minimizeCrossingsForRelocatedTargets ──────────────────────────────────────────────────────
		//
		// Called after relocating the targets of original_edge, when the caller has no more specific
		// start_layer of its own to offer: recomputes the shallowest layer among the parents of
		// original_edge's (now possibly relocated) targets, and runs global sifting from there if that
		// layer is shallower than any target's current layer.
		//
		void minimizeCrossingsForRelocatedTargets(const HyperedgePtr& original_edge);

		// ============================================================================
		// Helper methods for connection management
		// ============================================================================

		// ── resyncSegmentEndpoints ────────────────────────────────────────────────────────────────────
		//
		// Used by splitLongEdge to update a reused segment's source/target set in place, rather than
		// destroying and recreating it, when the segment's endpoints have changed between the
		// previous split and this one. Computes which sources/targets were added, removed, or kept,
		// breaks/establishes parent-child links accordingly, and applies the change to the segment.
		// No-ops entirely if nothing actually changed.
		//
		// Reports segment->getLayer() into out_altered_layers whenever the segment survives with a
		// different source or target set than before (gaining or losing a member both qualify).
		//
		void resyncSegmentEndpoints(const HyperedgePtr& segment, const std::vector<NodePtr>& new_sources, const std::vector<NodePtr>& new_targets, std::set<int>* out_altered_layers = nullptr);

		// ── splitLongEdge ────────────────────────────────────────────────────────────────────────────
		//
		// Decomposes a long hyperedge into a chain of short segment edges, inserting dummy nodes
		// in every intermediate layer so that the layering invariant is satisfied.
		//
		// The algorithm groups sources and targets by layer, then iterates layer-by-layer from the
		// shallowest source to the deepest target. For each step L → L+1, a segment edge is created
		// whose sources are the real sources at layer L (if any) plus a carry dummy produced by the
		// previous segment, and whose targets are the real targets at layer L+1 (if any) plus a new
		// carry dummy that will feed the next segment. This carry dummy threads the signal through
		// layers that have neither real sources nor real targets.
		//
		// If the edge was already split (segments exist), segments and dummies at layers still
		// covered by the new split are reused in place via resyncSegmentEndpoints rather than
		// destroyed and recreated; anything left over from the old split that isn't reused is torn
		// down.
		//
		void splitLongEdge(const HyperedgePtr& long_edge, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr);

		// ── dissolveSegments ─────────────────────────────────────────────────────────────────────────
		//
		// Removes all segment edges and their associated dummy nodes for the given set of original
		// edges. The original edges themselves are left intact; only the segment decomposition is
		// torn down so that the edges can be treated as un-split (i.e. their segment list becomes
		// empty again).
		//
		// All dummy sources and targets belonging to any of the segments are collected, removed from
		// their layers, and erased from all_nodes_ in a single batch for efficiency.
		//
		void dissolveSegments(const std::unordered_set<Hyperedge*>& long_edges);

		// ── removeTransitiveConnections ───────────────────────────────────────────────────────────────
		//
		// Given a set of parents and a set of children that are about to be linked by a new edge,
		// finds and removes all pre-existing hyperedge connections that the new edge makes redundant
		// by transitivity.
		//
		// Specifically, for every existing edge whose sources are ancestors of (or equal to) the
		// new parents AND whose targets are descendants of (or equal to) the new children, the
		// overlapping (source, target) combinations are redundant. The ancestor sources are removed
		// from those edges; if the remaining targets of those sources are still needed, a new trimmed
		// edge is created to preserve that non-redundant information. If that trimmed edge is long,
		// it is split, creating new dummy nodes in intermediate layers.
		//
		// This enforces the Hasse-diagram invariant: no connection exists if it can be inferred by
		// following other connections transitively.
		//
		// edge_to_skip, if provided, is excluded from the scan entirely. This is needed when a caller
		// has just created a replacement edge that exactly represents the connection currently being
		// added (parents -> children): without exclusion, that replacement would be found by this
		// function as a trivial self-match (its sources are among parents, its target is among
		// children) and be destroyed with nothing put in its place, silently discarding the very
		// connection the caller just built.
		//
		void removeTransitiveConnections(const std::vector<NodePtr>& parents, const std::vector<NodePtr>& children, const HyperedgePtr& edge_to_skip = nullptr, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr);

		// ── resolveOwnRedundantTargets ────────────────────────────────────────────────────────────────
		//
		// Used exclusively by addTargetToEdge, right before its sources are extended to also reach
		// `target`: strips any of edge's pre-existing targets that become transitively redundant once
		// edge's sources can reach them via `target` (and anything target already reaches). If this
		// drains every target from edge, edge is dissolved by the strip itself, and a fresh replacement
		// {edge's sources} -> {target} is built and settled in its place.
		//
		// Returns the replacement edge if edge was dissolved and replaced this way — in that case the
		// caller must not use edge any further, must not call edge->addTarget(target) itself (the
		// replacement already carries that connection), and must pass the returned edge as edge_to_skip
		// to any subsequent removeTransitiveConnections call, or that call will find and destroy it as
		// a trivial self-match. Returns nullptr if edge survived (possibly with some targets removed),
		// in which case the caller's normal edge->addTarget(target) is still required.
		//
		HyperedgePtr resolveOwnRedundantTargets(const HyperedgePtr& edge, const NodePtr& target, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr);

		// ── resolveTargetLayer ───────────────────────────────────────────────────────────────────────
		//
		// Computes the layer a node should currently occupy, following the depth rule or desired_layer
		// field of the node. The computation is performed in the following manner:
		// 
		//   - If node has no override (getDesiredLayer() == -1), returns the depth-rule layer.
		//   - If the override is still strictly deeper than the depth-rule layer, it is honoured
		//     as-is and returned unchanged.
		//   - If the override is no longer doing anything (it now equals the depth-rule layer) or is 
		//     no longer valid (it is now shallower than the depth-rule layer), the override is cleared
		//     and the plain depth-rule layer is returned instead.
		//
		int resolveTargetLayer(const NodePtr& node);

		// ── relocateNodes ────────────────────────────────────────────────────────────────────────────
		//
		// Determines whether any of the given nodes are sitting at a layer that disagrees with the
		// layer resolveTargetLayer computes for them (the depth rule, or a valid desired_layer
		// override — see resolveTargetLayer). Any node that needs to move is collected and handed
		// off to applyRelocationAndPropagate.
		//
		// Returns true if at least one relocation was performed, false otherwise.
		// Callers use this return value to skip redundant edge-splitting when the targets of a newly
		// created edge have already been moved by relocation.
		//
		bool relocateNodes(const std::vector<NodePtr>& nodes, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr);

		// ── choosePositionForRelocatedNode ───────────────────────────────────────────────────────────
		//
		// Hook for subclasses that track a horizontal coordinate per node (namely
		// GraphicalHypergraph) to control where a relocated node lands within its new layer's node
		// list, instead of always being appended at the end. Called once per node, right before it
		// is (re-)inserted into its new layer.
		//
		virtual int choosePositionForRelocatedNode(int /*new_layer*/, const NodePtr& /*node*/) const {
			return -1;
		}

		// ── renumberLayersFrom ────────────────────────────────────────────────────────────
		//
		// Shifts every layer numbered from_layer or deeper up by exactly 1 (old_layer -> old_layer + 1),
		// leaving from_layer itself empty and every layer below it untouched. This is a pure
		// renumbering: it moves LayerData entries to their new keys and keeps every node's and
		// hyperedge's own layer_ field in sync, but does not create, destroy, or reconnect anything.
		//
		void renumberLayersFrom(int from_layer);

		// ── applyRelocationAndPropagate ───────────────────────────────────────────────────────────────
		//
		// Moves each node in the relocation list to its new target layer and then propagates
		// the structural consequences through three phases:
		//
		//   Phase 1 – Move nodes and fix incoming edges:
		//     All relocated nodes are moved in LayerData. Every original edge that has a relocated
		//     node as a target is re-evaluated: if it is now short it is placed at the correct layer
		//     (dissolving any old segments); if it is still long it is re-split.
		//
		//   Phase 2 – Propagate depth changes to descendants:
		//     All descendants of the relocated nodes are collected. For each descendant, its target
		//     layer is computed via resolveTargetLayer (the depth rule, or the descendant's own valid
		//     desired_layer override — see resolveTargetLayer). Any descendant whose target layer no
		//     longer matches its current layer is moved there; a descendant carrying its own valid
		//     override that already satisfies invariant (1) is left in place rather than being forced
		//     back onto the plain depth rule. Nodes are processed in ascending layer order so that
		//     when a node is re-evaluated all of its parents have already been updated.
		//
		//   Phase 3 – Rebuild edges for affected descendants:
		//     Every original edge that touches a node whose layer actually changed in Phase 2 is
		//     re-evaluated for shortness and split or relocated as needed.
		//
		// A cleanUp call at the end removes any layers that have become empty.
		//
		void applyRelocationAndPropagate(const std::vector<std::pair<NodePtr, int>>& relocations, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr);

		// ── compactLayerNumbers ─────────────────────────────────────────────────────────
		//
		// Re-ranks every existing layer key to be dense and 0-based: the shallowest layer present
		// becomes 0, the next-shallowest becomes 1, and so on, with no gaps in between; regardless
		// of what the original numbers were or where any gaps sat.
		//
		// Called automatically at the end of cleanUp(), since that's the only place a layer entry is
		// ever erased. No-op (does not even walk layers_ a second time) if the numbering is already
		// dense from 0.
		//
		void compactLayerNumbers();

		// ── cleanUp ──────────────────────────────────────────────────────────────────────────────────
		//
		// Scans the layer map and erases any entries whose node list and outgoing edge list are both
		// empty. Called after structural modifications to prevent the layer map from accumulating
		// stale entries for layers that no longer contain any content.
		//
		void cleanUp();

		// ── parentIsInAncestors ───────────────────────────────────────────────────────────────────────
		//
		// Returns true if the given parent node can be reached by traversing upward (towards shallower
		// layers) from any of the given children nodes. Uses a pruned DFS that skips branches that are
		// provably shallower than the parent's layer, avoiding a full ancestor traversal.
		//
		// Used to detect redundant connections before they are added (if the parent is already an
		// ancestor, the connection adds no new structural information).
		//
		bool parentIsInAncestors(const std::vector<NodePtr>& children, const NodePtr& parent) const;

		// ── childIsInDescendants ──────────────────────────────────────────────────────────────────────
		//
		// Symmetric counterpart to parentIsInAncestors. Returns true if the given child node can be
		// reached by traversing downward (towards deeper layers) from any of the given parent nodes.
		// Branches deeper than the child's layer are pruned for efficiency.
		//
		bool childIsInDescendants(const std::vector<NodePtr>& parents, const NodePtr& child) const;

		// ── getAllAncestors ───────────────────────────────────────────────────────────────────────────
		//
		// Returns the set of all strict ancestors of the given nodes (i.e. every node reachable by
		// following parent links upward, excluding the input nodes themselves). Uses a recursive DFS
		// with a visited set to avoid revisiting nodes in graphs with shared ancestry.
		//
		std::unordered_set<Node*> getAllAncestors(const std::vector<NodePtr>& nodes);

		// ── getAllDescendants ─────────────────────────────────────────────────────────────────────────
		//
		// Returns the set of all strict descendants of the given nodes (i.e. every node reachable by
		// following child links downward, excluding the input nodes themselves). Uses a recursive DFS
		// with a visited set to avoid revisiting nodes in graphs with shared descendants.
		//
		std::unordered_set<Node*> getAllDescendants(const std::vector<NodePtr>& nodes);

		// ── checkCycles ───────────────────────────────────────────────────────────────────────────────
		//
		// Checks whether the subgraph reachable from the given node (following child links) contains
		// any directed cycle. Uses the standard DFS back-edge detection algorithm: a node is on the
		// current DFS path (tracked in a hash set for O(1) lookup); if a child is already on the path,
		// a cycle exists.
		//
		// Called before permanently committing any structural change that adds new parent/child links,
		// so that the graph can be rolled back if a cycle would be introduced.
		//
		bool checkCycles(const NodePtr& node);

		// ====================================================================
		// Crossing minimization management
		// ====================================================================

		// ── minimizeCrossings ────────────────────────────────────────────────────────────────────────
		//
		// Runs the global sifting algorithm over all layers from start_layer to the deepest layer
		// in the graph, writing the optimised node order back to LayerData::nodes.
		//
		// This is the heavy-weight crossing minimization path. It is called after operations that
		// cause widespread structural disruption — such as node relocation with propagation — where
		// many layers are affected simultaneously and a focused per-node pass would be insufficient.
		// The number of sifting rounds is tuned per call site: higher values (e.g. 10) are used when
		// the disruption is large; lower values (e.g. 3) are used when fewer nodes are affected and
		// preserving the existing layout matters more than reaching a global optimum.
		//
		// Returns the crossing count after sifting.
		//
		int minimizeCrossings(int sifting_rounds, int start_layer);

		// ── minimizeCrossingsForNodes ─────────────────────────────────────────────────────────────────
		//
		// Runs a targeted crossing minimization pass that only moves the blocks associated with the
		// given nodes (plus any hub whose every neighbour is also movable), leaving all other nodes
		// in place. This preserves the existing layout as much as possible — the mental map — while
		// still finding the best positions for the newly introduced nodes.
		//
		// This is the light-weight crossing minimization path. It is called when a small number of
		// new nodes are introduced (a new real node, or the dummy nodes from a single edge split)
		// and the rest of the graph should be disturbed as little as possible.
		//
		// The sifting is performed over [start_layer, end_layer]. The result is written back to
		// LayerData::nodes. Returns the crossing count after the pass.
		//
		int minimizeCrossingsForNodes(const std::vector<Node*>& nodes, int start_layer, int end_layer);

	private:
		// ── relabelLayerContents ─────────────────────────────────────────────────────────
		//
		// Shared plumbing for renumberLayersFrom and compactLayerNumbers: retargets every node's
		// (and, if active, its desired_layer_ override's) and every outgoing edge's stored layer
		// number from whatever it currently is to new_layer. Does not touch layers_ itself, callers
		// are responsible for moving the LayerData to its new key.
		//
		void relabelLayerContents(const LayerData& data, int new_layer);
	};
} // namespace hypergraph_logic