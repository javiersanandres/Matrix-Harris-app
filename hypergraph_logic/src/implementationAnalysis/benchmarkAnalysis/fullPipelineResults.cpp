#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using namespace hypergraph_logic;

// SOURCE_DIR resolves to the directory of this .cpp at compile time.
// The CSV is written there so it always lands next to the source file,
// regardless of where the binary is run from.
static const fs::path SOURCE_DIR = fs::path(__FILE__).parent_path();

// ============================================================================
// CSV parser
// ============================================================================

struct HyperedgeRecord {
    std::vector<std::string> sources;
    std::vector<std::string> targets;
};

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::vector<std::string> splitSemi(const std::string& line) {
    std::vector<std::string> parts;
    std::istringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, ';'))
        parts.push_back(trim(tok));
    return parts;
}

static std::vector<std::pair<int, HyperedgeRecord>>
parseBenchmarkCsv(const fs::path& path)
{
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open: " + path.string());

    std::map<int, HyperedgeRecord> edges;
    std::string line;
    bool header = true;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (header) { header = false; continue; }

        auto cols = splitSemi(line);
        if (cols.size() < 3) continue;

        int  id = std::stoi(cols[0]);
        char type = cols[1].empty() ? '?' : cols[1][0];
        const std::string& node = cols[2];

        if (type == 's') edges[id].sources.push_back(node);
        else if (type == 't') edges[id].targets.push_back(node);
    }

    return { edges.begin(), edges.end() };
}

// ============================================================================
// Hypergraph builder
//
// Only the following three calls are used:
//   createNode(label, pos, nullptr)
//   addConnection(source, target)
//   addSourceToEdge(edge, source)
//   addTargetToEdge(edge, target)
//
// WHY we do NOT use addSourceToEdge / addTargetToEdge for multi-endpoint edges
// ---------------------------------------------------------------------------
// addSourceToEdge and addTargetToEdge call removeTransitiveConnections
// internally. If the new source (or target) being added is an ancestor of an
// existing source (or all sources are ancestors of the new target), that call
// strips existing sources/targets off the edge to avoid redundancy. The edge
// then enters splitLongEdge with an empty sources_by_layer map, which tries
// to dereference begin() on an empty std::map → assertion crash.
//
// The safe construction is: for each (source, target) pair inside a logical
// hyperedge, issue one addConnection call. The Hypergraph groups edges that
// share endpoints naturally via its Hasse invariant, so the resulting graph
// correctly represents the benchmark structure without violating any internal
// invariants.
// ============================================================================

static GraphicalHypergraph buildHypergraph(
    const std::string& name,
    const std::vector<std::pair<int, HyperedgeRecord>>& records)
{
    GraphicalHypergraph g(name);
    std::unordered_map<std::string, NodePtr> node_map;

    // Ensure a node exists, creating it on first encounter (all at layer 0).
    auto ensure_node = [&](const std::string& id) -> const NodePtr& {
        auto [it, inserted] = node_map.emplace(id, NodePtr{});
        if (inserted)
            it->second = g.createNode(id, static_cast<int>(node_map.size() - 1), nullptr);
        return it->second;
        };

    // Pre-create all nodes: sources first, then any target-only nodes.
    for (const auto& [id, rec] : records)
        for (const auto& s : rec.sources) ensure_node(s);
    for (const auto& [id, rec] : records)
        for (const auto& t : rec.targets) ensure_node(t);


    for (const auto& [he_id, rec] : records) {
        if (rec.sources.empty() || rec.targets.empty()) continue;

        HyperedgePtr edge = nullptr;
        size_t anchor_i = 0, anchor_j = 0;

        for (size_t i = 0; i < rec.sources.size() && edge == nullptr; ++i) {
            for (size_t j = 0; j < rec.targets.size() && edge == nullptr; ++j) {
                try {
                    edge = g.addConnection(
                        node_map.at(rec.sources[i]),
                        node_map.at(rec.targets[j]));
                    anchor_i = i;
                    anchor_j = j;
                }
                catch (const std::exception& ex) {
                    std::cout << "    [edge " << he_id << " skip "
                        << rec.sources[i] << "->" << rec.targets[j]
                        << "]: " << ex.what() << "\n";
                }
            }
        }

        if (edge != nullptr) {
            for (size_t i = anchor_i + 1; i < rec.sources.size(); ++i) {
                try {
                    g.addSourceToEdge(edge, node_map.at(rec.sources[i]));
                }
                catch (const std::exception& ex) {
                    std::cout << "    [edge " << he_id << " skip src "
                        << rec.sources[i] << "]: " << ex.what() << "\n";
                }
            }

            for (size_t j = 0; j < rec.targets.size(); ++j) {
                if (j == anchor_j) continue;
                try {
                    g.addTargetToEdge(edge, node_map.at(rec.targets[j]));
                }
                catch (const std::exception& ex) {
                    std::cout << "    [edge " << he_id << " skip tgt "
                        << rec.targets[j] << "]: " << ex.what() << "\n";
                }
            }
        }
        else {
            std::cout << "    [edge " << he_id << ": Skipped ]" << "\n";
        }
    }

    return g;
}

