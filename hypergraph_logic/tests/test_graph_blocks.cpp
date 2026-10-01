#include "GraphicalHypergraph.h"
#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// ============================================================================
// Blocks of a regular diagram
//
// A block is a connected component: it can be moved sideways among the other
// blocks, up or down to start at another layer, or removed as a whole.
// ============================================================================

namespace hypergraph_logic {
    namespace graph_blocks_tests {

        constexpr double kFarLeft = -std::numeric_limits<double>::infinity();
        constexpr double kFarRight = std::numeric_limits<double>::infinity();

        NodePtr named(const GraphicalHypergraph& g, const std::string& name) {
            for (const auto& n : g.getAllNodes())
                if (!n->isDummy() && n->getName() == name) return n;
            return nullptr;
        }

        int layerOf(const GraphicalHypergraph& g, const std::string& name) {
            return named(g, name)->getLayer();
        }

        // Names of the real boxes of a layer, left to right.
        std::vector<std::string> layerNames(const GraphicalHypergraph& g, int layer) {
            std::vector<std::string> names;
            for (const auto& n : g.getNodesAt(layer))
                if (!n->isDummy()) names.push_back(n->getName());
            return names;
        }

        // Two blocks side by side: p1 -> {p2, p3} on the left, q1 -> q2 on the right.
        GraphicalHypergraph twoBlocks() {
            GraphicalHypergraph g("g");
            auto p1 = g.createNode("p1", 0, 0, nullptr);
            g.createNode("p2", 1, 0, p1);
            g.createNode("p3", 1, 1, p1);
            auto q1 = g.createNode("q1", 0, 1, nullptr);
            g.createNode("q2", 1, 2, q1);
            g.computeLayout();
            return g;
        }

        TEST(GraphBlocks, BlocksAreTheConnectedComponents) {
            auto g = twoBlocks();
            ASSERT_EQ(layerNames(g, 0), (std::vector<std::string>{ "p1", "q1" }));
            const auto blocks = g.getBlocks();
            ASSERT_EQ(blocks.size(), 2u);
            const auto regions = g.getOccupiedRegions();
            ASSERT_EQ(regions.size(), 2u);
            EXPECT_LT(regions[0].second, regions[1].first); // disjoint, left to right
            EXPECT_EQ(g.getComponentNodes(named(g, "q2").get()).size(), 2u);
        }

        TEST(GraphBlocks, MovingABlockSidewaysPlacesItAmongTheOthers) {
            auto g = twoBlocks();
            g.moveComponent(named(g, "q2").get(), kFarLeft);
            EXPECT_EQ(layerNames(g, 0), (std::vector<std::string>{ "q1", "p1" }));
            EXPECT_EQ(layerNames(g, 1), (std::vector<std::string>{ "q2", "p2", "p3" }));
        }

        TEST(GraphBlocks, MovingABlockDownKeepsItsShapeAndItsNewLayers) {
            auto g = twoBlocks();
            g.moveComponentToLayer(named(g, "q1").get(), 1);
            EXPECT_EQ(layerOf(g, "q1"), 1);
            EXPECT_EQ(layerOf(g, "q2"), 2);
            EXPECT_EQ(layerOf(g, "p1"), 0);

            // A later change elsewhere does not pull the moved root back up.
            g.removeNode(named(g, "p3"));
            EXPECT_EQ(layerOf(g, "q1"), 1);
            EXPECT_EQ(layerOf(g, "q2"), 2);
        }

        TEST(GraphBlocks, MovingAcrossAndDownAtOnce) {
            auto g = twoBlocks();
            g.moveComponent(named(g, "q1").get(), kFarLeft, 1);
            EXPECT_EQ(layerNames(g, 1), (std::vector<std::string>{ "q1", "p2", "p3" }));
            EXPECT_EQ(layerNames(g, 2), (std::vector<std::string>{ "q2" }));
        }

        TEST(GraphBlocks, MovingToTheLayerItAlreadyStartsAtIsRefused) {
            auto g = twoBlocks();
            EXPECT_THROW(g.moveComponentToLayer(named(g, "q1").get(), 0), std::invalid_argument);
        }

        TEST(GraphBlocks, RemovingABlockTakesOnlyItAndClosesEmptyLayers) {
            auto g = twoBlocks();
            g.moveComponentToLayer(named(g, "q1").get(), 1); // q now reaches layer 2
            ASSERT_EQ(g.getLayerCount(), 3);

            g.removeComponent(named(g, "q2").get());
            EXPECT_EQ(named(g, "q1"), nullptr);
            EXPECT_EQ(named(g, "q2"), nullptr);
            EXPECT_EQ(g.getAllNodes().size(), 3u);
            EXPECT_EQ(g.getLayerCount(), 2); // the emptied layer is gone
            EXPECT_EQ(g.getBlocks().size(), 1u);
            for (const auto& e : g.getAllHyperedges())
                for (const auto& s : e->getSources()) EXPECT_EQ(s->getName().front(), 'p');
        }

        TEST(GraphBlocks, ABoxOutsideTheGraphNamesNoBlock) {
            auto g = twoBlocks();
            Node stranger(NodeAttributes("x"));
            EXPECT_THROW(g.moveComponent(&stranger, 0.0), std::invalid_argument);
            EXPECT_THROW(g.removeComponent(&stranger), std::invalid_argument);
        }

    } // namespace graph_blocks_tests
} // namespace hypergraph_logic
