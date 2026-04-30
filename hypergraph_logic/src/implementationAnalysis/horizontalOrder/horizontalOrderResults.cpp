// ============================================================================
// horizontalOrderResults.cpp
//
// Efficiency measurements for the horizontal-order MIP (equations 26-30,
// Fridman et al. 2021).
//
// Two-layer random hypergraphs are generated inline. All tests are performed
// on the single layer boundary of a two-layer graph, since the MIP is solved
// independently per layer pair.
//
// ── Part 1: Stress test (MIP only) ──────────────────────────────────────────
//
//   For each configuration (n, e) where n = upper_nodes = lower_nodes and e
//   grows linearly:
//     • Generate one random two-layer hypergraph with RNG_SEED.
//     • Run assignCoordinates() + orderHyperedges(0).
//     • Record: crossings after MIP, elapsed time.
//   Results → results_stress.csv
//
// ── Part 2: MIP vs brute-force comparison ───────────────────────────────────
//
//   For a smaller range of (n, e) where brute-force over all n! orderings is
//   still tractable:
//     • Run MIP and brute-force on the same hypergraph.
//     • Record: MIP crossings, MIP time, BF crossings, BF time, optimal flag.
//   Results → results_comparison.csv
// ============================================================================

#include "HorizontalOrder.h"
#include "GraphicalHypergraph.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace hypergraph_logic;

static constexpr unsigned int RNG_SEED = 42;
static const fs::path SOURCE_DIR = fs::path(__FILE__).parent_path();

// ============================================================================
// Thin subclass — exposes node_layout_ for crossing helpers
// ============================================================================

class ResultsGraph : public GraphicalHypergraph {
public:
    explicit ResultsGraph(const std::string& name) : GraphicalHypergraph(name) {}
    const std::unordered_map<Node*, NodeLayout>& nodeLayout() const {
        return node_layout_;
    }
};

// ============================================================================
// Random two-layer hypergraph generator
//
// upper_nodes nodes at positions 0..upper_nodes-1 in layer 0.
// lower_nodes nodes at positions 0..lower_nodes-1 in layer 1.
// num_edges   hyperedges, each with 1..max_src sources and 1..max_tgt targets,
//             sampled without replacement from the respective layers.
// ============================================================================
static ResultsGraph generateTwoLayer(
    int upper_nodes, int lower_nodes, int num_edges,
    int min_src, int max_src, int min_tgt, int max_tgt,
    unsigned int seed)
{
    ResultsGraph g("random");
    std::mt19937 rng(seed);

    std::vector<NodePtr> upper(upper_nodes), lower(lower_nodes);
    for (int i = 0; i < upper_nodes; ++i)
        upper[i] = g.createNode("U" + std::to_string(i), i, nullptr);
    for (int j = 0; j < lower_nodes; ++j)
        lower[j] = g.createNode("L" + std::to_string(j), j, nullptr);

    // For each upper node, track which lower indices are already connected to it
    // across all existing hyperedges.
    // connected[u] = set of lower indices reachable from upper node u.
    std::vector<std::set<int>> connected(upper_nodes);

    using IndexVec = std::vector<int>;
    using EdgeKey = std::pair<IndexVec, IndexVec>;
    std::set<EdgeKey> used;

    std::uniform_int_distribution<int> sdist(min_src, max_src);
    std::uniform_int_distribution<int> tdist(min_tgt, max_tgt);

    std::vector<int> up_pool(upper_nodes), lo_pool(lower_nodes);
    std::iota(up_pool.begin(), up_pool.end(), 0);
    std::iota(lo_pool.begin(), lo_pool.end(), 0);

    int created = 0, attempts = 0;
    const int MAX_ATTEMPTS = num_edges * 500;

    while (created < num_edges && attempts < MAX_ATTEMPTS) {
        ++attempts;

        // Sample source indices.
        int ns = sdist(rng);
        std::shuffle(up_pool.begin(), up_pool.end(), rng);
        IndexVec si(up_pool.begin(), up_pool.begin() + ns);
        std::sort(si.begin(), si.end());

        // Compute the union of all lower indices already connected to any
        // of the sampled sources — these are forbidden for this edge.
        std::set<int> forbidden;
        for (int u : si)
            forbidden.insert(connected[u].begin(), connected[u].end());

        // Build the available target pool (lower indices not forbidden).
        std::vector<int> avail_lo;
        avail_lo.reserve(lower_nodes);
        for (int j = 0; j < lower_nodes; ++j)
            if (!forbidden.count(j)) avail_lo.push_back(j);

        // Need at least min_tgt targets available.
        if (static_cast<int>(avail_lo.size()) < min_tgt) continue;

        // Sample target indices from the available pool.
        int nt = std::uniform_int_distribution<int>(
            min_tgt, std::min(max_tgt, static_cast<int>(avail_lo.size())))(rng);
        std::shuffle(avail_lo.begin(), avail_lo.end(), rng);
        IndexVec ti(avail_lo.begin(), avail_lo.begin() + nt);
        std::sort(ti.begin(), ti.end());

        EdgeKey key{ si, ti };
        if (used.count(key)) continue;
        used.insert(key);

        // Register the new (source, target) connections.
        for (int u : si)
            for (int v : ti)
                connected[u].insert(v);

        HyperedgePtr edge = g.addConnection(upper[si[0]], lower[ti[0]]);
        for (int i = 1; i < ns; ++i) g.addSourceToEdge(edge, upper[si[i]]);
        for (int j = 1; j < nt; ++j) g.addTargetToEdge(edge, lower[ti[j]]);
        ++created;
    }

    if (created < num_edges)
        throw std::runtime_error(
            "generateTwoLayer: could only place " + std::to_string(created) +
            " / " + std::to_string(num_edges) + " unique edges after " +
            std::to_string(MAX_ATTEMPTS) + " attempts");

    return g;
}

