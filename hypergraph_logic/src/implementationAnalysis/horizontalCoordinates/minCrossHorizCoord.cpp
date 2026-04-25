#include "GlobalSifting.h"    // SiftState, BlockList, sifting helpers
#include "BrandesKopf.h"      // G2, assignHorizontalCoordinates, NODE_WIDTH/HEIGHT

#include <QApplication>
#include <QPushButton>
#include <QWheelEvent>
#include <QFrame>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>
#include <QTimer>

#include <algorithm>
#include <climits>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <iostream>

namespace fs = std::filesystem;
using namespace sifting_internal;
using namespace bk_internal;
using namespace hypergraph_logic;

static constexpr int SIFTING_ROUNDS = 10;

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
    if (!f) { inst.error = "cannot open"; return inst; }
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
            S = SiftState{}; return false;
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

static sifting_internal::BlockList orderBlocksByLayerPropagation(SiftState& S) {
    S.pi.assign(S.blocks.size(), INT_MAX);
    {
        const auto& first = S.g1_layers.begin()->second;
        for (int pos = 0; pos < static_cast<int>(first.size()); ++pos)
            S.pi[S.g1_nodes[first[pos]].block_id] = pos;
    }

    std::unordered_set<int> visited;
    for (int idx : S.g1_layers.begin()->second)
        visited.insert(S.g1_nodes[idx].block_id);

    bool firstLayer = true;
    for (auto& [key, nodes] : S.g1_layers) {
        if (firstLayer) { firstLayer = false; continue; }

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

    sifting_internal::BlockList B;
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
// G2Package
// ============================================================================

struct G2Package {
    G2                   g2;
    std::vector<double>  x;
    std::vector<int>     nodeLayer;
    std::vector<NodePtr> owned;
    int                  crossings = 0;

    int addReal(const std::string& label) {
        auto p = std::make_shared<Node>(label);
        owned.push_back(p);
        g2.nodes.push_back(p.get());
        return static_cast<int>(owned.size()) - 1;
    }

    int addDummy() {
        auto p = std::make_shared<Node>();
        owned.push_back(p);
        g2.nodes.push_back(p.get());
        return static_cast<int>(owned.size()) - 1;
    }

    void buildLookups() {
        int n = static_cast<int>(g2.nodes.size());
        nodeLayer.assign(n, 0);
        for (int i = 0; i < static_cast<int>(g2.layers.size()); ++i)
            for (int v : g2.layers[i])
                nodeLayer[v] = i;
    }

    // Return the coordinate span of the diagram in BK units.
    // bkSpan = (xMax - xMin) + NODE_WIDTH  so the left/right node edges are included.
    double bkSpan() const {
        if (x.empty()) return 1.0;
        double xMin = *std::min_element(x.begin(), x.end());
        double xMax = *std::max_element(x.begin(), x.end());
        return (xMax - xMin) + NODE_WIDTH;
    }
};

static G2Package buildG2Package(const SiftState& S,
    const std::vector<int>& pi,
    int crossings)
{
    G2Package pkg;
    pkg.crossings = crossings;

    int num_layers = static_cast<int>(S.g1_layers.size());
    pkg.g2.num_layers = num_layers;
    pkg.g2.layers.resize(num_layers);

    int n_g1 = static_cast<int>(S.g1_nodes.size());
    std::vector<int> g1_to_g2(n_g1, -1);

    for (const auto& [layer_idx, g1_ids] : S.g1_layers) {
        std::vector<std::pair<int, int>> pos_id;
        pos_id.reserve(g1_ids.size());
        for (int g1id : g1_ids)
            pos_id.push_back({ pi[g1id], g1id });
        std::sort(pos_id.begin(), pos_id.end());

        for (auto& [pos, g1id] : pos_id) {
            int g2id = pkg.addReal("n" + std::to_string(g1id));
            g1_to_g2[g1id] = g2id;
            pkg.g2.layers[layer_idx].push_back(g2id);
        }
    }

    int total = static_cast<int>(pkg.g2.nodes.size());
    pkg.g2.pos.resize(total);
    for (int layer = 0; layer < num_layers; ++layer)
        for (int p = 0; p < static_cast<int>(pkg.g2.layers[layer].size()); ++p)
            pkg.g2.pos[pkg.g2.layers[layer][p]] = p;

    std::vector<std::pair<int, int>> g2_edges;

    for (int u_g1 = 0; u_g1 < n_g1; ++u_g1) {
        int src_layer = S.g1_nodes[u_g1].g1_layer;
        for (int v_g1 : S.g1_out[u_g1]) {
            int tgt_layer = S.g1_nodes[v_g1].g1_layer;
            int gap = tgt_layer - src_layer;

            if (gap == 1) {
                g2_edges.push_back({ g1_to_g2[u_g1], g1_to_g2[v_g1] });
            }
            else if (gap > 1) {
                int prev_g2 = g1_to_g2[u_g1];
                for (int mid_layer = src_layer + 1; mid_layer < tgt_layer; ++mid_layer) {
                    int dummy_g2 = pkg.addDummy();
                    int pos_in_layer = static_cast<int>(pkg.g2.layers[mid_layer].size());
                    pkg.g2.layers[mid_layer].push_back(dummy_g2);
                    pkg.g2.pos.push_back(pos_in_layer);
                    g2_edges.push_back({ prev_g2, dummy_g2 });
                    prev_g2 = dummy_g2;
                }
                g2_edges.push_back({ prev_g2, g1_to_g2[v_g1] });
            }
        }
    }

    int n_g2 = static_cast<int>(pkg.g2.nodes.size());
    pkg.g2.upper.assign(n_g2, {});
    pkg.g2.lower.assign(n_g2, {});

    for (const auto& [u, v] : g2_edges) {
        pkg.g2.upper[v].push_back(u);
        pkg.g2.lower[u].push_back(v);
        pkg.owned[u]->addChild(pkg.owned[v]);
        pkg.owned[v]->addParent(pkg.owned[u]);
    }

    pkg.buildLookups();
    pkg.x = assignHorizontalCoordinates(pkg.g2);
    return pkg;
}

// ============================================================================
// Instance descriptor
// ============================================================================

struct Instance {
    std::string  name;
    G2Package    before;
    G2Package    after;
};

// ============================================================================
// GraphPanel
//
// Rendering model:
//   scale_  maps BK coordinate units → screen pixels.
//           At zoom_=1 it is chosen so the full BK span fills the panel width.
//           When zooming, scale_ is multiplied by zoom_ relative to the base.
//
//   layerH_ is the fixed inter-layer gap in pixels = LAYER_GAP * zoom_.
//           LAYER_GAP is the gap at zoom_=1.
//
// This means zooming in/out grows both axes uniformly and the widget calls
// resize() to update the scroll area's scrollable extent.  resize() is only
// ever called from recomputeGeometry(), which is only called explicitly —
// never from resizeEvent() — to avoid the recursive-layout crash.
// ============================================================================

class GraphPanel : public QWidget {
public:
    explicit GraphPanel(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(50, 50);
    }

    void setPackage(const G2Package* pkg) {
        pkg_ = pkg;
        zoom_ = 1.0;
        baseScale_ = 1.0;   // will be overwritten by fitToSize before first paint
        recomputeGeometry();
        update();
    }

    // Fit the diagram into availW × availH pixels and store the resulting
    // scale as the zoom-1 baseline.  Called by InstanceView once the scroll
    // area viewport has been laid out and its true size is known.
    void fitToSize(int availW, int availH) {
        if (!pkg_ || pkg_->x.empty()) return;

        double span = pkg_->bkSpan();
        int    nl = pkg_->g2.num_layers;

        // pixels available for drawing (subtract margin on both sides/ends)
        double drawW = std::max(availW - 2.0 * margin_, 1.0);
        double drawH = std::max(availH - 2.0 * margin_, 1.0);

        // scale that fits the width:  span * scale = drawW
        double scaleW = drawW / span;

        // scale that fits the height:
        //   vertical canvas = (nl-1)*LAYER_GAP*s + NODE_HEIGHT*s  where s = scale
        //   (LAYER_GAP and NODE_HEIGHT both scale together with scale)
        double totalH = (nl > 1 ? (nl - 1) * LAYER_GAP : LAYER_GAP) + NODE_HEIGHT;
        double scaleH = drawH / totalH;

        // Take the tighter constraint so the diagram fits in both dimensions.
        baseScale_ = std::clamp(std::min(scaleW, scaleH), 0.01, 20.0);
        zoom_ = 1.0;   // reset zoom whenever we refit
        recomputeGeometry();
        update();
    }

    void zoomBy(double factor) {
        zoom_ = std::clamp(zoom_ * factor, ZOOM_MIN, ZOOM_MAX);
        recomputeGeometry();
        update();
    }

    // Reset to fit-to-panel (caller must supply the viewport dimensions again).
    void resetToFit(int availW, int availH) {
        fitToSize(availW, availH);
    }

protected:
    void wheelEvent(QWheelEvent* e) override {
        if (e->modifiers() & Qt::ControlModifier) {
            double factor = (e->angleDelta().y() > 0) ? ZOOM_STEP : 1.0 / ZOOM_STEP;
            zoomBy(factor);
            e->accept();
        }
        else {
            e->ignore();
        }
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), Qt::white);
        if (!pkg_ || pkg_->x.empty()) return;
        drawEdges(p);
        drawNodes(p);
        drawLayerLabels(p);
    }

private:
    // ── constants ─────────────────────────────────────────────────────────────
    // LAYER_GAP: inter-layer distance in the same coordinate space as NODE_WIDTH.
    // At zoom_=1 / baseScale_=1 this is what one layer gap looks like on screen.
    // A value of 2.5× NODE_HEIGHT gives comfortable spacing.
    static constexpr double LAYER_GAP = NODE_HEIGHT * 2.5;
    static constexpr double ZOOM_STEP = 1.15;
    static constexpr double ZOOM_MIN = 0.05;
    static constexpr double ZOOM_MAX = 40.0;

    // ── colours ───────────────────────────────────────────────────────────────
    static constexpr QColor EDGE_COL{ 80,  80,  80, 200 };
    static constexpr QColor NODE_FILL{ 255, 253, 200 };
    static constexpr QColor NODE_BORDER{ 30,  30,  30 };
    static constexpr QColor NODE_LABEL{ 20,  20,  20 };
    static constexpr QColor DUMMY_COL{ 180,  60,  60 };
    static constexpr QColor LAYER_COL{ 160, 160, 160 };

    // ── layout state ──────────────────────────────────────────────────────────
    const G2Package* pkg_ = nullptr;
    double           zoom_ = 1.0;
    double           baseScale_ = 1.0;  // scale at zoom_=1 (set by fitToSize)
    double           margin_ = 24.0;
    double           scale_ = 1.0;  // effective scale = baseScale_ * zoom_
    double           xOff_ = 0.0;
    double           layerH_ = LAYER_GAP; // = LAYER_GAP * scale_

    // ── geometry helpers ──────────────────────────────────────────────────────
    double sx(double bkX) const { return xOff_ + bkX * scale_; }
    double sy(int layer)  const { return margin_ + layer * layerH_; }
    double halfW()        const { return NODE_WIDTH * scale_ * 0.5; }
    double halfH()        const { return NODE_HEIGHT * scale_ * 0.5; }

    QPointF bottomOf(int v) const {
        double cx = sx(pkg_->x[v]), cy = sy(pkg_->nodeLayer[v]);
        return pkg_->g2.nodes[v]->isDummy() ? QPointF(cx, cy)
            : QPointF(cx, cy + halfH());
    }
    QPointF topOf(int v) const {
        double cx = sx(pkg_->x[v]), cy = sy(pkg_->nodeLayer[v]);
        return pkg_->g2.nodes[v]->isDummy() ? QPointF(cx, cy)
            : QPointF(cx, cy - halfH());
    }

    // ── recomputeGeometry ─────────────────────────────────────────────────────
    void recomputeGeometry() {
        if (!pkg_ || pkg_->x.empty()) return;

        const auto& x = pkg_->x;
        double xMin = *std::min_element(x.begin(), x.end());

        // Effective scale: baseScale_ sets zoom=1, zoom_ multiplies on top.
        scale_ = baseScale_ * zoom_;
        layerH_ = LAYER_GAP * scale_;

        // xOff_ so that the left edge of the leftmost node sits at margin_.
        xOff_ = margin_ + halfW() - xMin * scale_;

        // Resize the widget to the full canvas so the scroll area can scroll.
        int nl = pkg_->g2.num_layers;
        int needW = int(pkg_->bkSpan() * scale_ + 2.0 * margin_) + 1;
        int needH = int((nl - 1) * layerH_ + 2.0 * margin_ + NODE_HEIGHT * scale_) + 1;
        resize(std::max(needW, 50), std::max(needH, 50));
    }

    // ── drawing ───────────────────────────────────────────────────────────────
    void drawEdges(QPainter& p) const {
        p.setPen(QPen(EDGE_COL, 1.2));
        for (int u = 0; u < static_cast<int>(pkg_->g2.nodes.size()); ++u)
            for (int v : pkg_->g2.lower[u])
                p.drawLine(bottomOf(u), topOf(v));
    }

    void drawNodes(QPainter& p) const {
        for (int v = 0; v < static_cast<int>(pkg_->g2.nodes.size()); ++v) {
            double cx = sx(pkg_->x[v]);
            double cy = sy(pkg_->nodeLayer[v]);
            if (pkg_->g2.nodes[v]->isDummy()) {
                double r = std::max(2.0, scale_ * 3.0);
                QPolygonF diamond;
                diamond << QPointF(cx, cy - r) << QPointF(cx + r, cy)
                    << QPointF(cx, cy + r) << QPointF(cx - r, cy);
                p.setPen(Qt::NoPen);
                p.setBrush(DUMMY_COL);
                p.drawPolygon(diamond);
            }
            else {
                double w = NODE_WIDTH * scale_, h = NODE_HEIGHT * scale_;
                QRectF box(cx - w * 0.5, cy - h * 0.5, w, h);
                p.setPen(Qt::NoPen);
                p.setBrush(NODE_FILL);
                p.drawRoundedRect(box, 4, 4);
                p.setPen(QPen(NODE_BORDER, 1.2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(box, 4, 4);
                p.setPen(NODE_LABEL);
                QFont f;
                f.setPixelSize(std::max(7, int(h * 0.36)));
                f.setBold(true);
                p.setFont(f);
                p.drawText(box, Qt::AlignCenter,
                    QString::fromStdString(pkg_->g2.nodes[v]->getName()));
            }
        }
    }

    void drawLayerLabels(QPainter& p) const {
        p.setPen(LAYER_COL);
        QFont f; f.setPixelSize(9); p.setFont(f);
        for (int i = 0; i < pkg_->g2.num_layers; ++i) {
            int cnt = static_cast<int>(pkg_->g2.layers[i].size());
            p.drawText(QPointF(2, sy(i) + 4),
                QString("L%1(%2)").arg(i).arg(cnt));
        }
    }
};

// ============================================================================
// InstanceView – two scroll areas side by side
//
// fit-to-panel timing:
//   Qt lays out widgets asynchronously.  When setInstance() is called from
//   the combo signal the scroll area viewport may not have its final size yet,
//   so we post a zero-delay QTimer::singleShot to defer fitPanels() until
//   the event loop has processed the pending layout pass.
// ============================================================================

class InstanceView : public QWidget {
public:
    explicit InstanceView(QWidget* parent = nullptr) : QWidget(parent) {
        auto* hbox = new QHBoxLayout(this);
        hbox->setContentsMargins(4, 4, 4, 4);
        hbox->setSpacing(8);

        auto makeColumn = [&](QLabel*& titleOut,
            GraphPanel*& panelOut,
            QScrollArea*& scrollOut) {
                auto* col = new QVBoxLayout;

                titleOut = new QLabel(this);
                titleOut->setAlignment(Qt::AlignCenter);
                titleOut->setStyleSheet(
                    "font-weight:bold; font-size:12px; color:#333;"
                    " border-bottom:1px solid #bbb; padding-bottom:2px;");

                scrollOut = new QScrollArea(this);
                // setWidgetResizable(false): the panel controls its own size via
                // resize() in recomputeGeometry(); the scroll area just provides bars.
                scrollOut->setWidgetResizable(false);
                scrollOut->setStyleSheet(
                    "QScrollArea { border:1px solid #ddd; background:#fff; }"
                    "QScrollBar:vertical   { background:#eee; width:8px; }"
                    "QScrollBar:horizontal { background:#eee; height:8px; }"
                    "QScrollBar::handle:vertical,"
                    "QScrollBar::handle:horizontal { background:#bbb; border-radius:3px; }");

                panelOut = new GraphPanel(scrollOut);
                scrollOut->setWidget(panelOut);

                col->addWidget(titleOut);
                col->addWidget(scrollOut, 1);
                return col;
            };

        auto* leftCol = makeColumn(beforeTitle_, beforePanel_, beforeScroll_);
        auto* rightCol = makeColumn(afterTitle_, afterPanel_, afterScroll_);

        auto* div = new QFrame(this);
        div->setFrameShape(QFrame::VLine);
        div->setStyleSheet("color:#cccccc;");

        hbox->addLayout(leftCol, 1);
        hbox->addWidget(div);
        hbox->addLayout(rightCol, 1);
    }

    void setInstance(const Instance* inst) {
        if (!inst) return;
        inst_ = inst;
        beforeTitle_->setText(
            QString("Before sifting  —  %1 crossings").arg(inst->before.crossings));
        afterTitle_->setText(
            QString("After sifting  —  %1 crossings").arg(inst->after.crossings));
        beforePanel_->setPackage(&inst->before);
        afterPanel_->setPackage(&inst->after);
        // Defer fit until the layout pass triggered by setPackage has finished.
        QTimer::singleShot(0, this, [this] { fitPanels(); });
    }

    // Fit both panels to their current scroll-area viewport.
    void fitPanels() {
        if (!inst_) return;
        fitOne(beforePanel_, beforeScroll_);
        fitOne(afterPanel_, afterScroll_);
    }

    void zoomBy(double factor) {
        beforePanel_->zoomBy(factor);
        afterPanel_->zoomBy(factor);
    }

    void resetZoom() {
        fitPanels();
    }

protected:
    void showEvent(QShowEvent* e) override {
        QWidget::showEvent(e);
        // Post rather than call directly: the viewport size is only finalised
        // after the show event has been fully processed.
        QTimer::singleShot(0, this, [this] { fitPanels(); });
    }

private:
    static void fitOne(GraphPanel* panel, QScrollArea* scroll) {
        int w = scroll->viewport()->width();
        int h = scroll->viewport()->height();
        if (w > 10 && h > 10)
            panel->fitToSize(w, h);
    }

    const Instance* inst_ = nullptr;
    QLabel* beforeTitle_ = nullptr;
    QLabel* afterTitle_ = nullptr;
    GraphPanel* beforePanel_ = nullptr;
    GraphPanel* afterPanel_ = nullptr;
    QScrollArea* beforeScroll_ = nullptr;
    QScrollArea* afterScroll_ = nullptr;
};

// ============================================================================
// MainWindow
// ============================================================================

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(std::vector<Instance> instances, QWidget* parent = nullptr)
        : QMainWindow(parent), instances_(std::move(instances))
    {
        setWindowTitle("Crossing Minimisation + Horizontal Coordinates");
        resize(1300, 780);

        auto* central = new QWidget(this);
        setCentralWidget(central);
        auto* vbox = new QVBoxLayout(central);
        vbox->setContentsMargins(8, 8, 8, 8);
        vbox->setSpacing(6);

        // ── top bar ──────────────────────────────────────────────────────────
        auto* topBar = new QHBoxLayout;

        auto* lbl = new QLabel("Instance:", central);
        lbl->setStyleSheet("color:#333; font-size:12px;");

        combo_ = new QComboBox(central);
        combo_->setStyleSheet(
            "QComboBox { background:#fff; color:#111; border:1px solid #aaa;"
            " border-radius:4px; padding:4px 10px; font-size:13px; min-width:320px; }"
            "QComboBox::drop-down { border:none; width:20px; }"
            "QComboBox QAbstractItemView { background:#fff; color:#111;"
            " selection-background-color:#d0e4ff; }");

        for (const auto& inst : instances_)
            combo_->addItem(QString::fromStdString(inst.name));

        info_ = new QLabel(central);
        info_->setStyleSheet("color:#666; font-size:11px;");

        auto makeBtn = [&](const QString& label, const QString& tip) {
            auto* btn = new QPushButton(label, central);
            btn->setToolTip(tip);
            btn->setFixedWidth(32);
            btn->setStyleSheet(
                "QPushButton { background:#fff; border:1px solid #aaa;"
                " border-radius:3px; font-size:14px; font-weight:bold; }"
                "QPushButton:hover { background:#e8f0ff; }");
            return btn;
            };
        auto* btnZoomIn = makeBtn("+", "Zoom in  (Ctrl+Scroll on either panel)");
        auto* btnZoomOut = makeBtn("−", "Zoom out (Ctrl+Scroll on either panel)");
        auto* btnReset = makeBtn("⊙", "Fit both panels to window");

        topBar->addWidget(lbl);
        topBar->addWidget(combo_, 1);
        topBar->addStretch();
        topBar->addWidget(info_);
        topBar->addSpacing(12);
        topBar->addWidget(btnZoomOut);
        topBar->addWidget(btnZoomIn);
        topBar->addWidget(btnReset);
        vbox->addLayout(topBar);

        QObject::connect(btnZoomIn, &QPushButton::clicked, [this] { view_->zoomBy(1.15); });
        QObject::connect(btnZoomOut, &QPushButton::clicked, [this] { view_->zoomBy(1.0 / 1.15); });
        QObject::connect(btnReset, &QPushButton::clicked, [this] { view_->resetZoom(); });

        // ── instance view ─────────────────────────────────────────────────────
        view_ = new InstanceView(central);
        vbox->addWidget(view_, 1);

        // ── legend ────────────────────────────────────────────────────────────
        auto* legendBar = new QHBoxLayout;
        legendBar->setSpacing(16);
        auto chip = [&](const QString& col, const QString& txt) {
            auto* h = new QHBoxLayout;
            auto* c = new QLabel; c->setFixedSize(13, 13);
            c->setStyleSheet(QString("background:%1; border-radius:2px;").arg(col));
            auto* t = new QLabel(txt);
            t->setStyleSheet("color:#555; font-size:11px;");
            h->addWidget(c); h->addWidget(t);
            return h;
            };
        legendBar->addLayout(chip("#fffdc8", "Real node"));
        legendBar->addLayout(chip("#b43c3c", "Dummy bend-point"));
        legendBar->addStretch();
        vbox->addLayout(legendBar);

        // ── wire combo ────────────────────────────────────────────────────────
        QObject::connect(combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int idx) {
                if (idx < 0 || idx >= static_cast<int>(instances_.size())) return;
                const Instance& inst = instances_[idx];
                view_->setInstance(&inst);
                double ratio = inst.before.crossings > 0
                    ? 100.0 * (1.0 - static_cast<double>(inst.after.crossings) / inst.before.crossings) : 0.0;
                info_->setText(
                    QString("nodes: %1   layers: %2   reduction: %3%")
                    .arg(inst.before.g2.nodes.size())
                    .arg(inst.before.g2.num_layers)
                    .arg(ratio, 0, 'f', 1));
            });

        combo_->setCurrentIndex(0);
        emit combo_->currentIndexChanged(0);
    }

private:
    std::vector<Instance> instances_;
    QComboBox* combo_ = nullptr;
    InstanceView* view_ = nullptr;
    QLabel* info_ = nullptr;
};

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");

    QPalette pal = app.palette();
    pal.setColor(QPalette::Window, QColor(245, 245, 245));
    pal.setColor(QPalette::WindowText, QColor(20, 20, 20));
    pal.setColor(QPalette::Base, QColor(255, 255, 255));
    pal.setColor(QPalette::Text, QColor(20, 20, 20));
    pal.setColor(QPalette::Button, QColor(220, 220, 220));
    pal.setColor(QPalette::ButtonText, QColor(20, 20, 20));
    pal.setColor(QPalette::Highlight, QColor(80, 140, 220));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(pal);

    fs::path dir = (argc > 1) ? argv[1] : fs::path(INSTANCES_DIR);
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

    std::vector<Instance> instances;
    instances.reserve(files.size());

    for (const auto& path : files) {
        ParsedInstance parsed = parseAsp(path);
        if (!parsed.ok) {
            std::cerr << "[SKIP] " << path.stem() << ": " << parsed.error << "\n";
            continue;
        }

        SiftState S_base;
        if (!buildState(parsed, S_base) || S_base.blocks.empty()) {
            std::cerr << "[SKIP] " << path.stem() << " (buildState failed)\n";
            continue;
        }

        int n = static_cast<int>(S_base.g1_nodes.size());
        std::vector<int> pi_before(n);
        for (const auto& [layer, ids] : S_base.g1_layers)
            for (int pos = 0; pos < static_cast<int>(ids.size()); ++pos)
                pi_before[ids[pos]] = pos;

        sifting_internal::BlockList B0(S_base.blocks.size());
        std::iota(B0.begin(), B0.end(), 0);
        int crossings_before = countTotalCrossings(S_base, B0);

        SiftState S_after = S_base;
        sifting_internal::BlockList B_after = orderBlocksByLayerPropagation(S_after);
        sortAdjacencies(S_after, B_after);
        for (int round = 0; round < SIFTING_ROUNDS; ++round) {
            int chi = 0;
            sifting_internal::BlockList snap = B_after;
            for (int i = S_after.fixed_position_count;
                i < static_cast<int>(B_after.size()); ++i)
                chi += siftingStep(S_after, B_after, snap[i]);
            if (chi >= 0) break;
        }
        int crossings_after = countTotalCrossings(S_after, B_after);

        std::vector<int> pi_after(n);
        for (const auto& [layer, ids] : S_after.g1_layers) {
            std::vector<std::pair<int, int>> pos_id;
            for (int id : ids)
                pos_id.push_back({ S_after.pi[S_after.g1_nodes[id].block_id], id });
            std::sort(pos_id.begin(), pos_id.end());
            for (int p = 0; p < static_cast<int>(pos_id.size()); ++p)
                pi_after[pos_id[p].second] = p;
        }

        Instance inst;
        inst.name = path.stem().string();
        inst.before = buildG2Package(S_base, pi_before, crossings_before);
        inst.after = buildG2Package(S_after, pi_after, crossings_after);
        instances.push_back(std::move(inst));

        std::cout << "Processed: " << inst.name
            << "  crossings " << crossings_before
            << " -> " << crossings_after << "\n";
    }

    if (instances.empty()) {
        std::cerr << "No instances could be processed.\n";
        return 1;
    }

    MainWindow w(std::move(instances));
    w.show();
    return app.exec();
}