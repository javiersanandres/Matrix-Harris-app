#pragma once

#include "Hypergraph.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <utility>
#include <unordered_set>
#include <string>

using json = nlohmann::json;

namespace port_assignment_internal { struct PortAssigner; };

namespace hypergraph_logic {

	// ── Port ──────────────────────────────────────────────────────────────────────
	//
	// A single connection point between a node and a hyperedge, carrying the
	// coordinates at which the vertical segment of the edge leaves or arrives
	// at the node.
	//
	// x is fixed by port assignment (stage 4), which only cares about the node's
	// width. y is fixed afterwards by assignYCoordinates (stage 5): the shape is
	// inscribed in its getWidth() x getHeight() box and y is where the vertical
	// line through x meets the shape's boundary — the bottom half for source
	// ports, the top half for target ports. For rectangles that is always the
	// box's bottom/top edge; for circles and rhombi it gets closer to the node's
	// centre the further x is from it. Dummy ports sit exactly on the layer's y.
	//
	// uncertain mirrors the connection's mark for this node (see
	// Hyperedge::isSourceUncertain / isTargetUncertain): the user is not sure this
	// node belongs to the connection, so the part of the line that only exists to
	// reach this port is drawn discontinuous. Always false on dummy nodes. It is
	// refreshed by refreshUncertainPorts(), which computeLayout() calls.
	//
	struct Port {
		Hyperedge* edge = nullptr;  // The hyperedge this port belongs to.
		double x = 0.0;             // Assigned x coordinate on the node's top/bottom edge.
		double y = 0.0;             // Assigned y coordinate on the node's boundary.
		bool uncertain = false;     // The connection is doubted at this node.
	};

	// ── NodeLayout ────────────────────────────────────────────────────────────────
	//
	// All layout data associated with a single node.
	struct NodeLayout {
		double x = 0.0;                 // Assigned x coordinate of the node centre by Brandes-Köpf.
		// The node this entry was laid out for. The map is keyed by raw pointer and entries of
		// deleted nodes are not erased, so a new node allocated at the same address would look
		// already laid out: an entry only counts while this is alive and is that very node.
		std::weak_ptr<Node> node;
		std::vector<Port> source_ports; // Ports for edges leaving this node (going downward).
		std::vector<Port> target_ports; // Ports for edges arriving at this node (from above).
	};

	// ============================================================================
	// GraphicalHypergraph
	//
	// Extends Hypergraph with all necessary functionality to support graphical
	// layout and rendering. This includes:
	// 1) Crossing minimization via Global Sifting.
	// 2) Node coordinate assignment via Brandes-Köpf algorithm.
	// 3) Removing horizontal overlapping of hyperedges in each layer with an MIP.
	// 4) Assigning ports to the hyperedges and removing vertical overlapping.
	// 5) Assigning y coordinate for the horizontal span of hyperedges.
	//
	// All the techniques used are based on the paper below:
	// Fridman, G., Vasiliev, Y., Puhkalo, V., & Ryzhov, V. (2021).
	// "A Mixed-Integer Program for Drawing Orthogonal Hyperedges
	// in a Hierarchical Hypergraph."
	// In: Mathematics 9, no. 16: 1903.
	// DOI: 10.3390/math9161903
	// ============================================================================
	class GraphicalHypergraph : public Hypergraph {
	public:
		explicit GraphicalHypergraph(const std::string& name);

		// ── Unique identity ───────────────────────────────────────────────────────
		//
		// Every GraphicalHypergraph receives a unique ID at construction time,
		// generated from a process-wide monotonically increasing counter. The ID
		// is preserved through clone() and toJSON()/fromJSON() so that the
		// JointGraphicalHypergraph can recognise a graph it has already
		// incorporated even after the graph has been serialized and reloaded.
		//
		const std::string& getId() const { return id_; }

		// ── Stage 1: crossing minimisation ────────────────────────────────────────
		//
		// Runs Global Sifting to reorder nodes within each layer so as to
		// minimise the total number of edge crossings.
		//
		//   sifting_rounds — maximum number of full passes over all nodes.
		//                    Terminates early when no improvement is found.
		//                    Default: 10 (empirical).
		//
		// Returns the total crossing count of the final ordering.
		//
		int minimizeCrossings() {
			return Hypergraph::minimizeCrossingsILP();
		}

