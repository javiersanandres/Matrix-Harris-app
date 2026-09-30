#include "GraphicalHypergraph.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <stdexcept>

// ============================================================================
// Line style of a connection (Hyperedge::isContinuous / setContinuous)
//
// Continuous by default; a property of the original hyperedge only (segments
// follow it); saved and cloned; and kept by the operations that rebuild a
// connection out of an existing one.
// ============================================================================

namespace hypergraph_logic {
    namespace line_style_tests {

        HyperedgePtr edgeBetween(const GraphicalHypergraph& g, const NodePtr& s, const NodePtr& t) {
            for (const auto& e : g.getAllHyperedges())
                if (!e->isSegment() && e->containsSource(s) && e->containsTarget(t)) return e;
            return nullptr;
        }

        NodePtr byName(const GraphicalHypergraph& g, const std::string& name) {
            for (const auto& n : g.getAllNodes())
                if (!n->isDummy() && n->getName() == name) return n;
            return nullptr;
        }

        TEST(LineStyle, ConnectionsAreContinuousByDefault) {
            GraphicalHypergraph g("default");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            ASSERT_NE(edgeBetween(g, a, b), nullptr);
            EXPECT_TRUE(edgeBetween(g, a, b)->isContinuous());
        }

        TEST(LineStyle, SegmentsFollowTheirConnectionAndCannotBeSet) {
            // a -> c spans two layers, so it is split into segments.
            GraphicalHypergraph g("segments");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            auto c = g.createNode("c", 2, 0, b);
            auto x = g.createNode("x", 0, 1, nullptr);
            g.addConnection(x, c);
            auto long_edge = edgeBetween(g, x, c);
            ASSERT_NE(long_edge, nullptr);

            long_edge->setContinuous(false);
            int segments = 0;
            for (const auto& e : g.getAllHyperedges()) {
                if (!e->isSegment() || e->getOrigin().lock() != long_edge) continue;
                ++segments;
                EXPECT_FALSE(e->isContinuous());
                EXPECT_THROW(e->setContinuous(true), std::logic_error);
            }
            EXPECT_GT(segments, 0);
        }

        TEST(LineStyle, SavedAndLoaded) {
            GraphicalHypergraph g("saved");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            auto c = g.createNode("c", 1, 1, a);
            g.addConnection(b, c); // a second connection, left continuous
            edgeBetween(g, a, b)->setContinuous(false);
            g.computeLayout();

            nlohmann::json j;
            g.toJSON(j);
            GraphicalHypergraph loaded = GraphicalHypergraph::fromJSON(j);
            EXPECT_FALSE(edgeBetween(loaded, byName(loaded, "a"), byName(loaded, "b"))->isContinuous());
            EXPECT_TRUE(edgeBetween(loaded, byName(loaded, "b"), byName(loaded, "c"))->isContinuous());
        }

        TEST(LineStyle, FilesWithoutTheFieldLoadContinuous) {
            GraphicalHypergraph g("old_file");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            edgeBetween(g, a, b)->setContinuous(false);
            g.computeLayout();

            nlohmann::json j;
            g.toJSON(j);
            for (auto& e : j.at("edges")) { // as saved before line styles existed
                e.erase("continuous");
                e.erase("uncertain_sources");
                e.erase("uncertain_targets");
            }
            GraphicalHypergraph loaded = GraphicalHypergraph::fromJSON(j);
            EXPECT_TRUE(edgeBetween(loaded, byName(loaded, "a"), byName(loaded, "b"))->isContinuous());
        }

        TEST(LineStyle, CloneKeepsIt) {
            GraphicalHypergraph g("clone");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            edgeBetween(g, a, b)->setContinuous(false);
            g.computeLayout();

            GraphicalHypergraph copy = g.clone();
            EXPECT_FALSE(edgeBetween(copy, byName(copy, "a"), byName(copy, "b"))->isContinuous());
        }

