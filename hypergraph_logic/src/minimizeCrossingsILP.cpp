#include "GlobalSifting.h"
#include "Highs.h"
#include "ILPBackendTestHook.h"

#ifdef GUROBI_AVAILABLE
#include "gurobi_c++.h"
#endif

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include <iostream>

// ==================================================================================
// 
// Exact ILP-based crossing minimization for the bipartite graph G1, offered as an
// alternative to the Global Sifting heuristic in minimizeCrossings.cpp.
//
// Jünger, M., Lee, S., Mutzel, P., & Odenthal, P. (1997).
// "A polyhedral approach to the multi-layer crossing minimization problem."
// In: Graph Drawing (GD 1997), LNCS 1353, pp. 13-24. Springer.
//
// This is the textbook model exactly (equation 3.8 and its constraints): one
// ordering variable x^i_uw per pair of nodes u < w sharing row i (u < w meaning
// u's CURRENT position in the row precedes w's -- the row is never re-sorted by
// node id), one crossing variable c^i_uvxy per compared pair of edges (u,v),(w,y)
// between rows i-1 and i, split into the two textbook cases on whether v < y or
// y < v (by current position in row i).
//
// The one addition beyond the textbook model is type-2-conflict prevention. A
// long edge's dummy chain is represented, inside one block, by a run of inner
// segments. For every pair of edges between two consecutive rows that are BOTH
// inner segments, instead of the usual crossing variable and linking
// inequalities, a hard equality is imposed forbidding them from crossing at all:
// x^i_vy = x^{i-1}_uw when v < y, or x^i_yv + x^{i-1}_uw = 1 when y < v.
//
// Solver backend: the model itself (columns + sparse rows, below) is built
// once, independent of which solver ends up running it. HiGHS is always
// available and is the default/fallback backend. When this translation unit
// was compiled with Gurobi (GUROBI_AVAILABLE, set by CMake when
// find_package(Gurobi) succeeds) *and* a working Gurobi license is actually
// present on the machine at runtime, Gurobi is used instead, since it's
// substantially faster on this kind of MIP.
// 
// ==================================================================================


namespace sifting_internal {

	using namespace hypergraph_logic;

	namespace {

		constexpr double kInf = 1.0e30; // HiGHS' convention for "infinite" bound.

		// Safety valves so a caller who points this at too big a graph fails fast
		// (before building a huge model) instead of hanging. Tune freely; these are
		// deliberately conservative given the O(m^3) transitivity blow-up per row
		// and match the "~40 vertices" ballpark the source paper reports for
		// practical branch-and-cut instances.
		constexpr long long kMaxTransitivityConstraints = 300000;
		constexpr long long kMaxColumns = 200000;

		constexpr double kILPTimeBudgetSeconds = 15.0;
		constexpr int kFallbackSiftingRounds = 10;

		// Incrementally-built row-wise sparse constraint, plus the finished bounds.
		// One instance == one constraint "lo <= sum(idx[i]*val[i]) <= hi".
		struct SparseRow {
			std::vector<int> idx;
			std::vector<double> val;
			double lo;
			double hi;
		};

		// The fully-assembled ILP, in a form neither solver backend needs to know
		// was built by this file. Every column is binary (0/1); col_integrality is
		// only consulted by the HiGHS backend (HiGHS wants it spelled out per
		// column), Gurobi columns are simply declared GRB_BINARY.
		struct ILPModel {
			std::vector<double> col_lower, col_upper, col_cost, col_guess;
			std::vector<HighsVarType> col_integrality;
			std::vector<SparseRow> rows;
		};

		// What a backend hands back: whether it found something usable, and if so
		// the column values and objective. "Usable" deliberately includes a
		// feasible-but-not-proved-optimal solution (e.g. the time budget ran out) --
		// runCrossingILP() only ever commits a result if it's no worse than the
		// heuristic fallback, so a good-enough incumbent is fine to hand back.
		struct ILPSolveResult {
			bool success = false;
			std::vector<double> col_value;
			double objective = 0.0;
		};

