#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace hypergraph_logic::layout_settings_tests {

    // ============================================================================
    // LayoutSettings: the distances the user may change
    // ============================================================================

    static LayoutSettings with(double block_sep, double layer_gap, double horizontal_sep) {
        LayoutSettings s;
        s.block_sep = block_sep;
        s.layer_gap = layer_gap;
        s.horizontal_sep = horizontal_sep;
        return s;
    }

    TEST(LayoutSettings, DefaultsAreTheLayoutConstants) {
        const LayoutSettings s;
        EXPECT_DOUBLE_EQ(s.block_sep, MIN_BLOCK_SEP);
        EXPECT_DOUBLE_EQ(s.layer_gap, LAYER_GAP);
        EXPECT_DOUBLE_EQ(s.horizontal_sep, HORIZONTAL_SEP);
        EXPECT_EQ(s.clamped(), s);
        EXPECT_EQ(GraphicalHypergraph("g").getLayoutSettings(), s);
    }

    TEST(LayoutSettings, ClampedKeepsEveryValueInItsRange) {
        const LayoutSettings low = with(1.0, 1.0, 1.0).clamped();
        EXPECT_DOUBLE_EQ(low.block_sep, LayoutSettings::MIN_BLOCK_SEP_SETTING);
        EXPECT_DOUBLE_EQ(low.layer_gap, LayoutSettings::MIN_LAYER_GAP_SETTING);
        EXPECT_DOUBLE_EQ(low.horizontal_sep, LayoutSettings::MIN_HORIZONTAL_SEP_SETTING);

        const LayoutSettings high = with(1e6, 1e6, 1e6).clamped();
        EXPECT_DOUBLE_EQ(high.block_sep, LayoutSettings::MAX_BLOCK_SEP_SETTING);
        EXPECT_DOUBLE_EQ(high.layer_gap, LayoutSettings::MAX_LAYER_GAP_SETTING);
        EXPECT_DOUBLE_EQ(high.horizontal_sep, LayoutSettings::MAX_HORIZONTAL_SEP_SETTING);

        // The guarantees the ranges rest on.
        EXPECT_DOUBLE_EQ(LayoutSettings::MIN_BLOCK_SEP_SETTING, 2.0 * MIN_VERTICAL_SEP);
        EXPECT_GT(LayoutSettings::MIN_HORIZONTAL_SEP_SETTING, ARCH_HEIGHT);
        EXPECT_GT(LayoutSettings::MIN_HORIZONTAL_SEP_SETTING, 2.0 * HOP_RADIUS);
    }

    // ============================================================================
    // A graph draws itself with its settings
    // ============================================================================

    // Three loose boxes side by side: nothing pulls them apart but the gap.
    TEST(LayoutSettings, BlockSepIsTheGapBetweenNeighbours) {
        GraphicalHypergraph g("g");
        NodePtr A = g.createNode("A", 0, -1, nullptr);
        NodePtr B = g.createNode("B", 0, -1, nullptr);
        NodePtr C = g.createNode("C", 0, -1, nullptr);
        g.computeLayout();
        for (double sep : { 20.0, 60.0, 150.0 }) {
            g.setLayoutSettings(with(sep, LAYER_GAP, HORIZONTAL_SEP));
            EXPECT_NEAR(g.getX(B) - g.getX(A), NODE_WIDTH + sep, 1e-9) << "sep " << sep;
            EXPECT_NEAR(g.getX(C) - g.getX(B), NODE_WIDTH + sep, 1e-9) << "sep " << sep;
        }
    }

    // A over B through a straight connection: no bar in between, just the gap.
    TEST(LayoutSettings, LayerGapSeparatesTheLayers) {
        GraphicalHypergraph g("g");
        NodePtr A = g.createNode("A", 0, -1, nullptr);
        g.createNode("B", 1, -1, A);
        g.computeLayout();
        for (double gap : { 20.0, 60.0, 200.0 }) {
            g.setLayoutSettings(with(MIN_BLOCK_SEP, gap, HORIZONTAL_SEP));
            const auto& layers = g.getLayerLayout();
            EXPECT_NEAR(layers.at(0) - layers.at(1), NODE_HEIGHT + gap, 1e-9) << "gap " << gap;
        }
    }

    // A -> {C, D} and B -> {C, D}: both bars span C to D, so they are stacked one over the other.
    TEST(LayoutSettings, HorizontalSepSeparatesStackedBars) {
        GraphicalHypergraph g("g");
        NodePtr A = g.createNode("A", 0, -1, nullptr);
        NodePtr B = g.createNode("B", 0, -1, nullptr);
        NodePtr C = g.createNode("C", 1, -1, A);
        NodePtr D = g.createNode("D", 1, -1, nullptr);
        g.addTargetToEdge(g.getAllHyperedges().front(), D);
        g.addTargetToEdge(g.addConnection(B, C), D);
        g.computeLayout();
        for (double sep : { 15.0, 30.0, 100.0 }) {
            const LayoutSettings s = with(MIN_BLOCK_SEP, 40.0, sep);
            g.setLayoutSettings(s);
            std::vector<double> bars;
            for (const auto& e : g.getAllHyperedges())
                if (!e->isSegment()) bars.push_back(g.getEdgeLayout().at(e.get()));
            ASSERT_EQ(bars.size(), 2u);
            EXPECT_NEAR(std::abs(bars[0] - bars[1]), sep, 1e-9) << "sep " << sep;
            // The first bar a layer gap under A and B, C and D a layer gap under the second.
            const auto& layers = g.getLayerLayout();
            EXPECT_NEAR(layers.at(0) - layers.at(1), NODE_HEIGHT + 2.0 * s.layer_gap + sep, 1e-9) << "sep " << sep;
        }
    }

    TEST(LayoutSettings, SetLayoutSettingsClampsAndCloneKeepsThem) {
        GraphicalHypergraph g("g");
        NodePtr A = g.createNode("A", 0, -1, nullptr);
        NodePtr B = g.createNode("B", 0, -1, nullptr);
        g.computeLayout();
        g.setLayoutSettings(with(1.0, 70.0, 25.0));
        EXPECT_EQ(g.getLayoutSettings(), with(LayoutSettings::MIN_BLOCK_SEP_SETTING, 70.0, 25.0));
        EXPECT_NEAR(g.getX(B) - g.getX(A), NODE_WIDTH + LayoutSettings::MIN_BLOCK_SEP_SETTING, 1e-9);

        const GraphicalHypergraph copy = g.clone();
        EXPECT_EQ(copy.getLayoutSettings(), g.getLayoutSettings());
        EXPECT_EQ(g.duplicate().getLayoutSettings(), g.getLayoutSettings());
    }

    // Boxes without a size of their own follow their graph's default sizes,
    // also those created later or copied with the graph.
    TEST(LayoutSettings, BoxesWithoutOwnSizeFollowTheGraphsDefaultSizes) {
        GraphicalHypergraph g("g");
        NodePtr A = g.createNode("A", 0, -1, nullptr);
        NodeAttributes own("B");
        own.height = 40.0;
        NodePtr B = g.createNode(own, 0, -1, nullptr);
        NodeAttributes circle("C");
        circle.shape = NodeShape::Circle;
        NodePtr C = g.createNode(circle, 0, -1, nullptr);
        g.computeLayout();

        LayoutSettings s;
        s.box_sizes.rectangle = BoxSize{ 150.0, 70.0 };
        s.box_sizes.circle = BoxSize{ 60.0, 60.0 };
        g.setLayoutSettings(s);
        EXPECT_DOUBLE_EQ(A->getWidth(), 150.0);
        EXPECT_DOUBLE_EQ(A->getHeight(), 70.0);
        EXPECT_DOUBLE_EQ(B->getWidth(), 150.0);
        EXPECT_DOUBLE_EQ(B->getHeight(), 40.0); // its own height
        EXPECT_DOUBLE_EQ(C->getWidth(), 60.0);
        EXPECT_NEAR(g.getX(B) - g.getX(A), 150.0 + MIN_BLOCK_SEP, 1e-9);

        NodePtr D = g.createNode("D", 0, -1, nullptr);
        g.computeLayout();
        EXPECT_DOUBLE_EQ(D->getWidth(), 150.0);

        const GraphicalHypergraph copy = g.clone();
        for (const auto& n : copy.getAllNodes())
            if (n->getName() == "A") EXPECT_DOUBLE_EQ(n->getWidth(), 150.0);

        // Out of range, they are brought inside it.
        s.box_sizes.rhombus = BoxSize{ 1.0, 1e6 };
        g.setLayoutSettings(s);
        EXPECT_EQ(g.getLayoutSettings().box_sizes.rhombus, (BoxSize{ MIN_NODE_WIDTH, MAX_NODE_HEIGHT }));
    }

    // The tightest gap still leaves every port on its box and the boxes apart,
    // which the vertical overlap detection relies on.
    TEST(LayoutSettings, TightestGapKeepsTheBoxesApartAndThePortsOnThem) {
        GraphicalHypergraph g("g");
        std::vector<NodePtr> top, bottom;
        for (int i = 0; i < 4; ++i) top.push_back(g.createNode("T" + std::to_string(i), 0, -1, nullptr));
        for (int i = 0; i < 4; ++i) bottom.push_back(g.createNode("B" + std::to_string(i), 1, -1, top[i]));
        for (int i = 0; i < 4; ++i) {
            g.addConnection(top[i], bottom[(i + 1) % 4]);
            g.addConnection(top[i], bottom[(i + 3) % 4]);
        }
        g.computeLayout();
        g.setLayoutSettings(with(LayoutSettings::MIN_BLOCK_SEP_SETTING, LAYER_GAP, HORIZONTAL_SEP));

        for (int layer : { 0, 1 }) {
            const auto& nodes = g.getLayerData(layer).nodes;
            for (size_t i = 0; i + 1 < nodes.size(); ++i) {
                const double gap = g.getX(nodes[i + 1]) - g.getX(nodes[i])
                    - (nodes[i]->getWidth() + nodes[i + 1]->getWidth()) / 2.0;
                EXPECT_GE(gap, LayoutSettings::MIN_BLOCK_SEP_SETTING - 1e-9);
            }
        }
        for (const auto& [node, layout] : g.getNodeLayout()) {
            const double half = node->getWidth() / 2.0;
            for (const auto* ports : { &layout.source_ports, &layout.target_ports })
                for (const Port& p : *ports) {
                    EXPECT_GE(p.x, layout.x - half - 1e-9);
                    EXPECT_LE(p.x, layout.x + half + 1e-9);
                }
        }
    }

} // namespace hypergraph_logic::layout_settings_tests
