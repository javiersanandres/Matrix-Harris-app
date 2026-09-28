// ============================================================================
// northDagsPipelineResults.cpp
//
// Full-pipeline benchmark (MH vs MH+GS) on the public North DAGs test suite.
//
// The North DAGs are 1277 connected, acyclic directed graphs with 10-100
// vertices, collected by Stephen North at AT&T Bell Labs from a graph drawing
// e-mail service. They are the standard benchmark for hierarchical (layered /
// Sugiyama-style) drawing and are distributed in GraphML format at
//
//     http://www.graphdrawing.org/data.html   ("North DAGs", GraphML)
//
// Extract the archive and pass the extracted folder as argv[1], or define
// NORTH_BENCHMARK_DIR at compile time. The folder is scanned recursively for
// *.graphml / *.xml files.
//
// Converting a DAG into a directed hierarchical hypergraph
// -------------------------------------------------------------------------
// 1. Duplicate edges and self-loops are dropped; a graph with a cycle is
//    rejected (the North DAGs are acyclic, so this is only a safety check).
// 2. Transitive reduction is applied up front. Hypergraph::addConnection
//    enforces the Hasse-diagram property anyway, so doing it here makes the
//    input explicit, keeps the builder from rejecting edges, and lets the
//    number of removed edges be reported.
// 3. Every node u with children becomes the source of one hyperedge
//    {u} -> children(u). Sources with IDENTICAL child sets are merged into a
//    single multi-source hyperedge {u1, u2, ...} -> children.
//
// After step 2 the sources of a merged hyperedge are pairwise incomparable,
// and so are the targets (if one were an ancestor of another, one of the
// edges would be transitive and would have been removed). That means the
// addSourceToEdge / addTargetToEdge calls below can never trigger the
// transitive-stripping path described in fullPipelineResults.cpp.
//
// Command line
// -------------------------------------------------------------------------
//   northDagsPipelineResults [dir] [--min-nodes=N] [--max-nodes=N]
//                            [--stride=K] [--limit=N] [--no-gui] [--verbose]
//
//   --min-nodes / --max-nodes  only run graphs whose vertex count is in range
//   --stride=K                 run every K-th eligible graph (quick sampling)
//   --limit=N                  stop after N instances
//   --no-gui                   skip the viewer (usable on headless machines)
//   --verbose                  print every rejected builder call
// ============================================================================

#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"
#include "HypergraphRenderer.h"

#include <QApplication>
#include <QComboBox>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QShowEvent>
#include <QSplitter>
#include <QTransform>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef NORTH_BENCHMARK_DIR
#define NORTH_BENCHMARK_DIR "north"
#endif

namespace fs = std::filesystem;
using namespace hypergraph_logic;
using namespace ui;   // HypergraphRenderer lives in namespace ui

// The CSV is written next to this source file, regardless of where the
// binary is run from (same convention as fullPipelineResults.cpp).
static const fs::path SOURCE_DIR = fs::path(__FILE__).parent_path();
static const char* CSV_NAME = "results_north_benchmark.csv";

// ============================================================================
// Command-line options
// ============================================================================

struct Options {
    fs::path dir = NORTH_BENCHMARK_DIR;
    int      min_nodes = 0;   // 0 = no lower bound
    int      max_nodes = 0;   // 0 = no upper bound
    int      stride = 1;
    int      limit = 0;   // 0 = run everything
    bool     gui = true;
    bool     verbose = false;
};

static Options parseOptions(int argc, char* argv[])
{
    Options o;
    auto intValue = [](const std::string& arg, const std::string& key) {
        return std::stoi(arg.substr(key.size()));
        };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--min-nodes=", 0) == 0) o.min_nodes = intValue(a, "--min-nodes=");
        else if (a.rfind("--max-nodes=", 0) == 0) o.max_nodes = intValue(a, "--max-nodes=");
        else if (a.rfind("--stride=", 0) == 0)    o.stride = std::max(1, intValue(a, "--stride="));
        else if (a.rfind("--limit=", 0) == 0)     o.limit = intValue(a, "--limit=");
        else if (a == "--no-gui")                 o.gui = false;
        else if (a == "--verbose")                o.verbose = true;
        else if (a.rfind("--", 0) == 0)
            throw std::runtime_error("Unknown option: " + a);
        else
            o.dir = a;
    }
    return o;
}

// ============================================================================
// Minimal GraphML reader
//
// Reads the <node id> and <edge source target> elements of the first
// top-level <graph>. Attributes, <data> payloads, ports and nested graphs are
// ignored; that is all the North DAGs contain. Edges are always read as
// source -> target, whatever edgedefault says.
// ============================================================================

struct DirectedGraph {
    std::vector<std::string>                         node_ids;  // file order
    std::vector<std::pair<std::string, std::string>> edges;     // file order
};

static std::string decodeEntities(const std::string& s)
{
    if (s.find('&') == std::string::npos) return s;
    static const std::pair<const char*, char> table[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''} };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        bool replaced = false;
        if (s[i] == '&') {
            for (const auto& [ent, ch] : table) {
                size_t len = std::char_traits<char>::length(ent);
                if (s.compare(i, len, ent) == 0) { out += ch; i += len; replaced = true; break; }
            }
        }
        if (!replaced) out += s[i++];
    }
    return out;
}

