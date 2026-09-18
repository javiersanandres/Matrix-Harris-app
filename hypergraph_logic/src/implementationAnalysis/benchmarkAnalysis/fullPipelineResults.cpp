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
using namespace ui;   // HypergraphRenderer lives in namespace ui

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
// Mirrors coreSweep/drawVerticalSegments from HypergraphRenderer without
// drawing anything. Two distinct things create a visible line crossing:
//
//   (a) A horizontal bar hopping over a vertical wire — bar_y falls inside
//       the booked y-interval of a wire at some x strictly between the
//       bar's x_min and x_max.
//   (b) A dummy relay node's port-alignment "jog": when a dummy node's
//       incoming port (from the gap above) and outgoing port (into the
//       current gap) sit at different x, the renderer bridges them with a
//       short horizontal segment at the dummy's own layer height
//       (layer_y_prev). That segment can itself run straight through a
//       different edge's vertical wire — a real crossing that has no bar
//       and so is easy to miss if you only look for (a).
//
// Sweep per layer gap (layer_idx-1 → layer_idx):
//
//   Step 1 — Book vertical occupancy using port x-coordinates, and record
//     any dummy-source port-alignment jogs seen along the way.
//     trivial (1-src/1-tgt, zero-width) edges: one full-gap wire.
//     non-trivial edges: source wires [layer_y_prev→bar_y], target wires [bar_y→layer_y].
//
//   Step 2 — For each non-trivial bar, count how many booked verticals
//     it crosses (x strictly inside [x_min,x_max] and bar_y inside the interval).
//
//   Step 3 — For each recorded jog, count how many booked verticals it
//     crosses the same way, using layer_y_prev in place of bar_y.
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

        // ── Step 1: book vertical occupancy ─────────────────────────────────
        std::map<double, std::vector<VerticalOccupancy>> vertical_occupancy;

        // Always construct a VerticalOccupancy from raw endpoints with this
        // helper instead of aggregate-initialising {a, b} directly — which
        // endpoint is numerically smaller depends on the layout's y-axis
        // convention and on which of (bar_y, node boundary) happens to be
        // "the bottom" for a given wire, and getting that backwards produces
        // an inverted interval that the y_lo < bar_y < y_hi straddle test
        // can never satisfy, silently dropping every crossing through it.
        auto makeRange = [](double a, double b) -> VerticalOccupancy {
            return { std::min(a, b), std::max(a, b) };
            };

        struct PortXY { double x; bool is_dummy; };

        // Per-segment data, computed once and reused in Step 2 below.
        // is_trivial means what it draws as: exactly one source, one
        // target, and they sit at the same x — i.e. the wire really is a
        // single straight vertical line with no horizontal offset to bridge.
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
                // Single straight wire occupying the whole gap at one x.
                double x = info.src_ports.front().x;
                double y_top = info.src_ports.front().is_dummy
                    ? layer_y_prev
                    : layer_y_prev - NODE_HEIGHT / 2.0;
                double y_bot = info.tgt_ports.front().is_dummy
                    ? layer_y
                    : layer_y + NODE_HEIGHT / 2.0;
                vertical_occupancy[x].push_back(makeRange(y_bot, y_top));
            }
            else {
                // Source wires: upper-node-bottom ↔ bar_y.
                for (const auto& pi : info.src_ports) {
                    double y_top = pi.is_dummy ? layer_y_prev
                        : layer_y_prev - NODE_HEIGHT / 2.0;
                    vertical_occupancy[pi.x].push_back(makeRange(info.bar_y, y_top));
                }
                // Target wires: bar_y ↔ lower-node-top.
                for (const auto& pi : info.tgt_ports) {
                    double y_bot = pi.is_dummy ? layer_y
                        : layer_y + NODE_HEIGHT / 2.0;
                    vertical_occupancy[pi.x].push_back(makeRange(info.bar_y, y_bot));
                }
            }

            segments.push_back(std::move(info));
        }

        // ── Step 2: count hops on non-trivial horizontal bars ─────────────────
        for (const auto& seg : segments) {
            if (seg.is_trivial) continue;

            double x_min = std::numeric_limits<double>::max();
            double x_max = -std::numeric_limits<double>::max();
            for (const auto& pi : seg.src_ports) { x_min = std::min(x_min, pi.x); x_max = std::max(x_max, pi.x); }
            for (const auto& pi : seg.tgt_ports) { x_min = std::min(x_min, pi.x); x_max = std::max(x_max, pi.x); }
            if (x_min >= x_max) continue;

            // Each booked vertical at x ∈ (x_min, x_max) whose y-interval
            // straddles bar_y is a hop (= a crossing).
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
    double      mh_time_sec;
    int         mh_crossings;
    double      mh_gs_time_sec;
    int         mh_gs_crossings;
};