// ============================================================================
// Formatting helper
// ============================================================================

static std::string fmtDouble(double v, int precision = 4) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << v;
    std::string s = ss.str();
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

// ============================================================================
// Crossing helpers
// ============================================================================

static double spanLo(const std::unordered_map<Node*, NodeLayout>& nl,
    const HyperedgePtr& e)
{
    double lo = std::numeric_limits<double>::max();
    for (const auto& s : e->getSources()) lo = std::min(lo, nl.at(s.get()).x);
    for (const auto& t : e->getTargets()) lo = std::min(lo, nl.at(t.get()).x);
    return lo;
}

static double spanHi(const std::unordered_map<Node*, NodeLayout>& nl,
    const HyperedgePtr& e)
{
    double hi = -std::numeric_limits<double>::max();
    for (const auto& s : e->getSources()) hi = std::max(hi, nl.at(s.get()).x);
    for (const auto& t : e->getTargets()) hi = std::max(hi, nl.at(t.get()).x);
    return hi;
}

// Total crossings for a given edge ordering.
// For each pair (i above j): CT += acs(e_i,e_j) + act(e_j,e_i).
static int totalCrossings(const std::vector<HyperedgePtr>& edges,
    const std::unordered_map<Node*, NodeLayout>& nl)
{
    int n = static_cast<int>(edges.size());
    int ct = 0;
    for (int i = 0; i < n; ++i) {
        double lo_i = spanLo(nl, edges[i]);
        double hi_i = spanHi(nl, edges[i]);
        for (int j = i + 1; j < n; ++j) {
            // acs(e_i, e_j): sources of e_j strictly inside span(e_i)
            for (const auto& s : edges[j]->getSources()) {
                double x = nl.at(s.get()).x;
                if (x > lo_i && x < hi_i) ++ct;
            }
            // act(e_j, e_i): targets of e_i strictly inside span(e_j)
            double lo_j = spanLo(nl, edges[j]);
            double hi_j = spanHi(nl, edges[j]);
            for (const auto& t : edges[i]->getTargets()) {
                double x = nl.at(t.get()).x;
                if (x > lo_j && x < hi_j) ++ct;
            }
        }
    }
    return ct;
}

// ============================================================================
// Brute-force: minimum crossings over all n! orderings
// ============================================================================

static std::pair<int, double> bruteForce(
    std::vector<HyperedgePtr> edges,
    const std::unordered_map<Node*, NodeLayout>& nl)
{
    int n = static_cast<int>(edges.size());
    std::vector<int> perm(n);
    std::iota(perm.begin(), perm.end(), 0);
    int best = std::numeric_limits<int>::max();

    auto t0 = std::chrono::high_resolution_clock::now();
    do {
        std::vector<HyperedgePtr> ordered(n);
        for (int i = 0; i < n; ++i) ordered[i] = edges[perm[i]];
        best = std::min(best, totalCrossings(ordered, nl));
    } while (std::next_permutation(perm.begin(), perm.end()));
    auto t1 = std::chrono::high_resolution_clock::now();

    return { best, std::chrono::duration<double, std::milli>(t1 - t0).count() };
}

// ============================================================================
// Configuration tables
// ============================================================================

struct StressConfig { int n; int e; };
struct CompConfig { int n; int e; };

