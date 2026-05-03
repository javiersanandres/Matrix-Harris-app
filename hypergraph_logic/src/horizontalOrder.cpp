#include "HorizontalOrder.h"

#include <gurobi_c++.h>

#include <algorithm>
#include <stdexcept>
#include <string>


// ==============================================================================================
// This module implements the crossing-minimisation MIP for the vertical order of hyperedge
// horizontal segments within a single layer, as described in equations (26)–(30) of:
//
//  Fridman, G., Vasiliev, Y., Puhkalo, V., & Ryzhov, V. (2021).
//	"A Mixed-Integer Program for Drawing Orthogonal Hyperedges
//	in a Hierarchical Hypergraph." 
//	In: Mathematics 9, no. 16: 1903.
//	DOI: 10.3390/math9161903
//
// The outgoing_edges vector of the layer is re-sorted in-place after the solve
// so that index 0 = topmost (highest y) bar on the canvas.
// ==============================================================================================
namespace horizontal_overlapping_internal {

    using namespace hypergraph_logic;

    HorizontalOrderSolver::HorizontalOrderSolver(
        int layer,
        std::map<int, LayerData>& layers,
        const std::unordered_map<Node*, NodeLayout>& node_layout)
        : layer_(layer)
        , layer_data_(layers.at(layer))
        , node_layout_(node_layout)
    {
    }

