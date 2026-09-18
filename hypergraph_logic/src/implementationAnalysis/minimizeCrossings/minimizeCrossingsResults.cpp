// ============================================================================
// minimizeCrossingsResults.cpp
//
// Efficiency measurements for the Global Sifting heuristic and the exact
// ILP-based technique (GlobalSifter::runCrossingILP).
//
// For each .asp instance, four techniques are measured:
//
//   (a) Natural — 50 distinct random permutations of the block list.
//       Each permutation is sifted for 10 rounds. Reported per instance:
//       avg_crossings (final, averaged over all 50 runs), avg_time_ms.
//
//   (b) Propagation — one run starting from orderBlocksByLayerPropagation.
//       The reported time includes both the ordering and the sifting phases.
//
//   (c) Barycenter — same orderBlocksByLayerPropagation seed as (b), refined
//       by GlobalSifter::runEfficientBarycenter before sifting. Since it
//       shares its seed and sifting rounds with (b), the delta between (b)
//       and (c) isolates exactly what the barycenter pass buys (or costs)
//       on top of an already-good seed.
//
//   (d) ILP — same orderBlocksByLayerPropagation + runEfficientBarycenter
//       seed as (c), then mirrors Hypergraph::minimizeCrossingsILP() end to
//       end: (c)'s full sifting result is kept as the fallback baseline,
//       GlobalSifter::runCrossingILP is attempted under ILP_TIME_BUDGET_SECONDS,
//       and whichever crossing count is smaller is reported. Time is the
//       full round-trip (ordering + barycenter + fallback sifting + ILP
//       attempt) -- exactly what a caller of minimizeCrossingsILP() pays,
//       win or lose.
//
// We only ever care about the FINAL crossing count and the time it took to
// get there -- no "before" counts, no ratios.
//
// Output (written next to this source file):
//   results.csv  -- one row per (instance, method): instance;method;crossings;time_ms
//   (semicolon-separated, comma as decimal separator — Spanish Excel format)
//
// Prints to stdout: progress + summary averages only.
//
// .asp format:
//   in_layer(<layer>, <node_name>)  ->  G1 node at g1_layer = layer
//   edge(<src>, <tgt>)              ->  direct g1_out[src] -> tgt  (no hubs)
//
// Usage:
//   ./minimizeCrossingsResults [path/to/AllInstances]
// ============================================================================
// minimizeCrossingsResults.cpp
//
// Efficiency measurements for the Global Sifting heuristic and the exact
// ILP-based technique (GlobalSifter::runCrossingILP).
//
// For each .asp instance, five techniques are measured:
//
//   (a) Natural — 50 distinct random permutations of the block list.
//       Each permutation is sifted for 10 rounds. Reported per instance:
//       avg_crossings (final, averaged over all 50 runs), avg_time_ms.
//
//   (b) Propagation — one run starting from orderBlocksByLayerPropagation.
//       The reported time includes both the ordering and the sifting phases.
//
//   (c) Barycenter — same orderBlocksByLayerPropagation seed as (b), refined
//       by GlobalSifter::runEfficientBarycenter before sifting. Since it
//       shares its seed and sifting rounds with (b), the delta between (b)
//       and (c) isolates exactly what the barycenter pass buys (or costs)
//       on top of an already-good seed.
//
//   (d) ILP-HiGHS / (e) ILP-Gurobi — same orderBlocksByLayerPropagation +
//       runEfficientBarycenter seed as (c), then mirror
//       Hypergraph::minimizeCrossingsILP() end to end: (c)'s full sifting
//       result is kept as the fallback baseline, GlobalSifter::runCrossingILP
//       is attempted under ILP_TIME_BUDGET_SECONDS, and whichever crossing
//       count is smaller is reported. Time is the full round-trip (ordering +
//       barycenter + fallback sifting + ILP attempt) -- exactly what a caller
//       of minimizeCrossingsILP() pays, win or lose. The two rows differ only
//       in which solver runCrossingILP() is pinned to for its ILP attempt
//       (via the ILPBackendTestHook.h test-only override) -- everything else,
//       including the seed and the time budget, is identical, so the two
//       rows are a fair head-to-head. ILP-Gurobi is reported as N/A if this
//       build wasn't compiled with Gurobi, or no valid Gurobi license is
//       usable on this machine right now.
//
// We only ever care about the FINAL crossing count and the time it took to
// get there -- no "before" counts, no ratios.
//
// Output (written next to this source file):
//   results.csv  -- one row per (instance, method): instance;method;crossings;time_ms
//   (semicolon-separated, comma as decimal separator — Spanish Excel format)
//   An unavailable row (ILP-Gurobi with no usable Gurobi) is written as
//   instance;ILP-Gurobi;N/A;N/A
//
// Prints to stdout: progress + summary averages only.
//
// .asp format:
//   in_layer(<layer>, <node_name>)  ->  G1 node at g1_layer = layer
//   edge(<src>, <tgt>)              ->  direct g1_out[src] -> tgt  (no hubs)
//
// Usage:
//   ./minimizeCrossingsResults [path/to/AllInstances]
// ============================================================================

