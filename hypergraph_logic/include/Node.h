#pragma once

#include <cstdint>
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
	// Node attributes
	//
	// Everything the user can introduce about a real node. Kept free of any Qt
	// dependency so the logic libraries stay GUI-agnostic; the UI translates
	// Color into a QColor (QColor(r, g, b, a)) when rendering.
	// ============================================================================

	enum class NodeShape { Rectangle, Circle, Rhombus };

	// Whether a node represents a fire and, if so, whether that fire has ashes.
	// A single enum (instead of two booleans) makes "ashes without fire" unrepresentable.
	enum class FireState { None, Fire, FireWithAshes };

	struct Color {
		uint8_t r = 0;
		uint8_t g = 0;
		uint8_t b = 0;
		uint8_t a = 255;

		bool operator==(const Color& other) const noexcept {
			return r == other.r && g == other.g && b == other.b && a == other.a;
		}
		bool operator!=(const Color& other) const noexcept { return !(*this == other); }
	};

	struct NodeAttributes {
		// Default font size in points: the one Qt applies by default to the node labels.
		static constexpr int DEFAULT_FONT_SIZE = 9;

		std::string name;
		NodeShape shape = NodeShape::Rectangle;
		Color colour = { 255, 255, 200, 255 };  // light yellow
		Color font_colour = { 0, 0, 0, 255 };   // black
		int font_size = DEFAULT_FONT_SIZE;
		FireState fire = FireState::None;

		NodeAttributes() = default;

		// Intentionally implicit: lets every API that takes NodeAttributes keep
		// accepting a plain label, in which case all other attributes are defaulted.
		NodeAttributes(std::string node_name) : name(std::move(node_name)) {}
		NodeAttributes(const char* node_name) : name(node_name) {}

		bool isFire() const noexcept { return fire != FireState::None; }
		bool hasAshes() const noexcept { return fire == FireState::FireWithAshes; }

		bool operator==(const NodeAttributes& other) const noexcept {
			return name == other.name && shape == other.shape && colour == other.colour
				&& font_colour == other.font_colour && font_size == other.font_size
				&& fire == other.fire;
		}
		bool operator!=(const NodeAttributes& other) const noexcept { return !(*this == other); }
	};

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
	// set of NodeAttributes.  Dummy nodes (isDummy() == true) are inserted
	// automatically during long-edge splitting and carry no attributes at all.
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
		friend class GraphicalHypergraph;  // Allow GraphicalHypergraph to set layer
	public:
		// ── Node (real) ───────────────────────────────────────────────────────────────────────────────
		//
		// Constructs a real node with the given attributes. Real nodes are the meaningful vertices
		// supplied by the caller; they are never created internally by the graph infrastructure.
		//
		explicit Node(NodeAttributes attributes);

		// ── Node (real, by name) ──────────────────────────────────────────────────────────────────────
		//
		// Convenience constructor: a real node with the given name and default attributes.
		//
		explicit Node(std::string name);
		explicit Node(const char* name);

		// ── Node (dummy) ──────────────────────────────────────────────────────────────────────────────
		//
		// Constructs a dummy node with no attributes. Dummy nodes are inserted automatically by the
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

		// ── getAttributes ─────────────────────────────────────────────────────────────────────────────
		//
		// Returns the attributes of this node. For dummy nodes, which carry none, a shared
		// default-constructed instance (empty name) is returned instead.
		//
		const NodeAttributes& getAttributes() const noexcept;

		// ── setAttributes ─────────────────────────────────────────────────────────────────────────────
		//
		// Replaces all attributes of this node at once. Primarily used during node fusion, where
		// two nodes are merged into one and the surviving node takes the requested attributes.
		// Throws std::logic_error on dummy nodes.
		//
		void setAttributes(NodeAttributes attributes);

		// ── getName ───────────────────────────────────────────────────────────────────────────────────
		//
		// Returns the name of this node. For dummy nodes the name is always an empty string.
		//
		const std::string& getName() const noexcept;

		// ── setName ───────────────────────────────────────────────────────────────────────────────────
		//
		// Updates only the name of this node, leaving every other attribute untouched.
		// Throws std::logic_error on dummy nodes.
		//
		void setName(const std::string& name);

		// ── Attribute shortcuts ───────────────────────────────────────────────────────────────────────
		//
		// Read-only shortcuts to the individual attributes. For dummy nodes they report the defaults.
		//
		NodeShape getShape() const noexcept;
		const Color& getColour() const noexcept;
		const Color& getFontColour() const noexcept;
		int getFontSize() const noexcept;
		FireState getFireState() const noexcept;
		bool isFire() const noexcept;
		bool hasAshes() const noexcept;

		// ====================================================================
		// Geometry
		// ====================================================================

		// ── getWidth / getHeight ──────────────────────────────────────────────────────────────────────
		//
		// Returns the width/height of the box this node occupies in the drawing, as defined in
		// LayoutTypes.h for its shape. Circles and rhombi report the side of the square they are
		// inscribed in. Dummy nodes report DUMMY_NODE_WIDTH / DUMMY_NODE_HEIGHT.
		//
		double getWidth() const noexcept;
		double getHeight() const noexcept;

		// ── getBoundaryHalfHeight ─────────────────────────────────────────────────────────────────────
		//
		// Returns the vertical distance from the node's centre to its boundary along the vertical
		// line at horizontal offset dx from that centre. The shape is symmetric, so the same value
		// applies above and below the centre. This is where a hyperedge's vertical segment meets
		// the node: for a rectangle it is always getHeight() / 2, for a circle and a rhombus it
		// shrinks towards 0 as |dx| approaches getWidth() / 2. Offsets outside the shape are
		// clamped to its left/right edge. Dummy nodes always return 0.
		//
		double getBoundaryHalfHeight(double dx) const noexcept;

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

		// ── getDesiredLayer ───────────────────────────────────────────────────────────────────────────
		//
		// Returns the layer the user has explicitly requested for this node via
		// Hypergraph::relocateNodeToLayer, or -1 if no such override is currently in effect and the
		// node is simply following the default depth rule (layer = max(parent layers) + 1).
		//
		// An override is only ever recorded while it is *doing something*, i.e. while it places the
		// node strictly deeper than the depth rule would. The owning Hypergraph clears it back to -1
		// automatically the moment it stops making a difference.
		//
		int getDesiredLayer() const noexcept;

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

		/// Set (or clear, with -1) the user-requested layer override (called by Hypergraph only)
		void setDesiredLayer(int desired_layer) noexcept;

		std::optional<NodeAttributes> attributes_; // std::nullopt for dummy nodes
		int layer_;  // Layer assignment by hypergraph
		int desired_layer_;  // User-requested layer override, or -1. Managed by Hypergraph only.
		std::vector<WeakNodePtr> parents_;
		std::vector<WeakNodePtr> children_;
	};
}