		// ── Stage 2: node x-coordinates ───────────────────────────────────────────
		//
		// Assigns an x-coordinate to every node using the Brandes-Köpf algorithm.
		//
		void assignXCoordinates();

		// ── computeLayout ─────────────────────────────────────────────────────────
		//
		// Run the complete layout pipeline, executing stages 2-5 in order.
		// After this call, all layout data is known and the hypergraph can
		// be represented graphically.
		//
		void computeLayout(const std::set<int>& mip_layers);
		void computeLayout();

		// ── refreshUncertainPorts ─────────────────────────────────────────────────
		//
		// Copies each connection's uncertain ends into Port::uncertain on the
		// real nodes' ports. Cheap and layout-neutral: marking an end does not move
		// anything, so callers that only change marks need this, not computeLayout.
		//
		void refreshUncertainPorts();

		// ── setHyperedgeOrder ─────────────────────────────────────────────────────
		//
		// Replaces the left-to-right order of the hyperedges leaving layer (the
		// order computeLayout's MIP chooses) with order, which must hold exactly
		// the same hyperedges; otherwise std::invalid_argument is thrown and
		// nothing changes. Only the stored order changes: call computeLayout({})
		// afterwards to redraw with it. Used to undo a change whose layout
		// reordered the bars, reproducing the previous drawing exactly.
		//
		void setHyperedgeOrder(int layer, const std::vector<HyperedgePtr>& order);

		// ── relocateNodeInLayer ───────────────────────────────────────────────────
		//
		// Allows the user to permute the nodes in a layer by providing a new
		// x coordinate for the node being moved. The coordinate is interpreted
		// only as an ordering signal — actual coordinates are reassigned by
		// computeLayout() which is called internally at the end.
		//
		void relocateNodeInLayer(const NodePtr& node, double new_x_coordinate,
								std::set<int>* out_altered_layers = nullptr);

		// ── relocateNodeToLayer ───────────────────────────────────────────────────
		//
		// Allows the user to move current node to the layer which span corresponds 
		// to new_y_coordinate. The span of a layer is calculated as follows:
		//	[ ( h(next(layer)) + h(layer) ) / 2, ( h(prev(layer)) + h(layer) ) / 2 ]
		// If there were no deepest layers the lower bound will be computed as:
		//				h(layer) - (H(layer) + LAYER_GAP) / 2
		// If there were no shallowest layers the upper bound will be computed as:
		//				h(layer) + (H(layer) + LAYER_GAP) / 2
		// where H(layer) is the height of the layer's tallest node. A coordinate
		// above every span (past h(shallowest) + (H(shallowest) + LAYER_GAP) / 2)
		// requests a new shallowest layer, and one below every span (past
		// h(deepest) - (H(deepest) + LAYER_GAP) / 2) a new deepest one: the same
		// half gap on both sides.
		// The layer is chosen by layerForY.
		//
		void relocateNodeToLayer(const NodePtr& node, double new_y_coordinate, 
									std::set<int>* out_altered_layers = nullptr);

		// ── layerForY ─────────────────────────────────────────────────────────────
		//
		// Returns the layer whose span (see relocateNodeToLayer) contains y: -1 when
		// y asks for a new shallowest layer, deepest + 1 when it asks for a new
		// deepest one, and 0 when no layout has been computed yet (empty graph).
		// The result is exactly what relocateNodeToLayer and createNode expect.
		//
		int layerForY(double y) const;

		// ── getX ──────────────────────────────────────────────────────────────────
		//
		// Returns the assigned x coordinate of the given node's centre.
		//
		double getX(const NodePtr& node) const;

		// ── Layout data accessors (read-only, for the graphical engine) ───────────

		// ── getNodeLayout ─────────────────────────────────────────────────────────
		//
		// Returns the full node layout map so that the graphical engine can read
		// each node's x coordinate and port assignments for rendering.
		// The map keys are raw Node pointers owned by this graph. The reference
		// is valid until the next call to computeLayout() or any mutating operation.
		//
		const std::unordered_map<Node*, NodeLayout>& getNodeLayout() const {
			return node_layout_;
		}

		// ── getEdgeLayout ─────────────────────────────────────────────────────────
		//
		// Returns the edge layout map so that the graphical engine can read the
		// y coordinate of each original hyperedge's horizontal bar.
		//
		const std::unordered_map<Hyperedge*, double>& getEdgeLayout() const {
			return edge_layout_;
		}