// ============================================================================
// Post-layout crossing counter
//
// Mirrors coreSweep from HypergraphRenderer without drawing anything.
// A crossing is any point where a horizontal bar would need a hop arc over
// a vertical wire — i.e. where bar_y falls inside the booked y-interval of
// a wire at some x strictly between the bar's x_min and x_max.
//
// Sweep per layer gap (layer_idx-1 → layer_idx):
//
//   Step 1 — Book vertical occupancy using port x-coordinates.
//     trivial (1-src/1-tgt, zero-width) edges: one full-gap wire.
//     non-trivial edges: source wires [layer_y_prev→bar_y], target wires [bar_y→layer_y].
//
//   Step 2 — For each non-trivial bar, count how many booked verticals
//     it crosses (x strictly inside [x_min,x_max] and bar_y inside the interval).
// ============================================================================

static double findPortX(const std::vector<Port>& ports, const Hyperedge* edge_ptr)
{
    for (const auto& p : ports)
        if (p.edge == edge_ptr) return p.x;
    return std::numeric_limits<double>::quiet_NaN();
}

struct VerticalOccupancy {
    double y_lo = 0.0;  // more-negative (bottom) end
    double y_hi = 0.0;  // less-negative (top) end
};

static int countLayoutCrossings(const GraphicalHypergraph& g)
{
    if (g.getLayers().empty()) return 0;

    const auto& node_layout = g.getNodeLayout();
    const auto& edge_layout = g.getEdgeLayout();
    const auto& layer_layout = g.getLayerLayout();

    int total = 0;

    std::vector<HyperedgePtr> incoming_edges;
    std::vector<NodePtr>      nodes_in_prev_layer;

    for (const auto& [layer_idx, layer_data] : g.getLayers()) {

        // No gap above the first layer.
        if (incoming_edges.empty() || nodes_in_prev_layer.empty()) {
            incoming_edges = layer_data.outgoing_edges;
            nodes_in_prev_layer = layer_data.nodes;
            continue;
        }

        double layer_y = layer_layout.at(layer_idx);
        double layer_y_prev = layer_layout.at(layer_idx - 1);
        // trivial_y: bar y for an edge whose span collapses to a single vertical
        const double trivial_y = layer_y_prev - NODE_HEIGHT / 2.0;

        // Mirror the renderer's stable_partition (trivials to the front).
        std::stable_partition(incoming_edges.begin(), incoming_edges.end(),
            [&](const HyperedgePtr& e) {
                auto it = edge_layout.find(e.get());
                return it != edge_layout.end() &&
                    std::abs(it->second - trivial_y) < 1e-9;
            });

        // ── Step 1: book vertical occupancy ───────────────────────────────────
        std::map<double, std::vector<VerticalOccupancy>> vertical_occupancy;

        for (const auto& edge : incoming_edges) {
            auto it_bar = edge_layout.find(edge.get());
            double bar_y = (it_bar != edge_layout.end()) ? it_bar->second : trivial_y;
            bool is_trivial = std::abs(bar_y - trivial_y) < 1e-9;

            // Gather port x-values for sources and targets of this segment.
            std::vector<double> src_xs, tgt_xs;
            for (const auto& src_node : edge->getSources()) {
                auto nl_it = node_layout.find(src_node.get());
                if (nl_it == node_layout.end()) continue;
                double px = findPortX(nl_it->second.source_ports, edge.get());
                if (!std::isnan(px)) src_xs.push_back(px);
            }
            for (const auto& tgt_node : edge->getTargets()) {
                auto nl_it = node_layout.find(tgt_node.get());
                if (nl_it == node_layout.end()) continue;
                double px = findPortX(nl_it->second.target_ports, edge.get());
                if (!std::isnan(px)) tgt_xs.push_back(px);
            }

            if (is_trivial) {
                // Single straight wire occupying the whole gap at one x.
                if (!src_xs.empty()) {
                    double x = src_xs.front();
                    double y_top = edge->getSources().front()->isDummy()
                        ? layer_y_prev
                        : layer_y_prev - NODE_HEIGHT / 2.0;
                    double y_bot = !tgt_xs.empty() && edge->getTargets().front()->isDummy()
                        ? layer_y
                        : layer_y + NODE_HEIGHT / 2.0;
                    vertical_occupancy[x].push_back({ y_bot, y_top });
                }
            }
            else {
                // Source wires: upper-node-bottom → bar_y.
                for (double x : src_xs) {
                    bool dummy_src = edge->getSources().size() == 1 &&
                        edge->getSources().front()->isDummy();
                    double y_top = dummy_src ? layer_y_prev
                        : layer_y_prev - NODE_HEIGHT / 2.0;
                    vertical_occupancy[x].push_back({ bar_y, y_top });
                }
                // Target wires: bar_y → lower-node-top.
                for (double x : tgt_xs) {
                    bool dummy_tgt = edge->getTargets().size() == 1 &&
                        edge->getTargets().front()->isDummy();
                    double y_bot = dummy_tgt ? layer_y
                        : layer_y + NODE_HEIGHT / 2.0;
                    vertical_occupancy[x].push_back({ bar_y, y_bot });
                }
            }
        }

        // ── Step 2: count hops on non-trivial horizontal bars ─────────────────
        for (const auto& edge : incoming_edges) {
            auto it_bar = edge_layout.find(edge.get());
            if (it_bar == edge_layout.end()) continue;
            double bar_y = it_bar->second;
            if (std::abs(bar_y - trivial_y) < 1e-9) continue;

            // Bar x-extent from port positions.
            double x_min = std::numeric_limits<double>::max();
            double x_max = -std::numeric_limits<double>::max();
            for (const auto& src_node : edge->getSources()) {
                auto nl_it = node_layout.find(src_node.get());
                if (nl_it == node_layout.end()) continue;
                double px = findPortX(nl_it->second.source_ports, edge.get());
                if (!std::isnan(px)) { x_min = std::min(x_min, px); x_max = std::max(x_max, px); }
            }
            for (const auto& tgt_node : edge->getTargets()) {
                auto nl_it = node_layout.find(tgt_node.get());
                if (nl_it == node_layout.end()) continue;
                double px = findPortX(nl_it->second.target_ports, edge.get());
                if (!std::isnan(px)) { x_min = std::min(x_min, px); x_max = std::max(x_max, px); }
            }
            if (x_min >= x_max) continue;

            // Each booked vertical at x ∈ (x_min, x_max) whose y-interval
            // straddles bar_y is a hop (= a crossing).
            for (const auto& [x, ranges] : vertical_occupancy) {
                if (x <= x_min || x >= x_max) continue;
                for (const auto& r : ranges) {
                    if (bar_y > r.y_lo && bar_y < r.y_hi) {
                        ++total;
                        break; // at most one crossing per x-column per bar
                    }
                }
            }
        }

        incoming_edges = layer_data.outgoing_edges;
        nodes_in_prev_layer = layer_data.nodes;
    }

    return total;
}

