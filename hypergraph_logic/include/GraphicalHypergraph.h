#pragma once
#include "Hypergraph.h"

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
    // 
    // Since all 5 algorithms are independent and can be applied in any order,
    // I've decided to keep all the code related to them in separate files and
    // only include the necessary interfaces in this class.
    // 
    // 
    // All the tecnhiques used are based on the paper below:
    // Fridman, G., Vasiliev, Y., Puhkalo, V., & Ryzhov, V. (2021).
    // "A Mixed-Integer Program for Drawing Orthogonal Hyperedges
    // in a Hierarchical Hypergraph." 
    // In: Mathematics 9, no. 16: 1903.
    // DOI: 10.3390/math9161903
    // 
    // There are some parts of the implementation which are described in other
    // papers, but the main reference for the overall approach is the one above.
    // 
    // ============================================================================
    class GraphicalHypergraph : public Hypergraph {
    public:
        using Hypergraph::Hypergraph;

        // ── Stage 1: crossing minimisation ────────────────────────────────────────
        //
        // Runs Global Sifting to reorder nodes within each layer so as to
        // minimise the total number of edge crossings.
        //
        //   sifting_rounds — maximum number of full passes over all nodes.
        //                    Terminates early when no improvement is found.
        //                    Default: 10 (empirical).
        //   start_layer    — first layer included in the sifted region.
        //                    Layers below start_layer are fixed anchors whose
        //                    edges still count toward crossings but whose nodes
        //                    are never moved. Pass 0 to sift the whole graph.
        //
        // Returns the total crossing count of the final ordering.
        int minimizeCrossings(int sifting_rounds = 10, int start_layer = 0);

        // ── Stage 2: node x-coordinates ───────────────────────────────────────────
        //
        // Assigns an x-coordinate to every node using the Brandes-Köpf algorithm.
        // Coordinates are in logical pixels (constants defined in LayoutTypes.h).
        void assignCoordinates();
        double getX(const NodePtr& node) const;

        // ── Stage 4: port assignment ───────────────────────────────────────────────
        //
        // Iterates over every layer pair (0->1, 1->2,..., n-2->n-1), building and
        // spacing ports then resolving vertical-segment overlaps for each pair in
        // turn. After this call every node in the graph has fully populated and
        // non-overlapping source_ports and target_ports.
        void assignPorts();

    protected:
        std::unordered_map<Node*, NodeLayout> node_layout_; // Map from node pointer to its layout data.
        std::unordered_map<Hyperedge*, double> edge_layout_; // Map from hyperedge pointer to its assigned layout data (y coordinate).
    };

} // namespace hypergraph_logic