// Parses key="value" / key='value' pairs from the inside of a tag
// (everything between '<' and '>', tag name included).
static std::unordered_map<std::string, std::string> parseAttributes(const std::string& tag)
{
    std::unordered_map<std::string, std::string> attrs;
    const size_t n = tag.size();
    size_t i = 0;
    auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };

    while (i < n && !isSpace(tag[i])) ++i;              // skip the tag name
    while (i < n) {
        while (i < n && isSpace(tag[i])) ++i;
        size_t key_start = i;
        while (i < n && tag[i] != '=' && tag[i] != '/' && !isSpace(tag[i])) ++i;
        std::string key = tag.substr(key_start, i - key_start);
        while (i < n && isSpace(tag[i])) ++i;
        if (i >= n || tag[i] != '=') { if (key.empty()) ++i; continue; }
        ++i;                                            // skip '='
        while (i < n && isSpace(tag[i])) ++i;
        if (i >= n) break;
        char quote = tag[i];
        if (quote != '"' && quote != '\'') { ++i; continue; }
        size_t value_start = ++i;
        size_t value_end = tag.find(quote, value_start);
        if (value_end == std::string::npos) break;
        attrs[key] = decodeEntities(tag.substr(value_start, value_end - value_start));
        i = value_end + 1;
    }
    return attrs;
}