		// ── HiGHS backend ──────────────────────────────────────────────────────
		// Always available; this is the code that used to live directly in
		// runCrossingILP() before there were two backends to choose between.
		ILPSolveResult solveWithHighs(const ILPModel& m, double time_budget_seconds) {
			ILPSolveResult result;

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
			highs.setOptionValue("time_limit", time_budget_seconds);
			if (highs.passModel(model) != HighsStatus::kOk) return result;

			// Warm start from the heuristic's already-computed block order.
			HighsSolution guess;
			guess.col_value = m.col_guess;
			guess.value_valid = true;
			highs.setSolution(guess);
			if (highs.run() != HighsStatus::kOk) return result;

			HighsModelStatus status = highs.getModelStatus();
			bool have_feasible_solution =
				(status == HighsModelStatus::kOptimal) ||
				(highs.getInfo().primal_solution_status == kSolutionStatusFeasible);
			if (!have_feasible_solution) return result;

			result.success = true;
			result.col_value = highs.getSolution().col_value;
			result.objective = highs.getInfo().objective_function_value;
			return result;
		}

#ifdef GUROBI_AVAILABLE
		// ── Gurobi backend ─────────────────────────────────────────────────────
		//
		// Only ever compiled in when CMake's find_package(Gurobi) succeeded --
		// but that only proves the SDK is installed, not that this machine has a
		// currently-valid license (a floating/token license server can be
		// unreachable, a named-user license can have expired, etc.), and there's
		// no reliable way to check that ahead of time other than asking Gurobi.
		// So both functions below are written to fail safely: constructing
		// GRBEnv *is* the license check, everything is wrapped in
		// try/catch(GRBException&), and any failure -- license or otherwise --
		// comes back as an unsuccessful ILPSolveResult rather than propagating,
		// so the caller (runCrossingILP) falls back to HiGHS transparently.

		// Whether Gurobi is actually usable here, checked once and cached for the
		// life of the process. Verifying a license can mean a round trip to a
		// license server, and once we know the answer there's no reason to pay
		// that cost again on every subsequent ILP solve.
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

		ILPSolveResult solveWithGurobi(const ILPModel& m, double time_budget_seconds) {
			ILPSolveResult result;
			try {
				GRBEnv env(true);
				env.set(GRB_IntParam_OutputFlag, 0);
				env.start();
				GRBModel model(env);
				model.set(GRB_DoubleParam_TimeLimit, time_budget_seconds);

				int n = static_cast<int>(m.col_lower.size());
				std::vector<GRBVar> vars(n);
				for (int i = 0; i < n; i++) {
					vars[i] = model.addVar(m.col_lower[i], m.col_upper[i], m.col_cost[i], GRB_BINARY);
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

				// Warm start from the heuristic's already-computed block order.
				for (int i = 0; i < n; i++) vars[i].set(GRB_DoubleAttr_Start, m.col_guess[i]);

				model.optimize();

				int status = model.get(GRB_IntAttr_Status);
				bool have_feasible_solution =
					(status == GRB_OPTIMAL) || (model.get(GRB_IntAttr_SolCount) > 0);
				if (!have_feasible_solution) return result;

				result.success = true;
				result.col_value.resize(n);
				for (int i = 0; i < n; i++) result.col_value[i] = vars[i].get(GRB_DoubleAttr_X);
				result.objective = model.get(GRB_DoubleAttr_ObjVal);
			}
			catch (GRBException&) {
				result = ILPSolveResult(); // discard any partial state; report failure
			}
			return result;
		}
#endif

	} // namespace

	// ── backend-override test hook (see ILPBackendTestHook.h) ──────────────────
	//
	// Deliberately a plain (non-atomic) variable: the hook is documented as
	// single-threaded-only, and runCrossingILP() below always resets it to
	// kAuto right after reading it, so it can never leak past the one call it
	// was set for.
	ILPBackendOverride g_ilp_backend_override = ILPBackendOverride::kAuto;

	void setILPBackendOverrideForTesting(ILPBackendOverride mode) {
		g_ilp_backend_override = mode;
	}

	bool isGurobiUsableForTesting() {
#ifdef GUROBI_AVAILABLE
		return gurobiUsable();
#else
		return false;
#endif
	}

	// ── runCrossingILP ────────────────────────────────────────────────────────────

