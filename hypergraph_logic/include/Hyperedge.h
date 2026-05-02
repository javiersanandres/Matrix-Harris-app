#pragma once

#include "Node.h"

#include <memory>
#include <vector>

namespace hypergraph_logic {
	class Hyperedge;
	using HyperedgePtr = std::shared_ptr<Hyperedge>;
	using WeakHyperedgePtr = std::weak_ptr<Hyperedge>;

	// Forward declaration for friend access
	class Hypergraph;

	// ============================================================================
	// Hyperedge
	//
	// A directed hyperedge  e = (S, T)  in a hierarchical hypergraph.
	//
	// Sources (S) and targets (T) are disjoint sets of nodes.  After long-edge
	// splitting every hyperedge connects only nodes on adjacent layers — that
	// invariant is enforced by the hypergraph, not here.
	//
	// Two kinds of hyperedge exist:
	//   Original  (isSegment() == false) — supplied by the caller.
	//   Segment   (isSegment() == true)  — one link in the chain that replaced
	//             a long original hyperedge during splitting.  A segment carries
	//             a weak back-reference to the original hyperedge it belongs to
	//             so the full chain can be reconstructed.
	//
	// Ownership model:
	//   - The graph holds HyperedgePtr (shared_ptr) for every hyperedge.
	//   - sources_ and targets_ are weak_ptr: the graph owns the nodes, and a
	//     hyperedge must not extend a node's lifetime.
	//   - origin_ is weak_ptr: the graph owns both the original hyperedge and
	//     its segments, so a segment must not extend the origin's lifetime.
	// ============================================================================
	class Hyperedge : public std::enable_shared_from_this<Hyperedge> {
		friend class Hypergraph;  // Allow Hypergraph to set layer
	public:

		// ── Hyperedge (original) ──────────────────────────────────────────────────────────────────────
		//
		// Constructs an original hyperedge supplied by the caller, with the given sets of source
		// and target nodes. The edge is marked as non-segment and its layer is initialised to -1
		// (unassigned); the Hypergraph sets the layer once the edge is registered.
		//
		explicit Hyperedge(const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets);

		// ── Hyperedge (segment) ───────────────────────────────────────────────────────────────────────
		//
		// Constructs a segment hyperedge created during long-edge splitting. The segment is marked
		// as a segment and stores a weak back-reference to its origin (the original hyperedge it
		// belongs to). Its layer is initialised to -1 and set by the Hypergraph upon registration.
		//
		explicit Hyperedge(const WeakHyperedgePtr& origin, const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets);

		// ── isSegment ─────────────────────────────────────────────────────────────────────────────────
		//
		// Returns true if this hyperedge is a segment created during long-edge splitting,
		// false if it is an original hyperedge supplied by the caller.
		//
		bool isSegment() const noexcept;

		// ── getOrigin ─────────────────────────────────────────────────────────────────────────────────
		//
		// Returns the weak pointer to the original hyperedge this segment belongs to.
		// If this hyperedge is not a segment, the returned weak_ptr is null (default-constructed).
		// Callers must lock the result before use, since the graph owns the origin's lifetime.
		//
		WeakHyperedgePtr getOrigin() const noexcept;

		// ── getLayer ──────────────────────────────────────────────────────────────────────────────────
		//
		// Returns the layer index this hyperedge originates from, i.e. the layer of its source nodes.
		// A value of -1 means the edge has not yet been assigned to any layer by the Hypergraph.
		// For segment edges this is the layer of that particular segment's sources, not of the
		// original edge's sources.
		//
		int getLayer() const noexcept;

		// ====================================================================
		// Adjacency
		// ====================================================================

		// ── getSources ────────────────────────────────────────────────────────────────────────────────
		//
		// Returns the list of source nodes of this hyperedge, resolving and skipping any expired
		// weak pointers. The order reflects the insertion order of addSource calls.
		//
		std::vector<NodePtr> getSources() const;

		// ── getTargets ────────────────────────────────────────────────────────────────────────────────
		//
		// Returns the list of target nodes of this hyperedge, resolving and skipping any expired
		// weak pointers. The order reflects the insertion order of addTarget calls.
		//
		std::vector<NodePtr> getTargets() const;

