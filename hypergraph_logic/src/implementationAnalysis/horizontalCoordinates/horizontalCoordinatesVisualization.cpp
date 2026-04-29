#include "BrandesKopf.h" 

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace hypergraph_logic;
using namespace bk_internal;

// ============================================================================
// Example descriptor
//
// Stores the G2 graph, the BK output coordinates, and two precomputed
// lookup tables that the renderer uses on every paint:
//   nodeLayer[v]  → which layer v belongs to  (avoids O(n) scan per node)
// ============================================================================

struct Example {
    std::string          name;
    G2                   g2;
    std::vector<double>  x;          // BK output: one entry per node
    std::vector<int>     nodeLayer;  // nodeLayer[v] = layer index of node v
    std::vector<NodePtr> owned;      // shared_ptrs that keep Node objects alive
    // owned[i] and g2.nodes[i] refer to the same Node

// Add a real node; returns its integer id.
    int addReal(const std::string& label) {
        auto ptr = std::make_shared<Node>(label);
        owned.push_back(ptr);
        g2.nodes.push_back(ptr.get());
        return static_cast<int>(owned.size()) - 1;
    }

    // Add a dummy node; returns its integer id.
    int addDummy() {
        auto ptr = std::make_shared<Node>();   // no-name constructor → isDummy()
        owned.push_back(ptr);
        g2.nodes.push_back(ptr.get());
        return static_cast<int>(owned.size()) - 1;
    }

    // Must be called after g2 and x are fully populated.
    void buildLookups() {
        int n = static_cast<int>(g2.nodes.size());
        nodeLayer.assign(n, 0);
        for (int i = 0; i < static_cast<int>(g2.layers.size()); ++i)
            for (int v : g2.layers[i])
                nodeLayer[v] = i;
    }
};

// ============================================================================
// Helper: populate upper/lower from an edge list, then call assignHorizontalCoordinates
// ============================================================================

static void finalise(Example& ex,
    const std::vector<std::pair<int, int>>& edges)
{
    int n = static_cast<int>(ex.g2.nodes.size());
    ex.g2.upper.assign(n, {});
    ex.g2.lower.assign(n, {});
    for (const auto& [u, v] : edges) {
        ex.g2.upper[v].push_back(u);
        ex.g2.lower[u].push_back(v);
        // Wire Node adjacency using the shared_ptrs in owned[].
        // addChild/addParent require shared_ptr<Node>, which owned[] provides.
        ex.owned[u]->addChild(ex.owned[v]);
        ex.owned[v]->addParent(ex.owned[u]);
    }
    ex.x = assignHorizontalCoordinates(ex.g2);
    ex.buildLookups();
}

// ============================================================================
// Example 1: the example from the BK paper for illustrating the algorithm.
// 
// Dummy nodes are marked with an asterisk (*).
// 
// Layer 0 : 0  1
// Layer 1 : 2  3  4* 5  6* 7* 8  9
// Layer 2 : 10 11 12* 13* 14* 15
// Layer 3 : 16 17 18* 19* 20* 21 22*
// Layer 4 : 23 24 25
// ============================================================================

