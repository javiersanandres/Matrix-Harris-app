#include "HorizontalOrder.h"
#include "Highs.h"
#include "HighsOutcome.h"
#include "HorizontalOrderBackendTestHook.h"

#ifdef GUROBI_AVAILABLE
#include <gurobi_c++.h>
#endif

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>


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
//
// Solver backend: the model itself (columns + sparse rows, below) is built
// once, independent of which solver ends up running iT. HiGHS is
// always available and is the default/fallback backend. When this
// translation unit was compiled with Gurobi (GUROBI_AVAILABLE, set by CMake
// when find_package(Gurobi) succeeds) AND a working Gurobi license is
// actually present on the machine at runtime, Gurobi is used instead, since
// it's substantially faster on this kind of MIP.
// ==============================================================================================
namespace horizontal_overlapping_internal {

    using namespace hypergraph_logic;

    namespace {

        constexpr double kInf = 1.0e30; // HiGHS' convention for "infinite" bound.

        // Both backends are capped at this budget: orderHyperedges() runs
        // interactively (after edits, on layout refresh, etc.), so a solver
        // that ran unbounded on a pathological instance would freeze the UI.
        // Both HiGHS and Gurobi return whatever incumbent they've found so
        // far when the limit is hit, not a hard failure -- solve() below
        // accepts that "good enough" solution the same way it would accept a
        // proven optimum.
        constexpr double kSolveTimeLimitSeconds = 1.0;

        // Incrementally-built row-wise sparse constraint, plus the finished bounds.
        // One instance == one constraint "lo <= sum(idx[i]*val[i]) <= hi".
        struct SparseRow {
            std::vector<int> idx;
            std::vector<double> val;
            double lo;
            double hi;
        };

        struct MipModel {
            std::vector<double> col_lower, col_upper, col_cost, col_guess;
            std::vector<HighsVarType> col_integrality;
            std::vector<SparseRow> rows;
        };

        // What a backend hands back: whether it found something usable, and if so
        // the column values.
        struct MipSolveResult {
            bool success = false;
            std::vector<double> col_value;
        };

        // ── HiGHS backend ──────────────────────────────────────────────────────
        MipSolveResult solveWithHighs(const MipModel& m) {
            MipSolveResult result;

            HighsModel model;
            HighsLp& lp = model.lp_;
            lp.num_col_ = static_cast<int>(m.col_lower.size());
            lp.num_row_ = static_cast<int>(m.rows.size());
            lp.col_cost_ = m.col_cost;
            lp.col_lower_ = m.col_lower;
            lp.col_upper_ = m.col_upper;
            lp.integrality_ = m.col_integrality;
            lp.sense_ = ObjSense::kMinimize;
            lp.offset_ = 0.0;

            lp.row_lower_.reserve(m.rows.size());
            lp.row_upper_.reserve(m.rows.size());
            lp.a_matrix_.format_ = MatrixFormat::kRowwise;
            lp.a_matrix_.start_.reserve(m.rows.size() + 1);
            for (const auto& row : m.rows) {
                lp.row_lower_.push_back(row.lo);
                lp.row_upper_.push_back(row.hi);
                lp.a_matrix_.index_.insert(lp.a_matrix_.index_.end(), row.idx.begin(), row.idx.end());
                lp.a_matrix_.value_.insert(lp.a_matrix_.value_.end(), row.val.begin(), row.val.end());
                lp.a_matrix_.start_.push_back(static_cast<int>(lp.a_matrix_.index_.size()));
            }
            lp.a_matrix_.num_col_ = lp.num_col_;
            lp.a_matrix_.num_row_ = lp.num_row_;

            Highs highs;
            highs.setOptionValue("output_flag", false);
            highs.setOptionValue("time_limit", kSolveTimeLimitSeconds);
            if (highs.passModel(model) != HighsStatus::kOk) return result;

            // Warm start from the layer's existing hyperedge order (see
            // col_guess in solve()). Purely advisory: if HiGHS can't use it for
            // any reason, it just falls back to solving from scratch.
            HighsSolution guess;
            guess.col_value = m.col_guess;
            guess.value_valid = true;
            highs.setSolution(guess);

            // Reaching the time limit is not a failure: run() then returns
            // kWarning and the best order found so far is still usable.
            const HighsStatus run_status = highs.run();
            if (!hypergraph_logic::highs_outcome::hasUsableSolution(highs, run_status)) return result;

            result.success = true;
            result.col_value = highs.getSolution().col_value;
            return result;
        }

#ifdef GUROBI_AVAILABLE
        // ── Gurobi backend ─────────────────────────────────────────────────────