    // ── Span computation ──────────────────────────────────────────────────────────
    //
    // span(e) = [min x(v), max x(v)]  over all v in S(e) union T(e).
    // Node x-coordinates come from node_layout_ which is populated by
    // assignXCoordinates() before this step is called.
    HorizontalOrderSolver::Span HorizontalOrderSolver::computeSpan(const HyperedgePtr& edge) const {
        double lo = std::numeric_limits<double>::max();
        double hi = -std::numeric_limits<double>::max();
        for (const auto& s : edge->getSources()) {
            double x = node_layout_.at(s.get()).x;
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
        for (const auto& t : edge->getTargets()) {
            double x = node_layout_.at(t.get()).x;
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
        return { lo, hi };
    }


    // ── Crossing auxiliary counts ─────────────────────────────────────────────────
    //
    // acs(e1, e2): sources of e2 whose x lies inside span(e1).
    // act(e1, e2): targets of e2 whose x lies inside span(e1).
    int HorizontalOrderSolver::acs(const HyperedgePtr& e1, const HyperedgePtr& e2) const {
        Span s1 = computeSpan(e1);
        int count = 0;
        for (const auto& src : e2->getSources()) {
            double x = node_layout_.at(src.get()).x;
            if (x >= s1.lo && x <= s1.hi) ++count;
        }
        return count;
    }

    int HorizontalOrderSolver::act(const HyperedgePtr& e1, const HyperedgePtr& e2) const {
        Span s1 = computeSpan(e1);
        int count = 0;
        for (const auto& tgt : e2->getTargets()) {
            double x = node_layout_.at(tgt.get()).x;
            if (x >= s1.lo && x <= s1.hi) ++count;
        }
        return count;
    }


    // ── solve ─────────────────────────────────────────────────────────────────────
    //
    // Builds and solves the MIP (26)–(30).
    //
    // Gurobi needs the variables to be indexed, so we have done the following:
    // Edges are indexed 0...n-1 in the order they appear in outgoing_edges.
    // For each ordered pair (i, j) with i < j we create:
    //   HO[i][j]  in {0,1}    (HO_{e_i, e_j} in the paper)
    //   CT[i][j]  >= 0        (CT_{e_i, e_j} in the paper)
    //
    // Constraint (27): CT[i][j] >= acs(e_i,e_j) + act(e_j,e_i) - M*(1 - HO[i][j])
    // Constraint (28): CT[i][j] >= acs(e_j,e_i) + act(e_i,e_j) - M*HO[i][j]
    // Constraint (29): 0 <= HO[i][j] - HO[i][k] + HO[j][k] <= 1  for all i<j<k
    // Constraint (30): CT[i][j] >= 0, HO[i][j] in {0,1}  (variable bounds)
    //
	// In the paper, they specify M to be "a sufficiently large constant". 
    // We need to choose a specific value for M to implement the constraints. 
    // A rather tight big-M value is max possible acs+act value = |S(e2)| + |T(e2)|.
    // We use the total node count as a safe global upper bound.
    //
    // After the solve, outgoing_edges is re-sorted by the induced order:
    // e_i comes before e_j (higher bar) iff HO[i][j] = 1.
    void HorizontalOrderSolver::solve() {
        auto& edges = layer_data_.outgoing_edges;
        int n = static_cast<int>(edges.size());

        // Nothing to order with fewer than two edges.
        if (n < 2) return;

        // Safe big-M: total number of nodes in the graph is an upper bound on
        // acs + act for any pair, since acs and act count subsets of nodes.
        int total_nodes = 0;
        for (const auto& e : edges)
            total_nodes += static_cast<int>(e->getSources().size()) + static_cast<int>(e->getTargets().size());
        double M = static_cast<double>(total_nodes);

        try {
            GRBEnv env(true);
            env.set(GRB_IntParam_OutputFlag, 0); // suppress solver output
            env.start();
            GRBModel model(env);

            // ── Variables ─────────────────────────────────────────────────────────
            //
            // HO[i][j] and CT[i][j] for all i < j.
            // We store them in flat upper-triangular maps keyed by (i,j).
            std::vector<std::vector<GRBVar>> HO(n, std::vector<GRBVar>(n));
            std::vector<std::vector<GRBVar>> CT(n, std::vector<GRBVar>(n));

            for (int i = 0; i < n; ++i) {
                for (int j = i + 1; j < n; ++j) {
                    std::string sij = std::to_string(i) + "_" + std::to_string(j);
                    HO[i][j] = model.addVar(0.0, 1.0, 0.0, GRB_BINARY, "HO_" + sij);
                    CT[i][j] = model.addVar(0.0, GRB_INFINITY, 1.0, GRB_CONTINUOUS, "CT_" + sij);
                }
            }

            model.update();

            // ── Objective (26): minimise sum CT_{e1,e2} ────────────────────────────
            //
            // Coefficients of 1.0 are already set in addVar above.
            model.set(GRB_IntAttr_ModelSense, GRB_MINIMIZE);

            // ── Constraints (27) and (28) ─────────────────────────────────────────
            for (int i = 0; i < n; ++i) {
                for (int j = i + 1; j < n; ++j) {
                    double a_ij = static_cast<double>(acs(edges[i], edges[j]));
                    double b_ij = static_cast<double>(act(edges[i], edges[j]));
                    double a_ji = static_cast<double>(acs(edges[j], edges[i]));
                    double b_ji = static_cast<double>(act(edges[j], edges[i]));

                    std::string sij = std::to_string(i) + "_" + std::to_string(j);

                    // (27): CT[i][j] >= acs(e_i,e_j) + act(e_j,e_i) - M*(1 - HO[i][j])
                    //     <-> CT[i][j] - M * HO[i][j] >= acs(e_i, e_j) + act(e_j, e_i) - M
                    model.addConstr(CT[i][j] - M * HO[i][j] >= a_ij + b_ji - M , "c27_" + sij);

                    // (28): CT[i][j] >= acs(e_j,e_i) + act(e_i,e_j) - M*HO[i][j]
                    //    <-> CT[i][j] + M*HO[i][j] >= a_ji + b_ij
                    model.addConstr(CT[i][j] + M * HO[i][j] >= a_ji + b_ij, "c28_" + sij);
                }
            }

            // ── Constraint (29): transitivity ─────────────────────────────────────
            //
            // For all i < j < k:  0 <= HO[i][j] - HO[i][k] + HO[j][k] <= 1
            //
            // This enforces a consistent total order on the HO variables:
            // if e_i > e_j and e_j > e_k then e_i > e_k.
            for (int i = 0; i < n; ++i) {
                for (int j = i + 1; j < n; ++j) {
                    for (int k = j + 1; k < n; ++k) {
                        std::string sijk = std::to_string(i) + "_"
                            + std::to_string(j) + "_"
                            + std::to_string(k);
                        GRBLinExpr expr = HO[i][j] - HO[i][k] + HO[j][k];
                        model.addConstr(expr >= 0.0, "c29_lo_" + sijk);
                        model.addConstr(expr <= 1.0, "c29_hi_" + sijk);
                    }
                }
            }

            // ── Solve ─────────────────────────────────────────────────────────────
            model.optimize();

            int status = model.get(GRB_IntAttr_Status);
            if (status != GRB_OPTIMAL && status != GRB_SUBOPTIMAL) {
                throw std::runtime_error(
                    "HorizontalOrderSolver: Gurobi did not find a feasible solution "
                    "(status = " + std::to_string(status) + ")");
            }

            // ── Extract order and re-sort outgoing_edges ──────────────────────────
            std::vector<double> score(n, 0.0);
            for (int i = 0; i < n; ++i) {
                for (int j = i + 1; j < n; ++j) {
                    double ho = HO[i][j].get(GRB_DoubleAttr_X);
                    if (ho > 0.5) {
                        score[i] += 1.0; // e_i is above e_j
                    }
                    else {
                        score[j] += 1.0; // e_j is above e_i
                    }
                }
            }

            // Sort indices by descending score: highest score = topmost bar.
            std::vector<int> order(n);
			for (int i = 0; i < n; i++) order[i] = i;
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                return score[a] > score[b];
                });

            // Apply the sort to outgoing_edges.
            std::vector<HyperedgePtr> sorted_edges(n);
            for (int rank = 0; rank < n; ++rank)
                sorted_edges[rank] = edges[order[rank]];
            edges = std::move(sorted_edges);

        }
        catch (const GRBException& ex) {
            throw std::runtime_error(
                std::string("HorizontalOrderSolver: Gurobi exception: ") + ex.getMessage());
        }
    }
} // namespace horizontal_overlapping_internal


// ============================================================================
// GraphicalHypergraph::orderHyperedges
// 
// Constructs a HorizontalOrderSolver for the given layer and delegates 
// immediately. This must be called after assignXCoordinates() and before 
// assignPorts(), since the port-ordering policy depends on hyperedge order 
// being already established.
// ============================================================================
namespace hypergraph_logic {
	using namespace horizontal_overlapping_internal;

    void GraphicalHypergraph::orderHyperedges(int layer) {
        if (layers_.find(layer) == layers_.end()) return;
        if (layers_.at(layer).outgoing_edges.size() < 2) return;
        HorizontalOrderSolver(layer, layers_, node_layout_).solve();
    }
} // namespace hypergraph_logic