// ============================================================================
// Result structs
// ============================================================================

struct BenchmarkResult {
    std::string instance;
    double      mh_time_sec;
    int         mh_crossings;
    double      mh_mip_time_sec;
    int         mh_mip_crossings;
};

// ============================================================================
// Run one benchmark instance
// ============================================================================

static BenchmarkResult runBenchmark(const fs::path& csv_path)
{
    std::string stem = csv_path.stem().string();
    auto        records = parseBenchmarkCsv(csv_path);

    // ── MH: global sifting only ───────────────────────────────────────────────
    GraphicalHypergraph g_mh = buildHypergraph(stem, records);
    auto t0 = std::chrono::high_resolution_clock::now();
    int  mh_cross = g_mh.minimizeCrossings();
    auto t1 = std::chrono::high_resolution_clock::now();
    double mh_sec = std::chrono::duration<double>(t1 - t0).count();

    // ── MH + MIP: sifting + full layout pipeline ──────────────────────────────
    GraphicalHypergraph g_full = buildHypergraph(stem, records);
    auto t2 = std::chrono::high_resolution_clock::now();
    g_full.minimizeCrossings();
    g_full.computeLayout();
    auto t3 = std::chrono::high_resolution_clock::now();
    double mh_mip_sec = std::chrono::duration<double>(t3 - t2).count();
    int    mh_mip_cross = countLayoutCrossings(g_full);

    return { stem, mh_sec, mh_cross, mh_mip_sec, mh_mip_cross };
}

// ============================================================================
// Output helpers
// ============================================================================

