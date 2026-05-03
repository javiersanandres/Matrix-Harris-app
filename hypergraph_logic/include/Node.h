#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
#include <map>

namespace hypergraph_logic {
	class Node;

	using NodePtr = std::shared_ptr<Node>;
	using WeakNodePtr = std::weak_ptr<Node>;

	// ============================================================================
	// Node
	//
	// A vertex in a hierarchical hypergraph.
	//
	// A node is a pure structural object: it knows who its parents and children
	// are, and nothing else about topology.  Layer/depth is a property of the
	// graph as a whole and is computed and stored by the hypergraph, not here.
	//
	// Real nodes  (isDummy() == false) are supplied by the caller and carry a
	// name.  Dummy nodes (isDummy() == true) are inserted automatically during
	// long-edge splitting. 
	//
	// Ownership model:
	//   - The graph holds shared_ptr<Node> for every vertex.
	//   - children_ are weak_ptr so a child does not keep its parent alive.
	//   - parents_  are weak_ptr to break ownership cycles entirely.
	// ============================================================================
	class Hypergraph;  // Forward declaration
	class GraphicalHypergraph;  // Forward declaration

	class Node : public std::enable_shared_from_this<Node> {
		friend class Hypergraph;  // Allow Hypergraph to set layer
		friend class GraphicalHypergraph;  // Allow GraphicalHypergraph to access layout data
	public:
		// ── Node (real) ───────────────────────────────────────────────────────────────────────────────
		//
		// Constructs a real node with the given name. Real nodes are the meaningful vertices
		// supplied by the caller; they are never created internally by the graph infrastructure.
		//
		explicit Node(std::string name);

		// ── Node (dummy) ──────────────────────────────────────────────────────────────────────────────
		//
		// Constructs a dummy node with no name. Dummy nodes are inserted automatically by the
		// graph when a long hyperedge is split into a chain of short segment edges, one dummy
		// per intermediate layer. They are invisible to the caller and carry no semantic content.
		//
		explicit Node();

		// ── isDummy ───────────────────────────────────────────────────────────────────────────────────
		//
		// Returns true if this node is a dummy node created during long-edge splitting,
		// false if it is a real node supplied by the caller.
		//
		bool isDummy() const noexcept;

		// ── getName ───────────────────────────────────────────────────────────────────────────────────
		//
		// Returns the name of this node. For dummy nodes the name is always an empty string.
		//
		const std::string& getName() const noexcept;

		// ── setName ───────────────────────────────────────────────────────────────────────────────────
		//
		// Updates the name of this node. Primarily used during node fusion, where two nodes
		// are merged into one and the surviving node is renamed to the requested label.
		//
		void setName(const std::string& name);

		// ====================================================================
		// Layer management
		// ====================================================================

		// ── getLayer ──────────────────────────────────────────────────────────────────────────────────
		//
		// Returns the layer index this node currently occupies, as assigned by the owning Hypergraph.
		// Layer 0 is the shallowest (root) level; larger values are deeper.
		// The value is kept in sync by the graph whenever the node is relocated.
		//
		int getLayer() const noexcept;

		// ====================================================================
		// Adjacency queries
		// ====================================================================

		// ── getChildren ───────────────────────────────────────────────────────────────────────────────
		//
		// Returns the list of direct children of this node (nodes in the immediately deeper layer
		// that this node is a parent of), resolving and skipping any expired weak pointers.
		// The order reflects the insertion order of addChild calls.
		//
		std::vector<NodePtr> getChildren() const;

		// ── getParents ────────────────────────────────────────────────────────────────────────────────
		//
		// Returns the list of direct parents of this node (nodes in a shallower layer that have
		// this node as a child), resolving and skipping any expired weak pointers.
		// The order reflects the insertion order of addParent calls.
		//
		std::vector<NodePtr> getParents() const;