// "y:ShapeNode" -> "ShapeNode", "node/" -> "node"
static std::string localTagName(const std::string& tag, bool closing)
{
    size_t start = closing ? 1 : 0;
    size_t end = start;
    while (end < tag.size() && tag[end] != '/' &&
        !std::isspace(static_cast<unsigned char>(tag[end]))) ++end;
    std::string name = tag.substr(start, end - start);
    size_t colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

static DirectedGraph parseGraphML(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open: " + path.string());
    const std::string xml((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    DirectedGraph g;
    std::unordered_set<std::string> known_nodes;
    auto addNode = [&](const std::string& id) {
        if (known_nodes.insert(id).second) g.node_ids.push_back(id);
        };

    int  graph_depth = 0;
    bool first_graph_done = false;
    size_t pos = 0;

    while ((pos = xml.find('<', pos)) != std::string::npos) {
        if (xml.compare(pos, 4, "<!--") == 0) {
            size_t e = xml.find("-->", pos + 4);
            if (e == std::string::npos) break;
            pos = e + 3;
            continue;
        }
        if (xml.compare(pos, 9, "<![CDATA[") == 0) {
            size_t e = xml.find("]]>", pos + 9);
            if (e == std::string::npos) break;
            pos = e + 3;
            continue;
        }
        size_t end = xml.find('>', pos);
        if (end == std::string::npos) break;
        std::string tag = xml.substr(pos + 1, end - pos - 1);
        pos = end + 1;
        if (tag.empty() || tag[0] == '?' || tag[0] == '!') continue;

        const bool closing = tag[0] == '/';
        const bool self_closing = !closing && tag.back() == '/';
        const std::string name = localTagName(tag, closing);

        if (name == "graph") {
            if (closing) {
                if (--graph_depth == 0) first_graph_done = true;
            }
            else if (!self_closing) {
                ++graph_depth;
            }
            continue;
        }
        if (first_graph_done) break;                 // only the first top-level graph
        if (closing || graph_depth != 1) continue;   // skip nested graphs

        if (name == "node") {
            auto attrs = parseAttributes(tag);
            auto it = attrs.find("id");
            if (it == attrs.end())
                throw std::runtime_error("<node> without id in " + path.string());
            addNode(it->second);
        }
        else if (name == "edge") {
            auto attrs = parseAttributes(tag);
            auto s = attrs.find("source");
            auto t = attrs.find("target");
            if (s == attrs.end() || t == attrs.end())
                throw std::runtime_error("<edge> without source/target in " + path.string());
            addNode(s->second);   // lenient: tolerate undeclared endpoints
            addNode(t->second);
            g.edges.emplace_back(s->second, t->second);
        }
    }

    if (g.node_ids.empty())
        throw std::runtime_error("No nodes found in " + path.string());
    return g;
}

// ============================================================================
// DAG -> hyperedge records
// ============================================================================

struct HyperedgeRecord {
    std::vector<std::string> sources;
    std::vector<std::string> targets;
};

struct PreparedInstance {
    std::string                                  name;
    std::vector<std::string>                     nodes_topo;   // labels in topological order
    std::vector<std::pair<int, HyperedgeRecord>> records;

    int nodes = 0;
    int input_edges = 0;   // distinct, non-loop edges in the file
    int dropped_duplicates = 0;
    int dropped_self_loops = 0;
    int reduced_edges = 0;   // edges left after transitive reduction
    int hyperedges = 0;
    int multi_source = 0;   // hyperedges with more than one source
};

static PreparedInstance prepareInstance(const std::string& name, const DirectedGraph& dg)
{
    PreparedInstance inst;
    inst.name = name;

    const int n = static_cast<int>(dg.node_ids.size());
    inst.nodes = n;

    std::unordered_map<std::string, int> index;
    for (int i = 0; i < n; ++i) index.emplace(dg.node_ids[i], i);

    // ── adjacency without duplicates / self-loops ────────────────────────────
    std::vector<std::vector<int>> children(n);
    std::vector<int> indegree(n, 0);
    {
        std::vector<std::unordered_set<int>> seen(n);
        for (const auto& [s, t] : dg.edges) {
            int u = index.at(s), v = index.at(t);
            if (u == v) { ++inst.dropped_self_loops; continue; }
            if (!seen[u].insert(v).second) { ++inst.dropped_duplicates; continue; }
            children[u].push_back(v);
            ++indegree[v];
            ++inst.input_edges;
        }
    }

    // ── topological order (Kahn, ties broken by file order) ─────────────────
    std::vector<int> topo;
    topo.reserve(n);
    {
        std::priority_queue<int, std::vector<int>, std::greater<int>> ready;
        std::vector<int> deg = indegree;
        for (int i = 0; i < n; ++i) if (deg[i] == 0) ready.push(i);
        while (!ready.empty()) {
            int u = ready.top(); ready.pop();
            topo.push_back(u);
            for (int v : children[u]) if (--deg[v] == 0) ready.push(v);
        }
        if (static_cast<int>(topo.size()) != n)
            throw std::runtime_error("graph contains a cycle");
    }
    std::vector<int> topo_pos(n);
    for (int i = 0; i < n; ++i) topo_pos[topo[i]] = i;

    for (int u : topo) inst.nodes_topo.push_back(dg.node_ids[u]);

    // ── descendant sets, computed in reverse topological order ──────────────
    std::vector<std::vector<char>> desc(n, std::vector<char>(n, 0));
    for (auto it = topo.rbegin(); it != topo.rend(); ++it) {
        int u = *it;
        for (int c : children[u]) {
            desc[u][c] = 1;
            for (int k = 0; k < n; ++k) if (desc[c][k]) desc[u][k] = 1;
        }
    }

    // ── transitive reduction: u->v is redundant if another child reaches v ──
    std::vector<std::vector<int>> reduced(n);
    for (int u = 0; u < n; ++u) {
        for (int v : children[u]) {
            bool redundant = false;
            for (int w : children[u])
                if (w != v && desc[w][v]) { redundant = true; break; }
            if (!redundant) reduced[u].push_back(v);
        }
        std::sort(reduced[u].begin(), reduced[u].end(),
            [&](int a, int b) { return topo_pos[a] < topo_pos[b]; });
        inst.reduced_edges += static_cast<int>(reduced[u].size());
    }

    // ── group sources by identical child set ─────────────────────────────────
    std::map<std::vector<int>, size_t> group_of;   // child set -> group index
    std::vector<std::pair<std::vector<int>, std::vector<int>>> groups;  // (sources, targets)
    for (int u : topo) {
        if (reduced[u].empty()) continue;
        auto [it, inserted] = group_of.emplace(reduced[u], groups.size());
        if (inserted) groups.push_back({ {u}, reduced[u] });
        else          groups[it->second].first.push_back(u);
    }

    int id = 0;
    for (const auto& [srcs, tgts] : groups) {
        HyperedgeRecord rec;
        for (int s : srcs) rec.sources.push_back(dg.node_ids[s]);
        for (int t : tgts) rec.targets.push_back(dg.node_ids[t]);
        if (rec.sources.size() > 1) ++inst.multi_source;
        inst.records.emplace_back(id++, std::move(rec));
    }
    inst.hyperedges = static_cast<int>(inst.records.size());
    return inst;
}

// ============================================================================
// Hypergraph builder
//
// Same construction strategy as fullPipelineResults.cpp:
//   createNode(label, pos, nullptr)   — every node, at layer 0
//   addConnection(source, target)     — anchors each hyperedge
//   addSourceToEdge / addTargetToEdge — remaining endpoints
// Nodes are created in topological order and hyperedges are added in the
// topological order of their first source. Rejected calls are counted rather
// than printed (1277 instances would flood the console); --verbose prints them.
// ============================================================================

struct BuildStats {
    int failed_connections = 0;   // addConnection attempts that threw
    int failed_sources = 0;
    int failed_targets = 0;
    int dropped_hyperedges = 0;   // no anchor connection could be created

    int total() const { return failed_sources + failed_targets + dropped_hyperedges; }
};

static GraphicalHypergraph buildHypergraph(const PreparedInstance& inst,
    BuildStats& stats, bool verbose)
{
    GraphicalHypergraph g(inst.name);
    std::unordered_map<std::string, NodePtr> node_map;

    int position = 0;
    for (const auto& label : inst.nodes_topo)
        node_map.emplace(label, g.createNode(label, position++, nullptr));

    auto report = [&](int he_id, const std::string& what, const std::exception& ex) {
        if (verbose)
            std::cout << "    [" << inst.name << " edge " << he_id << " " << what
            << "]: " << ex.what() << "\n";
        };

    for (const auto& [he_id, rec] : inst.records) {
        HyperedgePtr edge = nullptr;
        size_t anchor_i = 0, anchor_j = 0;

        for (size_t i = 0; i < rec.sources.size() && edge == nullptr; ++i) {
            for (size_t j = 0; j < rec.targets.size() && edge == nullptr; ++j) {
                try {
                    edge = g.addConnection(node_map.at(rec.sources[i]),
                        node_map.at(rec.targets[j]));
                    anchor_i = i;
                    anchor_j = j;
                }
                catch (const std::exception& ex) {
                    ++stats.failed_connections;
                    report(he_id, "skip " + rec.sources[i] + "->" + rec.targets[j], ex);
                }
            }
        }

        if (edge == nullptr) {
            ++stats.dropped_hyperedges;
            if (verbose) std::cout << "    [" << inst.name << " edge " << he_id << ": skipped]\n";
            continue;
        }

        for (size_t i = anchor_i + 1; i < rec.sources.size(); ++i) {
            try { g.addSourceToEdge(edge, node_map.at(rec.sources[i])); }
            catch (const std::exception& ex) {
                ++stats.failed_sources;
                report(he_id, "skip src " + rec.sources[i], ex);
            }
        }
        for (size_t j = 0; j < rec.targets.size(); ++j) {
            if (j == anchor_j) continue;
            try { g.addTargetToEdge(edge, node_map.at(rec.targets[j])); }
            catch (const std::exception& ex) {
                ++stats.failed_targets;
                report(he_id, "skip tgt " + rec.targets[j], ex);
            }
        }
    }
    return g;
}

// ============================================================================
// Post-layout crossing counter
//
// Identical to countLayoutCrossings() in fullPipelineResults.cpp, so the
// numbers from both benchmarks are directly comparable. See that file for
// the full description of the sweep.
// ============================================================================

static double findPortX(const std::vector<Port>& ports, const Hyperedge* edge_ptr)
{
    for (const auto& p : ports)
        if (p.edge == edge_ptr) return p.x;
    return std::numeric_limits<double>::quiet_NaN();
}

struct VerticalOccupancy {
    double y_lo = 0.0;
    double y_hi = 0.0;
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

        if (incoming_edges.empty() || nodes_in_prev_layer.empty()) {
            incoming_edges = layer_data.outgoing_edges;
            nodes_in_prev_layer = layer_data.nodes;
            continue;
        }

        double layer_y = layer_layout.at(layer_idx);
        double layer_y_prev = layer_layout.at(layer_idx - 1);

        std::map<double, std::vector<VerticalOccupancy>> vertical_occupancy;
        auto makeRange = [](double a, double b) -> VerticalOccupancy {
            return { std::min(a, b), std::max(a, b) };
            };

        struct PortXY { double x; bool is_dummy; };
        struct SegmentInfo {
            std::vector<PortXY> src_ports, tgt_ports;
            bool   is_trivial = false;
            double bar_y = 0.0;
        };
        std::vector<SegmentInfo> segments;
        segments.reserve(incoming_edges.size());

        for (const auto& edge : incoming_edges) {
            SegmentInfo info;

            for (const auto& src_node : edge->getSources()) {
                auto nl_it = node_layout.find(src_node.get());
                if (nl_it == node_layout.end()) continue;
                double px = findPortX(nl_it->second.source_ports, edge.get());
                if (!std::isnan(px)) info.src_ports.push_back({ px, src_node->isDummy() });
            }
            for (const auto& tgt_node : edge->getTargets()) {
                auto nl_it = node_layout.find(tgt_node.get());
                if (nl_it == node_layout.end()) continue;
                double px = findPortX(nl_it->second.target_ports, edge.get());
                if (!std::isnan(px)) info.tgt_ports.push_back({ px, tgt_node->isDummy() });
            }

            info.is_trivial = (info.src_ports.size() == 1 && info.tgt_ports.size() == 1 &&
                std::abs(info.src_ports.front().x - info.tgt_ports.front().x) < 1e-9);

            auto it_bar = edge_layout.find(edge.get());
            info.bar_y = (it_bar != edge_layout.end())
                ? it_bar->second
                : (layer_y_prev - NODE_HEIGHT / 2.0);

            if (info.is_trivial) {
                double x = info.src_ports.front().x;
                double y_top = info.src_ports.front().is_dummy
                    ? layer_y_prev : layer_y_prev - NODE_HEIGHT / 2.0;
                double y_bot = info.tgt_ports.front().is_dummy
                    ? layer_y : layer_y + NODE_HEIGHT / 2.0;
                vertical_occupancy[x].push_back(makeRange(y_bot, y_top));
            }
            else {
                for (const auto& pi : info.src_ports) {
                    double y_top = pi.is_dummy ? layer_y_prev : layer_y_prev - NODE_HEIGHT / 2.0;
                    vertical_occupancy[pi.x].push_back(makeRange(info.bar_y, y_top));
                }
                for (const auto& pi : info.tgt_ports) {
                    double y_bot = pi.is_dummy ? layer_y : layer_y + NODE_HEIGHT / 2.0;
                    vertical_occupancy[pi.x].push_back(makeRange(info.bar_y, y_bot));
                }
            }

            segments.push_back(std::move(info));
        }

        for (const auto& seg : segments) {
            if (seg.is_trivial) continue;

            double x_min = std::numeric_limits<double>::max();
            double x_max = -std::numeric_limits<double>::max();
            for (const auto& pi : seg.src_ports) { x_min = std::min(x_min, pi.x); x_max = std::max(x_max, pi.x); }
            for (const auto& pi : seg.tgt_ports) { x_min = std::min(x_min, pi.x); x_max = std::max(x_max, pi.x); }
            if (x_min >= x_max) continue;

            for (const auto& [x, ranges] : vertical_occupancy) {
                if (x <= x_min || x >= x_max) continue;
                for (const auto& r : ranges) {
                    if (seg.bar_y > r.y_lo && seg.bar_y < r.y_hi) {
                        ++total;
                        break;
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
    int    nodes = 0;
    int    input_edges = 0;
    int    reduced_edges = 0;
    int    hyperedges = 0;
    int    multi_source = 0;
    int    build_failures = 0;   // sources/targets/hyperedges the builder rejected
    int    layers = 0;   // after layout (MH+GS graph)
    double mh_time_sec = 0.0;
    int    mh_crossings = 0;
    double mh_gs_time_sec = 0.0;
    int    mh_gs_crossings = 0;
};

struct BenchmarkRun {
    BenchmarkResult     result;
    GraphicalHypergraph graph_mh;
    GraphicalHypergraph graph_mh_gs;
};

// ============================================================================
// Run one benchmark instance
// ============================================================================

static BenchmarkRun runBenchmark(const PreparedInstance& inst, bool verbose)
{
    // ── MH: layout only, no explicit (global) sifting ─────────────────────────
    BuildStats stats_mh;
    GraphicalHypergraph g_mh = buildHypergraph(inst, stats_mh, verbose);
    auto t0 = std::chrono::high_resolution_clock::now();
    g_mh.computeLayout();
    auto t1 = std::chrono::high_resolution_clock::now();
    double mh_sec = std::chrono::duration<double>(t1 - t0).count();
    int    mh_cross = countLayoutCrossings(g_mh);

    // ── MH + GS: Global Sifting, then the same layout pipeline ────────────────
    BuildStats stats_gs;
    GraphicalHypergraph g_mh_gs = buildHypergraph(inst, stats_gs, false);
    auto t2 = std::chrono::high_resolution_clock::now();
    g_mh_gs.minimizeCrossings();
    g_mh_gs.computeLayout();
    auto t3 = std::chrono::high_resolution_clock::now();
    double mh_gs_sec = std::chrono::duration<double>(t3 - t2).count();
    int    mh_gs_cross = countLayoutCrossings(g_mh_gs);

    BenchmarkResult r;
    r.instance = inst.name;
    r.nodes = inst.nodes;
    r.input_edges = inst.input_edges;
    r.reduced_edges = inst.reduced_edges;
    r.hyperedges = inst.hyperedges;
    r.multi_source = inst.multi_source;
    r.build_failures = stats_mh.total();
    r.layers = g_mh_gs.getLayerCount();
    r.mh_time_sec = mh_sec;
    r.mh_crossings = mh_cross;
    r.mh_gs_time_sec = mh_gs_sec;
    r.mh_gs_crossings = mh_gs_cross;

    return BenchmarkRun{ std::move(r), std::move(g_mh), std::move(g_mh_gs) };
}

// ============================================================================
// Output helpers
// ============================================================================

static const int TABLE_WIDTH = 112;

static void printTableHeader() {
    std::cout << "\n" << std::string(TABLE_WIDTH, '=') << "\n"
        << std::left << std::setw(22) << "Instance"
        << std::right << std::setw(6) << "|V|"
        << std::right << std::setw(7) << "|E|"
        << std::right << std::setw(8) << "|E_red|"
        << std::right << std::setw(6) << "|H|"
        << std::right << std::setw(6) << "Lay"
        << std::right << std::setw(11) << "MH (s)"
        << std::right << std::setw(11) << "MH+GS (s)"
        << std::right << std::setw(12) << "Cross (MH)"
        << std::right << std::setw(15) << "Cross (MH+GS)"
        << std::right << std::setw(8) << "Fails"
        << "\n" << std::string(TABLE_WIDTH, '-') << "\n";
}

static void printRow(const BenchmarkResult& r) {
    std::cout
        << std::left << std::setw(22) << r.instance
        << std::right << std::setw(6) << r.nodes
        << std::right << std::setw(7) << r.input_edges
        << std::right << std::setw(8) << r.reduced_edges
        << std::right << std::setw(6) << r.hyperedges
        << std::right << std::setw(6) << r.layers
        << std::right << std::setw(11) << std::fixed << std::setprecision(3) << r.mh_time_sec
        << std::right << std::setw(11) << std::fixed << std::setprecision(3) << r.mh_gs_time_sec
        << std::right << std::setw(12) << r.mh_crossings
        << std::right << std::setw(15) << r.mh_gs_crossings
        << std::right << std::setw(8) << r.build_failures
        << "\n";
}

static void printSummary(const std::vector<BenchmarkResult>& results) {
    std::cout << std::string(TABLE_WIDTH, '=') << "\n";
    if (results.empty()) { std::cout << "No instances were run.\n"; return; }

    double total_mh = 0, total_gs = 0;
    long   sum_mh = 0, sum_gs = 0;
    int    max_mh = 0, max_gs = 0;
    int    gs_better = 0, gs_equal = 0, gs_worse = 0, with_failures = 0;
    int    transitive_removed = 0;

    for (const auto& r : results) {
        total_mh += r.mh_time_sec;
        total_gs += r.mh_gs_time_sec;
        sum_mh += r.mh_crossings;
        sum_gs += r.mh_gs_crossings;
        max_mh = std::max(max_mh, r.mh_crossings);
        max_gs = std::max(max_gs, r.mh_gs_crossings);
        if (r.mh_gs_crossings < r.mh_crossings) ++gs_better;
        else if (r.mh_gs_crossings > r.mh_crossings) ++gs_worse;
        else                                         ++gs_equal;
        if (r.build_failures > 0) ++with_failures;
        transitive_removed += r.input_edges - r.reduced_edges;
    }

    const int n = static_cast<int>(results.size());
    auto line = [](const std::string& label, const std::string& value) {
        std::cout << std::left << std::setw(40) << label
            << std::right << std::setw(12) << value << "\n";
        };
    auto fmt = [](double v, int prec) {
        std::ostringstream ss; ss << std::fixed << std::setprecision(prec) << v; return ss.str();
        };

    std::cout << "SUMMARY\n" << std::string(TABLE_WIDTH, '-') << "\n";
    line("Instances run", std::to_string(n));
    line("Transitive edges removed (total)", std::to_string(transitive_removed));
    line("Instances with builder rejections", std::to_string(with_failures));
    line("Total MH time (s)", fmt(total_mh, 3));
    line("Total MH+GS time (s)", fmt(total_gs, 3));
    line("Mean crossings (MH)", fmt(static_cast<double>(sum_mh) / n, 2));
    line("Mean crossings (MH+GS)", fmt(static_cast<double>(sum_gs) / n, 2));
    line("Max crossings (MH)", std::to_string(max_mh));
    line("Max crossings (MH+GS)", std::to_string(max_gs));
    line("MH+GS better / equal / worse",
        std::to_string(gs_better) + " / " + std::to_string(gs_equal) + " / " + std::to_string(gs_worse));
    if (sum_mh > 0)
        line("Total crossing reduction by GS (%)",
            fmt(100.0 * static_cast<double>(sum_mh - sum_gs) / static_cast<double>(sum_mh), 1));
    std::cout << std::string(TABLE_WIDTH, '=') << "\n";
}

static void writeCsv(const std::vector<BenchmarkResult>& results) {
    fs::path out = SOURCE_DIR / CSV_NAME;
    std::ofstream f(out);
    if (!f) {
        std::cerr << "WARNING: could not write CSV to " << out << "\n";
        return;
    }
    f << "instance;nodes;input_edges;reduced_edges;hyperedges;multi_source_hyperedges;"
        "layers;build_failures;mh_time_sec;mh_crossings;mh_gs_time_sec;mh_gs_crossings\n";
    for (const auto& r : results)
        f << r.instance << ";" << r.nodes << ";" << r.input_edges << ";" << r.reduced_edges << ";"
        << r.hyperedges << ";" << r.multi_source << ";" << r.layers << ";" << r.build_failures << ";"
        << std::fixed << std::setprecision(6) << r.mh_time_sec << ";" << r.mh_crossings << ";"
        << std::fixed << std::setprecision(6) << r.mh_gs_time_sec << ";" << r.mh_gs_crossings << "\n";
    std::cout << "\nCSV written to " << fs::absolute(out) << "\n";
}

// ============================================================================
// Instance discovery
// ============================================================================

// Natural ordering so that g.10.2 < g.10.10 < g.100.0 (North DAG file names
// encode vertex count and index).
static bool naturalLess(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[i])) &&
            std::isdigit(static_cast<unsigned char>(b[j]))) {
            size_t i2 = i, j2 = j;
            while (i2 < a.size() && std::isdigit(static_cast<unsigned char>(a[i2]))) ++i2;
            while (j2 < b.size() && std::isdigit(static_cast<unsigned char>(b[j2]))) ++j2;
            unsigned long long x = std::stoull(a.substr(i, i2 - i));
            unsigned long long y = std::stoull(b.substr(j, j2 - j));
            if (x != y) return x < y;
            i = i2; j = j2;
        }
        else {
            if (a[i] != b[j]) return a[i] < b[j];
            ++i; ++j;
        }
    }
    return a.size() - i < b.size() - j;
}

static std::vector<fs::path> findGraphFiles(const fs::path& dir)
{
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".graphml" || ext == ".xml") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        return naturalLess(a.filename().string(), b.filename().string());
        });
    return files;
}