	int GlobalSifter::runCrossingILP(double time_budget_seconds) {
		// Rows (G1 layers) in ascending order.
		std::vector<int> row_keys;
		row_keys.reserve(S_.g1_layers.size());
		for (const auto& [layer, nodes] : S_.g1_layers) row_keys.push_back(layer);
		std::sort(row_keys.begin(), row_keys.end());
		if (row_keys.size() < 2) return 0; // nothing to cross.

		// Node-index list per row, in CURRENT order -- i.e. whatever order
		// S_.g1_layers already has them in (the heuristic's finished order, since
		// Hypergraph::minimizeCrossingsILP() always calls runSifting() first on
		// this same sifter). u < w in every constraint below means exactly this:
		// u's CURRENT position in the row precedes w's.
		std::vector<std::vector<int>> row_nodes(row_keys.size());
		for (size_t r = 0; r < row_keys.size(); r++)
			row_nodes[r] = S_.g1_layers.at(row_keys[r]); // copy, order preserved

		// g1 node index -> its current position within its own row.
		std::vector<int> pos_in_row(S_.g1_nodes.size(), -1);
		for (const auto& nodes : row_nodes)
			for (size_t p = 0; p < nodes.size(); p++)
				pos_in_row[nodes[p]] = static_cast<int>(p);

		// Safety valve on model size, before we build anything.
		long long worst_case_triples = 0;
		for (const auto& nodes : row_nodes) {
			long long m = static_cast<long long>(nodes.size());
			worst_case_triples += m * (m - 1) * (m - 2) / 6;
		}
		if (worst_case_triples > kMaxTransitivityConstraints) return -1;

		// One binary ordering variable x^i_uw per pair of nodes u < w sharing a
		// row (u < w by current position, as above). A raw G1 node belongs to
		// exactly one row, so a given pair can only ever be created once.
		std::map<std::pair<int, int>, int> pair_var;
		std::vector<double> col_lower, col_upper, col_cost, col_guess;
		std::vector<HighsVarType> col_integrality;

		// The heuristic (runSifting(), called on this same sifter just before
		// runCrossingILP()) has already left a genuine total order over blocks in
		// S_.pi. Restricting that order to any single row automatically satisfies
		// every transitivity constraint, and since it treats each chain as one
		// atomic block with a single pi value shared across every row it spans,
		// two edges from the same chain-pair comparison get identical guessed
		// order under it too -- so this guess is a fully feasible integral
		// solution, not just a rough hint, and gives HiGHS a valid incumbent to
		// prune against from the start. guessOrder(u,v) answers "does u come
		// before v" for ANY pair u,v, independent of which one is the row's
		// "u" or "w" label.
		auto guessOrder = [&](int u, int v) -> double {
			return (S_.pi[S_.g1_nodes[u].block_id] < S_.pi[S_.g1_nodes[v].block_id]) ? 1.0 : 0.0;
			};

		auto addColumn = [&](double cost, double guess) -> int {
			int idx = static_cast<int>(col_lower.size());
			col_lower.push_back(0.0);
			col_upper.push_back(1.0);
			col_cost.push_back(cost);
			col_integrality.push_back(HighsVarType::kInteger);
			col_guess.push_back(guess);
			return idx;
			};

		// Every pair of nodes sharing a row gets exactly one column, keyed (u,w)
		// with u appearing first in the row's current order -- created once, up
		// front, per row.
		for (const auto& nodes : row_nodes) {
			for (size_t i = 0; i < nodes.size(); i++) {
				for (size_t j = i + 1; j < nodes.size(); j++) {
					int u = nodes[i], w = nodes[j]; // u before w by current position
					int idx = addColumn(0.0, guessOrder(u, w)); // pure ordering variables don't enter the objective
					pair_var.emplace(std::make_pair(u, w), idx);
				}
			}
		}

		if (static_cast<long long>(col_lower.size()) > kMaxColumns) return -1;
		if (pair_var.empty()) return 0; // no row has 2+ nodes: nothing to decide.

		// Is the G1 edge (u,v) an inner segment of a long edge's dummy chain? True
		// iff both endpoints belong to the same block and that block is a genuine
		// multi-node chain (not a real node's own singleton block). Read-only
		// classification -- never mutates block state.
		auto isInnerSegment = [&](int u, int v) -> bool {
			int bu = S_.g1_nodes[u].block_id;
			int bv = S_.g1_nodes[v].block_id;
			if (bu != bv) return false;
			return S_.blocks[bu].g1_nodes.size() > 1;
			};

		// Constraint rows.
		std::vector<SparseRow> rows;

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
			rows.push_back(std::move(row));
			};

		// Transitivity within each row: 0 <= x_uw + x_wz - x_uz <= 1 for every
		// u < w < z by current position (standard order-polytope inequality).
		for (const auto& nodes : row_nodes) {
			int m = static_cast<int>(nodes.size());
			for (int i = 0; i < m; i++) {
				for (int j = i + 1; j < m; j++) {
					int Xuw = pair_var.at({ nodes[i], nodes[j] });
					for (int k = j + 1; k < m; k++) {
						int Xwz = pair_var.at({ nodes[j], nodes[k] });
						int Xuz = pair_var.at({ nodes[i], nodes[k] });
						addConstraint({ {Xuw, 1.0}, {Xwz, 1.0}, {Xuz, -1.0} }, 0.0, 1.0);
					}
				}
			}
		}