// One benchmark instance's numbers PLUS both fully laid-out graphs (MH-only
// and MH+GS), kept alive so the post-run viewer can render each with the
// exact same GraphicalHypergraph its crossing count above was computed
// from.
struct BenchmarkRun {
    BenchmarkResult      result;
    GraphicalHypergraph  graph_mh;     // computeLayout() only, no explicit sifting
    GraphicalHypergraph  graph_mh_gs;  // minimizeCrossings() [Global Sifting] + computeLayout()
};

// ============================================================================
// Run one benchmark instance
// ============================================================================

static BenchmarkRun runBenchmark(const fs::path& csv_path)
{
    std::string stem = csv_path.stem().string();
    auto        records = parseBenchmarkCsv(csv_path);

    // ── MH: layout only, no explicit (global) sifting ─────────────────────────
    GraphicalHypergraph g_mh = buildHypergraph(stem, records);
    auto t0 = std::chrono::high_resolution_clock::now();
    g_mh.computeLayout();
    auto t1 = std::chrono::high_resolution_clock::now();
    double mh_sec = std::chrono::duration<double>(t1 - t0).count();
    int mh_cross = countLayoutCrossings(g_mh);

    // ── MH + GS: Global Sifting, then the same layout pipeline ────────────────
    GraphicalHypergraph g_mh_gs = buildHypergraph(stem, records);
    auto t2 = std::chrono::high_resolution_clock::now();
    g_mh_gs.minimizeCrossings();
    g_mh_gs.computeLayout();
    auto t3 = std::chrono::high_resolution_clock::now();
    double mh_gs_sec = std::chrono::duration<double>(t3 - t2).count();
    int    mh_gs_cross = countLayoutCrossings(g_mh_gs);

    BenchmarkResult result{ stem, mh_sec, mh_cross, mh_gs_sec, mh_gs_cross };
    return BenchmarkRun{ std::move(result), std::move(g_mh), std::move(g_mh_gs) };
}

// ============================================================================
// Output helpers
// ============================================================================

static void printTableHeader() {
    std::cout << "\n" << std::string(82, '=') << "\n"
        << std::left << std::setw(20) << "Instance"
        << std::right << std::setw(12) << "MH (s)"
        << std::right << std::setw(12) << "MH+GS (s)"
        << std::right << std::setw(14) << "Cross (MH)"
        << std::right << std::setw(18) << "Cross (MH+GS)"
        << "\n" << std::string(82, '-') << "\n";
}

static void printRow(const BenchmarkResult& r) {
    std::cout
        << std::left << std::setw(20) << r.instance
        << std::right << std::setw(12) << std::fixed << std::setprecision(3) << r.mh_time_sec
        << std::right << std::setw(12) << std::fixed << std::setprecision(3) << r.mh_gs_time_sec
        << std::right << std::setw(14) << r.mh_crossings
        << std::right << std::setw(18) << r.mh_gs_crossings
        << "\n";
}

static void printSummary(const std::vector<BenchmarkResult>& results) {
    std::cout << std::string(82, '=') << "\n";
    double total_mh = 0, total_gs = 0;
    int    max_mh = 0, max_gs = 0;
    for (const auto& r : results) {
        total_mh += r.mh_time_sec;
        total_gs += r.mh_gs_time_sec;
        max_mh = std::max(max_mh, r.mh_crossings);
        max_gs = std::max(max_gs, r.mh_gs_crossings);
    }
    int n = static_cast<int>(results.size());
    std::cout << "SUMMARY\n" << std::string(82, '-') << "\n"
        << std::left << std::setw(36) << "Instances run"
        << std::right << std::setw(6) << n << "\n"
        << std::left << std::setw(36) << "Total MH time (s)"
        << std::right << std::setw(6) << std::fixed << std::setprecision(3) << total_mh << "\n"
        << std::left << std::setw(36) << "Total MH+GS time (s)"
        << std::right << std::setw(6) << std::fixed << std::setprecision(3) << total_gs << "\n"
        << std::left << std::setw(36) << "Max crossings (MH)"
        << std::right << std::setw(6) << max_mh << "\n"
        << std::left << std::setw(36) << "Max crossings (MH+GS)"
        << std::right << std::setw(6) << max_gs << "\n"
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
    f << "instance;mh_time_sec;mh_crossings;mh_gs_time_sec;mh_gs_crossings\n";
    for (const auto& r : results)
        f << r.instance << ";"
        << std::fixed << std::setprecision(6) << r.mh_time_sec << ";"
        << r.mh_crossings << ";"
        << std::fixed << std::setprecision(6) << r.mh_gs_time_sec << ";"
        << r.mh_gs_crossings << "\n";
    std::cout << "\nCSV written to " << fs::absolute(out) << "\n";
}