static Example buildExample1() {
    Example ex;
    ex.name = "Paper's example 1";
    G2& g = ex.g2;
    g.num_layers = 5;

    const std::vector<bool> dummy = {
        //   0      1      2      3      4      5      6      7      8      9
            false, false, false, false, true,  false, true,  true,  false, false,
            //  10     11     12     13     14     15
                false, false, true,  true,  true,  false,
                //  16     17     18     19     20     21     22
                    false, false, true,  true,  true,  false, true,
                    //  23     24     25
                        false, false, false
    };

    ex.owned.reserve(26);
    g.nodes.reserve(26);
    for (int i = 0; i < 26; ++i)
        dummy[i] ? ex.addDummy() : ex.addReal(std::to_string(i));

    g.layers.resize(5);
    g.layers[0] = { 0, 1 };
    g.layers[1] = { 2, 3, 4, 5, 6, 7, 8, 9 };
    g.layers[2] = { 10, 11, 12, 13, 14, 15 };
    g.layers[3] = { 16, 17, 18, 19, 20, 21, 22 };
    g.layers[4] = { 23, 24, 25 };

    g.pos = {
        0, 1,
        0, 1, 2, 3, 4, 5, 6, 7,
        0, 1, 2, 3, 4, 5,
        0, 1, 2, 3, 4, 5, 6,
        0, 1, 2
    };

    finalise(ex, {
        {0,2},{0,7},{0,9},{1,4},{1,6},
        {3,11},{4,11},{5,11},{6,12},{7,13},{8,11},{8,15},{9,11},{9,14},
        {10,16},{10,17},{10,21},{12,19},{13,20},{14,21},{15,18},{15,22},
        {16,23},{16,24},{17,24},{18,23},{19,25},{20,25},{21,25},{22,25}
        });
    return ex;
}

// ============================================================================
// Example 2: Another exmaple from the paper, illustrating how well the
//            algorithm can perform.
//
//  Layer 0: 0   1 
//  Layer 1: 2*  3*  4*  5   6*     
//  Layer 2: 7*  8*  9   10   11*  12*
//  Layer 3: 13*  14*  15   16   17*  18*
//  Layer 4: 19*  20*  21   22*  23*  24   25*  26*
//  Layer 5: 27*  28*  29   30   31*  32*  33   34*  35*
//  Layer 6: 36   37*  38   39   40   41*  42*  43*  44*
//  Layer 7: 45   46*  47   48   49*  50   51*
//  Layer 8: 52   53   54*  55*
//  Layer 9: 56
// ============================================================================
static Example buildExample2() {
    Example ex;
    ex.name = "Paper's example 2";
    G2& g = ex.g2;
    g.num_layers = 10;

    const std::vector<bool> dummy = {
        // L0: 0   1 
        false, false,
        // L1: 2*  3*  4*  5   6*
        true , true , true , false, true ,
        // L2: 7*  8*  9   10   11*  12*
        true , true , false, false, true , true ,
        // L3: 13*  14*  15   16   17*  18*
        true , true , false, false, true , true ,
        // L4: 19*  20*  21   22*  23*  24   25*  26*
        true , true , false, true , true , false, true , true ,
        // L5: 27*  28*  29   30   31*  32*  33   34*  35*
        true , true , false, false, true , true , false, true , true ,
        // L6: 36   37*  38   39   40   41*  42*  43*  44*
        false, true , false, false, false, true , true , true , true ,
        // L7: 45   46*  47   48   49*  50   51*
        false, true , false, false, true , false, true ,
        // L8: 52   53   54*  55*
        false, false, true , true ,
        // L9: 56 
        false
    };

    ex.owned.reserve(dummy.size());
    g.nodes.reserve(dummy.size());
    for (int i = 0; i < static_cast<int>(dummy.size()); ++i)
        dummy[i] ? ex.addDummy() : ex.addReal(std::to_string(i));

    g.layers.resize(10);
    g.layers[0] = { 0, 1 };
    g.layers[1] = { 2, 3, 4, 5, 6 };
    g.layers[2] = { 7, 8, 9, 10, 11, 12 };
    g.layers[3] = { 13, 14, 15, 16, 17, 18 };
    g.layers[4] = { 19, 20, 21, 22, 23, 24, 25, 26 };
    g.layers[5] = { 27, 28, 29, 30, 31, 32, 33, 34, 35 };
    g.layers[6] = { 36, 37, 38, 39, 40, 41, 42, 43, 44 };
    g.layers[7] = { 45, 46, 47, 48, 49, 50, 51 };
    g.layers[8] = { 52, 53, 54, 55 };
    g.layers[9] = { 56 };

    g.pos = {
        // L0: 0  1
        0, 1,
        // L1: 2  3  4  5  6
        0, 1, 2, 3, 4,
        // L2: 7  8  9  10  11  12
        0, 1, 2, 3, 4, 5,
        // L3: 13  14  15  16  17  18
        0, 1, 2, 3, 4, 5,
        // L4: 19  20  21  22  23  24  25  26
        0, 1, 2, 3, 4, 5, 6, 7,
        // L5: 27  28  29  30  31  32  33  34  35
        0, 1, 2, 3, 4, 5, 6, 7, 8,
        // L6: 36  37  38  39  40  41  42  43  44
        0, 1, 2, 3, 4, 5, 6, 7, 8,
        // L7: 45  46  47  48  49  50  51
        0, 1, 2, 3, 4, 5, 6,
        // L8: 52  53  54  55
        0, 1, 2, 3,
        // L9: 56
        0
    };

    finalise(ex, {
        {0, 2}, {0, 3}, {0, 4}, {0, 5}, {1, 5}, {1, 6},
        {2, 7}, {3, 8}, {4, 9}, {5, 9}, {5, 10}, {5, 11}, {6, 12},
        {7, 13}, {8, 14}, {9, 15}, {10, 16}, {11, 17}, {12, 18},
        {13, 19 }, { 14, 20 }, { 15, 21 }, { 15, 22 }, { 15, 23 }, { 16, 24 }, { 17, 25 }, { 18, 26 },
        {19, 27 }, { 20, 28 }, { 21, 29 }, { 21, 30 }, { 22, 31 }, { 23, 32 }, { 24, 33 }, { 25, 34 }, { 26, 35 },
        {27, 36}, {28, 37}, {29, 36}, { 29, 38 }, {29, 39},  { 30, 39 }, {30, 40}, { 31, 40 }, {32, 41}, {33, 42}, {34, 43}, {35, 44},
        {36, 45}, {37, 46}, {38, 45}, { 38, 47 }, {40, 47}, {40, 48}, {40, 50 }, { 41, 49 }, {42, 50}, {43, 51}, {44, 50},
        {46, 52}, {47, 52}, {48, 53}, {49, 54}, {51, 55},
        {52, 56 }, { 53, 56 }, { 54, 56 }, { 55, 56 }
        });
    return ex;
}