#include "GlobalSifting.h"
#include "ILPBackendTestHook.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using namespace sifting_internal;


// ============================================================================
// TestGlobalSifter
//
// Subclass of GlobalSifter using the GS_TEST no-op constructor so that S_ and
// B_ can be injected directly. Exposes protected methods as free-function shims
// matching the call signatures used throughout this file.
// ============================================================================

struct ResultsSifter : GlobalSifter {
    ResultsSifter(SiftState S, BlockList B) {
        S_ = std::move(S);
        B_ = std::move(B);
    }
    void callSortAdjacencies() { sortAdjacencies(); }
    int  callSiftingStep(int a) { return siftingStep(a); }
    int  callCountCrossings() { return countCrossings(); }
    void callRunEfficientBarycenter(int max_iterations) { runEfficientBarycenter(max_iterations); }
    int  callRunCrossingILP(double time_budget_seconds) { return runCrossingILP(time_budget_seconds); }
};

static void sortAdjacencies(SiftState& S, BlockList& B) {
    ResultsSifter g(S, B);
    g.callSortAdjacencies();
    S = g.S_; B = g.B_;
}

static int siftingStep(SiftState& S, BlockList& B, int a) {
    ResultsSifter g(S, B);
    int r = g.callSiftingStep(a);
    S = g.S_; B = g.B_;
    return r;
}

static int countTotalCrossings(SiftState& S, BlockList& B) {
    ResultsSifter g(S, B);
    int r = g.callCountCrossings();
    S = g.S_; B = g.B_;
    return r;
}

static void runEfficientBarycenter(SiftState& S, BlockList& B, int max_iterations = 100) {
    ResultsSifter g(S, B);
    g.callRunEfficientBarycenter(max_iterations);
    S = g.S_; B = g.B_;
}

// Returns the resulting crossing count on success, or -1 if the solver
// couldn't produce a usable solution within the time budget -- exactly
// GlobalSifter::runCrossingILP's own contract. On success, S/B are
// overwritten with the solved ordering (also matching runCrossingILP);
// on failure they're left untouched.
static int runCrossingILP(SiftState& S, BlockList& B, double time_budget_seconds) {
    ResultsSifter g(S, B);
    int r = g.callRunCrossingILP(time_budget_seconds);
    if (r >= 0) { S = g.S_; B = g.B_; }
    return r;
}

static constexpr int SIFTING_ROUNDS = 10;
static constexpr int RANDOM_RUNS = 50;
static constexpr int RNG_SEED = 42;
static constexpr double ILP_TIME_BUDGET_SECONDS = 30.0; // matches minimizeCrossingsILP()'s default

// Directory of this source file — the CSV is written here.
static const fs::path SOURCE_DIR = fs::path(__FILE__).parent_path();

// ============================================================================
// Formatting helper: double with comma as decimal separator
// ============================================================================

static std::string fmtDouble(double v, int precision = 4) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << v;
    std::string s = ss.str();
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