		// ── containsSource ────────────────────────────────────────────────────────────────────────────
		//
		// Returns true if the given node appears in the source list of this hyperedge.
		// Null input always returns false. Expired weak pointers in the internal list are
		// skipped silently during the scan.
		//
		bool containsSource(const NodePtr& node) const;

		// ── containsTarget ────────────────────────────────────────────────────────────────────────────
		//
		// Returns true if the given node appears in the target list of this hyperedge.
		// Null input always returns false. Expired weak pointers in the internal list are
		// skipped silently during the scan.
		//
		bool containsTarget(const NodePtr& node) const;

		// ====================================================================
		// Mutation
		// ====================================================================

		// ── addSource ─────────────────────────────────────────────────────────────────────────────────
		//
		// Appends the given node to the source list of this hyperedge, stored as a weak pointer.
		// No-ops if the node is null or is already present in the source list.
		// Does not establish any parent/child links on the nodes — the Hypergraph handles that.
		//
		void addSource(const NodePtr& node);

		// ── addTarget ─────────────────────────────────────────────────────────────────────────────────
		//
		// Appends the given node to the target list of this hyperedge, stored as a weak pointer.
		// No-ops if the node is null or is already present in the target list.
		// Does not establish any parent/child links on the nodes — the Hypergraph handles that.
		//
		void addTarget(const NodePtr& node);

		// ── removeSource ──────────────────────────────────────────────────────────────────────────────
		//
		// Removes the given node from the source list of this hyperedge. Also prunes any expired
		// weak pointers encountered during the scan as a side effect.
		// Returns true if the node was found and removed, false otherwise.
		// Does not update any parent/child links on the nodes — the Hypergraph handles that.
		//
		bool removeSource(const NodePtr& node);

		// ── removeTarget ──────────────────────────────────────────────────────────────────────────────
		//
		// Removes the given node from the target list of this hyperedge. Also prunes any expired
		// weak pointers encountered during the scan as a side effect.
		// Returns true if the node was found and removed, false otherwise.
		// Does not update any parent/child links on the nodes — the Hypergraph handles that.
		//
		bool removeTarget(const NodePtr& node);

		// ── replaceSource (single) ────────────────────────────────────────────────────────────────────
		//
		// Convenience overload that replaces oldNode with a single newNode in the source list.
		// Delegates to the vector overload.
		//
		void replaceSource(const NodePtr& oldNode, const NodePtr& newNode);

		// ── replaceSource (multi) ─────────────────────────────────────────────────────────────────────
		//
		// Replaces oldNode with one or more new source nodes in the source list, inserted at the
		// position previously occupied by oldNode. New nodes already present in the source list are
		// skipped to prevent duplicates. If none of the new nodes can be inserted (all duplicates),
		// oldNode is re-inserted to avoid leaving the source list shorter than intended.
		// Does not update any parent/child links — the Hypergraph handles that separately.
		//
		void replaceSource(const NodePtr& oldNode, const std::vector<NodePtr>& newNodes);

		// ── replaceTarget (single) ────────────────────────────────────────────────────────────────────
		//
		// Convenience overload that replaces oldNode with a single newNode in the target list.
		// Delegates to the vector overload.
		//
		void replaceTarget(const NodePtr& oldNode, const NodePtr& newNode);

		// ── replaceTarget (multi) ─────────────────────────────────────────────────────────────────────
		//
		// Replaces oldNode with one or more new target nodes in the target list, inserted at the
		// position previously occupied by oldNode. New nodes already present in the target list are
		// skipped to prevent duplicates. If none of the new nodes can be inserted (all duplicates),
		// oldNode is re-inserted to avoid leaving the target list shorter than intended.
		// Does not update any parent/child links — the Hypergraph handles that separately.
		//
		void replaceTarget(const NodePtr& oldNode, const std::vector<NodePtr>& newNodes);

	private:
		bool is_segment_;
		WeakHyperedgePtr origin_;
		int layer_;  // Layer this hyperedge originates from (set by Hypergraph)
		std::vector<WeakNodePtr> sources_;
		std::vector<WeakNodePtr> targets_;

		/// Set the layer this hyperedge originates from (called by Hypergraph only)
		void setLayer(int layer) noexcept;
	};

}