#pragma once
#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"

#include <unordered_map>
#include <vector>

namespace horizontal_overlapping_internal {

    using namespace hypergraph_logic;

    // ── HorizontalOrderSolver ─────────────────────────────────────────────────────
    //
    // Internal helper that solves the MIP described in equations (26)–(30) of
    // Fridman et al. (2021) for a single layer pair, establishing the vertical
    // order of hyperedge horizontal segments so as to minimise crossing count.
    //
    // The problem is formulated in the following manner:
    // 
    // Given the set E_H of outgoing hyperedges on layer 'layer' (all pairs
    // ordered e_1 ≺ e_2 by their index in outgoing_edges), the MIP introduces:
    //
    //   HO_{e1,e2} in {0,1}   1 if e1 is higher than e2, 0 otherwise.
    //   CT_{e1,e2} in >= 0    crossing count between e1 and e2.
    //
    // The objective (26) minimises sum CT_{e1,e2}. (count crossing)
    //
    // Auxiliary quantities (computed from node x-coordinates):
    //   acs(e1, e2) —> sources of e2 that lie strictly inside span(e1).
    //   act(e1, e2) —> targets of e2 that lie strictly inside span(e1).
    //   span(e)     —> [min x(v), max x(v)] over all sources and targets of e.
    struct HorizontalOrderSolver {

        HorizontalOrderSolver(int layer,
            std::map<int, LayerData>& layers,
            const std::unordered_map<Node*, NodeLayout>& node_layout);

        // ── solve ─────────────────────────────────────────────────────────────────
        //
        // Runs the Gurobi MIP and re-sorts outgoing_edges in-place.
        // Throws std::runtime_error if Gurobi fails to find an optimal solution.
        void solve();

    private:
        int layer_;
        LayerData& layer_data_;
        const std::unordered_map<Node*, NodeLayout>& node_layout_;

        // ── Span computation ──────────────────────────────────────────────────────
        struct Span { double lo; double hi; };
        Span computeSpan(const HyperedgePtr& edge) const;

        // ── Crossing auxiliary counts ─────────────────────────────────────────────
        //
        // acs(e1, e2): number of sources of e2 strictly inside span(e1).
        // act(e1, e2): number of targets of e2 strictly inside span(e1).
        int acs(const HyperedgePtr& e1, const HyperedgePtr& e2) const;
        int act(const HyperedgePtr& e1, const HyperedgePtr& e2) const;
    };

} // namespace horizontal_overlapping_internal