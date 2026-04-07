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

		// Original hyperedge supplied by the caller.
		explicit Hyperedge(const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets);

		// Segment created during long-edge splitting.
		// origin is the original hyperedge this segment belongs to.
		explicit Hyperedge(const WeakHyperedgePtr& origin, const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets);

		bool isSegment() const noexcept;

		// The original hyperedge this segment belongs to.
		// Returns a null (default-constructed) weak_ptr if this is not a segment.
		WeakHyperedgePtr getOrigin() const noexcept;

		/// Get the layer this hyperedge originates from (layer of sources)
		int getLayer() const noexcept;

		// ------------------------------------------------------------------
		// Adjacency
		// ------------------------------------------------------------------
		std::vector<NodePtr> getSources() const;
		std::vector<NodePtr> getTargets() const;

		/// Check if a specific node is among the targets of this hyperedge
		bool containsSource(const NodePtr& node) const;
		bool containsTarget(const NodePtr& node) const;

		// ------------------------------------------------------------------
		// Mutation 
		// ------------------------------------------------------------------
		void addSource(const NodePtr& node);
		void addTarget(const NodePtr& node);

		bool removeSource(const NodePtr& node);
		bool removeTarget(const NodePtr& node);

		void replaceSource(const NodePtr& oldNode, const NodePtr& newNode);
		void replaceSource(const NodePtr& oldNode, const std::vector<NodePtr>& newNodes);
		void replaceTarget(const NodePtr& oldNode, const NodePtr& newNode);
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