#include "GraphicalHypergraph.h"
#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// ============================================================================
// Copying and pasting blocks and diagrams
//
// copyOf takes a closed group of boxes (whole blocks) into a graph of its own,
// paste puts such a graph back into a diagram as a new block, placed like a
// moved one, and duplicate copies a whole diagram under a new identity.
// ============================================================================

namespace hypergraph_logic {
    namespace copy_paste_tests {

        constexpr double kFarRight = std::numeric_limits<double>::infinity();

        NodePtr named(const GraphicalHypergraph& g, const std::string& name) {
            for (const auto& n : g.getAllNodes())
                if (!n->isDummy() && n->getName() == name) return n;
            return nullptr;
        }

        std::vector<std::string> layerNames(const GraphicalHypergraph& g, int layer) {
            std::vector<std::string> names;
            for (const auto& n : g.getNodesAt(layer))
                if (!n->isDummy()) names.push_back(n->getName());
            return names;
        }

        int realBoxes(const GraphicalHypergraph& g) {
            int count = 0;
            for (const auto& n : g.getAllNodes()) count += !n->isDummy();
            return count;
        }

        HyperedgePtr edgeBetween(const GraphicalHypergraph& g, const std::string& s, const std::string& t) {
            for (const auto& e : g.getAllHyperedges())
                if (!e->isSegment() && e->containsSource(named(g, s)) && e->containsTarget(named(g, t))) return e;
            return nullptr;
        }

        // p1 -> {p2, p3} on the left; q1 -> q2 on the right, moved down a layer.
        GraphicalHypergraph twoBlocks() {
            GraphicalHypergraph g("g");
            auto p1 = g.createNode("p1", 0, 0, nullptr);
            g.createNode("p2", 1, 0, p1);
            g.createNode("p3", 1, 1, p1);
            auto q1 = g.createNode("q1", 0, 1, nullptr);
            g.createNode("q2", 1, 2, q1);
            g.computeLayout();
            g.moveComponentToLayer(q1.get(), 1);
            return g;
        }

        TEST(CopyPaste, ACopiedBlockHoldsOnlyItsBoxesFromTheFirstLayer) {
            auto g = twoBlocks();
            ASSERT_EQ(named(g, "q1")->getLayer(), 1);

            const auto copy = g.copyOf(g.getComponentNodes(named(g, "q1").get()));
            EXPECT_EQ(realBoxes(copy), 2);
            EXPECT_EQ(layerNames(copy, 0), (std::vector<std::string>{ "q1" }));
            EXPECT_EQ(layerNames(copy, 1), (std::vector<std::string>{ "q2" }));
            EXPECT_NE(edgeBetween(copy, "q1", "q2"), nullptr);
            EXPECT_NE(copy.getId(), g.getId());
            EXPECT_EQ(realBoxes(g), 5) << "the original is left alone";
        }

        TEST(CopyPaste, ACopyKeepsTheLineStyleAndTheBoxes) {
            auto g = twoBlocks();
            edgeBetween(g, "p1", "p2")->setContinuous(false);
            const auto copy = g.copyOf(g.getComponentNodes(named(g, "p1").get()));
            ASSERT_NE(edgeBetween(copy, "p1", "p2"), nullptr);
            EXPECT_TRUE(edgeBetween(copy, "p1", "p2")->allEndsUncertain());
            EXPECT_NE(named(copy, "p2"), named(g, "p2")) << "boxes of its own";
        }

        TEST(CopyPaste, AGroupLinkedToOtherBoxesCannotBeCopied) {
            auto g = twoBlocks();
            EXPECT_THROW(g.copyOf({ named(g, "p1").get() }), std::invalid_argument);
        }

        TEST(CopyPaste, PastingAddsANewBlockWhereItIsDropped) {
            auto g = twoBlocks();
            auto copy = g.copyOf(g.getComponentNodes(named(g, "p1").get()));
            const auto boxes = g.paste(std::move(copy), kFarRight, 1);

            EXPECT_EQ(boxes.size(), 3u);
            EXPECT_EQ(realBoxes(g), 8);
            EXPECT_EQ(g.getBlocks().size(), 3u);
            // Its first box starts in layer 1, right of everything.
            EXPECT_EQ(layerNames(g, 1).back(), "p1");
            EXPECT_EQ(layerNames(g, 2), (std::vector<std::string>{ "q2", "p2", "p3" }));
            for (const auto& n : g.getAllNodes())
                EXPECT_TRUE(g.getNodeLayout().count(n.get())) << "laid out";
        }

        TEST(CopyPaste, PastingAboveEverythingOpensALayer) {
            auto g = twoBlocks();
            auto copy = g.copyOf(g.getComponentNodes(named(g, "q1").get()));
            g.paste(std::move(copy), kFarRight, -1);
            EXPECT_EQ(layerNames(g, 0), (std::vector<std::string>{ "q1" }));
            EXPECT_EQ(named(g, "p1")->getLayer(), 1);
        }

        TEST(CopyPaste, ACopyReadBackFromJsonCanBePastedMoreThanOnce) {
            auto g = twoBlocks();
            json j;
            g.copyOf(g.getComponentNodes(named(g, "p1").get())).toJSON(j);

            g.paste(GraphicalHypergraph::fromJSON(j), kFarRight, 0);
            g.paste(GraphicalHypergraph::fromJSON(j), kFarRight, 0);
            EXPECT_EQ(realBoxes(g), 11);
            EXPECT_EQ(g.getBlocks().size(), 4u);
        }

        TEST(CopyPaste, PastingIntoAnEmptyDiagram) {
            auto g = twoBlocks();
            GraphicalHypergraph empty("empty");
            empty.paste(g.copyOf(g.getComponentNodes(named(g, "q1").get())), 0.0, 3);
            EXPECT_EQ(layerNames(empty, 0), (std::vector<std::string>{ "q1" }));
            EXPECT_EQ(layerNames(empty, 1), (std::vector<std::string>{ "q2" }));
        }

        TEST(CopyPaste, CopyingEverythingIsTheWholeDiagram) {
            auto g = twoBlocks();
            std::unordered_set<Node*> all;
            for (const auto& n : g.getAllNodes()) all.insert(n.get());
            const auto copy = g.copyOf(all);
            EXPECT_EQ(realBoxes(copy), 5);
            EXPECT_EQ(named(copy, "q1")->getLayer(), 1);
        }

        TEST(CopyPaste, ADuplicateIsTheSameDrawingUnderAnotherId) {
            auto g = twoBlocks();
            const auto dup = g.duplicate();
            EXPECT_NE(dup.getId(), g.getId());
            EXPECT_EQ(dup.getName(), g.getName());
            for (int layer = 0; layer < 3; ++layer)
                EXPECT_EQ(layerNames(dup, layer), layerNames(g, layer));
            EXPECT_DOUBLE_EQ(dup.getNodeLayout().at(named(dup, "q2").get()).x,
                             g.getNodeLayout().at(named(g, "q2").get()).x);
        }

    } // namespace copy_paste_tests
} // namespace hypergraph_logic