// Part 1: n from 3 to 25 (step 2).
// For each node count we test four edge densities: low, medium, high, and max.
// The maximum number of distinct edges given the uniqueness rule is bounded by
// upper_nodes * lower_nodes (each source-target pair can appear in at most one
// edge). We cap the high density at 80% of that to leave a comfortable margin
// for the generator, and we use max_src = max_tgt = min(4, n/2) to allow
// multi-source/target edges while keeping the pool large enough.
static std::vector<StressConfig> buildStressConfigs() {
    std::vector<StressConfig> cfgs;
    for (int n = 3; n <= 25; n += 2) {
        int theoretical_max = n * n; // loose upper bound
        int low = std::max(2, n / 3);
        int mid = std::max(4, n);
        int high = std::max(8, n * 2);
        int stress = std::max(10, static_cast<int>(theoretical_max * 0.4));
        cfgs.push_back({ n, low });
        cfgs.push_back({ n, mid });
        cfgs.push_back({ n, high });
        cfgs.push_back({ n, stress });
    }
    return cfgs;
}

// Part 2: n from 3 to 10 (step 1), e from 2 to 7 (brute-force tractable).
static std::vector<CompConfig> buildCompConfigs() {
    std::vector<CompConfig> cfgs;
    for (int n = 3; n <= 10; ++n)
        for (int e = 2; e <= 9; ++e)
            cfgs.push_back({ n, e });
    return cfgs;
}

// ============================================================================
// Result structures
// ============================================================================

struct StressResult {
    std::string name;
    int         crossings_after;
    double      time_ms;
};

struct CompResult {
    std::string name;
    int         mip_crossings;
    double      mip_time_ms;
    int         bf_crossings;
    double      bf_time_ms;
    bool        optimal;
};

// ============================================================================
// Run one stress measurement
// ============================================================================

static StressResult runStress(const StressConfig& cfg) {
    // Allow up to half the layer as sources/targets so multi-endpoint edges
    // are common and the generator can place many distinct edges.
    int max_src = std::max(1, std::min(cfg.n / 2, 4));

    ResultsGraph g = generateTwoLayer(
        cfg.n, cfg.n, cfg.e, 1, max_src, 1, max_src, RNG_SEED);
    g.assignCoordinates();

    auto t0 = std::chrono::high_resolution_clock::now();
    g.orderHyperedges(0);
    auto t1 = std::chrono::high_resolution_clock::now();

    return {
        "hypergraph_(" + std::to_string(cfg.n) + "," + std::to_string(cfg.e) + ")",
        totalCrossings(g.getLayers().at(0).outgoing_edges, g.nodeLayout()),
        std::chrono::duration<double, std::milli>(t1 - t0).count()
    };
}

// ============================================================================
// Run one comparison measurement
// ============================================================================

static CompResult runComparison(const CompConfig& cfg) {
    int max_src = std::max(1, std::min(cfg.n / 2, 4));

    ResultsGraph g = generateTwoLayer(
        cfg.n, cfg.n, cfg.e, 1, max_src, 1, max_src, RNG_SEED);
    g.assignCoordinates();

    // Snapshot edges before MIP reorders them.
    std::vector<HyperedgePtr> snapshot = g.getLayers().at(0).outgoing_edges;
    const auto& nl = g.nodeLayout();

    auto t0 = std::chrono::high_resolution_clock::now();
    g.orderHyperedges(0);
    auto t1 = std::chrono::high_resolution_clock::now();
    double mip_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    int    mip_ct = totalCrossings(g.getLayers().at(0).outgoing_edges, nl);

    auto [bf_ct, bf_ms] = bruteForce(snapshot, nl);

    return {
        "hypergraph_(" + std::to_string(cfg.n) + "," + std::to_string(cfg.e) + ")",
        mip_ct, mip_ms, bf_ct, bf_ms, (mip_ct == bf_ct)
    };
}

// ============================================================================
// CSV writers
// ============================================================================

static void writeStressCsv(const std::vector<StressResult>& results) {
    fs::path out = SOURCE_DIR / "results_stress.csv";
    std::ofstream f(out);
    f << "instance;crossings_after_mip;time_ms\n";
    for (const auto& r : results)
        f << r.name << ";" << r.crossings_after << ";" << fmtDouble(r.time_ms) << "\n";
    std::cout << "Stress results      -> " << fs::absolute(out) << "\n";
}

static void writeComparisonCsv(const std::vector<CompResult>& results) {
    fs::path out = SOURCE_DIR / "results_comparison.csv";
    std::ofstream f(out);
    f << "instance;mip_crossings;mip_time_ms;bf_crossings;bf_time_ms;optimal\n";
    for (const auto& r : results)
        f << r.name << ";"
        << r.mip_crossings << ";" << fmtDouble(r.mip_time_ms) << ";"
        << r.bf_crossings << ";" << fmtDouble(r.bf_time_ms) << ";"
        << (r.optimal ? "yes" : "no") << "\n";
    std::cout << "Comparison results  -> " << fs::absolute(out) << "\n";
}

