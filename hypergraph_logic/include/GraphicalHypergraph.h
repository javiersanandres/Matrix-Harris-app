#pragma once
#include "Hypergraph.h"

namespace hypergraph_logic {

    struct HyperedgeLayout {
        double y = 0.0;                                 // horizontal line y-coord
        std::unordered_map<Node*, double> source_x;     // source port x coordinates
		std::unordered_map<Node*, double> target_x;     // target port x coordinates
    };


    // ============================================================================
    // GraphicalHypergraph
    //
    // Extends Hypergraph with all necessary functionality to support graphical 
    // layout and rendering. This includes:
    // 1) Crossing minimization via Global Sifting.
	// 2) Node coordinate assignment via Brandes-Köpf algorithm.
    // 3) Computing the order of hyperedges in each layer with an MIP.
    // 4) Assgining ports to the hyperedges with another MIP.
    // 5) Assinging a y coordinate for the horizontal line of hyperedges.
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

        // Run Global Sifting crossing minimization.
        //
        // sifting_rounds : maximum number of full passes over all blocks.
        //                  Terminates early if no improvement is found.
        //                  The default is 10, due to empirical observations.
        // start_layer    : first hypergraph layer to include in the sifted region.
        //                  Layers below start_layer are treated as fixed anchors
        //                  (their edges still count toward crossings but those
        //                  nodes are never moved).  Pass 0 to sift the whole graph.
        //                  This way, we can prevent the whole graph from being processed
        //                  and only focus on the affected region after a change, which is
		//                  more consisting with an interactive workflow.
		//                  The default is 0, which means that the whole graph is sifted.
        //
        // Returns the total crossing count of the final ordering.
        int minimizeCrossings(int sifting_rounds = 10, int start_layer = 0);

		// Run Horizontal coordinate assignment (Brandes–Köpf).
        // 
        // Assigns an x-coordinate to every node in the graph.
        // Coordinates are in logical pixels (see LayoutTypes.h for constants).
        void assignCoordinates();
        double getX(const NodePtr& node) const;

    private:
		std::unordered_map<Node*, double> node_layout_; // Map from node pointer to its assigned x coordinate in the layout.
		std::unordered_map<Hyperedge*, HyperedgeLayout> edge_layout_; // Map from hyperedge pointer to its assigned layout data (y coordinate, port assignments, etc.)
    };

} // namespace hypergraph_logic