		// ── getAllAncestors ───────────────────────────────────────────────────────────────────────────
		//
		// Returns the set of all strict ancestors of this node, i.e. every node reachable by
		// following parent links upward transitively. The node itself is not included.
		// Uses a recursive DFS with a visited check to avoid revisiting nodes in graphs with
		// shared ancestry.
		//
		std::unordered_set<Node*> getAllAncestors() const;

		// ── getAllDescendants ─────────────────────────────────────────────────────────────────────────
		//
		// Returns the set of all strict descendants of this node, i.e. every node reachable by
		// following child links downward transitively. The node itself is not included.
		// Uses a recursive DFS with a visited check to avoid revisiting nodes in graphs with
		// shared descendants.
		//
		std::unordered_set<Node*> getAllDescendants() const;

		// ====================================================================
		// Adjacency mutation operations
		// ====================================================================

		// ── addParent ─────────────────────────────────────────────────────────────────────────────────
		//
		// Registers the given node as a direct parent of this node, storing it as a weak pointer.
		// No-ops if parent is null or is already present in the parent list.
		// Does not establish the reciprocal child link on the parent — the caller is responsible
		// for keeping both sides of the relationship consistent.
		//
		void addParent(const NodePtr& parent);

		// ── addChild ──────────────────────────────────────────────────────────────────────────────────
		//
		// Registers the given node as a direct child of this node, storing it as a weak pointer.
		// No-ops if child is null or is already present in the child list.
		// Does not establish the reciprocal parent link on the child — the caller is responsible
		// for keeping both sides of the relationship consistent.
		//
		void addChild(const NodePtr& child);

		// ── removeParent ──────────────────────────────────────────────────────────────────────────────
		//
		// Removes the given node from the parent list of this node. Also prunes any expired
		// weak pointers encountered during the scan as a side effect.
		// Returns true if the parent was found and removed, false otherwise.
		// Does not touch the child list of the removed parent — the caller handles that.
		//
		bool removeParent(const NodePtr& parent);

		// ── removeChild ───────────────────────────────────────────────────────────────────────────────
		//
		// Removes the given node from the child list of this node. Also prunes any expired
		// weak pointers encountered during the scan as a side effect.
		// Returns true if the child was found and removed, false otherwise.
		// Does not touch the parent list of the removed child — the caller handles that.
		//
		bool removeChild(const NodePtr& child);

		// ── replaceParent ─────────────────────────────────────────────────────────────────────────────
		//
		// Replaces oldParent with newParent in this node's parent list, preserving the position
		// of the old entry. If newParent is already present in the parent list, the operation is
		// skipped entirely to avoid duplicates.
		// Does not update the child lists of either oldParent or newParent — the graph handles
		// the reciprocal rewiring separately, by design, so that mid-rewiring state stays consistent.
		//
		void replaceParent(const NodePtr& oldParent, const NodePtr& newParent);

		// ── replaceChild (single) ─────────────────────────────────────────────────────────────────────
		//
		// Convenience overload that replaces oldChild with a single newChild in this node's child list.
		// Delegates to the vector overload.
		//
		void replaceChild(const NodePtr& oldChild, const NodePtr& newChild);

		// ── replaceChild (multi) ──────────────────────────────────────────────────────────────────────
		//
		// Replaces oldChild with one or more new children in this node's child list, inserted at
		// the position previously occupied by oldChild. New children that are already present in
		// the child list are silently skipped to prevent duplicates.
		// Does not update the parent lists of the old or new children — the graph handles the
		// reciprocal rewiring separately, by design, so that mid-rewiring state stays consistent.
		//
		void replaceChild(const NodePtr& oldChild, const std::vector<NodePtr>& newChildren);

	private:
		/// Set the layer this node belongs to (called by Hypergraph only)
		void setLayer(int layer) noexcept;

		bool is_dummy_;
		std::string name_; // empty for dummy nodes
		int layer_;  // Layer assignment by hypergraph
		std::vector<WeakNodePtr> parents_;
		std::vector<WeakNodePtr> children_;
	};
}