// Instance name: file name without the .graphml extension ("g.10.0").
static std::string instanceName(const fs::path& p) { return p.stem().string(); }

// ============================================================================
// Post-run viewer (same layout as fullPipelineResults.cpp)
// ============================================================================

class ZoomableGraphicsView : public QGraphicsView {
public:
    explicit ZoomableGraphicsView(QWidget* parent = nullptr) : QGraphicsView(parent) {
        setRenderHint(QPainter::Antialiasing);
        setDragMode(QGraphicsView::ScrollHandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    }

    void zoomBy(double factor) {
        double next = zoom_ * factor;
        if (next < ZOOM_MIN || next > ZOOM_MAX) return;
        zoom_ = next;
        setTransform(QTransform::fromScale(zoom_, zoom_));
    }

    void resetZoom() {
        zoom_ = 1.0;
        resetTransform();
    }

protected:
    void wheelEvent(QWheelEvent* e) override {
        if (e->modifiers() & Qt::ControlModifier) {
            zoomBy(e->angleDelta().y() > 0 ? ZOOM_STEP : 1.0 / ZOOM_STEP);
            e->accept();
        }
        else {
            QGraphicsView::wheelEvent(e);
        }
    }

private:
    static constexpr double ZOOM_STEP = 1.15;
    static constexpr double ZOOM_MIN = 0.1;
    static constexpr double ZOOM_MAX = 8.0;
    double zoom_ = 1.0;
};

class HypergraphViewerWindow : public QMainWindow {
public:
    explicit HypergraphViewerWindow(std::vector<BenchmarkRun> runs, QWidget* parent = nullptr)
        : QMainWindow(parent), runs_(std::move(runs))
    {
        setWindowTitle("North DAGs — MH vs MH+GS Layout");
        resize(1150, 900);
        setStyleSheet("QMainWindow, QWidget { background: #f5f5f5; }");

        auto* central = new QWidget(this);
        setCentralWidget(central);
        auto* vbox = new QVBoxLayout(central);
        vbox->setContentsMargins(10, 10, 10, 10);
        vbox->setSpacing(8);

        auto* topBar = new QHBoxLayout;
        auto* lbl = new QLabel("Instance:", central);
        lbl->setStyleSheet("color:#333333; font-size:12px;");

        combo_ = new QComboBox(central);
        combo_->setStyleSheet(
            "QComboBox {"
            "  background:#ffffff; color:#111111;"
            "  border:1px solid #aaaaaa; border-radius:4px;"
            "  padding:4px 10px; font-size:13px; min-width:280px; }"
            "QComboBox::drop-down { border:none; width:20px; }"
            "QComboBox QAbstractItemView {"
            "  background:#ffffff; color:#111111;"
            "  selection-background-color:#d0e4ff; }");
        for (const auto& run : runs_)
            combo_->addItem(QString::fromStdString(run.result.instance));

        stats_ = new QLabel(central);
        stats_->setStyleSheet("color:#555555; font-size:11px;");

        topBar->addWidget(lbl);
        topBar->addWidget(combo_, 1);
        topBar->addSpacing(12);
        topBar->addWidget(stats_);
        vbox->addLayout(topBar);

        auto* splitter = new QSplitter(Qt::Vertical, central);
        mh_panel_ = makePanel("MH  (layout only, no explicit sifting)", splitter);
        gs_panel_ = makePanel("MH + GS  (Global Sifting, then layout)", splitter);
        splitter->addWidget(mh_panel_.container);
        splitter->addWidget(gs_panel_.container);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);
        vbox->addWidget(splitter, 1);

