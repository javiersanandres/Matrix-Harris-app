#pragma once
#include "Hypergraph.h"
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

namespace hypergraph_logic {

	// ── Port ──────────────────────────────────────────────────────────────────────
	//
	// A single connection point between a node and a hyperedge, carrying the
	// x-coordinate at which the vertical segment of the edge leaves or arrives
	// at the node.
	struct Port {
		Hyperedge* edge = nullptr;  // The hyperedge this port belongs to.
		double x = 0.0;             // Assigned x coordinate on the node's top/bottom edge.
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
		int minimizeCrossings(int sifting_rounds = 10) {
			return Hypergraph::minimizeCrossings(sifting_rounds, 0);
		}

		// ── computeLayout ─────────────────────────────────────────────────────────
		//
		// Run the complete layout pipeline, executing stages 2-5 in order.
		// After this call, all layout data is known and the hypergraph can
		// be represented graphically.
		//
		void computeLayout();

		// ── relocateNodeInLayer ───────────────────────────────────────────────────
		//
		// Allows the user to permute the nodes in a layer by providing a new
		// x coordinate for the node being moved. The coordinate is interpreted
		// only as an ordering signal — actual coordinates are reassigned by
		// computeLayout() which is called internally at the end.
		//
		void relocateNodeInLayer(const NodePtr& node, double new_x_coordinate);

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

		// ── Stage 2: node x-coordinates ───────────────────────────────────────────
		//
		// Assigns an x-coordinate to every node using the Brandes-Köpf algorithm.
		//
		void assignXCoordinates();

		// ── Stage 3: horizontal order of hyperedge bars ───────────────────────────
		//
		// Solves the MIP for the given layer to find the vertical ordering of
		// hyperedge horizontal bars that minimises the number of crossings.
		//
		void orderHyperedges(int layer);

		// ── Stage 4: port assignment ───────────────────────────────────────────────
		//
		// Builds and spaces ports for every layer pair and resolves
		// vertical-segment overlaps.
		//
		void assignPorts();

		// ── Stage 5: edge y-coordinates ───────────────────────────────────────────
		//
		// Assigns a y coordinate to the horizontal span of each hyperedge and a
		// y coordinate to each layer.
		//
		void assignYCoordinates();

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