// ============================================================================
// Example 3: balanced binary tree (3 layers, all real nodes)
//
//          [0]
//        [1]   [2]
//     [3][4]  [5][6]
// ============================================================================

static Example buildExample3() {
    Example ex;
    ex.name = "Balanced binary tree (7 nodes)";
    G2& g = ex.g2;
    g.num_layers = 3;

    for (int i = 0; i < 7; ++i)
        ex.addReal(std::to_string(i));

    g.layers.resize(3);
    g.layers[0] = { 0 };
    g.layers[1] = { 1, 2 };
    g.layers[2] = { 3, 4, 5, 6 };

    g.pos = { 0,  0,1,  0,1,2,3 };

    finalise(ex, { {0,1},{0,2},{1,3},{1,4},{2,5},{2,6} });
    return ex;
}

// ============================================================================
// Example 4: a crossing pair of edges.
//
//  Layer 0: [0]  [1]
//  Layer 1: [2]  [3]
//  Edges: 0→3 and 1→2  (they cross)
// ============================================================================

static Example buildExample4() {
    Example ex;
    ex.name = "Crossing pair (type-1 conflict)";
    G2& g = ex.g2;
    g.num_layers = 2;

    for (int i = 0; i < 4; ++i)
        ex.addReal(std::to_string(i));

    g.layers.resize(2);
    g.layers[0] = { 0, 1 };
    g.layers[1] = { 2, 3 };

    g.pos = { 0,1,  0,1 };

    finalise(ex, { {0,3},{1,2} });
    return ex;
}

