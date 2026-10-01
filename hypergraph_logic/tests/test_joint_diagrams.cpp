#include "JointGraphicalHypergraph.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

// ============================================================================
// Diagrams inside the joint
//
// Each box knows the diagram(s) it came from; connected components and the
// regions they occupy are derived from the structure and layout. A diagram
// that is not mixed with another (no connection or fusion between them) can be
// removed or moved; new and moved diagrams are placed by the clicked x among
// the occupied regions.
// ============================================================================

namespace hypergraph_logic {
    namespace joint_diagrams_tests {

        constexpr double kFarLeft = -std::numeric_limits<double>::infinity();
        constexpr double kFarRight = std::numeric_limits<double>::infinity();

        // p -> c (two layers), boxes named <prefix>1 and <prefix>2.
        GraphicalHypergraph chain(const std::string& prefix) {
            GraphicalHypergraph g(prefix);
            auto a = g.createNode(prefix + "1", 0, 0, nullptr);
            g.createNode(prefix + "2", 1, 0, a);
            g.computeLayout();
            return g;
        }

        NodePtr named(const GraphicalHypergraph& g, const std::string& name) {
            for (const auto& n : g.getAllNodes())
                if (!n->isDummy() && n->getName() == name) return n;
            return nullptr;
        }

        double xOf(const GraphicalHypergraph& g, const std::string& name) {
            return g.getNodeLayout().at(named(g, name).get()).x;
        }

        // Names of the real boxes of a layer, left to right.
        std::vector<std::string> layerNames(const GraphicalHypergraph& g, int layer) {
            std::vector<std::string> names;
            for (const auto& n : g.getNodesAt(layer))
                if (!n->isDummy()) names.push_back(n->getName());
            return names;
        }

        HyperedgePtr edgeBetween(const GraphicalHypergraph& g, const NodePtr& s, const NodePtr& t) {
            for (const auto& e : g.getAllHyperedges())
                if (!e->isSegment() && e->containsSource(s) && e->containsTarget(t)) return e;
            return nullptr;
        }

        // ── Components and regions ──────────────────────────────────────────────

        TEST(JointDiagrams, EachDiagramIsItsOwnComponentAndRegion) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a"), b = chain("b");
            j->addHypergraph(a, kFarLeft);
            j->addHypergraph(b, kFarRight);

            const auto components = j->getComponents();
            ASSERT_EQ(components.size(), 2u);
            for (const auto& c : components) {
                ASSERT_EQ(c.graph_ids.size(), 1u);
                EXPECT_LT(c.left, c.right);
            }
            const auto regions = j->getOccupiedRegions();
            ASSERT_EQ(regions.size(), 2u);
            EXPECT_LT(regions[0].second, regions[1].first); // disjoint, left to right

            EXPECT_EQ(j->graphsOf(named(*j, "a1").get()), std::set<std::string>{ a.getId() });
            EXPECT_EQ(j->getIncorporatedName(b.getId()), "b");
            EXPECT_TRUE(j->isSeparable(a.getId()));
            EXPECT_TRUE(j->isSeparable(b.getId()));
        }

        // ── Placement ───────────────────────────────────────────────────────────

        TEST(JointDiagrams, ADiagramClickedBetweenTwoRegionsGoesBetweenThem) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a"), c = chain("c"), b = chain("b");
            j->addHypergraph(a, kFarLeft);
            j->addHypergraph(c, kFarRight);
            const auto regions = j->getOccupiedRegions();
            ASSERT_EQ(regions.size(), 2u);