		// ── getLayerLayout ────────────────────────────────────────────────────────
		//
		// Returns the layer layout map so that the graphical engine can read the
		// y coordinate of each layer's node row.
		//
		const std::unordered_map<int, double>& getLayerLayout() const {
			return layer_layout_;
		}

		// ── Persistence ───────────────────────────────────────────────────────────

		// ── toJSON ────────────────────────────────────────────────────────────────
		//
		// Serializes the complete state of the graph into a nlohmann::json object.
		// Use this overload to embed the graph directly into a larger JSON document
		// (e.g. a Project save file) without touching the filesystem.
		//
		void toJSON(nlohmann::json& j) const;

		// Convenience overload: serializes to a JSON file at the given path.
		// Throws std::runtime_error if the file cannot be opened for writing.
		void toJSON(const std::string& path) const;

		// ── fromJSON ──────────────────────────────────────────────────────────────
		//
		// Static factory that reconstructs a GraphicalHypergraph from a
		// nlohmann::json object previously produced by toJSON(json&). Use this
		// overload to deserialize a graph that is embedded in a larger document.
		// The unique ID stored in the object is restored exactly.
		//
		static GraphicalHypergraph fromJSON(const nlohmann::json& j);

		// Convenience overload: deserializes from a JSON file at the given path.
		// Throws std::runtime_error if the file cannot be opened or the JSON is
		// malformed.
		static GraphicalHypergraph fromJSON(const std::string& path);

		// ============================================================================
		// Blocks
		//
		// A block is a connected component: a maximal set of boxes linked by
		// connections (dummies included). Its span is [min(x - width/2),
		// max(x + width/2)] over its boxes, and the occupied regions are the union
		// of all spans, merged into disjoint closed intervals. Both are computed
		// from the current structure and layout on every call (linear in the size
		// of the graph), so they always match whatever operation ran last.
		//
		// A block has no connection to anything outside it, so it can be removed,
		// or moved as a whole, sideways and up or down:
		//
		//   - click_x: placement among the other blocks' regions. An x inside a
		//     region goes right before that region if it is in its left half,
		//     right after it otherwise (the exact middle goes after); any other x
		//     is used as it is (placementPoint). In every layer the block's boxes
		//     are inserted, as one run, between the boxes centred left of that
		//     point and the rest.
		//   - top_layer: the layer its shallowest box ends up in, keeping the
		//     block's own shape. A negative value opens new layers above
		//     everything (like relocateNodeToLayer(node, -1)); a value past the
		//     deepest layer puts the block right below everything. Layers left
		//     empty are closed up, so the block ends up level with the content
		//     that was at top_layer.
		//
		// Only the vertical move takes top_layer; only the horizontal one, click_x
		// (the block then keeps its current horizontal place, re-evaluated with
		// the same rule in its new layers); the three-argument form does both.
		// Afterwards the graph is laid out again (no crossing minimization nor
		// bar-ordering MIP: a block's own crossings do not change), and every
		// box's layer override is refreshed so the moved boxes keep their new
		// layers under later operations. Removing closes up the layers left
		// empty and lays the graph out again too.
		//
		// A box that is not in the graph throws std::invalid_argument, and so
		// does a vertical-only move that would leave the block where it is.
		// ============================================================================

		struct Block {
			std::vector<Node*> nodes;  // dummies included
			double left = 0.0;         // span (from the current layout)
			double right = 0.0;
		};

		std::vector<Block> getBlocks() const;
		std::vector<std::pair<double, double>> getOccupiedRegions() const;

		// The regions of every block but those of group (what a group being moved
		// is placed among), and where placement puts a group dropped at click_x
		// given such regions. For previews of a move.
		std::vector<std::pair<double, double>> getOccupiedRegionsExcluding(const std::unordered_set<Node*>& group) const;
		static double placementPoint(const std::vector<std::pair<double, double>>& regions, double click_x);

		// Boxes (dummies included) of the block that contains box.
		std::unordered_set<Node*> getComponentNodes(const Node* box) const { return componentNodesOf(box); }

		void moveComponent(const Node* box, double click_x);
		void moveComponentToLayer(const Node* box, int top_layer);
		void moveComponent(const Node* box, double click_x, int top_layer);
		void removeComponent(const Node* box);

		// ── clone ─────────────────────────────────────────────────────────────────
		//
		// Produces a fully independent deep copy of this graph, including all
		// layout data. The clone receives the same unique ID as the original so
		// that the JointGraphicalHypergraph can still recognise it.
		//
		GraphicalHypergraph clone() const;