		// Crossing variables and linking constraints between every pair of
		// consecutive rows, with type-2-conflict prevention for pairs of inner
		// segments. Edges are compared as (u,v),(w,y) with u < w by current
		// position in the upper row (i-1), exactly as the equations are stated.
		for (size_t r = 0; r + 1 < row_keys.size(); r++) {
			std::set<std::pair<int, int>> edge_set; // distinct (upper,lower) G1 edges
			for (int a : row_nodes[r]) {
				for (int b : S_.g1_out[a]) {
					if (S_.g1_nodes[b].g1_layer != row_keys[r + 1]) continue;
					edge_set.insert({ a, b });
				}
			}
			if (edge_set.size() < 2) continue; // fewer than 2 edges: nothing can cross here.

			std::vector<std::pair<int, int>> edges(edge_set.begin(), edge_set.end());
			// Sort by current position of the upper endpoint so the pairwise loop
			// below only ever needs to consider "this edge vs. a later one" to
			// already have u < w.
			std::sort(edges.begin(), edges.end(), [&](const auto& e1, const auto& e2) {
				return pos_in_row[e1.first] < pos_in_row[e2.first];
				});
			int ne = static_cast<int>(edges.size());

			for (int e1 = 0; e1 < ne; e1++) {
				for (int e2 = e1 + 1; e2 < ne; e2++) {
					auto [u, v] = edges[e1]; // pos_in_row[u] < pos_in_row[w] by the sort above
					auto [w, y] = edges[e2];
					if (u == w || v == y) continue; // shared endpoint: can never cross.

					int Xuw = pair_var.at({ u, w });
					bool inner_pair = isInnerSegment(u, v) && isInnerSegment(w, y);

					if (pos_in_row[v] < pos_in_row[y]) {
						int Xvy = pair_var.at({ v, y });
						if (inner_pair) {
							// Type-2-conflict prevention: x^i_vy = x^{i-1}_uw.
							addConstraint({ {Xvy, 1.0}, {Xuw, -1.0} }, 0.0, 0.0);
							continue;
						}
						int c_idx = addColumn(1.0, std::fabs(guessOrder(v, y) - guessOrder(u, w)));
						// -c <= x_vy - x_uw <= c
						addConstraint({ {Xvy, 1.0}, {Xuw, -1.0}, {c_idx, 1.0} }, 0.0, kInf);
						addConstraint({ {Xvy, 1.0}, {Xuw, -1.0}, {c_idx, -1.0} }, -kInf, 0.0);
					}
					else {
						int Xyv = pair_var.at({ y, v });
						if (inner_pair) {
							// Type-2-conflict prevention: x^i_yv + x^{i-1}_uw = 1.
							addConstraint({ {Xyv, 1.0}, {Xuw, 1.0} }, 1.0, 1.0);
							continue;
						}
						int c_idx = addColumn(1.0, std::fabs(guessOrder(v, y) - guessOrder(u, w)));
						// 1-c <= x_yv + x_uw <= 1+c
						addConstraint({ {Xyv, 1.0}, {Xuw, 1.0}, {c_idx, 1.0} }, 1.0, kInf);
						addConstraint({ {Xyv, 1.0}, {Xuw, 1.0}, {c_idx, -1.0} }, -kInf, 1.0);
					}
				}
			}
		}

		if (col_lower.empty()) return 0; // nothing was ever decidable: already optimal.

		// Assemble the backend-neutral model, then solve it. Normally (kAuto)
		// Gurobi is tried first when this build has it and a valid license is
		// actually present on this machine right now; any solve failure --
		// including a Gurobi run that throws partway through -- falls back to
		// HiGHS rather than giving up. A forced backend (see
		// ILPBackendTestHook.h) skips that auto-detect entirely, for
		// benchmarking one solver in isolation; it is read once here and reset
		// immediately so it can never apply to a later call.
		ILPModel ilp_model;
		ilp_model.col_lower = std::move(col_lower);
		ilp_model.col_upper = std::move(col_upper);
		ilp_model.col_cost = std::move(col_cost);
		ilp_model.col_guess = std::move(col_guess);
		ilp_model.col_integrality = std::move(col_integrality);
		ilp_model.rows = std::move(rows);

		ILPBackendOverride backend = g_ilp_backend_override;
		g_ilp_backend_override = ILPBackendOverride::kAuto;