        TEST(LineStyle, BoxInsertedIntoAConnectionKeepsItsStyle) {
            GraphicalHypergraph g("insert");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            auto edge = edgeBetween(g, a, b);
            edge->setContinuous(false);

            auto m = g.createNodeInEdge(NodeAttributes("m"), edge);
            ASSERT_NE(edgeBetween(g, a, m), nullptr);
            ASSERT_NE(edgeBetween(g, m, b), nullptr);
            EXPECT_FALSE(edgeBetween(g, a, m)->isContinuous());
            EXPECT_FALSE(edgeBetween(g, m, b)->isContinuous());
        }

        TEST(LineStyle, WhatRemainsOfAConnectionKeepsItsStyle) {
            // {a, x} -> b, dashed; removing a -> b leaves x -> b, still dashed.
            GraphicalHypergraph g("remove");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto x = g.createNode("x", 0, 1, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            g.addSourceToEdge(edgeBetween(g, a, b), x);
            edgeBetween(g, a, b)->setContinuous(false);

            g.removeConnection(a, b);
            ASSERT_NE(edgeBetween(g, x, b), nullptr);
            EXPECT_FALSE(edgeBetween(g, x, b)->isContinuous());
        }

        // ── Uncertain ends ("conexión dudosa") ────────────────────────────────

        TEST(UncertainEnds, MarkedPerEndAndClearedByAWholeLineStyle) {
            // a -> {b, c}
            GraphicalHypergraph g("marks");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            auto c = g.createNode("c", 1, 1, nullptr);
            auto x = g.createNode("x", 1, 2, nullptr);
            auto e = edgeBetween(g, a, b);
            g.addTargetToEdge(e, c);
            EXPECT_FALSE(e->hasUncertainEnds());
            EXPECT_TRUE(e->isContinuous());

            e->setTargetUncertain(b, true);
            EXPECT_TRUE(e->isTargetUncertain(b.get()));
            EXPECT_FALSE(e->isTargetUncertain(c.get()));
            EXPECT_FALSE(e->isSourceUncertain(a.get()));
            EXPECT_FALSE(e->isContinuous());     // partly dashed...
            EXPECT_FALSE(e->allEndsUncertain()); // ...not as a whole

            EXPECT_THROW(e->setSourceUncertain(x, true), std::invalid_argument); // x is not in it
            EXPECT_THROW(e->setTargetUncertain(a, true), std::invalid_argument); // a is a source

            e->setContinuous(true); // a decision for the whole connection wins
            EXPECT_FALSE(e->hasUncertainEnds());
            e->setContinuous(false);
            EXPECT_TRUE(e->allEndsUncertain());
        }

        TEST(UncertainEnds, DoubtingTheOnlyEndOnASideDoubtsTheWhole) {
            // a -> {b, c}: a is the only source, so doubting it doubts every end;
            // clearing it clears them all.
            GraphicalHypergraph g("lone");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            auto c = g.createNode("c", 1, 1, nullptr);
            auto e = edgeBetween(g, a, b);
            g.addTargetToEdge(e, c);
            e->setTargetUncertain(c, true);

            e->setSourceUncertain(a, true);
            EXPECT_TRUE(e->allEndsUncertain());
            e->setSourceUncertain(a, false);
            EXPECT_FALSE(e->hasUncertainEnds());
        }

        TEST(UncertainEnds, SegmentsAnswerForTheirConnectionAndCannotBeMarked) {
            GraphicalHypergraph g("segments");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            auto c = g.createNode("c", 2, 0, b);
            auto x = g.createNode("x", 0, 1, nullptr);
            g.addConnection(x, c);
            auto long_edge = edgeBetween(g, x, c);
            long_edge->setTargetUncertain(c, true);

            int segments = 0;
            for (const auto& e : g.getAllHyperedges()) {
                if (!e->isSegment() || e->getOrigin().lock() != long_edge) continue;
                ++segments;
                EXPECT_TRUE(e->isTargetUncertain(c.get()));
                EXPECT_THROW(e->setTargetUncertain(c, false), std::logic_error);
            }
            EXPECT_GT(segments, 0);
        }

        TEST(UncertainEnds, PortsCarryTheMarkOnRealNodesOnly) {
            GraphicalHypergraph g("ports");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            auto c = g.createNode("c", 2, 0, b);
            auto x = g.createNode("x", 0, 1, nullptr);
            g.addConnection(x, c); // long: goes through a dummy at layer 1
            auto long_edge = edgeBetween(g, x, c);
            long_edge->setSourceUncertain(x, true);
            g.computeLayout();

            int uncertain_ports = 0;
            for (const auto& [node, layout] : g.getNodeLayout()) {
                for (const auto& p : layout.source_ports) {
                    if (node->isDummy()) EXPECT_FALSE(p.uncertain);
                    uncertain_ports += p.uncertain;
                }
                for (const auto& p : layout.target_ports) {
                    if (node->isDummy()) EXPECT_FALSE(p.uncertain);
                    uncertain_ports += p.uncertain;
                }
            }
            EXPECT_EQ(uncertain_ports, 2); // x is its only source: both real ends are doubted

            // Clearing the mark reaches the ports without relaying out.
            long_edge->setSourceUncertain(x, false);
            g.refreshUncertainPorts();
            for (const auto& [node, layout] : g.getNodeLayout())
                for (const auto& p : layout.source_ports) EXPECT_FALSE(p.uncertain);
        }

        TEST(UncertainEnds, SavedLoadedAndCloned) {
            // a -> {b, c} with only b doubted: it must come back partly dashed.
            GraphicalHypergraph g("saved");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            auto c = g.createNode("c", 1, 1, nullptr);
            g.addTargetToEdge(edgeBetween(g, a, b), c);
            edgeBetween(g, a, b)->setTargetUncertain(b, true);
            g.computeLayout();

            nlohmann::json j;
            g.toJSON(j);
            GraphicalHypergraph loaded = GraphicalHypergraph::fromJSON(j);
            auto le = edgeBetween(loaded, byName(loaded, "a"), byName(loaded, "b"));
            EXPECT_TRUE(le->isTargetUncertain(byName(loaded, "b").get()));
            EXPECT_FALSE(le->isTargetUncertain(byName(loaded, "c").get()));
            EXPECT_FALSE(le->isSourceUncertain(byName(loaded, "a").get()));
            bool port_marked = false;
            for (const auto& p : loaded.getNodeLayout().at(byName(loaded, "b").get()).target_ports)
                port_marked = port_marked || p.uncertain;
            EXPECT_TRUE(port_marked);

            GraphicalHypergraph copy = g.clone();
            auto ce = edgeBetween(copy, byName(copy, "a"), byName(copy, "b"));
            EXPECT_TRUE(ce->isTargetUncertain(byName(copy, "b").get()));
            EXPECT_FALSE(ce->isTargetUncertain(byName(copy, "c").get()));
            EXPECT_FALSE(ce->isSourceUncertain(byName(copy, "a").get()));
        }

        TEST(UncertainEnds, ANodeLeavingTheConnectionLosesItsMark) {
            // {a, x} -> b with x doubted; removing x from it drops x's mark.
            GraphicalHypergraph g("leave");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto x = g.createNode("x", 0, 1, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            auto e = edgeBetween(g, a, b);
            g.addSourceToEdge(e, x);
            e->setSourceUncertain(x, true);

            g.removeSourcesFromHyperedge(e, { x.get() }, true);
            EXPECT_FALSE(e->isSourceUncertain(x.get()));
            EXPECT_FALSE(e->hasUncertainEnds());
        }

        TEST(UncertainEnds, BoxInsertedIntoAConnectionKeepsTheMarksOfItsEnds) {
            // a -> {b, c} with b doubted; m inserted: a -> m -> {b, c}.
            GraphicalHypergraph g("insert");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            auto c = g.createNode("c", 1, 1, nullptr);
            auto edge = edgeBetween(g, a, b);
            g.addTargetToEdge(edge, c);
            edge->setTargetUncertain(b, true);

            auto m = g.createNodeInEdge(NodeAttributes("m"), edge);
            EXPECT_TRUE(edgeBetween(g, m, b)->isTargetUncertain(b.get()));
            EXPECT_FALSE(edgeBetween(g, m, c)->isTargetUncertain(c.get()));
            EXPECT_FALSE(edgeBetween(g, a, m)->hasUncertainEnds());
        }

    } // namespace line_style_tests
} // namespace hypergraph_logic