        QObject::connect(combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int idx) { showRun(idx); });

        if (!runs_.empty()) {
            combo_->setCurrentIndex(0);
            showRun(0);
        }
    }

protected:
    void showEvent(QShowEvent* e) override {
        QMainWindow::showEvent(e);
        fit(mh_panel_);
        fit(gs_panel_);
    }

private:
    struct Panel {
        QWidget* container = nullptr;
        QLabel* info = nullptr;
        ZoomableGraphicsView* view = nullptr;
        QGraphicsScene* scene = nullptr;
        std::unordered_map<Node*, QGraphicsRectItem*>      node_items;
        std::unordered_map<Hyperedge*, QGraphicsPathItem*> edge_items;
    };

    Panel makePanel(const QString& title, QWidget* parent) {
        Panel p;
        p.container = new QWidget(parent);
        auto* v = new QVBoxLayout(p.container);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);

        auto* header = new QHBoxLayout;
        auto* titleLbl = new QLabel(title, p.container);
        titleLbl->setStyleSheet("color:#333333; font-size:12px; font-weight:bold;");

        p.info = new QLabel(p.container);
        p.info->setStyleSheet("color:#555555; font-size:11px;");

        auto makeBtn = [&](const QString& label, const QString& tip) {
            auto* btn = new QPushButton(label, p.container);
            btn->setToolTip(tip);
            btn->setFixedWidth(32);
            btn->setStyleSheet(
                "QPushButton { background:#fff; border:1px solid #aaa;"
                " border-radius:3px; font-size:14px; font-weight:bold; }"
                "QPushButton:hover { background:#e8f0ff; }");
            return btn;
            };
        auto* btnZoomIn = makeBtn("+", "Zoom in  (Ctrl+Scroll)");
        auto* btnZoomOut = makeBtn(QString::fromUtf8("\u2212"), "Zoom out (Ctrl+Scroll)");
        auto* btnReset = makeBtn(QString::fromUtf8("\u2299"), "Reset zoom / fit to view");

        header->addWidget(titleLbl);
        header->addStretch();
        header->addWidget(p.info);
        header->addSpacing(12);
        header->addWidget(btnZoomOut);
        header->addWidget(btnZoomIn);
        header->addWidget(btnReset);
        v->addLayout(header);

        p.scene = new QGraphicsScene(p.container);
        p.view = new ZoomableGraphicsView(p.container);
        p.view->setScene(p.scene);
        p.view->setStyleSheet("QGraphicsView { border:1px solid #cccccc; background:#ffffff; }");
        v->addWidget(p.view, 1);

        ZoomableGraphicsView* view = p.view;
        QGraphicsScene* scene = p.scene;
        QObject::connect(btnZoomIn, &QPushButton::clicked, [view] { view->zoomBy(1.15); });
        QObject::connect(btnZoomOut, &QPushButton::clicked, [view] { view->zoomBy(1.0 / 1.15); });
        QObject::connect(btnReset, &QPushButton::clicked, [view, scene] {
            view->resetZoom();
            if (!scene->itemsBoundingRect().isEmpty())
                view->fitInView(scene->itemsBoundingRect(), Qt::KeepAspectRatio);
            });

        return p;
    }

    void renderInto(Panel& p, const GraphicalHypergraph& g, double time_sec, int crossings) {
        p.node_items.clear();
        p.edge_items.clear();
        HypergraphRenderer::render(g, p.scene, p.node_items, p.edge_items);

        p.info->setText(QString("nodes: %1   edges: %2   crossings: %3   time: %4 s")
            .arg(p.node_items.size())
            .arg(p.edge_items.size())
            .arg(crossings)
            .arg(time_sec, 0, 'f', 3));

        fit(p);
    }

    void fit(Panel& p) {
        if (!p.scene) return;
        p.view->resetZoom();
        if (!p.scene->itemsBoundingRect().isEmpty())
            p.view->fitInView(p.scene->itemsBoundingRect(), Qt::KeepAspectRatio);
    }

    void showRun(int idx) {
        if (idx < 0 || idx >= static_cast<int>(runs_.size())) return;
        const auto& run = runs_[idx];
        const auto& r = run.result;
        stats_->setText(QString("|V| %1   |E| %2   |E_red| %3   |H| %4 (%5 multi-source)   builder rejections %6")
            .arg(r.nodes).arg(r.input_edges).arg(r.reduced_edges)
            .arg(r.hyperedges).arg(r.multi_source).arg(r.build_failures));
        renderInto(mh_panel_, run.graph_mh, r.mh_time_sec, r.mh_crossings);
        renderInto(gs_panel_, run.graph_mh_gs, r.mh_gs_time_sec, r.mh_gs_crossings);
    }

    std::vector<BenchmarkRun> runs_;

    QComboBox* combo_ = nullptr;
    QLabel* stats_ = nullptr;
    Panel mh_panel_;
    Panel gs_panel_;
};

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[])
{
    Options opt;
    try { opt = parseOptions(argc, argv); }
    catch (const std::exception& ex) {
        std::cerr << "ERROR: " << ex.what() << "\n";
        return 1;
    }

    std::cout << "North DAGs Benchmark — Crossing Minimisation & Layout Pipeline\n";
    std::cout << "Benchmark directory: " << fs::absolute(opt.dir) << "\n";
    std::cout << "CSV output:          " << fs::absolute(SOURCE_DIR / CSV_NAME) << "\n";
    if (opt.min_nodes > 0 || opt.max_nodes > 0)
        std::cout << "Vertex range:        [" << opt.min_nodes << ", "
        << (opt.max_nodes > 0 ? std::to_string(opt.max_nodes) : "inf") << "]\n";
    if (opt.stride > 1) std::cout << "Stride:              every " << opt.stride << "th graph\n";
    if (opt.limit > 0) std::cout << "Limit:               " << opt.limit << " instances\n";

    if (!fs::exists(opt.dir)) {
        std::cerr << "ERROR: benchmark directory not found: " << fs::absolute(opt.dir) << "\n"
            << "Download the North DAGs (GraphML) from http://www.graphdrawing.org/data.html,\n"
            << "extract them, and pass the folder as argv[1] or set NORTH_BENCHMARK_DIR.\n";
        return 1;
    }

    std::vector<fs::path> files = findGraphFiles(opt.dir);
    if (files.empty()) {
        std::cerr << "No .graphml files found in " << fs::absolute(opt.dir) << "\n";
        return 1;
    }
    std::cout << "Graph files found:   " << files.size() << "\n";

    printTableHeader();
    std::vector<BenchmarkResult> results;
    std::vector<BenchmarkRun>    runs;
    int eligible = 0;

    for (const auto& path : files) {
        if (opt.limit > 0 && static_cast<int>(results.size()) >= opt.limit) break;
        const std::string name = instanceName(path);
        try {
            PreparedInstance inst = prepareInstance(name, parseGraphML(path));

            if (opt.min_nodes > 0 && inst.nodes < opt.min_nodes) continue;
            if (opt.max_nodes > 0 && inst.nodes > opt.max_nodes) continue;
            if (eligible++ % opt.stride != 0) continue;

            BenchmarkRun run = runBenchmark(inst, opt.verbose);
            results.push_back(run.result);
            printRow(run.result);
            if (opt.gui) runs.push_back(std::move(run));
        }
        catch (const std::exception& ex) {
            std::cerr << "  [SKIP] " << name << ": " << ex.what() << "\n";
        }
    }

    printSummary(results);
    writeCsv(results);

    if (!opt.gui) return 0;
    if (runs.empty()) {
        std::cerr << "No hypergraphs to display.\n";
        return 0;
    }

    // QApplication is created only now, so --no-gui runs work without a display.
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    HypergraphViewerWindow viewer(std::move(runs));
    viewer.show();
    return app.exec();
}