// ============================================================================
// Parser
// ============================================================================

struct ParsedInstance {
    std::unordered_map<std::string, int>             node_layer;
    std::vector<std::pair<std::string, std::string>> edges;
    bool        ok = false;
    std::string error;
};

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string innerParens(const std::string& line) {
    size_t a = line.find('('), b = line.rfind(')');
    if (a == std::string::npos || b == std::string::npos || b <= a) return "";
    return line.substr(a + 1, b - a - 1);
}

static std::vector<std::string> splitComma(const std::string& s) {
    std::vector<std::string> parts;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) parts.push_back(trim(tok));
    return parts;
}

static ParsedInstance parseAsp(const fs::path& path) {
    ParsedInstance inst;
    std::ifstream f(path);
    if (!f) { inst.error = "cannot open file"; return inst; }
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '%') continue;
        if (line.rfind("in_layer(", 0) == 0) {
            auto p = splitComma(innerParens(line));
            if (p.size() == 2) inst.node_layer[p[1]] = std::stoi(p[0]);
        }
        else if (line.rfind("edge(", 0) == 0) {
            auto p = splitComma(innerParens(line));
            if (p.size() == 2) inst.edges.push_back({ p[0], p[1] });
        }
    }
    inst.ok = true;
    return inst;
}

// ============================================================================
// SiftState builder
// ============================================================================

static bool buildState(const ParsedInstance& inst, SiftState& S) {
    std::vector<std::pair<int, std::string>> ordered;
    ordered.reserve(inst.node_layer.size());
    for (const auto& [name, layer] : inst.node_layer)
        ordered.push_back({ layer, name });
    std::sort(ordered.begin(), ordered.end());

    std::unordered_map<std::string, int> name_to_g1;
    name_to_g1.reserve(ordered.size());
    for (const auto& [layer, name] : ordered) {
        int idx = static_cast<int>(S.g1_nodes.size());
        S.g1_nodes.emplace_back(nullptr, layer);
        S.g1_layers[layer].push_back(idx);
        name_to_g1[name] = idx;
    }

    int n = static_cast<int>(S.g1_nodes.size());
    S.g1_in.assign(n, {});
    S.g1_out.assign(n, {});

    for (const auto& [src_name, tgt_name] : inst.edges) {
        auto sit = name_to_g1.find(src_name);
        auto tit = name_to_g1.find(tgt_name);
        if (sit == name_to_g1.end() || tit == name_to_g1.end()) {
            S = SiftState{};
            return false;
        }
        S.g1_out[sit->second].push_back(tit->second);
        S.g1_in[tit->second].push_back(sit->second);
    }

    for (int i = 0; i < n; ++i) {
        S.g1_nodes[i].block_id = i;
        S.blocks.emplace_back(std::vector<int>{i});
    }
    S.pi.resize(n, 0);
    S.fixed_position_count = 0;
    return true;
}

// ============================================================================
// orderBlocksByLayerPropagation
// ============================================================================

static BlockList orderBlocksByLayerPropagation(SiftState& S) {
    S.pi.assign(S.blocks.size(), INT_MAX);
    {
        const auto& first = S.g1_layers.begin()->second;
        for (int pos = 0; pos < static_cast<int>(first.size()); ++pos)
            S.pi[S.g1_nodes[first[pos]].block_id] = pos;
    }

    std::unordered_set<int> visited;
    for (int idx : S.g1_layers.begin()->second)
        visited.insert(S.g1_nodes[idx].block_id);

    bool first = true;
    for (auto& [key, nodes] : S.g1_layers) {
        if (first) { first = false; continue; }

        std::vector<int> new_nodes;
        for (int idx : nodes)
            if (!visited.count(S.g1_nodes[idx].block_id))
                new_nodes.push_back(idx);

        std::unordered_map<int, int> left_of;
        for (int idx : new_nodes) left_of[idx] = INT_MAX;
        for (int idx : new_nodes)
            for (int par : S.g1_in[idx])
                left_of[idx] = std::min(left_of[idx],
                    S.pi[S.g1_nodes[par].block_id]);

        std::stable_sort(new_nodes.begin(), new_nodes.end(),
            [&](int a, int b) { return left_of.at(a) < left_of.at(b); });

        for (int pos = 0; pos < static_cast<int>(new_nodes.size()); ++pos) {
            int bid = S.g1_nodes[new_nodes[pos]].block_id;
            S.pi[bid] = pos;
            visited.insert(bid);
        }
    }

    BlockList B;
    B.reserve(S.blocks.size());
    std::unordered_set<int> seen;
    for (auto& [key, nodes] : S.g1_layers)
        for (int idx : nodes) {
            int bid = S.g1_nodes[idx].block_id;
            if (seen.insert(bid).second) B.push_back(bid);
        }

    for (int pos = 0; pos < static_cast<int>(B.size()); ++pos)
        S.pi[B[pos]] = pos;
    return B;
}