	protected:
		// ── Unique ID ─────────────────────────────────────────────────────────────
		//
		// Assigned once at construction from a process-wide counter.
		// Preserved verbatim by clone() and fromJSON().
		//
		std::string id_;

		std::unordered_map<Node*, NodeLayout>  node_layout_;
		std::unordered_map<Hyperedge*, double> edge_layout_;
		std::unordered_map<int, double>        layer_layout_;

		// ── mergeFrom ─────────────────────────────────────────────────────────────
		//
		// Merges the contents of another GraphicalHypergraph (passed by rvalue so
		// its internal containers can be moved rather than copied) into this graph.
		//
		// Layer merging follows the side-by-side rule:
		//   left = true  — incoming nodes and outgoing edges are prepended.
		//   left = false — incoming nodes and outgoing edges are appended.
		//
		void mergeFrom(GraphicalHypergraph&& other, bool left);

		// Same, but the incoming nodes of each layer are inserted as one block at
		// position insert_at(layer) of that layer (clamped to its size; a layer
		// this graph does not have yet is created). The incoming hyperedges are
		// appended to each layer's outgoing edges.
		void mergeFrom(GraphicalHypergraph&& other, const std::function<size_t(int layer)>& insert_at);

		// ── Stage 3: horizontal order of hyperedge bars ───────────────────────────
		//
		// Solves the MIP for the given layer to find the vertical ordering of
		// hyperedge horizontal bars that minimises the number of crossings.
		//
		void orderHyperedges(int layer, int total_mip_layers = 1.0);

		// ── Stage 3.5: settle dummy chains before any port jog/conflict logic ─────
		//
		// Finds every maximal run of chain-linked dummy nodes (length 1 counts
		// a lone "isolated" dummy is handled by exactly the same machinery) and
		// moves each one, as a single rigid unit, to a settled x.
		void placeDummyChains(std::vector<port_assignment_internal::PortAssigner*>& assigners, std::vector<double>& min_spacing);

		// ── Stage 4: port assignment ───────────────────────────────────────────────
		//
		// Builds and spaces ports for every layer pair and resolves
		// vertical-segment overlaps.
		//
		void assignPorts();

		// ── Stage 4.5: recentre node boxes under their own ports ──────────────────
		//
		// After buildPorts()/solveVerticalOverlaps() have fixed every port's final
		// x-coordinate, a node's box may no longer look centred under its own
		// ports (jog-reduction and conflict-solving both nudge individual ports
		// independently of the node they sit on). This pass leaves ports exactly
		// where they are and instead moves the node's box so that its left/right
		// margin to its own leftmost/rightmost port is equal.
		void recentreNodesUnderPorts();

		// ── Stage 4.6: centre lone root sources under their single hyperedge ──────
		//
		// For every node with no parents and exactly one outgoing hyperedge (a
		// single source port), tries to move both the node and its port to the
		// midpoint of that hyperedge's other endpoints, as long as doing so
		// keeps at least 2*MIN_VERTICAL_SEP away from the nearest source port
		// on each side within the same layer.
		void centerSingleHyperedgeRoots(std::vector<port_assignment_internal::PortAssigner*>& assigners);

		// ── Stage 5: edge y-coordinates ───────────────────────────────────────────
		//
		// Assigns a y coordinate to the horizontal span of each hyperedge, a
		// y coordinate to each layer, and a y coordinate to every port.
		//
		// Every node's centre lies on its layer's y. LAYER_GAP is measured from
		// the layer's tallest node, i.e. from layer_y -/+ getLayerHeight(layer) / 2.
		//
		void assignYCoordinates();

		// ── Stage 5.1: port y-coordinates ─────────────────────────────────────────
		//
		// Given layer_layout_ and every port's x, sets each port's y to the point
		// where the vertical line through it meets its node's boundary (see Port).
		// Called at the end of assignYCoordinates, and by fromJSON, since port y
		// is fully determined by the rest of the layout and is not persisted.
		//
		void assignPortYCoordinates();

		// ── getLayerHeight ────────────────────────────────────────────────────────
		//
		// Height of the tallest node in the given layer (dummies count as 0), or
		// 0 if the layer does not exist.
		//
		double getLayerHeight(int layer) const;

