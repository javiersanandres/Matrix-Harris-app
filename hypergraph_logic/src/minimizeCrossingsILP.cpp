#include "GlobalSifting.h"
#include "Highs.h"
#include "HighsOutcome.h"
#include "ILPBackendTestHook.h"
#include "ILPCancellationToken.h"
#include "GurobiRuntime.h"


#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

// ==================================================================================
// 
// Exact ILP-based crossing minimization for the bipartite graph G1, offered as an
// alternative to the Global Sifting heuristic in minimizeCrossings.cpp.
//
// Jünger, M., Lee, S., Mutzel, P., & Odenthal, P. (1997).
// "A polyhedral approach to the multi-layer crossing minimization problem."
// In: Graph Drawing (GD 1997), LNCS 1353, pp. 13-24. Springer.
//
// The textbook model (equation 3.8 and its constraints), plus type-2-conflict
// prevention and the equivalences.
//
// ── The drawing ──────────────────────────────────────────────────────────────
//
// Rows r = 0..2n: row 2l holds the nodes of layer l (real nodes and dummies), row
// 2l+1 the hubs of the connections from layer l to layer l+1. A hub is joined by a
// segment to each of its sources (the row above) and to each of its targets (the
// row below). A long connection's dummies, with the hubs between them, form one
// block; a segment whose two ends are in the same block is an inner segment.
//
// In a layer row r:
//   D_r   its dummies;
//   E_r   its equivalences, each a set of real nodes E = {e_1, ..., e_k}, k >= 2,
//         e_1 being a fixed representative; different equivalences are disjoint;
//   N_r   its real nodes in no equivalence. Two of them with exactly the same hubs
//         above and below (the same connections in and out) are twins: swapping
//         them never changes the crossings.
//
// Every row has a reference order, its current one, written a < b. It only names
// the variables: the solution may reverse any pair.
//
// ── Variables ────────────────────────────────────────────────────────────────
//
//   x^r_ab    in {0,1}  for a < b in row r: 1 iff a is placed before b.
//             For any a != b of a row, X^r_ab ("a before b") stands for x^r_ab if
//             a < b, and for 1 - x^r_ba if b < a. It is not a variable of its own.
//   c^r_uvwy  in {0,1}  for two segments (u,v), (w,y) from row r to row r+1 with
//             u < w and v != y (segments sharing an end never cross), not both
//             inner: 1 iff they cross.
//   c^r_dE    in {0,1}  for d in D_r and E in E_r with k = 2: 1 iff d lies between
//             the two members of E, crossing E's line.
//   l^r_dE, m^r_dE in [0,1]  for d in D_r and E in E_r with k >= 3: 1 iff d lies
//             before (l) or after (m) every member of E.
//
// ── Model ────────────────────────────────────────────────────────────────────
//
//   min   sum c^r_uvwy  +  sum_{k = 2} c^r_dE  +  sum_{k >= 3} (1 - l^r_dE - m^r_dE)
//
//   s.t.
//   (1)  0 <= x^r_uw + x^r_wz - x^r_uz <= 1          u < w < z in row r
//   (2)  -c^r_uvwy <= X^{r+1}_vy - X^r_uw <= c^r_uvwy    (u,v), (w,y) not both inner
//   (3)  X^{r+1}_vy = X^r_uw                          (u,v), (w,y) both inner
//   (4)  X^r_{a e_j} = X^r_{a e_1}                    a in N_r, E in E_r, j = 2..k
//   (5)  X^r_ef = X^r_{e_1 f_1}                       E != F in E_r, (e,f) in E x F
//   (6)  -c^r_dE <= X^r_{d e_2} - X^r_{d e_1} <= c^r_dE  d in D_r, E in E_r, k = 2
//   (7)  l^r_dE <= X^r_{d e_j},  m^r_dE <= X^r_{e_j d}   d in D_r, E in E_r, k >= 3,
//                                                      j = 1..k
//   (8)  X^r_ab = 1                                    a, b twins in N_r, a before b
//                                                      in the heuristic's order
//
//   (1) makes every row a linear order.
//   (2) two segments cross iff their upper ends and their lower ends are in different
//       orders. Written with X it is one line; with x it is the textbook's two cases,
//       -c <= x_vy - x_uw <= c when v < y, and 1 - c <= x_yv + x_uw <= 1 + c when y < v.
//   (3) two long connections never cross (type-2 conflicts): (2) with c = 0.
//   (4) a real node is on one side of all the members of every equivalence.
//   (5) one equivalence lies wholly before or after another, so they never mix. The
//       members of one equivalence are not constrained among themselves: the solve
//       may reorder them.
//   (6) a dummy between the two members of an equivalence crosses its line once.
//   (7) the same for three or more members: the objective raises l and m as far as
//       (7) lets it, so 1 - l - m is 0 when d is on one side of E and 1 when d lies
//       between two members. This bounds the fractional solutions tighter than (6)
//       would with k >= 3; with k = 2 both are equally tight and (6) is smaller.
//       l and m need not be integer: with x integer, their best values are 0 or 1.
//   (8) twins keep the order they have: every order of them has the same crossings,
//       so this only spares the solver proving the same thing once per order. It is
//       a bound on the pair's column, not a row. Members of an equivalence are never
//       fixed this way, which would pin their whole equivalence.
//
//   The objective counts the segment crossings and the dummies crossing an
//   equivalence line, as Global Sifting does. Its constant term (one per pair d, E
//   with k >= 3) is added back to the count reported.
//
// ── Building it ──────────────────────────────────────────────────────────────
//
// (4) and (5) are not added as rows: the pairs they make equal (or opposite) share
// one column, and the transitivity rows that leaves always true, or repeated, are
// left out. The size limits below count the model that is actually built.
//
// Solver backend: the model itself (columns + sparse rows, below) is built
// once, independent of which solver ends up running it. HiGHS is always
// available and is the default/fallback backend. When Gurobi (any version,
// found at run time, see GurobiRuntime.h) is installed and licensed on the
// machine, Gurobi is used instead, since it's substantially faster on this
// kind of MIP.
//
// Two run modes (see ILPCancellationToken.h for the public API): by default
// ("fast"), the exact-solve attempt is capped at kILPTimeBudgetSeconds and
// always falls back to the heuristic if that runs out. If
// hypergraph_logic::beginInterruptibleILP(token) was called immediately
// before this solve, it instead runs with NO time limit, cooperatively
// checking `token` from inside whichever backend is solving and stopping as
// soon as it's cancelled ("slow", externally stoppable via a UI Pause
// button). Both backends return whatever incumbent they'd found so far when
// stopped this way, same as running out of a time budget.
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
		// practical branch-and-cut instances. The first two count the model as built
		// (merged columns, transitivity rows kept); the last bounds the triples looked
		// at to build it.
		constexpr long long kMaxTransitivityConstraints = 300000;
		constexpr long long kMaxColumns = 200000;
		constexpr long long kMaxEnumeratedTriples = 5000000;

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
		// was built by this file. Every column is in [0,1]; col_integrality says
		// which are binary (kInteger) and which continuous. HiGHS takes it as is,
		// Gurobi declares the binary ones GRB_BINARY.
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
			bool out_of_time = false; // Gurobi only: see GurobiSolution::out_of_time
		};

		// ── HiGHS backend ──────────────────────────────────────────────────────
		// Always available; this is the code that used to live directly in
		// runCrossingILP() before there were two backends to choose between.
		//
		// token == nullptr: "fast" mode, capped at time_budget_seconds.
		// token != nullptr: "slow" mode -- time_budget_seconds is ignored
		// entirely (no time_limit is set at all) and HiGHS's own MIP-interrupt
		// callback is used instead, checking *token on every call (HiGHS makes
		// this call periodically, whenever its MIP solver checks its own
		// computation limits) and asking HiGHS to stop as soon as it's been
		// cancelled by setting data_in->user_interrupt.
		ILPSolveResult solveWithHighs(const ILPModel& m, double time_budget_seconds, ILPCancellationToken* token) {
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

			if (token != nullptr) {
				// Slow mode: no time limit; ask HiGHS to call us back
				// periodically during the MIP search so we can request a stop.
				highs.setCallback(
					[](int, const std::string&, const HighsCallbackDataOut*,
						HighsCallbackDataIn* data_in, void* user_data) {
							auto* tok = static_cast<ILPCancellationToken*>(user_data);
							if (tok != nullptr && tok->isCancelled()) {
								data_in->user_interrupt = 1;
							}
					},
					static_cast<void*>(token));
				highs.startCallback(static_cast<int>(HighsCallbackType::kCallbackMipInterrupt));
			}
			else {
				highs.setOptionValue("time_limit", time_budget_seconds);
			}

			if (highs.passModel(model) != HighsStatus::kOk) return result;

			// Warm start from the heuristic's already-computed block order.
			HighsSolution guess;
			guess.col_value = m.col_guess;
			guess.value_valid = true;
			highs.setSolution(guess);

			// Stopping early is not a failure: when the time limit ("fast"
			// mode) or the user's "Pausar" ("slow" mode) ends the search,
			// run() returns kWarning and the best solution found so far is
			// still usable (see HighsOutcome.h).
			const HighsStatus run_status = highs.run();
			if (!highs_outcome::hasUsableSolution(highs, run_status)) return result;

			result.success = true;
			result.col_value = highs.getSolution().col_value;
			result.objective = highs.getInfo().objective_function_value;
			return result;
		}

		// ── Gurobi backend ─────────────────────────────────────────────────────
		//
		// Gurobi is found at run time (any version, see GurobiRuntime.h), and
		// whether it has a currently-valid license (a license server can be
		// unreachable, a named-user license can have expired, etc.) can only be
		// known by asking it. So this backend fails safely: any problem, license
		// or otherwise, comes back as an unsuccessful ILPSolveResult rather than
		// propagating, so the caller (runCrossingILP) falls back to HiGHS
		// transparently. Running out of time with no solution is no problem of
		// that kind (out_of_time): the caller then keeps the heuristic's order.

		// Whether Gurobi is installed and licensed here (checked once and cached
		// for the life of the process: verifying a license can mean a round
		// trip to a license server), and not turned off (setGurobiAllowed).
		bool gurobiUsable() {
			return gurobiInUse();
		}

		// token == nullptr: "fast" mode, capped at time_budget_seconds.
		// token != nullptr: "slow" mode -- no time limit at all; Gurobi polls
		// the token during the search instead and stops as soon as it is
		// cancelled, keeping the best order found so far (the same pattern as
		// the HiGHS interrupt callback above).
		ILPSolveResult solveWithGurobi(const ILPModel& m, double time_budget_seconds, ILPCancellationToken* token) {
			GurobiMip mip;
			mip.lower = m.col_lower;
			mip.upper = m.col_upper;
			mip.cost = m.col_cost;
			mip.binary.reserve(m.col_integrality.size());
			for (HighsVarType type : m.col_integrality) mip.binary.push_back(type == HighsVarType::kInteger);
			mip.start = m.col_guess; // warm start from the heuristic's already-computed block order
			mip.rows.reserve(m.rows.size());
			for (const auto& row : m.rows)
				mip.rows.push_back({ row.idx.data(), row.val.data(), static_cast<int>(row.idx.size()), row.lo, row.hi });
			if (token != nullptr) mip.should_stop = [token] { return token->isCancelled(); };
			else                  mip.time_limit = time_budget_seconds;

			GurobiSolution solution = solveMipWithGurobi(mip);
			ILPSolveResult result;
			result.out_of_time = solution.out_of_time;
			if (!solution.success) return result;
			result.success = true;
			result.col_value = std::move(solution.x);
			result.objective = solution.objective;
			return result;
		}

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
		return gurobiUsable();
	}

	// ── interruptible-solve hook (see ILPCancellationToken.h) ──────────────────
	//
	// Deliberately a plain (non-atomic) pointer: only the pointer itself needs
	// no synchronization (it's set on one thread before the solve starts and
	// read once, at the very start of that same solve, by runCrossingILP()
	// below, which immediately resets it to nullptr so it can never leak past
	// the one call it was armed for).
	ILPCancellationToken* g_active_cancel_token = nullptr;

	// ── runCrossingILP ────────────────────────────────────────────────────────────

	int GlobalSifter::runCrossingILP(double time_budget_seconds) {
		// The hooks armed for this call apply to it alone, so they are taken before
		// anything else: no early return below may leave them for a later call.
		ILPBackendOverride backend = g_ilp_backend_override;
		g_ilp_backend_override = ILPBackendOverride::kAuto;

		ILPCancellationToken* cancel_token = g_active_cancel_token;
		g_active_cancel_token = nullptr;

		// Rows (G1 layers) in ascending order.
		std::vector<int> row_keys;
		row_keys.reserve(S_.g1_layers.size());
		for (const auto& [layer, nodes] : S_.g1_layers) row_keys.push_back(layer);
		std::sort(row_keys.begin(), row_keys.end());
		if (row_keys.size() < 2) return 0; // nothing to cross.

		// Node-index list per row, in CURRENT order -- i.e. whatever order
		// S_.g1_layers already has them in (the heuristic's finished order, since
		// Hypergraph::minimizeCrossingsILP() always calls runSifting() first on
		// this same sifter). This is the reference order: u < w in every constraint
		// below means u's CURRENT position in the row precedes w's.
		std::vector<std::vector<int>> row_nodes(row_keys.size());
		for (size_t r = 0; r < row_keys.size(); r++)
			row_nodes[r] = S_.g1_layers.at(row_keys[r]); // copy, order preserved

		// g1 node index -> its current position within its own row.
		std::vector<int> pos_in_row(S_.g1_nodes.size(), -1);
		for (const auto& nodes : row_nodes)
			for (size_t p = 0; p < nodes.size(); p++)
				pos_in_row[nodes[p]] = static_cast<int>(p);

		// Safety valve on model size, before we build anything. Without equivalences
		// every triple is a transitivity row of its own, so the limit on rows applies
		// right away; with them, merging may leave fewer, and the rows are counted as
		// they are built.
		long long worst_case_triples = 0;
		for (const auto& nodes : row_nodes) {
			long long m = static_cast<long long>(nodes.size());
			worst_case_triples += m * (m - 1) * (m - 2) / 6;
		}
		if (worst_case_triples > kMaxEnumeratedTriples) return -1;
		if (chains_.empty() && worst_case_triples > kMaxTransitivityConstraints) return -1;

		// ── Ordering variables ───────────────────────────────────────────────────
		//
		// One pair per two nodes u < w sharing a row. A raw G1 node belongs to exactly
		// one row, so a given pair can only ever be created once.
		std::map<std::pair<int, int>, int> pair_index;
		std::vector<std::pair<int, int>> pairs;
		for (const auto& nodes : row_nodes) {
			for (size_t i = 0; i < nodes.size(); i++) {
				for (size_t j = i + 1; j < nodes.size(); j++) {
					pair_index.emplace(std::make_pair(nodes[i], nodes[j]), static_cast<int>(pairs.size()));
					pairs.push_back({ nodes[i], nodes[j] });
				}
			}
		}
		if (pairs.empty()) return 0; // no row has 2+ nodes: nothing to decide.

		// X_ab, "a before b", for any two nodes of a row: the pair's x, or 1 - x when
		// b comes first in the reference order (flip).
		struct Lit {
			int pair;
			char flip;
		};
		auto before = [&](int a, int b) -> Lit {
			if (pos_in_row[a] < pos_in_row[b]) return { pair_index.at({ a, b }), 0 };
			return { pair_index.at({ b, a }), 1 };
		};

		// The members of every chain, and the dummies and lone real nodes of its rows.
		struct ChainRow {
			std::vector<std::vector<int>> chains; // the members (g1 indices) of each
			std::vector<int> dummies;             // D_r
			std::vector<int> lone;                // N_r
		};
		std::map<int, ChainRow> chain_rows;
		for (const auto& [row, chain_ids] : chains_at_row_) {
			ChainRow& cr = chain_rows[row];
			for (int c : chain_ids) {
				cr.chains.emplace_back();
				for (int bid : chains_[c].blocks) cr.chains.back().push_back(S_.blocks[bid].g1_nodes.front());
			}
			for (int g1 : S_.g1_layers.at(row)) {
				const Node* original = S_.g1_nodes[g1].original;
				if (original == nullptr) continue;
				if (original->isDummy()) cr.dummies.push_back(g1);
				else if (chainOf(S_.g1_nodes[g1].block_id) < 0) cr.lone.push_back(g1);
			}
		}

		// The pairs (4) and (5) make equal form classes, each sharing one column: the
		// pairs of a lone node a with a chain E, represented by (a, e_1), and those of two
		// chains E and F, represented by (e_1, f_1). rep[p] is the representative of p's
		// class (p itself when it is in none), and flipped[p] whether p's x is 1 minus the
		// representative's. A pair has either a lone node and a member, or members of two
		// chains, so the classes never overlap: one level is all it takes, and no class
		// can tie a pair to itself with the opposite sign.
		std::vector<int> rep(pairs.size());
		std::iota(rep.begin(), rep.end(), 0);
		std::vector<char> flipped(pairs.size(), 0);
		// X_l = X_r: x_l = x_r, or 1 - x_r when the two are written the other way round.
		auto tie = [&](Lit l, Lit r) {
			rep[l.pair] = r.pair;
			flipped[l.pair] = l.flip ^ r.flip;
		};
		for (const auto& [row, cr] : chain_rows) {
			// (4) a lone real node on one side of all the members of every chain.
			for (int a : cr.lone)
				for (const auto& members : cr.chains)
					for (size_t j = 1; j < members.size(); j++)
						tie(before(a, members[j]), before(a, members[0]));
			// (5) two chains never mix.
			for (size_t c = 0; c < cr.chains.size(); c++) {
				for (size_t d = c + 1; d < cr.chains.size(); d++) {
					const auto& E = cr.chains[c];
					const auto& F = cr.chains[d];
					const Lit representative = before(E[0], F[0]);
					for (int e : E)
						for (int f : F)
							if (e != E[0] || f != F[0]) tie(before(e, f), representative);
				}
			}
		}

		std::vector<double> col_lower, col_upper, col_cost, col_guess;
		std::vector<HighsVarType> col_integrality;

		// The heuristic (runSifting(), called on this same sifter just before
		// runCrossingILP()) has already left a genuine total order over blocks in
		// S_.pi. Restricting that order to any single row automatically satisfies
		// every transitivity constraint, and since it treats each chain as one
		// atomic block with a single pi value shared across every row it spans,
		// two edges from the same chain-pair comparison get identical guessed
		// order under it too. It also keeps every equivalence together, with only
		// dummies between its members, so the pairs merged above all agree under it.
		// So this guess is a fully feasible integral solution, not just a rough
		// hint, and gives the solver a valid incumbent to prune against from the
		// start. guessOrder(u,v) answers "does u come before v" for ANY pair u,v,
		// independent of which one is the row's "u" or "w" label.
		auto guessOrder = [&](int u, int v) -> double {
			return (S_.pi[S_.g1_nodes[u].block_id] < S_.pi[S_.g1_nodes[v].block_id]) ? 1.0 : 0.0;
			};

		auto addColumn = [&](double cost, double guess, bool binary = true) -> int {
			int idx = static_cast<int>(col_lower.size());
			col_lower.push_back(0.0);
			col_upper.push_back(1.0);
			col_cost.push_back(cost);
			col_integrality.push_back(binary ? HighsVarType::kInteger : HighsVarType::kContinuous);
			col_guess.push_back(guess);
			return idx;
			};

		// One column per class, created up front: its representative's.
		std::vector<int> col_of_rep(pairs.size(), -1);
		for (int p = 0; p < static_cast<int>(pairs.size()); p++) {
			if (rep[p] != p) continue;
			// pure ordering variables don't enter the objective
			col_of_rep[p] = addColumn(0.0, guessOrder(pairs[p].first, pairs[p].second));
		}
		if (static_cast<long long>(col_lower.size()) > kMaxColumns) return -1;

		// (8) Twins: real nodes of a layer row in no equivalence with exactly the same
		// hubs above and below (the same connections in and out). Swapping two of them
		// never changes the crossings, so their order is fixed, as the heuristic left it
		// (so the warm start still satisfies it): the solver does not have to prove the
		// same thing once per order of them. Members of an equivalence are never fixed,
		// which would pin their whole equivalence. Their pairs are never merged, so each
		// has a column of its own.
		for (size_t r = 0; r < row_keys.size(); r++) {
			if (row_keys[r] % 2 != 0) continue; // hub rows: two hubs never share both ends
			std::map<std::pair<std::vector<int>, std::vector<int>>, std::vector<int>> twins;
			for (int g1 : row_nodes[r]) {
				const Node* original = S_.g1_nodes[g1].original;
				if (original == nullptr || original->isDummy() || chainOf(S_.g1_nodes[g1].block_id) >= 0) continue;
				std::vector<int> up = S_.g1_in[g1], down = S_.g1_out[g1];
				std::sort(up.begin(), up.end());
				std::sort(down.begin(), down.end());
				twins[{ std::move(up), std::move(down) }].push_back(g1);
			}
			for (auto& [neighbours, group] : twins) {
				if (group.size() < 2) continue;
				std::sort(group.begin(), group.end(), [&](int a, int b) { return guessOrder(a, b) == 1.0; });
				for (size_t i = 0; i < group.size(); i++) {
					for (size_t j = i + 1; j < group.size(); j++) {
						// X_{t_i t_j} = 1: the pair's x at 1, or at 0 when written the other way.
						const Lit l = before(group[i], group[j]);
						const int col = col_of_rep[rep[l.pair]];
						if ((flipped[l.pair] ^ l.flip) != 0) col_upper[col] = 0.0;
						else col_lower[col] = 1.0;
					}
				}
			}
		}

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

		// Constraint rows, built as linear expressions over the columns: a literal
		// adds its column (or a constant minus it), a column adds itself.
		struct Linear {
			std::map<int, double> terms;
			double constant = 0.0;
		};
		auto addLit = [&](Linear& e, Lit l, double coeff) {
			const int col = col_of_rep[rep[l.pair]];
			if ((flipped[l.pair] ^ l.flip) != 0) { e.constant += coeff; e.terms[col] -= coeff; }
			else e.terms[col] += coeff;
			};
		auto expr = [&](std::initializer_list<std::pair<Lit, double>> lits,
			std::initializer_list<std::pair<int, double>> cols = {}) {
				Linear e;
				for (const auto& [l, coeff] : lits) addLit(e, l, coeff);
				for (const auto& [col, coeff] : cols) e.terms[col] += coeff;
				return e;
			};

		std::vector<SparseRow> rows;
		// Rows already added, to leave out repeated ones.
		std::set<std::tuple<std::vector<std::pair<int, double>>, double, double>> seen;

		// lo <= e <= hi. Every column lies in [0,1] (the twins' fixed ones within it), so
		// a row the expression can never break for any values in [0,1] is left out, as is
		// one already added. Returns whether it was added.
		auto addConstraint = [&](const Linear& e, double lo, double hi) -> bool {
			std::vector<std::pair<int, double>> terms;
			double low = 0.0, high = 0.0; // the expression's range over [0,1] columns
			for (const auto& [idx, coeff] : e.terms) {
				if (coeff == 0.0) continue;
				terms.push_back({ idx, coeff });
				(coeff < 0.0 ? low : high) += coeff;
			}
			const double row_lo = (lo <= -kInf) ? -kInf : lo - e.constant;
			const double row_hi = (hi >= kInf) ? kInf : hi - e.constant;
			if (low >= row_lo - 1e-9 && high <= row_hi + 1e-9) return false;
			if (!seen.insert({ terms, row_lo, row_hi }).second) return false;
			SparseRow row;
			for (const auto& [idx, coeff] : terms) {
				row.idx.push_back(idx);
				row.val.push_back(coeff);
			}
			row.lo = row_lo;
			row.hi = row_hi;
			rows.push_back(std::move(row));
			return true;
			};

		// (1) Transitivity within each row: 0 <= x_uw + x_wz - x_uz <= 1 for every
		// u < w < z (standard order-polytope inequality).
		long long transitivity_rows = 0;
		for (const auto& nodes : row_nodes) {
			int m = static_cast<int>(nodes.size());
			for (int i = 0; i < m; i++) {
				for (int j = i + 1; j < m; j++) {
					const Lit Xuw = before(nodes[i], nodes[j]);
					for (int k = j + 1; k < m; k++) {
						const Linear e = expr({ { Xuw, 1.0 },
							{ before(nodes[j], nodes[k]), 1.0 }, { before(nodes[i], nodes[k]), -1.0 } });
						if (addConstraint(e, 0.0, 1.0) && ++transitivity_rows > kMaxTransitivityConstraints)
							return -1;
					}
				}
			}
		}

		// (2)-(3) Crossing variables and linking constraints between every pair of
		// consecutive rows, with type-2-conflict prevention for pairs of inner
		// segments. Edges are compared as (u,v),(w,y) with u < w in the upper row.
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

					// X_vy - X_uw: 0 when the two pairs are in the same order. In the
					// textbook's terms, x_vy - x_uw when v < y, and 1 - x_yv - x_uw
					// when y < v.
					const Lit Xuw = before(u, w), Xvy = before(v, y);
					if (isInnerSegment(u, v) && isInnerSegment(w, y)) {
						// (3) Type-2-conflict prevention: X_vy = X_uw.
						addConstraint(expr({ { Xvy, 1.0 }, { Xuw, -1.0 } }), 0.0, 0.0);
						continue;
					}
					// (2) -c <= X_vy - X_uw <= c
					int c_idx = addColumn(1.0, std::fabs(guessOrder(v, y) - guessOrder(u, w)));
					addConstraint(expr({ { Xvy, 1.0 }, { Xuw, -1.0 } }, { { c_idx, 1.0 } }), 0.0, kInf);
					addConstraint(expr({ { Xvy, 1.0 }, { Xuw, -1.0 } }, { { c_idx, -1.0 } }), -kInf, 0.0);
				}
			}
		}

		// (6)-(7) The dummies between the members of a chain, one crossing each with
		// its line.
		double objective_constant = 0.0;
		for (const auto& [row, cr] : chain_rows) {
			for (int d : cr.dummies) {
				for (const auto& E : cr.chains) {
					if (E.size() == 2) {
						// (6) -c <= X_{d e_2} - X_{d e_1} <= c
						const Lit second = before(d, E[1]), first = before(d, E[0]);
						int c_idx = addColumn(1.0, std::fabs(guessOrder(d, E[1]) - guessOrder(d, E[0])));
						addConstraint(expr({ { second, 1.0 }, { first, -1.0 } }, { { c_idx, 1.0 } }), 0.0, kInf);
						addConstraint(expr({ { second, 1.0 }, { first, -1.0 } }, { { c_idx, -1.0 } }), -kInf, 0.0);
						continue;
					}
					// (7) l <= X_{d e_j}, m <= X_{e_j d}; 1 - l - m in the objective.
					bool all_after = true, all_before = true; // d, in the guess
					for (int e : E) {
						all_after = all_after && guessOrder(d, e) == 1.0;
						all_before = all_before && guessOrder(e, d) == 1.0;
					}
					int l_idx = addColumn(-1.0, all_after ? 1.0 : 0.0, false);
					int m_idx = addColumn(-1.0, all_before ? 1.0 : 0.0, false);
					objective_constant += 1.0;
					for (int e : E) {
						addConstraint(expr({ { before(d, e), -1.0 } }, { { l_idx, 1.0 } }), -kInf, 0.0);
						addConstraint(expr({ { before(e, d), -1.0 } }, { { m_idx, 1.0 } }), -kInf, 0.0);
					}
				}
			}
		}

		if (col_lower.empty()) return 0; // nothing was ever decidable: already optimal.

		// Assemble the backend-neutral model, then solve it. Normally (kAuto)
		// Gurobi is tried first when this build has it and a valid license is
		// actually present on this machine right now; a Gurobi that fails to run
		// -- including one that throws partway through -- falls back to HiGHS
		// rather than giving up, but one that runs out of time with no solution
		// does not: HiGHS would only spend a budget of its own on the same model,
		// and the heuristic's order stands either way. A forced backend (see
		// ILPBackendTestHook.h) skips that auto-detect entirely, for
		// benchmarking one solver in isolation; it was read, and reset, at the
		// start of this call, so it can never apply to a later one.
		ILPModel ilp_model;
		ilp_model.col_lower = std::move(col_lower);
		ilp_model.col_upper = std::move(col_upper);
		ilp_model.col_cost = std::move(col_cost);
		ilp_model.col_guess = std::move(col_guess);
		ilp_model.col_integrality = std::move(col_integrality);
		ilp_model.rows = std::move(rows);

		ILPSolveResult result;
		switch (backend) {
		case ILPBackendOverride::kForceHighs:
			result = solveWithHighs(ilp_model, time_budget_seconds, cancel_token);
			break;
		case ILPBackendOverride::kForceGurobi:
			result = solveWithGurobi(ilp_model, time_budget_seconds, cancel_token);
			// No fallback here on purpose: a caller that explicitly forced
			// Gurobi wants to know Gurobi failed, not get a HiGHS number back
			// mislabeled as Gurobi's.
			break;
		case ILPBackendOverride::kAuto:
		default:
			if (gurobiUsable()) {
				result = solveWithGurobi(ilp_model, time_budget_seconds, cancel_token);
			}
			// HiGHS steps in when Gurobi is not there or failed to run; not when it ran
			// out of time, which HiGHS would only repeat with a budget of its own.
			if (!result.success && !result.out_of_time) {
				result = solveWithHighs(ilp_model, time_budget_seconds, cancel_token);
			}
			break;
		}
		if (!result.success) return -1; // neither backend found anything usable in time.

		const std::vector<double>& sol = result.col_value;
		int achieved_crossings = static_cast<int>(std::lround(result.objective + objective_constant));

		// Reorder each row of S_.g1_layers to match the solution. Every row is
		// independent here (a node belongs to exactly one row), so this is a
		// plain per-row sort by the solved "a before b" of every pair.
		auto valueOf = [&](Lit l) {
			const double x = sol[col_of_rep[rep[l.pair]]];
			return (flipped[l.pair] ^ l.flip) != 0 ? 1.0 - x : x;
			};
		for (size_t r = 0; r < row_keys.size(); r++) {
			auto& nodes_vec = S_.g1_layers.at(row_keys[r]);
			std::sort(nodes_vec.begin(), nodes_vec.end(), [&](int a, int b) {
				return a != b && valueOf(before(a, b)) > 0.5;
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
	// Always runs the existing heuristic pipeline first as a safety net, then
	// attempts the exact solve in one of two modes (see ILPCancellationToken.h):
	// by default ("fast"), up to kILPTimeBudgetSeconds; or, if
	// beginInterruptibleILP() was called immediately before this call, with no
	// time limit at all, stoppable instead via the token passed there ("slow").
	// Whichever result is smaller is committed to layers_: the ILP path via
	// writeBackFromG1Order(), the heuristic path via the existing block/pi-based
	// writeBack(). A cancelled or timed-out exact solve is handled identically
	// either way -- it just falls back to the heuristic baseline. Both keep every
	// equivalence together (the exact model through its constraints (4)-(5), see
	// the top of this file) and count the dummies crossing its line.
	int Hypergraph::minimizeCrossingsILP() {
		// The hooks armed for this call (beginInterruptibleILP, the backend override)
		// are dropped however it ends: runCrossingILP takes them when it runs, but this
		// may return, or throw, before that, and a token left behind would outlive the
		// dialog that owns it.
		struct DropHooks {
			~DropHooks() {
				g_active_cancel_token = nullptr;
				g_ilp_backend_override = ILPBackendOverride::kAuto;
			}
		} drop_hooks;

		if (getLayers().empty() || getAllHyperedges().size() < 2) {
			throw std::logic_error("No hay cruces que minimizar.");
		}
		int last_layer = static_cast<int>(layers_.rbegin()->first);

		placeUnpositionedNodes(0, last_layer);
		GlobalSifter sifter(0, last_layer, layers_, true, {}, {}, equivalencesToSift());
		// The barycenter pass, the baseline and the exact solve may all reorder a chain's
		// members: the stored chains follow the layers afterwards.
		auto commitSifting = [&] { sifter.writeBack(); syncEquivalenceOrder(); };
		if (sifter.countCrossings() == 0) { commitSifting(); return 0; }

		// Strong heuristic baseline
		sifter.runSifting(kFallbackSiftingRounds, false, true);
		int fallback_crossings = sifter.countCrossings();
		if (fallback_crossings == 0) { commitSifting(); return 0; }

		int ilp_crossings = sifter.runCrossingILP(kILPTimeBudgetSeconds);
		if (ilp_crossings >= 0 && ilp_crossings <= fallback_crossings) {
			sifter.writeBackFromG1Order();
			syncEquivalenceOrder();
			return ilp_crossings;
		}

		commitSifting();
		return fallback_crossings;
	}

	void beginInterruptibleILP(ILPCancellationToken& token) {
		token.reset();
		g_active_cancel_token = &token;
	}

} // namespace hypergraph_logic