            j->addHypergraph(b, (regions[0].second + regions[1].first) / 2.0);
            EXPECT_EQ(layerNames(*j, 0), (std::vector<std::string>{ "a1", "b1", "c1" }));
            EXPECT_EQ(layerNames(*j, 1), (std::vector<std::string>{ "a2", "b2", "c2" }));
        }

        TEST(JointDiagrams, AClickInsideARegionGoesToTheNearerSideOfIt) {
            for (bool left_half : { true, false }) {
                auto j = JointGraphicalHypergraph::create("j");
                auto a = chain("a"), b = chain("b");
                j->addHypergraph(a, 0.0);
                const auto region = j->getOccupiedRegions().at(0);
                const double third = (region.second - region.first) / 3.0;
                j->addHypergraph(b, left_half ? region.first + third : region.second - third);

                const std::vector<std::string> expected = left_half
                    ? std::vector<std::string>{ "b1", "a1" } : std::vector<std::string>{ "a1", "b1" };
                EXPECT_EQ(layerNames(*j, 0), expected) << (left_half ? "left half" : "right half");
            }
        }

        TEST(JointDiagrams, TheExactMiddleOfARegionGoesRight) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a"), b = chain("b");
            j->addHypergraph(a, 0.0);
            const auto region = j->getOccupiedRegions().at(0);
            j->addHypergraph(b, (region.first + region.second) / 2.0);
            EXPECT_EQ(layerNames(*j, 0), (std::vector<std::string>{ "a1", "b1" }));
        }

        TEST(JointDiagrams, ADeeperDiagramGetsItsOwnLayers) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a");
            GraphicalHypergraph deep("d");
            auto d1 = deep.createNode("d1", 0, 0, nullptr);
            auto d2 = deep.createNode("d2", 1, 0, d1);
            deep.createNode("d3", 2, 0, d2);
            j->addHypergraph(a, kFarLeft);
            j->addHypergraph(deep, kFarRight);
            EXPECT_EQ(j->getLayerCount(), 3);
            EXPECT_EQ(layerNames(*j, 2), (std::vector<std::string>{ "d3" }));
        }

        // ── Mixing ──────────────────────────────────────────────────────────────

        TEST(JointDiagrams, AConnectionMixesTwoDiagramsAndRemovingItUnmixesThem) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a"), b = chain("b");
            j->addHypergraph(a, kFarLeft);
            j->addHypergraph(b, kFarRight);

            j->addConnection(named(*j, "a1"), named(*j, "b2"));
            j->computeLayout();
            EXPECT_FALSE(j->isSeparable(a.getId()));
            EXPECT_FALSE(j->isSeparable(b.getId()));
            EXPECT_EQ(j->getComponents().size(), 1u);
            EXPECT_THROW(j->removeHypergraph(a.getId()), std::logic_error);
            EXPECT_THROW(j->moveHypergraph(b.getId(), kFarLeft), std::logic_error);

            j->removeConnection(named(*j, "a1"), named(*j, "b2"));
            j->computeLayout();
            EXPECT_TRUE(j->isSeparable(a.getId()));
            EXPECT_TRUE(j->isSeparable(b.getId()));
        }

        TEST(JointDiagrams, AFusedBoxBelongsToBothDiagrams) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a"), b = chain("b");
            j->addHypergraph(a, kFarLeft);
            j->addHypergraph(b, kFarRight);

            j->fuseNodes(named(*j, "a2"), named(*j, "b2"), NodeAttributes("ab"));
            j->computeLayout();
            const NodePtr fused = named(*j, "ab");
            ASSERT_NE(fused, nullptr);
            EXPECT_EQ(j->graphsOf(fused.get()), (std::set<std::string>{ a.getId(), b.getId() }));
            EXPECT_FALSE(j->isSeparable(a.getId()));
            EXPECT_FALSE(j->isSeparable(b.getId()));
        }

        TEST(JointDiagrams, UnknownDiagramsAreNotSeparable) {
            auto j = JointGraphicalHypergraph::create("j");
            EXPECT_FALSE(j->isSeparable("not-there"));
            EXPECT_THROW(j->removeHypergraph("not-there"), std::invalid_argument);
        }

        // ── Removing ────────────────────────────────────────────────────────────

        TEST(JointDiagrams, RemovingADiagramTakesAllOfItAndLetsItBeAddedAgain) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a");
            GraphicalHypergraph deep("d");
            auto d1 = deep.createNode("d1", 0, 0, nullptr);
            auto d2 = deep.createNode("d2", 1, 0, d1);
            auto d3 = deep.createNode("d3", 2, 0, d2);
            auto e1 = deep.createNode("e1", 0, 1, nullptr);
            deep.addConnection(e1, d3); // long edge from layer 0 to 2: a dummy in layer 1
            j->addHypergraph(a, kFarLeft);
            j->addHypergraph(deep, kFarRight);
            ASSERT_EQ(j->getLayerCount(), 3);

            j->removeHypergraph(deep.getId());
            EXPECT_EQ(j->getAllNodes().size(), 2u); // a1, a2: no dummies left behind
            EXPECT_EQ(j->getLayerCount(), 2);       // the emptied layer is gone
            EXPECT_EQ(j->getIncorporatedIds().count(deep.getId()), 0u);
            for (const auto& e : j->getAllHyperedges())
                for (const auto& s : e->getSources()) EXPECT_EQ(s->getName().front(), 'a');

            EXPECT_NO_THROW(j->addHypergraph(deep, kFarRight));
            EXPECT_EQ(j->getLayerCount(), 3);
        }

        TEST(JointDiagrams, RemovingAMixedBlockTakesEveryDiagramInIt) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a"), b = chain("b"), c = chain("c");
            j->addHypergraph(a, kFarRight);
            j->addHypergraph(b, kFarRight);
            j->addHypergraph(c, kFarRight);
            j->addConnection(named(*j, "a1"), named(*j, "b2")); // a and b: one block
            j->computeLayout();

            j->removeComponent(named(*j, "b1").get());
            EXPECT_EQ(layerNames(*j, 0), (std::vector<std::string>{ "c1" }));
            EXPECT_EQ(layerNames(*j, 1), (std::vector<std::string>{ "c2" }));
            // Neither a nor b has a box left: both can be added again; c stays.
            EXPECT_EQ(j->getIncorporatedIds(), (std::unordered_set<std::string>{ c.getId() }));
            EXPECT_NO_THROW(j->addHypergraph(a, kFarLeft));
            EXPECT_TRUE(j->isSeparable(c.getId()));
        }

        TEST(JointDiagrams, RemovingOneBlockOfADiagramKeepsTheRestOfIt) {
            // One diagram made of two separate chains: two blocks.
            auto j = JointGraphicalHypergraph::create("j");
            GraphicalHypergraph g("two");
            auto x1 = g.createNode("x1", 0, 0, nullptr);
            g.createNode("x2", 1, 0, x1);
            auto y1 = g.createNode("y1", 0, 1, nullptr);
            g.createNode("y2", 1, 1, y1);
            j->addHypergraph(g, kFarLeft);
            ASSERT_EQ(j->getComponents().size(), 2u);

            j->removeComponent(named(*j, "x2").get());
            EXPECT_EQ(layerNames(*j, 0), (std::vector<std::string>{ "y1" }));
            EXPECT_EQ(j->getIncorporatedIds().count(g.getId()), 1u); // y1, y2 are still its
            EXPECT_TRUE(j->isSeparable(g.getId()));
            EXPECT_THROW(j->addHypergraph(g, kFarRight), std::invalid_argument);
        }

        TEST(JointDiagrams, RemovingTheBlockOfABoxOutsideTheJointIsRefused) {
            auto j = JointGraphicalHypergraph::create("j");
            Node stranger(NodeAttributes("x"));
            EXPECT_THROW(j->removeComponent(&stranger), std::invalid_argument);
        }

        // ── Moving ──────────────────────────────────────────────────────────────

        TEST(JointDiagrams, MovingADiagramPlacesItAmongTheOthers) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a"), b = chain("b"), c = chain("c");
            j->addHypergraph(a, kFarRight);
            j->addHypergraph(b, kFarRight);
            j->addHypergraph(c, kFarRight);

            j->moveHypergraph(a.getId(), kFarRight);
            EXPECT_EQ(layerNames(*j, 0), (std::vector<std::string>{ "b1", "c1", "a1" }));
            EXPECT_EQ(layerNames(*j, 1), (std::vector<std::string>{ "b2", "c2", "a2" }));

            // Back between b and c: clicked in the gap between their regions.
            const double gap = (xOf(*j, "b1") + xOf(*j, "c1")) / 2.0;
            j->moveHypergraph(a.getId(), gap);
            EXPECT_EQ(layerNames(*j, 0), (std::vector<std::string>{ "b1", "a1", "c1" }));
        }

        // ── Moving up and down ──────────────────────────────────────────────────

        int layerOf(const GraphicalHypergraph& g, const std::string& name) {
            return named(g, name)->getLayer();
        }

        TEST(JointDiagrams, ADiagramCanBeMadeToStartAtADeeperLayer) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a"), b = chain("b");
            j->addHypergraph(a, kFarLeft);
            j->addHypergraph(b, kFarRight);

            j->moveHypergraphToLayer(b.getId(), 1);
            EXPECT_EQ(layerOf(*j, "b1"), 1);
            EXPECT_EQ(layerOf(*j, "b2"), 2);
            EXPECT_EQ(layerOf(*j, "a1"), 0);
            EXPECT_EQ(j->getLayerCount(), 3);
            // It keeps its horizontal place: still right of a.
            EXPECT_GT(xOf(*j, "b1"), xOf(*j, "a1"));

            // The root keeps its new layer through later operations.
            EXPECT_EQ(named(*j, "b1")->getDesiredLayer(), 1);
            j->removeConnection(named(*j, "b1"), named(*j, "b2"));
            EXPECT_EQ(layerOf(*j, "b1"), 1);
        }

        TEST(JointDiagrams, ANegativeLayerOpensLayersAboveEverything) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a"), b = chain("b");
            j->addHypergraph(a, kFarLeft);
            j->addHypergraph(b, kFarRight);

            j->moveHypergraphToLayer(a.getId(), -1);
            EXPECT_EQ(layerOf(*j, "a1"), 0);
            EXPECT_EQ(layerOf(*j, "b1"), 1); // pushed down by the new layer
            EXPECT_EQ(named(*j, "b1")->getDesiredLayer(), 1);
            EXPECT_EQ(j->getLayerCount(), 3);
        }

        TEST(JointDiagrams, PastTheDeepestLayerGoesRightBelowEverything) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a"), b = chain("b");
            j->addHypergraph(a, kFarLeft);
            j->addHypergraph(b, kFarRight);

            j->moveHypergraphToLayer(a.getId(), 10);
            EXPECT_EQ(layerOf(*j, "a1"), 2); // empty layers in between are closed up
            EXPECT_EQ(layerOf(*j, "a2"), 3);
            EXPECT_EQ(j->getLayerCount(), 4);
        }

        TEST(JointDiagrams, MovingToTheLayerItAlreadyStartsAtIsRefused) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a");
            j->addHypergraph(a, kFarLeft);
            EXPECT_THROW(j->moveHypergraphToLayer(a.getId(), 0), std::invalid_argument);
        }

        TEST(JointDiagrams, MovingAcrossAndDownAtOnce) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a"), b = chain("b");
            j->addHypergraph(a, kFarLeft);
            j->addHypergraph(b, kFarRight);

            j->moveHypergraph(b.getId(), kFarLeft, 1);
            EXPECT_EQ(layerOf(*j, "b1"), 1);
            EXPECT_EQ(layerNames(*j, 1), (std::vector<std::string>{ "b1", "a2" }));
        }

        // ── Moving connected components ─────────────────────────────────────────

        TEST(JointDiagrams, AMixedComponentMovesAsAWhole) {
            auto j = JointGraphicalHypergraph::create("j");
            auto a = chain("a"), b = chain("b"), c = chain("c");
            j->addHypergraph(a, kFarRight);
            j->addHypergraph(b, kFarRight);
            j->addHypergraph(c, kFarRight);
            j->addConnection(named(*j, "a1"), named(*j, "b2")); // a and b: one component
            j->computeLayout();
            ASSERT_FALSE(j->isSeparable(a.getId()));

            j->moveComponentToLayer(named(*j, "b1").get(), 1);
            EXPECT_EQ(layerOf(*j, "a1"), 1);
            EXPECT_EQ(layerOf(*j, "b1"), 1);
            EXPECT_EQ(layerOf(*j, "b2"), 2);
            EXPECT_EQ(layerOf(*j, "c1"), 0);

            j->moveComponent(named(*j, "c1").get(), kFarRight);
            j->moveComponent(named(*j, "a1").get(), kFarRight);
            EXPECT_EQ(layerNames(*j, 0), (std::vector<std::string>{ "c1" }));
            EXPECT_EQ(layerNames(*j, 1), (std::vector<std::string>{ "c2", "a1", "b1" }));
        }

        TEST(JointDiagrams, ABoxOutsideTheJointNamesNoComponent) {
            auto j = JointGraphicalHypergraph::create("j");
            Node stranger(NodeAttributes("x"));
            EXPECT_THROW(j->moveComponent(&stranger, 0.0), std::invalid_argument);
        }

        // ── Copies and files ────────────────────────────────────────────────────

        TEST(JointDiagrams, OriginsSurviveCloneAndFile) {
            nlohmann::json saved;
            std::string a_id, b_id;
            {
                auto j = JointGraphicalHypergraph::create("j");
                auto a = chain("a"), b = chain("b");
                a_id = a.getId(); b_id = b.getId();
                j->addHypergraph(a, kFarLeft);
                j->addHypergraph(b, kFarRight);
                j->fuseNodes(named(*j, "a2"), named(*j, "b2"), NodeAttributes("ab"));
                j->computeLayout();

                auto copy = j->cloneJoint();
                EXPECT_EQ(copy->graphsOf(named(*copy, "ab").get()), (std::set<std::string>{ a_id, b_id }));
                EXPECT_EQ(copy->graphsOf(named(*copy, "a1").get()), std::set<std::string>{ a_id });
                EXPECT_EQ(copy->getIncorporatedName(a_id), "a");
                j->toJSON(saved);
            }
            auto loaded = JointGraphicalHypergraph::fromJSON(saved);
            EXPECT_EQ(loaded->graphsOf(named(*loaded, "ab").get()), (std::set<std::string>{ a_id, b_id }));
            EXPECT_EQ(loaded->graphsOf(named(*loaded, "b1").get()), std::set<std::string>{ b_id });
            EXPECT_EQ(loaded->getIncorporatedName(b_id), "b");
            EXPECT_FALSE(loaded->isSeparable(a_id));
        }

        TEST(JointDiagrams, FilesWithoutOriginsHaveNoSeparableDiagrams) {
            nlohmann::json saved;
            std::string a_id;
            {
                auto j = JointGraphicalHypergraph::create("j");
                auto a = chain("a");
                a_id = a.getId();
                j->addHypergraph(a, kFarLeft);
                j->toJSON(saved);
            }
            saved.erase("node_graphs");
            saved.erase("incorporated_names");
            auto loaded = JointGraphicalHypergraph::fromJSON(saved);
            EXPECT_FALSE(loaded->isSeparable(a_id));
            EXPECT_THROW(loaded->removeHypergraph(a_id), std::logic_error);
            EXPECT_EQ(loaded->graphsOf(named(*loaded, "a1").get()), std::set<std::string>{ "" });
        }

    } // namespace joint_diagrams_tests
} // namespace hypergraph_logic