        bool gurobiUsable() {
            static const bool usable = [] {
                try {
                    GRBEnv env(true); // empty/default env; construction is the license check
                    env.set(GRB_IntParam_OutputFlag, 0);
                    env.start();
                    return true;
                }
                catch (GRBException&) {
                    return false;
                }
                }();
            return usable;
        }

        MipSolveResult solveWithGurobi(const MipModel& m) {
            MipSolveResult result;
            try {
                GRBEnv env(true);
                env.set(GRB_IntParam_OutputFlag, 0);
                env.start();
                GRBModel model(env);
                model.set(GRB_DoubleParam_TimeLimit, kSolveTimeLimitSeconds);

                int n = static_cast<int>(m.col_lower.size());
                std::vector<GRBVar> vars(n);
                for (int i = 0; i < n; i++) {
                    char type = (m.col_integrality[i] == HighsVarType::kInteger) ? GRB_BINARY : GRB_CONTINUOUS;
                    double lb = (m.col_lower[i] <= -kInf) ? -GRB_INFINITY : m.col_lower[i];
                    double ub = (m.col_upper[i] >= kInf) ? GRB_INFINITY : m.col_upper[i];
                    vars[i] = model.addVar(lb, ub, m.col_cost[i], type);
                }
                model.update(); // vars must exist before addRange() can reference them below

                for (const auto& row : m.rows) {
                    GRBLinExpr expr = 0.0;
                    for (size_t k = 0; k < row.idx.size(); k++)
                        expr += row.val[k] * vars[row.idx[k]];
                    double lo = (row.lo <= -kInf) ? -GRB_INFINITY : row.lo;
                    double hi = (row.hi >= kInf) ? GRB_INFINITY : row.hi;
                    model.addRange(expr, lo, hi);
                }

                model.set(GRB_IntAttr_ModelSense, GRB_MINIMIZE);

                // Warm start from the layer's existing hyperedge order.
                for (int i = 0; i < n; i++) vars[i].set(GRB_DoubleAttr_Start, m.col_guess[i]);

                model.optimize();

                int status = model.get(GRB_IntAttr_Status);
                bool have_feasible_solution =
                    (status == GRB_OPTIMAL) || (status == GRB_SUBOPTIMAL) ||
                    (model.get(GRB_IntAttr_SolCount) > 0);
                if (!have_feasible_solution) return result;

                result.success = true;
                result.col_value.resize(n);
                for (int i = 0; i < n; i++) result.col_value[i] = vars[i].get(GRB_DoubleAttr_X);
            }
            catch (GRBException&) {
                result = MipSolveResult(); // discard any partial state; report failure
            }
            return result;
        }
#endif

    } // namespace

    // ── backend-override test hook (see HorizontalOrderBackendTestHook.h) ──────
    ILPBackendOverride g_backend_override = ILPBackendOverride::kAuto;

    void setHorizontalOrderBackendOverrideForTesting(ILPBackendOverride mode) {
        g_backend_override = mode;
    }

    bool isGurobiUsableForHorizontalOrderTesting() {
#ifdef GUROBI_AVAILABLE
        return gurobiUsable();
#else
        return false;
#endif
    }

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
    // Both backends need the variables indexed, so we do the following:
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

        // ── Assemble the backend-neutral model ──────────────────────────────────
        //
        // HO[i][j] and CT[i][j] for all i < j, flattened into columns and looked
        // up via these index tables.
        std::vector<std::vector<int>> HO_idx(n, std::vector<int>(n, -1));
        std::vector<std::vector<int>> CT_idx(n, std::vector<int>(n, -1));

        MipModel model;

        // Warm start from the layer's CURRENT hyperedge order, i.e. exactly
        // what's already in outgoing_edges before this call reorders it.
        // Interactive use means a good order will typically already exist
        // (either from a previous solve, or from the user's own edits), so
        // treating it as a seed rather than solving from scratch every time
        // both speeds up the solve and biases towards NOT reshuffling edges
        // the user didn't ask to move. Edges are indexed 0..n-1 by their
        // CURRENT position, so "e_i before e_j" for every i<j pair is true
        // BY CONSTRUCTION here -- the guess is simply HO[i][j]=1 for all
        // i<j, with CT[i][j] guessed to match (the smallest value that
        // satisfies (27) when HO=1; (28) is never binding there since M is
        // large). That makes this a genuinely feasible integral starting
        // solution (the current order, unchanged), not just a rough hint.
        auto addColumn = [&](double lower, double upper, double cost, bool integer, double guess) -> int {
            int idx = static_cast<int>(model.col_lower.size());
            model.col_lower.push_back(lower);
            model.col_upper.push_back(upper);
            model.col_cost.push_back(cost);
            model.col_integrality.push_back(integer ? HighsVarType::kInteger : HighsVarType::kContinuous);
            model.col_guess.push_back(guess);
            return idx;
            };