// ============================================================================
// Post-run viewer
//
// Shows every hypergraph the benchmark just processed: for the selected
// instance, BOTH the MH-only layout and the MH+GS (Global Sifting) layout
// are rendered at once, stacked top/bottom (rather than side by side) since
// these layered layouts tend to be wide and a horizontal split would
// squeeze each one's width. Both panels use the SAME HypergraphRenderer
// used by the interactive app — no drawing logic is reimplemented here.
// ============================================================================

// Ctrl+Wheel zooms; plain wheel/drag pans/scrolls as usual.
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
    // Takes ownership of the runs (moves them in) so both graphs — and the
    // Node*/Hyperedge* pointers HypergraphRenderer keys its item maps by —
    // stay alive for as long as the window is open.
    explicit HypergraphViewerWindow(std::vector<BenchmarkRun> runs, QWidget* parent = nullptr)
        : QMainWindow(parent), runs_(std::move(runs))
    {
        setWindowTitle("Benchmark Hypergraphs — MH vs MH+GS Layout");
        resize(1150, 900);
        setStyleSheet("QMainWindow, QWidget { background: #f5f5f5; }");

        auto* central = new QWidget(this);
        setCentralWidget(central);
        auto* vbox = new QVBoxLayout(central);
        vbox->setContentsMargins(10, 10, 10, 10);
        vbox->setSpacing(8);

        // ── top bar: just the instance selector ──
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

        topBar->addWidget(lbl);
        topBar->addWidget(combo_, 1);
        vbox->addLayout(topBar);

        // ── two stacked panels: MH on top, MH+GS on the bottom ──
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
    // fitInView needs real widget sizes, which aren't final until the window
    // is actually shown — refit both panels once that's happened.
    void showEvent(QShowEvent* e) override {
        QMainWindow::showEvent(e);
        fit(mh_panel_);
        fit(gs_panel_);
    }

private:
    // One half of the split view: a title/info header, zoom controls, and
    // its own scene + view.
    struct Panel {
        QWidget*                                            container = nullptr;
        QLabel*                                              info = nullptr;
        ZoomableGraphicsView*                                view = nullptr;
        QGraphicsScene*                                      scene = nullptr;
        std::unordered_map<Node*, QGraphicsRectItem*>        node_items;
        std::unordered_map<Hyperedge*, QGraphicsPathItem*>   edge_items;
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
        // The real renderer — same call the interactive app makes.
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
        renderInto(mh_panel_, run.graph_mh, run.result.mh_time_sec, run.result.mh_crossings);
        renderInto(gs_panel_, run.graph_mh_gs, run.result.mh_gs_time_sec, run.result.mh_gs_crossings);
    }

    std::vector<BenchmarkRun> runs_;

    QComboBox* combo_ = nullptr;
    Panel mh_panel_;
    Panel gs_panel_;
};

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[])
{
    // Constructed up front, as Qt requires, even though most of main() below
    // is unchanged console-mode benchmarking; the GUI only appears at the end.
    QApplication app(argc, argv);
    app.setStyle("Fusion");

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
    std::vector<BenchmarkRun>    runs;   // kept for the viewer, launched below

    for (const auto& path : csv_files) {
        try {
            BenchmarkRun run = runBenchmark(path);
            results.push_back(run.result);
            printRow(run.result);
            runs.push_back(std::move(run));
        }
        catch (const std::exception& ex) {
            std::cerr << "  [SKIP] " << path.stem().string() << ": " << ex.what() << "\n";
        }
    }

    printSummary(results);
    writeCsv(results);

    // ── all computations are done — now show every hypergraph, menu-selectable ──
    if (runs.empty()) {
        std::cerr << "No hypergraphs to display.\n";
        return 0;
    }
    HypergraphViewerWindow viewer(std::move(runs));
    viewer.show();
    return app.exec();
}