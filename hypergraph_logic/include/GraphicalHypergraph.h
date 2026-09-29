#pragma once

#include "Hypergraph.h"
#include <nlohmann/json.hpp>
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
	struct Port {
		Hyperedge* edge = nullptr;  // The hyperedge this port belongs to.
		double x = 0.0;             // Assigned x coordinate on the node's top/bottom edge.
		double y = 0.0;             // Assigned y coordinate on the node's boundary.
	};

	// ── NodeLayout ────────────────────────────────────────────────────────────────
	//
	// All layout data associated with a single node.
	struct NodeLayout {
		double x = 0.0;                 // Assigned x coordinate of the node centre by Brandes-Köpf.
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
		// above h(shallowest) + H(shallowest) / 2 + LAYER_GAP requests a new
		// shallowest layer; anything else outside every span, a new deepest one.
		//
		void relocateNodeToLayer(const NodePtr& node, double new_y_coordinate, 
									std::set<int>* out_altered_layers = nullptr);

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

		// ── Stage 3: horizontal order of hyperedge bars ───────────────────────────
		//
		// Solves the MIP for the given layer to find the vertical ordering of
		// hyperedge horizontal bars that minimises the number of crossings.
		//
		void orderHyperedges(int layer);

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
		// last completed computeLayout() call). If the node has no recorded x yet (e.g. it has never
		// been through computeLayout before — new nodes are not routed through this hook, but this
		// guards against it defensively anyway), falls back to -1 (append), matching the base class.
		//
		// Position is chosen as the index of the first node already in new_layer whose recorded x is
		// strictly greater than this node's x — i.e. nodes are kept in ascending-x order, and among
		// nodes that end up with equal x, the relocated node is placed after all of them, preserving
		// their relative order and making the tie-break deterministic (insertion order) rather than
		// arbitrary.
		//
		int choosePositionForRelocatedNode(int new_layer, const NodePtr& node) const override;

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