		ILPSolveResult result;
		switch (backend) {
		case ILPBackendOverride::kForceHighs:
			result = solveWithHighs(ilp_model, time_budget_seconds);
			break;
		case ILPBackendOverride::kForceGurobi:
#ifdef GUROBI_AVAILABLE
			result = solveWithGurobi(ilp_model, time_budget_seconds);
#endif
			// No fallback here on purpose: a caller that explicitly forced
			// Gurobi wants to know Gurobi failed, not get a HiGHS number back
			// mislabeled as Gurobi's.
			break;
		case ILPBackendOverride::kAuto:
		default:
#ifdef GUROBI_AVAILABLE
			if (gurobiUsable()) {
				result = solveWithGurobi(ilp_model, time_budget_seconds);
			}
#endif
			if (!result.success) {
				result = solveWithHighs(ilp_model, time_budget_seconds);
			}
			break;
		}
		if (!result.success) return -1; // neither backend found anything usable in time.

		const std::vector<double>& sol = result.col_value;
		int achieved_crossings = static_cast<int>(std::lround(result.objective));

		// Reorder each row of S_.g1_layers to match the solution. Every row is
		// independent here (a node belongs to exactly one row), so this is a
		// plain per-row sort keyed by current position, not by raw node id.
		for (size_t r = 0; r < row_keys.size(); r++) {
			auto& nodes_vec = S_.g1_layers.at(row_keys[r]);
			std::sort(nodes_vec.begin(), nodes_vec.end(), [&](int a, int b) {
				if (a == b) return false;
				bool a_first = pos_in_row[a] < pos_in_row[b];
				int u = a_first ? a : b, w = a_first ? b : a;
				double x_uw = sol[pair_var.at({ u, w })];
				bool u_before_w = x_uw > 0.5;
				return a_first ? u_before_w : !u_before_w;
				});
		}

		return achieved_crossings;
	}


	// ── writeBackFromG1Order ────────────────────────────────────────────────────
	//
	// Counterpart to writeBack() for the ILP path: writeBack() sorts each real
	// layer's existing LayerData::nodes vector by S_.pi[block_id]; this sorts it
	// by the node order runCrossingILP() left in S_.g1_layers instead. Mirrors
	// writeBack()'s own iteration and in-place-sort pattern exactly -- it never
	// rebuilds LayerData::nodes from S_.g1_nodes[.].original (that would require
	// fabricating a new NodePtr, corrupting the existing shared_ptr refcounting),
	// it only reorders the NodePtr entries already there, via
	// S_.node_to_g1.at(node.get()) to resolve each one back to its G1 index.
	void GlobalSifter::writeBackFromG1Order() {
		for (auto& [layer, data] : layers_) {
			if (layer < start_layer_ || layer > end_layer_) continue;

			auto it = S_.g1_layers.find(2 * layer);
			if (it == S_.g1_layers.end()) continue;

			std::unordered_map<int, int> position; // g1 index -> position in the ILP-solved order
			position.reserve(it->second.size());
			for (int pos = 0; pos < static_cast<int>(it->second.size()); pos++)
				position[it->second[pos]] = pos;

			std::sort(data.nodes.begin(), data.nodes.end(),
				[&](NodePtr a, NodePtr b) {
					int ga = S_.node_to_g1.at(a.get());
					int gb = S_.node_to_g1.at(b.get());
					return position.at(ga) < position.at(gb);
				});
		}
	}

} // namespace sifting_internal

// ======================================================================================
// Hypergraph: public entry point
// ======================================================================================
namespace hypergraph_logic {

	using namespace sifting_internal;

	// ── minimizeCrossingsILP ────────────────────────────────────────────────────
	//
	// Exact alternative to minimizeCrossings() for the whole graph.
	// 
	// Always runs the existing heuristic pipeline first as a safety net
	// then spends up to kILPTimeBudgetSeconds attempting the exact solve.
	// Whichever result is smaller is committed to layers_: the ILP path via
	// writeBackFromG1Order(), the heuristic path via the existing block/pi-based
	// writeBack().
	int Hypergraph::minimizeCrossingsILP() {
		if (getLayers().empty()) return 0;
		int last_layer = static_cast<int>(layers_.rbegin()->first);

		GlobalSifter sifter(0, last_layer, layers_, true, kFallbackSiftingRounds);
		if (sifter.countCrossings() == 0) { sifter.writeBack(); return 0; }

		// Strong heuristic baseline
		sifter.runSifting(kFallbackSiftingRounds);
		int fallback_crossings = sifter.countCrossings();
		if (fallback_crossings == 0) { sifter.writeBack(); return 0; }

		// Attempt the exact solve (node-level, independent internal state).
		int ilp_crossings = sifter.runCrossingILP(kILPTimeBudgetSeconds);
		if (ilp_crossings >= 0 && ilp_crossings <= fallback_crossings) {
			sifter.writeBackFromG1Order();
			return ilp_crossings;
		}

		sifter.writeBack();
		return fallback_crossings;
	}

} // namespace hypergraph_logic