// ============================================================================
// Summary printers
// ============================================================================

static void printStressSummary(const std::vector<StressResult>& results) {
    double total_ms = 0, max_ms = 0;
    int max_ct = 0;
    for (const auto& r : results) {
        total_ms += r.time_ms;
        max_ms = std::max(max_ms, r.time_ms);
        max_ct = std::max(max_ct, r.crossings_after);
    }
    std::cout << "\n" << std::string(52, '=') << "\n";
    std::cout << "STRESS SUMMARY\n" << std::string(52, '-') << "\n";
    std::cout << std::left << std::setw(30) << "Configurations run"
        << std::right << std::setw(10) << results.size() << "\n";
    std::cout << std::left << std::setw(30) << "Total MIP time (ms)"
        << std::right << std::setw(10) << std::fixed << std::setprecision(2) << total_ms << "\n";
    std::cout << std::left << std::setw(30) << "Max crossings"
        << std::right << std::setw(10) << max_ct << "\n";
    std::cout << std::left << std::setw(30) << "Slowest instance (ms)"
        << std::right << std::setw(10) << std::fixed << std::setprecision(2) << max_ms << "\n";
    std::cout << std::string(52, '=') << "\n";
}

static void printComparisonSummary(const std::vector<CompResult>& results) {
    int    optimal_count = 0;
    double mip_ms = 0, bf_ms = 0;
    for (const auto& r : results) {
        if (r.optimal) ++optimal_count;
        mip_ms += r.mip_time_ms;
        bf_ms += r.bf_time_ms;
    }
    int n = static_cast<int>(results.size());
    std::cout << "\n" << std::string(52, '=') << "\n";
    std::cout << "COMPARISON SUMMARY\n" << std::string(52, '-') << "\n";
    std::cout << std::left << std::setw(30) << "Configurations run"
        << std::right << std::setw(10) << n << "\n";
    std::cout << std::left << std::setw(30) << "MIP optimal"
        << std::right << std::setw(9) << optimal_count << " / " << n << "\n";
    std::cout << std::left << std::setw(30) << "Avg MIP time (ms)"
        << std::right << std::setw(10) << std::fixed << std::setprecision(2)
        << (n ? mip_ms / n : 0.0) << "\n";
    std::cout << std::left << std::setw(30) << "Avg BF  time (ms)"
        << std::right << std::setw(10) << std::fixed << std::setprecision(2)
        << (n ? bf_ms / n : 0.0) << "\n";
    std::cout << std::string(52, '=') << "\n";
}

// ============================================================================
// main
// ============================================================================

int main() {
    std::cout << "Horizontal Order MIP — Efficiency Measurements\n";
    std::cout << "RNG seed: " << RNG_SEED << "\n\n";

    // ── Part 1: stress ────────────────────────────────────────────────────────
    std::cout << "----- Part 1: Stress test (MIP only, nodes 3..25) --------------------- \n";
    std::vector<StressResult> stress_results;
    for (const auto& cfg : buildStressConfigs()) {
        try {
            StressResult r = runStress(cfg);
            stress_results.push_back(r);
            std::cout << "  " << std::left << std::setw(28) << r.name
                << "  ct=" << std::setw(5) << r.crossings_after
                << "  " << std::fixed << std::setprecision(2) << r.time_ms << " ms\n";
        }
        catch (const std::exception& ex) {
            std::cerr << "  [SKIP] n=" << cfg.n << " e=" << cfg.e
                << ": " << ex.what() << "\n";
        }
    }
    printStressSummary(stress_results);
    writeStressCsv(stress_results);

    // ── Part 2: MIP vs brute force ────────────────────────────────────────────
    std::cout << "\n----- Part 2: MIP vs brute force (edges 2..7) ---------------------\n";
    std::vector<CompResult> comp_results;
    for (const auto& cfg : buildCompConfigs()) {
        try {
            CompResult r = runComparison(cfg);
            comp_results.push_back(r);
            std::cout << "  " << std::left << std::setw(24) << r.name
                << "  MIP=" << std::setw(4) << r.mip_crossings
                << " (" << std::fixed << std::setprecision(1) << r.mip_time_ms << "ms)"
                << "  BF=" << std::setw(4) << r.bf_crossings
                << " (" << std::fixed << std::setprecision(1) << r.bf_time_ms << "ms)"
                << (r.optimal ? "  OK" : "  SUBOPTIMAL") << "\n";
        }
        catch (const std::exception& ex) {
            std::cerr << "  [SKIP] n=" << cfg.n << " e=" << cfg.e
                << ": " << ex.what() << "\n";
        }
    }
    printComparisonSummary(comp_results);
    writeComparisonCsv(comp_results);

    return 0;
}