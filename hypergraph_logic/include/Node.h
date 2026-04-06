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

	class Node : public std::enable_shared_from_this<Node> {
		friend class Hypergraph;  // Allow Hypergraph to set layer
	public:
		// Real node constructor.
		explicit Node(std::string name);

		// Dummy node constructor.
		explicit Node();

		bool isDummy() const noexcept;
		const std::string& getName() const noexcept;

		// ====================================================================
		// Layer management
		// ====================================================================

		/// Get the layer this node belongs to
		int getLayer() const noexcept;

		// Adjacency queries
		std::vector<NodePtr> getChildren() const;
		std::vector<NodePtr> getParents() const;

		std::unordered_set<Node*> getAllAncestors() const;
		std::unordered_set<Node*> getAllDescendants() const;

		// Adjacency mutation operations.

		void addParent(const NodePtr& parent);
		void addChild(const NodePtr& child);

		bool removeParent(const NodePtr& parent);
		bool removeChild(const NodePtr& child);

		void replaceParent(const NodePtr& oldParent, const NodePtr& newParent);
		void replaceChild(const NodePtr& oldChild, const NodePtr& newChild);
		void replaceChild(const NodePtr& oldChild, const std::vector<NodePtr>& newChildren);

	private:
		/// Set the layer this node belongs to (called by Hypergraph only)
		void setLayer(int layer) noexcept;

		bool is_dummy_;
		std::string name_; // empty for dummy nodes
		int layer_;  // Layer assignment by hypergraph (nullopt if not assigned)
		std::vector<WeakNodePtr> parents_;
		std::vector<WeakNodePtr> children_;
	};
}