// ============================================================================
// Example 5: A complex topology.
//
//  Layer 0:  [0]  [1]  [2]  [3]  [4]
//  Layer 1:  [5]  [6]  [7*] [8]  [9*]   7*,9* are dummies (long edges)
//  Layer 2:  [10] [11] [12] [13]
//
//  Long edges: 0 → 7* → 11,  4 → 9* → 12
//  Fan-out from node 2:  2→5, 2→6, 2→8
// ============================================================================

static Example buildExample5() {
    Example ex;
    ex.name = "Some random complex topology";
    G2& g = ex.g2;
    g.num_layers = 3;

    const std::vector<bool> dummy = {
        //  0      1      2      3      4      5      6      7      8      9     10     11     12     13
            false, false, false, false, false, false, false, true,  false, true, false, false, false, false
    };
    for (int i = 0; i < 14; ++i)
        dummy[i] ? ex.addDummy() : ex.addReal(std::to_string(i));

    g.layers.resize(3);
    g.layers[0] = { 0, 1, 2, 3, 4 };
    g.layers[1] = { 5, 6, 7, 8, 9 };
    g.layers[2] = { 10, 11, 12, 13 };

    g.pos = { 0,1,2,3,4,  0,1,2,3,4,  0,1,2,3 };

    finalise(ex, {
        {0,7},  {7,11},          // long edge: 0 → 7* → 11
        {4,9},  {9,12},          // long edge: 4 → 9* → 12
        {2,5},  {2,6},  {2,8},  // fan-out from 2
        {1,6},  {3,8},
        {5,10}, {6,11}, {8,12}, {8,13}
        });
    return ex;
}

// ============================================================================
// GraphView
//
// Renders one Example inside a QScrollArea.
//
// Layout model:
//   • LAYER_GAP is a fixed pixel distance between layer centre-lines at
//     zoom = 1.  It never adapts to the window height, so every graph uses
//     the same vertical spacing regardless of how many layers it has.
//   • zoom_ scales both axes uniformly.  Ctrl+Wheel zooms in/out; the widget
//     resizes itself accordingly so the scroll area can scroll both axes.
//   • The horizontal scale (scale_) maps BK coordinate units to pixels and
//     also multiplies by zoom_, so the x-spread grows/shrinks together with
//     the vertical spacing.
// ============================================================================

#include <QWheelEvent>

class GraphView : public QWidget {
public:
    explicit GraphView(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(200, 150);
    }

    void setExample(const Example* ex) {
        ex_ = ex;
        recomputeGeometry();
        update();
    }

    // Zoom in / out programmatically (e.g. from +/- buttons in the toolbar).
    void zoomBy(double factor) {
        zoom_ = std::clamp(zoom_ * factor, ZOOM_MIN, ZOOM_MAX);
        recomputeGeometry();
        update();
    }

    void resetZoom() {
        zoom_ = 1.0;
        recomputeGeometry();
        update();
    }

protected:
    // Ctrl+Wheel → zoom; plain Wheel → scroll (handled by the parent QScrollArea)
    void wheelEvent(QWheelEvent* e) override {
        if (e->modifiers() & Qt::ControlModifier) {
            double factor = (e->angleDelta().y() > 0) ? ZOOM_STEP : 1.0 / ZOOM_STEP;
            zoomBy(factor);
            e->accept();
        }
        else {
            e->ignore();   // let the scroll area handle plain scrolling
        }
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), BG);

        if (!ex_ || ex_->x.empty()) return;

        drawEdges(p);
        drawNodes(p);
        drawLayerLabels(p);
    }