// ============================================================================
// Result types -- final crossing count + elapsed time only.
// ============================================================================

struct SingleRunResult {
    int    crossings;
    double elapsed_ms;
    bool   available = true; // false only for e.g. ILP-Gurobi when Gurobi isn't usable here
};

struct NaturalAggResult {
    double avg_crossings;
    double avg_time_ms;
};

// ============================================================================
// Sifting runner — works on copies so S_base is never modified
// ============================================================================

static SingleRunResult runSifting(SiftState S, BlockList B) {
    sortAdjacencies(S, B);

    auto t0 = std::chrono::high_resolution_clock::now();
    int numblocks = static_cast<int>(B.size());
    for (int round = 0; round < SIFTING_ROUNDS; ++round) {
        int chi = 0;
        BlockList snap = B;
        for (int i = S.fixed_position_count; i < numblocks; ++i)
            chi += siftingStep(S, B, snap[i]);
        if (chi >= 0) break;
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    SingleRunResult r;
    r.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    r.crossings = countTotalCrossings(S, B);
    return r;
}

// ============================================================================
// (a) Natural — 50 random permutations, averaged
// ============================================================================

static NaturalAggResult runNatural(const SiftState& S_base, std::mt19937& rng) {
    int n = static_cast<int>(S_base.blocks.size());
    BlockList base_B(n);
    std::iota(base_B.begin(), base_B.end(), 0);

    std::vector<BlockList> perms;
    perms.reserve(RANDOM_RUNS);
    std::set<BlockList> seen_perms;

    const int max_attempts = RANDOM_RUNS * 20;
    int attempts = 0;
    while (static_cast<int>(perms.size()) < RANDOM_RUNS && attempts < max_attempts) {
        BlockList p = base_B;
        std::shuffle(p.begin(), p.end(), rng);
        if (seen_perms.insert(p).second)
            perms.push_back(p);
        ++attempts;
    }

    double sum_crossings = 0, sum_time = 0;
    for (const auto& perm : perms) {
        SingleRunResult r = runSifting(S_base, perm);
        sum_crossings += r.crossings;
        sum_time += r.elapsed_ms;
    }

    int runs = static_cast<int>(perms.size());
    return { sum_crossings / runs, sum_time / runs };
}

// ============================================================================
// (c) Barycenter — orderBlocksByLayerPropagation seed, refined by
// GlobalSifter::runEfficientBarycenter, then sifted. Mirrors Propagation's
// timing convention (ordering + refinement + sifting all folded into
// elapsed_ms) so the two rows are directly comparable.
// ============================================================================

static SingleRunResult runBarycenter(SiftState S_base) {
    auto t_ord_start = std::chrono::high_resolution_clock::now();
    BlockList B = orderBlocksByLayerPropagation(S_base);
    runEfficientBarycenter(S_base, B);
    auto t_ord_end = std::chrono::high_resolution_clock::now();
    double ordering_ms = std::chrono::duration<double, std::milli>(
        t_ord_end - t_ord_start).count();

    SingleRunResult r = runSifting(S_base, B);
    r.elapsed_ms += ordering_ms;   // fold ordering + barycenter cost into total time
    return r;
}

// ============================================================================
// (d)/(e) ILP-HiGHS / ILP-Gurobi — same orderBlocksByLayerPropagation +
// runEfficientBarycenter seed as Barycenter. From there this mirrors
// Hypergraph::minimizeCrossingsILP() end to end: full sifting on the seed is
// the fallback baseline, then GlobalSifter::runCrossingILP is attempted under
// ILP_TIME_BUDGET_SECONDS -- pinned to `backend` via the ILPBackendTestHook.h
// test-only override so the two rows exercise one solver each instead of
// whatever auto-detect would have picked -- and whichever crossing count is
// smaller wins. Reported elapsed_ms is the FULL round trip (ordering +
// barycenter + fallback sifting + ILP attempt), since that is exactly what a
// caller of minimizeCrossingsILP() pays regardless of which side ends up
// winning.
//
// Returns available=false (crossings/elapsed_ms left at 0) without running
// anything when `backend` is kForceGurobi and Gurobi isn't compiled in or
// isn't currently licensed on this machine -- checked up front via
// isGurobiUsableForTesting() so we never mistake "Gurobi unavailable" for
// "Gurobi found 0 improvement".
// ============================================================================

static SingleRunResult runILP(SiftState S_base, ILPBackendOverride backend) {
    if (backend == ILPBackendOverride::kForceGurobi && !isGurobiUsableForTesting()) {
        return { 0, 0.0, false };
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    BlockList B = orderBlocksByLayerPropagation(S_base);
    runEfficientBarycenter(S_base, B);

    // Fallback baseline: same seed, fully sifted (mirrors minimizeCrossingsILP()'s
    // own safety net, which always runs this unconditionally before attempting
    // the exact solve).
    SiftState S_fallback = S_base;
    BlockList B_fallback = B;
    SingleRunResult fallback = runSifting(S_fallback, B_fallback);

    // Exact solve attempt, on a fresh copy of the same seed, pinned to `backend`.
    SiftState S_ilp = S_base;
    BlockList B_ilp = B;
    sortAdjacencies(S_ilp, B_ilp);
    setILPBackendOverrideForTesting(backend);
    int ilp_crossings = runCrossingILP(S_ilp, B_ilp, ILP_TIME_BUDGET_SECONDS);

    auto t1 = std::chrono::high_resolution_clock::now();

    SingleRunResult r;
    r.crossings = (ilp_crossings >= 0 && ilp_crossings <= fallback.crossings)
        ? ilp_crossings : fallback.crossings;
    r.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    r.available = true;
    return r;
}

// ============================================================================
// Combined CSV writer
// One row per (instance, method): instance;method;crossings;time_ms
// Semicolon-separated, comma as decimal separator (Spanish Excel format).
// An unavailable row (available == false) is written as method;N/A;N/A.
// Written next to this source file.
// ============================================================================

static void writeCombinedCsv(
    const std::vector<std::string>& names,
    const std::vector<NaturalAggResult>& nat,
    const std::vector<SingleRunResult>& prop,
    const std::vector<SingleRunResult>& bary,
    const std::vector<SingleRunResult>& ilp_highs,
    const std::vector<SingleRunResult>& ilp_gurobi)
{
    fs::path out = SOURCE_DIR / "results.csv";
    std::ofstream f(out);
    f << "instance;method;crossings;time_ms\n";
    for (size_t i = 0; i < names.size(); ++i) {
        f << names[i] << ";Natural;"
            << fmtDouble(nat[i].avg_crossings) << ";"
            << fmtDouble(nat[i].avg_time_ms) << "\n";
        f << names[i] << ";Propagation;"
            << prop[i].crossings << ";"
            << fmtDouble(prop[i].elapsed_ms) << "\n";
        f << names[i] << ";Barycenter;"
            << bary[i].crossings << ";"
            << fmtDouble(bary[i].elapsed_ms) << "\n";
        f << names[i] << ";ILP-HiGHS;"
            << ilp_highs[i].crossings << ";"
            << fmtDouble(ilp_highs[i].elapsed_ms) << "\n";
        if (ilp_gurobi[i].available) {
            f << names[i] << ";ILP-Gurobi;"
                << ilp_gurobi[i].crossings << ";"
                << fmtDouble(ilp_gurobi[i].elapsed_ms) << "\n";
        }
        else {
            f << names[i] << ";ILP-Gurobi;N/A;N/A\n";
        }
    }
    std::cout << "Results -> " << fs::absolute(out) << "\n";
}

// ============================================================================
// Summary printer
// ============================================================================

static void printSummary(
    const std::vector<NaturalAggResult>& nat,
    const std::vector<SingleRunResult>& prop,
    const std::vector<SingleRunResult>& bary,
    const std::vector<SingleRunResult>& ilp_highs,
    const std::vector<SingleRunResult>& ilp_gurobi)
{
    auto avg = [](const auto& v, auto fn) {
        if (v.empty()) return 0.0;
        double s = 0;
        for (const auto& r : v) s += fn(r);
        return s / static_cast<double>(v.size());
        };

    // Only averages over rows actually marked available -- so ILP-Gurobi's
    // average isn't dragged toward zero by instances where it was skipped.
    auto avgAvailable = [](const std::vector<SingleRunResult>& v, auto fn) {
        double s = 0; int n = 0;
        for (const auto& r : v) if (r.available) { s += fn(r); ++n; }
        return n > 0 ? s / n : 0.0;
        };

    double nat_crossings = avg(nat, [](const NaturalAggResult& r) { return r.avg_crossings; });
    double nat_time = avg(nat, [](const NaturalAggResult& r) { return r.avg_time_ms; });
    double prop_crossings = avg(prop, [](const SingleRunResult& r) { return static_cast<double>(r.crossings); });
    double prop_time = avg(prop, [](const SingleRunResult& r) { return r.elapsed_ms; });
    double bary_crossings = avg(bary, [](const SingleRunResult& r) { return static_cast<double>(r.crossings); });
    double bary_time = avg(bary, [](const SingleRunResult& r) { return r.elapsed_ms; });
    double ilp_highs_crossings = avg(ilp_highs, [](const SingleRunResult& r) { return static_cast<double>(r.crossings); });
    double ilp_highs_time = avg(ilp_highs, [](const SingleRunResult& r) { return r.elapsed_ms; });

    int gurobi_available_count = 0;
    for (const auto& r : ilp_gurobi) if (r.available) ++gurobi_available_count;
    bool gurobi_any_available = gurobi_available_count > 0;
    double ilp_gurobi_crossings = avgAvailable(ilp_gurobi, [](const SingleRunResult& r) { return static_cast<double>(r.crossings); });
    double ilp_gurobi_time = avgAvailable(ilp_gurobi, [](const SingleRunResult& r) { return r.elapsed_ms; });

    std::cout << "\n";
    std::cout << std::string(48, '=') << "\n";
    std::cout << "SUMMARY  (averages across all instances)\n";
    std::cout << std::string(48, '-') << "\n";
    std::cout << std::left << std::setw(16) << ""
        << std::right << std::setw(14) << "Avg.Crossings"
        << std::setw(14) << "Avg.Time(ms)"
        << "\n";
    std::cout << std::string(48, '-') << "\n";

    auto row = [&](const std::string& label, double crossings, double time) {
        std::cout << std::left << std::setw(16) << label
            << std::right
            << std::setw(14) << std::fixed << std::setprecision(2) << crossings
            << std::setw(14) << std::fixed << std::setprecision(2) << time
            << "\n";
        };

    row("Natural", nat_crossings, nat_time);
    row("Propagation", prop_crossings, prop_time);
    row("Barycenter", bary_crossings, bary_time);
    row("ILP-HiGHS", ilp_highs_crossings, ilp_highs_time);
    if (gurobi_any_available) {
        row("ILP-Gurobi", ilp_gurobi_crossings, ilp_gurobi_time);
        if (gurobi_available_count < static_cast<int>(ilp_gurobi.size())) {
            std::cout << "  (ILP-Gurobi averaged over " << gurobi_available_count
                << "/" << ilp_gurobi.size() << " instances; rest were N/A)\n";
        }
    }
    else {
        std::cout << std::left << std::setw(16) << "ILP-Gurobi"
            << std::right << std::setw(28) << "N/A (Gurobi unavailable)" << "\n";
    }
    std::cout << std::string(48, '=') << "\n";
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    fs::path dir = (argc > 1) ? argv[1] : INSTANCES_DIR;

    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::cerr << "Error: directory not found: " << dir << "\n";
        return 1;
    }

    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ".asp")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());

    if (files.empty()) {
        std::cerr << "No .asp files found in " << dir << "\n";
        return 1;
    }

    std::cout << "Global Sifting / ILP - Efficiency Measurements\n";
    std::cout << "Instances    : " << fs::absolute(dir) << "\n";
    std::cout << "Rounds       : " << SIFTING_ROUNDS << "\n";
    std::cout << "Random runs  : " << RANDOM_RUNS << "  (seed " << RNG_SEED << ")\n";
    std::cout << "ILP budget   : " << ILP_TIME_BUDGET_SECONDS << "s per instance (per backend)\n";
    std::cout << "Gurobi       : " << (isGurobiUsableForTesting()
        ? "available (valid license detected) -- ILP-Gurobi will run"
        : "unavailable -- ILP-Gurobi will be reported as N/A") << "\n\n";

    std::mt19937 rng(RNG_SEED);

    std::vector<std::string>       instance_names;
    std::vector<NaturalAggResult>  nat_results;
    std::vector<SingleRunResult>   prop_results;
    std::vector<SingleRunResult>   bary_results;
    std::vector<SingleRunResult>   ilp_highs_results;
    std::vector<SingleRunResult>   ilp_gurobi_results;
    int skipped = 0;

    for (const auto& path : files) {
        const std::string name = path.stem().string();

        ParsedInstance inst = parseAsp(path);
        if (!inst.ok) {
            std::cerr << "[SKIP] " << name << ": " << inst.error << "\n";
            ++skipped;
            continue;
        }

        SiftState S_base;
        if (!buildState(inst, S_base) || S_base.blocks.empty()) {
            std::cerr << "[SKIP] " << name << "\n";
            ++skipped;
            continue;
        }

        // (a) Natural: 50 random permutations
        nat_results.push_back(runNatural(S_base, rng));

        // (b) Propagation: time includes ordering + sifting
        SiftState S_prop = S_base;

        auto t_ord_start = std::chrono::high_resolution_clock::now();
        BlockList B_prop = orderBlocksByLayerPropagation(S_prop);
        auto t_ord_end = std::chrono::high_resolution_clock::now();
        double ordering_ms = std::chrono::duration<double, std::milli>(
            t_ord_end - t_ord_start).count();

        SingleRunResult r_prop = runSifting(S_prop, B_prop);
        r_prop.elapsed_ms += ordering_ms;   // fold ordering cost into total time
        prop_results.push_back(r_prop);

        // (c) Barycenter: same seed as (b), refined by runEfficientBarycenter
        bary_results.push_back(runBarycenter(S_base));

        // (d)/(e) ILP: same seed as (c), each backend run on a fresh copy of it
        ilp_highs_results.push_back(runILP(S_base, ILPBackendOverride::kForceHighs));
        ilp_gurobi_results.push_back(runILP(S_base, ILPBackendOverride::kForceGurobi));

        instance_names.push_back(name);
        std::cout << "  processed: " << name << "\n";
    }

    printSummary(nat_results, prop_results, bary_results, ilp_highs_results, ilp_gurobi_results);

    std::cout << "\n";
    writeCombinedCsv(instance_names, nat_results, prop_results, bary_results,
        ilp_highs_results, ilp_gurobi_results);

    if (skipped > 0)
        std::cout << "\n(" << skipped << " instance(s) skipped)\n";

    return 0;
}