		// ── choosePositionForRelocatedNode (override) ─────────────────────────────
		//
		// Overrides Hypergraph's default (always append) hook. A relocation is purely a vertical
		// translation — the node's x coordinate from its previous layout is otherwise about to
		// become meaningless once it lands in a completely different layer — so we treat that old
		// x as a hint of where it belongs horizontally among its new layer's siblings and place it
		// there instead of always at the end.
		//
		// Looks up the node's x in node_layout_ as it stood *before* this relocation (i.e. from the
		// last completed computeLayout() call). If the node has no valid x (it has never been through
		// computeLayout), its x is guessed from its neighbours (guessX); with nothing to go on either,
		// falls back to -1 (append), matching the base class.
		//
		// Position is chosen as the index of the first node already in new_layer whose recorded x is
		// strictly greater than this node's x — i.e. nodes are kept in ascending-x order, and among
		// nodes that end up with equal x, the relocated node is placed after all of them, preserving
		// their relative order and making the tie-break deterministic (insertion order) rather than
		// arbitrary.
		//
		int choosePositionForRelocatedNode(int new_layer, const NodePtr& node) const override;

		// ── placeUnpositionedNodes (override) ─────────────────────────────────────
		//
		// Goes through [first_layer, last_layer] top-down. Nodes with a valid layout
		// (see validLayout) keep their relative order; every other node is moved:
		//   1. If an x can be guessed for it (guessX), it is inserted before the first
		//      node of the layer whose x (laid out or guessed) is greater.
		//   2. Otherwise, it goes under its leftmost parent: before the first node whose
		//      leftmost parent is further right. A root with no guess stays right after
		//      its left-hand neighbour.
		// Guesses made on a layer feed the ones below, so a chain of new dummies is
		// laid along one straight line.
		//
		void placeUnpositionedNodes(int first_layer, int last_layer) override;

		// ── validLayout ───────────────────────────────────────────────────────────
		//
		// The node's layout entry, or nullptr when it has none or the entry belongs
		// to a deleted node that happened to live at the same address.
		//
		const NodeLayout* validLayout(const Node* node) const;

		// ── guessX ────────────────────────────────────────────────────────────────
		//
		// An x for a node with no valid layout, from neighbours whose x is known
		// (laid out, or already in `guessed`):
		//   - A dummy: walk up through single-parent dummies with no known x until
		//     some parents have one, and likewise down through single-child dummies;
		//     interpolate linearly between both ends by layer (just one end if only
		//     one is found), i.e. where the straight line would go.
		//   - Any other node: the average x of its parents and children.
		// nullopt when no neighbour gives anything.
		//
		std::optional<double> guessX(const Node* node, const std::unordered_map<const Node*, double>& guessed) const;

		// ── Blocks: helpers (see "Blocks") ────────────────────────────────────────

		// Blocks of the current graph, ignoring some boxes.
		std::vector<Block> blocksExcluding(const std::unordered_set<Node*>& excluded) const;
		static std::vector<std::pair<double, double>> mergeSpans(std::vector<std::pair<double, double>> spans);

		// Boxes of the block that contains box (throws if it is not in the graph).
		std::unordered_set<Node*> componentNodesOf(const Node* box) const;

		// Moves a closed group of boxes (no connection to any box outside it): its
		// shallowest box to top_layer, placed at click_x among the rest. A missing
		// click_x keeps the group's current horizontal place; a missing top_layer,
		// its layers.
		void moveGroup(const std::unordered_set<Node*>& group,
			std::optional<double> click_x, std::optional<int> top_layer);

		// Takes a closed group of boxes out of the graph, with every hyperedge
		// touching it, then closes up empty layers and lays the graph out again.
		void removeGroup(const std::unordered_set<Node*>& doomed);

		// Called by removeGroup right before the boxes go (they are still alive
		// and in the graph), for subclasses that keep data about boxes.
		virtual void beforeRemovingBoxes(const std::unordered_set<Node*>& /*doomed*/) {}

		// Sets every real box's layer override from the depth rule: its own
		// layer when it sits deeper than the rule would put it, -1 otherwise.
		void refreshLayerOverrides();

		// Makes sure every box has coordinates (needed to place by x).
		void ensureLayout();

	private:
		// ── ID generation ─────────────────────────────────────────────────────────
		//
		// Returns the next available ID string from a process-wide atomic counter.
		// Called exactly once per GraphicalHypergraph construction; clone() and
		// fromJSON() restore the existing ID directly without calling this.
		//
		static std::string generateId();
	};

} // namespace hypergraph_logic