private:
    // ── constants ─────────────────────────────────────────────────────────────
    static constexpr double LAYER_GAP = 225.0;  // px between layer centre-lines at zoom=1
    static constexpr double ZOOM_STEP = 1.15;
    static constexpr double ZOOM_MIN = 0.15;
    static constexpr double ZOOM_MAX = 6.0;

    // ── colours ──────────────────────────────────────────────────────────────
    static constexpr QColor BG{ 255, 255, 255 };
    static constexpr QColor EDGE_COL{ 60,  60,  60, 200 };
    static constexpr QColor NODE_FILL{ 255, 253, 200 };
    static constexpr QColor NODE_BORDER{ 30,  30,  30 };
    static constexpr QColor NODE_LABEL{ 20,  20,  20 };
    static constexpr QColor DUMMY_COL{ 180,  60,  60 };
    static constexpr QColor LAYER_LABEL{ 160, 160, 160 };

    // ── layout state ─────────────────────────────────────────────────────────
    const Example* ex_ = nullptr;
    double         zoom_ = 1.0;
    double         margin_ = 48.0;
    double         scale_ = 1.0;   // BK unit → screen pixel (includes zoom_)
    double         xOff_ = 0.0;
    double         layerH_ = LAYER_GAP;  // always LAYER_GAP * zoom_

    // ── geometry helpers ─────────────────────────────────────────────────────

    double sx(double bkX) const { return xOff_ + bkX * scale_; }
    double sy(int layer)  const { return margin_ + layer * layerH_; }
    double halfW()        const { return NODE_WIDTH * scale_ * 0.5; }
    double halfH()        const { return NODE_HEIGHT * scale_ * 0.5; }

    QPointF bottomOf(int v) const {
        double cx = sx(ex_->x[v]), cy = sy(ex_->nodeLayer[v]);
        return ex_->g2.nodes[v]->isDummy() ? QPointF(cx, cy)
            : QPointF(cx, cy + halfH());
    }

    QPointF topOf(int v) const {
        double cx = sx(ex_->x[v]), cy = sy(ex_->nodeLayer[v]);
        return ex_->g2.nodes[v]->isDummy() ? QPointF(cx, cy)
            : QPointF(cx, cy - halfH());
    }

    // ── recomputeGeometry ────────────────────────────────────────────────────
    //
    // Called whenever the example changes or zoom changes.
    // Derives scale_ and xOff_ from the BK coordinates and the current zoom,
    // then resizes the widget so the scroll area knows the full canvas size.
    // Never called from resizeEvent to avoid the recursive-layout crash.

    void recomputeGeometry() {
        if (!ex_ || ex_->x.empty()) return;

        const auto& x = ex_->x;
        const G2& g = ex_->g2;

        double xMin = *std::min_element(x.begin(), x.end());
        double xMax = *std::max_element(x.begin(), x.end());

        // Fixed layer gap scaled by zoom
        layerH_ = LAYER_GAP * zoom_;

        // Horizontal scale: 1 BK unit = NODE_WIDTH screen pixels at zoom 1.
        // Multiply by zoom_ so x and y scale together.
        double bkSpan = (xMax - xMin) + NODE_WIDTH;
        scale_ = (NODE_WIDTH / 1.0) * zoom_;           // 1 BK unit ≡ NODE_WIDTH px
        // Clamp so very sparse graphs don't explode and dense ones stay readable
        scale_ = std::clamp(scale_, 0.15 * zoom_, 2.0 * zoom_);

        // xOff_: left edge of leftmost node sits at margin_
        xOff_ = margin_ + halfW() - xMin * scale_;

        // Resize the widget to the full canvas size so the scroll area scrolls
        int nl = g.num_layers;
        int needW = int(bkSpan * scale_ + 2.0 * margin_) + 1;
        int needH = int((nl - 1) * layerH_ + 2.0 * margin_ + NODE_HEIGHT * scale_) + 1;
        resize(std::max(needW, 200), std::max(needH, 150));
    }

    // ── drawing ──────────────────────────────────────────────────────────────

    void drawEdges(QPainter& p) const {
        const G2& g = ex_->g2;
        p.setPen(QPen(EDGE_COL, 1.5));
        for (int u = 0; u < static_cast<int>(g.nodes.size()); ++u)
            for (int v : g.lower[u])
                p.drawLine(bottomOf(u), topOf(v));
    }

    void drawNodes(QPainter& p) const {
        const G2& g = ex_->g2;
        for (int v = 0; v < static_cast<int>(g.nodes.size()); ++v) {
            double cx = sx(ex_->x[v]);
            double cy = sy(ex_->nodeLayer[v]);

            if (g.nodes[v]->isDummy()) {
                drawDummy(p, cx, cy);
            }
            else {
                drawReal(p, v, cx, cy);
            }
        }
    }

    void drawReal(QPainter& p, int v, double cx, double cy) const {
        double w = NODE_WIDTH * scale_;
        double h = NODE_HEIGHT * scale_;
        QRectF box(cx - w * 0.5, cy - h * 0.5, w, h);

        // Fill
        p.setPen(Qt::NoPen);
        p.setBrush(NODE_FILL);
        p.drawRoundedRect(box, 5, 5);

        // Border
        p.setPen(QPen(NODE_BORDER, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box, 5, 5);

        // Label: node id
        p.setPen(NODE_LABEL);
        QFont f;
        f.setPixelSize(std::max(9, int(h * 0.38)));
        f.setBold(true);
        p.setFont(f);
        p.drawText(box, Qt::AlignCenter,
            QString::fromStdString(ex_->g2.nodes[v]->getName()));
    }

    void drawDummy(QPainter& p, double cx, double cy) const {
        // Small diamond / rotated square
        double r = std::max(3.5, scale_ * 4.0);
        QPolygonF diamond;
        diamond << QPointF(cx, cy - r)
            << QPointF(cx + r, cy)
            << QPointF(cx, cy + r)
            << QPointF(cx - r, cy);
        p.setPen(Qt::NoPen);
        p.setBrush(DUMMY_COL);
        p.drawPolygon(diamond);
    }

    void drawLayerLabels(QPainter& p) const {
        p.setPen(LAYER_LABEL);
        QFont f;
        f.setPixelSize(10);
        p.setFont(f);
        for (int i = 0; i < ex_->g2.num_layers; ++i) {
            int cnt = static_cast<int>(ex_->g2.layers[i].size());
            p.drawText(QPointF(4, sy(i) + 4),
                QString("L%1 (%2)").arg(i).arg(cnt));
        }
    }
};

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("BK Horizontal Coordinate Visualizer");
        resize(1100, 720);
        setStyleSheet("QMainWindow, QWidget { background: #f5f5f5; }");

        // ── build all examples up-front ──
        examples_.push_back(buildExample1());
        examples_.push_back(buildExample2());
        examples_.push_back(buildExample3());
        examples_.push_back(buildExample4());
        examples_.push_back(buildExample5());

        // ── central layout ──
        auto* central = new QWidget(this);
        setCentralWidget(central);
        auto* vbox = new QVBoxLayout(central);
        vbox->setContentsMargins(10, 10, 10, 10);
        vbox->setSpacing(8);

        // ── top bar: combo + info label ──
        auto* topBar = new QHBoxLayout;

        auto* lbl = new QLabel("Example:", central);
        lbl->setStyleSheet("color:#333333; font-size:12px;");

        combo_ = new QComboBox(central);
        combo_->setStyleSheet(
            "QComboBox {"
            "  background:#ffffff; color:#111111;"
            "  border:1px solid #aaaaaa; border-radius:4px;"
            "  padding:4px 10px; font-size:13px; min-width:300px; }"
            "QComboBox::drop-down { border:none; width:20px; }"
            "QComboBox QAbstractItemView {"
            "  background:#ffffff; color:#111111;"
            "  selection-background-color:#d0e4ff; }");

        for (const auto& ex : examples_)
            combo_->addItem(QString::fromStdString(ex.name));

        info_ = new QLabel(central);
        info_->setStyleSheet("color:#555555; font-size:11px;");

        // Zoom controls
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
        auto* btnZoomIn = makeBtn("+", "Zoom in  (Ctrl+Scroll)");
        auto* btnZoomOut = makeBtn("−", "Zoom out (Ctrl+Scroll)");
        auto* btnReset = makeBtn("⊙", "Reset zoom");

        topBar->addWidget(lbl);
        topBar->addWidget(combo_, 1);
        topBar->addStretch();
        topBar->addWidget(info_);
        topBar->addSpacing(12);
        topBar->addWidget(btnZoomOut);
        topBar->addWidget(btnZoomIn);
        topBar->addWidget(btnReset);
        vbox->addLayout(topBar);

        // Wire zoom buttons
        QObject::connect(btnZoomIn, &QPushButton::clicked, [this] { view_->zoomBy(1.15); });
        QObject::connect(btnZoomOut, &QPushButton::clicked, [this] { view_->zoomBy(1.0 / 1.15); });
        QObject::connect(btnReset, &QPushButton::clicked, [this] { view_->resetZoom(); });

        // ── scroll area + graph view ──
        scroll_ = new QScrollArea(central);
        scroll_->setStyleSheet(
            "QScrollArea { border:1px solid #cccccc; background:#ffffff; }"
            "QScrollBar:vertical   { background:#eeeeee; width:10px; }"
            "QScrollBar:horizontal { background:#eeeeee; height:10px; }"
            "QScrollBar::handle:vertical, QScrollBar::handle:horizontal"
            "  { background:#aaaaaa; border-radius:3px; }");
        scroll_->setWidgetResizable(false);  // GraphView manages its own size for zoom/scroll

        view_ = new GraphView(scroll_);
        scroll_->setWidget(view_);
        vbox->addWidget(scroll_, 1);

        // ── legend row ──
        auto* legendBar = new QHBoxLayout;
        legendBar->setSpacing(20);
        auto makeChip = [&](const QString& col, const QString& txt) {
            auto* h = new QHBoxLayout;
            auto* chip = new QLabel;
            chip->setFixedSize(14, 14);
            chip->setStyleSheet(QString("background:%1; border-radius:2px;").arg(col));
            auto* t = new QLabel(txt);
            t->setStyleSheet("color:#444444; font-size:11px;");
            h->addWidget(chip); h->addWidget(t);
            return h;
            };
        legendBar->addLayout(makeChip("#fffdc8", "Real node (light yellow)"));
        legendBar->addLayout(makeChip("#b43c3c", "Dummy bend-point (red diamond)"));
        legendBar->addStretch();
        vbox->addLayout(legendBar);

        // ── wire combo (lambda, no Q_OBJECT needed) ──
        QObject::connect(combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int idx) {
                if (idx < 0 || idx >= static_cast<int>(examples_.size())) return;
                const Example& ex = examples_[idx];
                view_->setExample(&ex);
                info_->setText(QString("nodes: %1   layers: %2")
                    .arg(ex.g2.nodes.size())
                    .arg(ex.g2.num_layers));
            });

        // ── show first example ──
        combo_->setCurrentIndex(0);
        emit combo_->currentIndexChanged(0);
    }

private:
    std::vector<Example> examples_;
    QComboBox* combo_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    GraphView* view_ = nullptr;
    QLabel* info_ = nullptr;
};


int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");

    QPalette pal = app.palette();
    pal.setColor(QPalette::Window, QColor(245, 245, 245));
    pal.setColor(QPalette::WindowText, QColor(20, 20, 20));
    pal.setColor(QPalette::Base, QColor(255, 255, 255));
    pal.setColor(QPalette::AlternateBase, QColor(240, 240, 240));
    pal.setColor(QPalette::Text, QColor(20, 20, 20));
    pal.setColor(QPalette::Button, QColor(225, 225, 225));
    pal.setColor(QPalette::ButtonText, QColor(20, 20, 20));
    pal.setColor(QPalette::Highlight, QColor(80, 140, 220));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(pal);

    MainWindow w;
    w.show();
    return app.exec();
}