static void printTableHeader() {
    std::cout << "\n" << std::string(82, '=') << "\n"
        << std::left << std::setw(20) << "Instance"
        << std::right << std::setw(12) << "MH (s)"
        << std::right << std::setw(12) << "MH+MIP (s)"
        << std::right << std::setw(14) << "Cross (MH)"
        << std::right << std::setw(18) << "Cross (MH+MIP)"
        << "\n" << std::string(82, '-') << "\n";
}

static void printRow(const BenchmarkResult& r) {
    std::cout
        << std::left << std::setw(20) << r.instance
        << std::right << std::setw(12) << std::fixed << std::setprecision(3) << r.mh_time_sec
        << std::right << std::setw(12) << std::fixed << std::setprecision(3) << r.mh_mip_time_sec
        << std::right << std::setw(14) << r.mh_crossings
        << std::right << std::setw(18) << r.mh_mip_crossings
        << "\n";
}

static void printSummary(const std::vector<BenchmarkResult>& results) {
    std::cout << std::string(82, '=') << "\n";
    double total_mh = 0, total_mhm = 0;
    int    max_mh = 0, max_mhm = 0;
    for (const auto& r : results) {
        total_mh += r.mh_time_sec;
        total_mhm += r.mh_mip_time_sec;
        max_mh = std::max(max_mh, r.mh_crossings);
        max_mhm = std::max(max_mhm, r.mh_mip_crossings);
    }
    int n = static_cast<int>(results.size());
    std::cout << "SUMMARY\n" << std::string(82, '-') << "\n"
        << std::left << std::setw(36) << "Instances run"
        << std::right << std::setw(6) << n << "\n"
        << std::left << std::setw(36) << "Total MH time (s)"
        << std::right << std::setw(6) << std::fixed << std::setprecision(3) << total_mh << "\n"
        << std::left << std::setw(36) << "Total MH+MIP time (s)"
        << std::right << std::setw(6) << std::fixed << std::setprecision(3) << total_mhm << "\n"
        << std::left << std::setw(36) << "Max crossings (MH)"
        << std::right << std::setw(6) << max_mh << "\n"
        << std::left << std::setw(36) << "Max crossings (MH+MIP)"
        << std::right << std::setw(6) << max_mhm << "\n"
        << std::string(82, '=') << "\n";
}

static void writeCsv(const std::vector<BenchmarkResult>& results) {
    // SOURCE_DIR resolves to the directory of this .cpp at compile time,
    // so the CSV always lands next to the source file.
    fs::path out = SOURCE_DIR / "results_benchmark.csv";
    std::ofstream f(out);
    if (!f) {
        std::cerr << "WARNING: could not write CSV to " << out << "\n";
        return;
    }
    f << "instance;mh_time_sec;mh_crossings;mh_mip_time_sec;mh_mip_crossings\n";
    for (const auto& r : results)
        f << r.instance << ";"
        << std::fixed << std::setprecision(6) << r.mh_time_sec << ";"
        << r.mh_crossings << ";"
        << std::fixed << std::setprecision(6) << r.mh_mip_time_sec << ";"
        << r.mh_mip_crossings << "\n";
    std::cout << "\nCSV written to " << fs::absolute(out) << "\n";
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[])
{
    fs::path dir = (argc > 1) ? argv[1] : BENCHMARK_DIR;

    std::cout << "Benchmark Analysis — Crossing Minimisation & Layout Pipeline\n";
    std::cout << "Benchmark directory: " << fs::absolute(dir) << "\n";
    std::cout << "CSV output:          " << fs::absolute(SOURCE_DIR / "results_benchmark.csv") << "\n";

    if (!fs::exists(dir)) {
        std::cerr << "ERROR: benchmark directory not found: " << fs::absolute(dir) << "\n"
            << "Pass the path as argv[1] or set BENCHMARK_DIR at compile time.\n";
        return 1;
    }

    std::vector<fs::path> csv_files;
    for (const auto& entry : fs::directory_iterator(dir))
        if (entry.path().extension() == ".csv")
            csv_files.push_back(entry.path());
    std::sort(csv_files.begin(), csv_files.end());

    if (csv_files.empty()) {
        std::cerr << "No CSV files found in " << fs::absolute(dir) << "\n";
        return 1;
    }

    printTableHeader();
    std::vector<BenchmarkResult> results;

    for (const auto& path : csv_files) {
        try {
            BenchmarkResult r = runBenchmark(path);
            results.push_back(r);
            printRow(r);
        }
        catch (const std::exception& ex) {
            std::cerr << "  [SKIP] " << path.stem().string() << ": " << ex.what() << "\n";
        }
    }

    printSummary(results);
    writeCsv(results);
    return 0;
}