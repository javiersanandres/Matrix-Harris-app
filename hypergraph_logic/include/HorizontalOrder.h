#pragma once
#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"

#include <unordered_map>
#include <vector>

namespace horizontal_overlapping_internal {

    using namespace hypergraph_logic;

    // ── HorizontalOrderSolver ─────────────────────────────────────────────────────
    //
    // Internal helper that solves the model described in equations (26)–(30) of
    // Fridman et al. (2021) for a single layer pair, establishing the vertical
    // order of hyperedge horizontal segments so as to minimise crossing count.
    //
    // The problem is formulated in the following manner:
    // 
    // Given the set E_H of outgoing hyperedges on layer 'layer' (all pairs
    // ordered e_1 ≺ e_2 by their index in outgoing_edges), the model, a 0-1
    // ILP (the linear ordering problem), introduces:
    //
    //   HO_{e1,e2} in {0,1}   1 if e1 is higher than e2, 0 otherwise.
    //
    // The objective (26) minimises the crossings between every two hyperedges.
    // The paper counts them in variables CT_{e1,e2}; here, with the nodes' x
    // already known, each pair's crossings in either order are constants, so
    // they are the cost of HO itself and CT is not needed (see solve()).
    //
    // Auxiliary quantities (computed from node x-coordinates):
    //   acs(e1, e2) —> sources of e2 that lie strictly inside span(e1).
    //   act(e1, e2) —> targets of e2 that lie strictly inside span(e1).
    //   span(e)     —> [min x(v), max x(v)] over all sources and targets of e.
    struct HorizontalOrderSolver {

        HorizontalOrderSolver(int layer,
            int total_mip_layers,
            std::map<int, LayerData>& layers,
            const std::unordered_map<Node*, NodeLayout>& node_layout);

        // ── solve ─────────────────────────────────────────────────────────────────
        //
        // Solves the ILP (with Gurobi when it is usable, otherwise HiGHS) and
        // re-sorts outgoing_edges in-place. If no solution is found in time,
        // the layer keeps its order.
        void solve();

    private:
        int layer_;
        int total_mip_layers_;
        LayerData& layer_data_;
        const std::unordered_map<Node*, NodeLayout>& node_layout_;

        // ── Span computation ──────────────────────────────────────────────────────
        struct Span { double lo; double hi; };
        Span computeSpan(const HyperedgePtr& edge) const;

        // ── Crossing auxiliary counts ─────────────────────────────────────────────
        //
        // acs(e1, e2): number of sources of e2 strictly inside span(e1).
        // act(e1, e2): number of targets of e2 strictly inside span(e1).
        // Both take span(e1), computed once per edge by solve().
        int acs(const Span& span_e1, const HyperedgePtr& e2) const;
        int act(const Span& span_e1, const HyperedgePtr& e2) const;
    };

} // namespace horizontal_overlapping_internal