        auto addConstraint = [&](std::vector<std::pair<int, double>> terms, double lo, double hi) {
            std::map<int, double> merged; // merge duplicate columns, if any
            for (auto& [idx, coeff] : terms) merged[idx] += coeff;
            SparseRow row;
            for (auto& [idx, coeff] : merged) {
                if (coeff == 0.0) continue;
                row.idx.push_back(idx);
                row.val.push_back(coeff);
            }
            row.lo = (lo <= -kInf) ? -kInf : lo;
            row.hi = (hi >= kInf) ? kInf : hi;
            model.rows.push_back(std::move(row));
            };

        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                double a_ij = static_cast<double>(acs(edges[i], edges[j]));
                double b_ij = static_cast<double>(act(edges[i], edges[j]));
                double a_ji = static_cast<double>(acs(edges[j], edges[i]));
                double b_ji = static_cast<double>(act(edges[j], edges[i]));

                // Guess: keep the current order, i.e. HO[i][j] = 1 (e_i, at
                // the smaller CURRENT index, stays above e_j). With HO fixed
                // to 1, (27) needs CT >= a_ij + b_ji; (28) needs CT >= a_ji +
                // b_ij - M, which is not binding since M is large. So the
                // smallest feasible CT under this guess is max(0, a_ij+b_ji).
                HO_idx[i][j] = addColumn(0.0, 1.0, 0.0, /*integer=*/true, /*guess=*/1.0);
                CT_idx[i][j] = addColumn(0.0, kInf, 1.0, /*integer=*/false, /*guess=*/std::max(0.0, a_ij + b_ji));

                int ho = HO_idx[i][j];
                int ct = CT_idx[i][j];

                // (27): CT[i][j] - M*HO[i][j] >= acs(e_i,e_j) + act(e_j,e_i) - M
                addConstraint({ {ct, 1.0}, {ho, -M} }, a_ij + b_ji - M, kInf);

                // (28): CT[i][j] + M*HO[i][j] >= acs(e_j,e_i) + act(e_i,e_j)
                addConstraint({ {ct, 1.0}, {ho, M} }, a_ji + b_ij, kInf);
            }
        }

        // ── Constraint (29): transitivity ─────────────────────────────────────
        //
        // For all i < j < k:  0 <= HO[i][j] - HO[i][k] + HO[j][k] <= 1
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                for (int k = j + 1; k < n; ++k) {
                    addConstraint(
                        { {HO_idx[i][j], 1.0}, {HO_idx[i][k], -1.0}, {HO_idx[j][k], 1.0} },
                        0.0, 1.0);
                }
            }
        }

        // ── Solve: Gurobi if usable (or forced), else HiGHS ─────────────────────
        //
        // Normally (kAuto) Gurobi is tried first when this build has it and a
        // valid license is actually present on this machine right now; any solve
        // failure falls back to HiGHS rather than giving up. A forced backend
        // (see HorizontalOrderBackendTestHook.h) skips that auto-detect entirely,
        // for benchmarking one solver in isolation; it is read once here and
        // reset immediately so it can never apply to a later call.
        ILPBackendOverride backend = g_backend_override;
        g_backend_override = ILPBackendOverride::kAuto;

        MipSolveResult result;
        switch (backend) {
        case ILPBackendOverride::kForceHighs:
            result = solveWithHighs(model);
            break;
        case ILPBackendOverride::kForceGurobi:
#ifdef GUROBI_AVAILABLE
            result = solveWithGurobi(model);
#endif
            // No fallback here on purpose: a caller that explicitly forced
            // Gurobi wants to know Gurobi failed, not get a HiGHS number back
            // mislabeled as Gurobi's.
            break;
        case ILPBackendOverride::kAuto:
        default:
#ifdef GUROBI_AVAILABLE
            if (gurobiUsable()) {
                result = solveWithGurobi(model);
            }
#endif
            if (!result.success) {
                result = solveWithHighs(model);
            }
            break;
        }

        if (!result.success) {
            // The MIP could not be resolved in time and therefore 
            // the previous order is preserved.
            return;
        }

        // ── Extract order and re-sort outgoing_edges ──────────────────────────
        std::vector<double> score(n, 0.0);
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                double ho = result.col_value[HO_idx[i][j]];
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