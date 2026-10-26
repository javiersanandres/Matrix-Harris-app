#include "GraphicalHypergraph.h"
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

// ============================================================================
// Equivalences between boxes (Hypergraph::addEquivalence and the rules every
// other operation keeps for them): a chain shares one layer, its members sit
// side by side with nothing but dummies in between, and no cycle may run
// through it.
// ============================================================================

namespace hypergraph_logic {
    namespace equivalence_tests {

        using Graph = GraphicalHypergraph;

        NodePtr box(Graph& g, const char* name, int layer, const NodePtr& parent = nullptr) {
            return g.createNode(NodeAttributes(name), layer, -1, parent);
        }

        // A root appended at the end of the given layer.
        NodePtr boxAtEnd(Graph& g, const char* name, int layer) {
            return g.createNode(NodeAttributes(name), layer, 1000, nullptr);
        }

        // A child of parent appended at the end of the layer below it.
        NodePtr childAtEnd(Graph& g, const char* name, const NodePtr& parent) {
            return g.createNode(NodeAttributes(name), parent->getLayer() + 1, 1000, parent);
        }

        // The names of the real boxes of a layer, left to right.
        std::vector<std::string> row(const Graph& g, int layer) {
            std::vector<std::string> names;
            for (const auto& n : g.getNodesAt(layer))
                if (!n->isDummy()) names.push_back(n->getName());
            return names;
        }

        std::vector<std::string> chainOf(const Graph& g, const NodePtr& n) {
            std::vector<std::string> names;
            if (const EquivalenceChain* c = g.getEquivalenceChain(n.get()))
                for (const auto& m : c->members) names.push_back(m->getName());
            return names;
        }

        // Every chain: one layer, members in order, only dummies in between.
        void expectChainsTogether(const Graph& g) {
            for (const auto& chain : g.getEquivalences()) {
                ASSERT_GE(chain.members.size(), 2u);
                ASSERT_EQ(chain.continuous.size(), chain.members.size() - 1);
                const int layer = chain.members.front()->getLayer();
                const auto nodes = g.getNodesAt(layer);
                int previous = -1;
                for (const auto& m : chain.members) {
                    ASSERT_EQ(m->getLayer(), layer) << m->getName();
                    const int at = static_cast<int>(std::find(nodes.begin(), nodes.end(), m) - nodes.begin());
                    ASSERT_LT(at, static_cast<int>(nodes.size())) << m->getName();
                    if (previous >= 0) {
                        ASSERT_GT(at, previous) << m->getName();
                        for (int i = previous + 1; i < at; ++i)
                            EXPECT_TRUE(nodes[i]->isDummy()) << nodes[i]->getName() << " inside a chain";
                    }
                    previous = at;
                }
            }
        }

        // Every node at the layer the depth rule gives it, chains counted as one.
        void expectLayering(const Graph& g) {
            for (const auto& n : g.getAllNodes()) {
                for (const auto& p : n->getParents())
                    EXPECT_LT(p->getLayer(), n->getLayer()) << p->getName() << " -> " << n->getName();
                if (n->getDesiredLayer() != -1)
                    EXPECT_EQ(n->getDesiredLayer(), n->getLayer()) << n->getName();
            }
        }

        // ── Adding ────────────────────────────────────────────────────────────

        TEST(Equivalences, OnTheSameLayerTheRightOneComesNextToTheLeftOne) {
            Graph g("same layer");
            auto a = boxAtEnd(g, "a", 0);
            auto x = boxAtEnd(g, "x", 0);
            auto b = boxAtEnd(g, "b", 0);
            g.computeLayout();

            ASSERT_TRUE(g.canAddEquivalence(b, a));
            g.addEquivalence(b, a);
            EXPECT_EQ(row(g, 0), (std::vector<std::string>{ "a", "b", "x" }));
            EXPECT_EQ(chainOf(g, a), (std::vector<std::string>{ "a", "b" }));
            EXPECT_TRUE(g.isEquivalenceContinuous(a, b));
            expectChainsTogether(g);
        }

        TEST(Equivalences, BoxesWithOnlyDummiesBetweenThemStayWhereTheyAre) {
            Graph g("only dummies");
            auto q = box(g, "q", 0);
            auto m = childAtEnd(g, "m", q);
            auto c = childAtEnd(g, "c", m);
            auto p = boxAtEnd(g, "p", 0);
            auto a = childAtEnd(g, "a", p);
            g.addConnection(p, c); // long: a dummy at layer 1
            auto b = childAtEnd(g, "b", p);
            g.computeLayout();

            auto fullRow = [&] {
                std::vector<std::string> names;
                for (const auto& n : g.getNodesAt(1)) names.push_back(n->isDummy() ? "." : n->getName());
                return names;
            };
            const auto before = fullRow();
            ASSERT_EQ(before, (std::vector<std::string>{ "m", "a", ".", "b" }));

            g.addEquivalence(a, b);
            EXPECT_EQ(fullRow(), before);
            EXPECT_EQ(chainOf(g, a), (std::vector<std::string>{ "a", "b" }));
            expectChainsTogether(g);
        }

        TEST(Equivalences, TheMoverStopsAtTheDummiesNextToTheOtherEnd) {
            Graph g("minimum move");
            auto p = box(g, "p", 0);
            auto a = childAtEnd(g, "a", p);
            auto q = boxAtEnd(g, "q", 0);
            auto m = childAtEnd(g, "m", q);
            auto c = childAtEnd(g, "c", m);
            auto b = childAtEnd(g, "b", q);
            g.addConnection(p, c); // long: a dummy at layer 1
            g.computeLayout();

            auto fullRow = [&] {
                std::vector<std::string> names;
                for (const auto& n : g.getNodesAt(1)) names.push_back(n->isDummy() ? "." : n->getName());
                return names;
            };
            ASSERT_EQ(fullRow(), (std::vector<std::string>{ "a", ".", "m", "b" }));

            // b passes m, which takes its place, but not the dummy next to a.
            g.addEquivalence(a, b);
            EXPECT_EQ(fullRow(), (std::vector<std::string>{ "a", ".", "b", "m" }));
            expectChainsTogether(g);
        }

        TEST(Equivalences, TheShorterChainIsTheOneThatMoves) {
            Graph g("shorter");
            auto a = boxAtEnd(g, "a", 0);
            auto x = boxAtEnd(g, "x", 0);
            auto b = boxAtEnd(g, "b", 0);
            auto c = boxAtEnd(g, "c", 0);
            g.computeLayout();
            g.addEquivalence(b, c, false);
            g.computeLayout();

            g.addEquivalence(a, b);
            EXPECT_EQ(row(g, 0), (std::vector<std::string>{ "x", "a", "b", "c" }));
            EXPECT_EQ(chainOf(g, a), (std::vector<std::string>{ "a", "b", "c" }));
            EXPECT_TRUE(g.isEquivalenceContinuous(a, b));
            EXPECT_FALSE(g.isEquivalenceContinuous(b, c));
            expectChainsTogether(g);
        }

        TEST(Equivalences, TheShallowerBoxComesDownWithWhatHangsFromIt) {
            Graph g("down");
            auto a = box(g, "a", 0);
            auto a1 = box(g, "a1", 1, a);
            auto p = box(g, "p", 0);
            auto b = box(g, "b", 1, p);
            g.computeLayout();

            g.addEquivalence(a, b);
            EXPECT_EQ(a->getLayer(), 1);
            EXPECT_EQ(b->getLayer(), 1);
            EXPECT_EQ(a1->getLayer(), 2);
            expectChainsTogether(g);
            expectLayering(g);
        }

        TEST(Equivalences, DummiesAndConnectedBoxesAreRefused) {
            Graph g("refused");
            auto a = box(g, "a", 0);
            auto b = box(g, "b", 1, a);
            auto c = box(g, "c", 2, b);
            auto d = box(g, "d", 0);
            g.addConnection(d, c); // long: a dummy at layer 1
            g.computeLayout();

            EXPECT_FALSE(g.canAddEquivalence(a, b));
            EXPECT_THROW(g.addEquivalence(a, b), std::logic_error);
            EXPECT_FALSE(g.canAddEquivalence(a, c)); // through b
            EXPECT_FALSE(g.canAddEquivalence(a, a));
            NodePtr dummy;
            for (const auto& n : g.getNodesAt(1)) if (n->isDummy()) dummy = n;
            ASSERT_TRUE(dummy);
            EXPECT_FALSE(g.canAddEquivalence(b, dummy));
            EXPECT_THROW(g.addEquivalence(b, dummy), std::invalid_argument);
            EXPECT_TRUE(g.canAddEquivalence(b, d));
        }

        TEST(Equivalences, ACycleThroughAnotherChainIsRefused) {
            // x -> y, z -> w; y == z: then w == x would close x -> y == z -> w == x.
            Graph g("cycle");
            auto x = box(g, "x", 0);
            auto y = box(g, "y", 1, x);
            auto z = box(g, "z", 0);
            auto w = box(g, "w", 1, z);
            g.computeLayout();
            ASSERT_TRUE(g.canAddEquivalence(y, z));
            g.addEquivalence(y, z);
            EXPECT_EQ(w->getLayer(), 2);

            EXPECT_FALSE(g.canAddEquivalence(w, x));
            EXPECT_THROW(g.addEquivalence(w, x), std::logic_error);
            // Nor may a connection close it, or join two members.
            EXPECT_FALSE(g.canAddConnection(w, x));
            EXPECT_THROW(g.addConnection(w, x), std::logic_error);
            EXPECT_FALSE(g.canAddConnection(y, z));
            EXPECT_THROW(g.addConnection(y, z), std::logic_error);
            EXPECT_FALSE(g.canAddConnection(w, y));
            EXPECT_THROW(g.addConnection(w, y), std::logic_error);
            // A connection that only goes down is fine.
            EXPECT_TRUE(g.canAddConnection(x, w));
            expectChainsTogether(g);
        }

        TEST(Equivalences, OnlyChainEndsCanBeLinkedAndEachSideOnlyOnce) {
            Graph g("sides");
            auto c = boxAtEnd(g, "c", 0);
            auto a = boxAtEnd(g, "a", 0);
            auto b = boxAtEnd(g, "b", 0);
            auto e = boxAtEnd(g, "e", 0);
            g.computeLayout();
            g.addEquivalence(a, b);
            g.computeLayout();

            EXPECT_FALSE(g.canAddEquivalence(c, b)); // b has a on its left
            EXPECT_THROW(g.addEquivalence(c, b), std::logic_error);
            EXPECT_FALSE(g.canAddEquivalence(e, a)); // a has b on its right
            EXPECT_FALSE(g.canAddEquivalence(a, b)); // already equivalent
            EXPECT_TRUE(g.canAddEquivalence(c, a));
            EXPECT_TRUE(g.canAddEquivalence(b, e));
        }

        // ── Removing ──────────────────────────────────────────────────────────

        TEST(Equivalences, RemovingTheLineLetsEachPartGoBackUp) {
            Graph g("split");
            auto a = box(g, "a", 0);
            auto a1 = box(g, "a1", 1, a);
            auto p = box(g, "p", 0);
            auto b = box(g, "b", 1, p);
            g.computeLayout();
            g.addEquivalence(a, b);
            g.computeLayout();

            ASSERT_TRUE(g.canRemoveEquivalence(a, b));
            g.removeEquivalence(b, a);
            EXPECT_TRUE(g.getEquivalences().empty());
            EXPECT_EQ(a->getLayer(), 0);
            EXPECT_EQ(a1->getLayer(), 1);
            EXPECT_EQ(b->getLayer(), 1);
            EXPECT_THROW(g.removeEquivalence(a, b), std::logic_error);
            expectLayering(g);
        }

        TEST(Equivalences, RemovingAMiddleLineSplitsTheChain) {
            Graph g("middle");
            auto a = boxAtEnd(g, "a", 0);
            auto b = boxAtEnd(g, "b", 0);
            auto c = boxAtEnd(g, "c", 0);
            auto d = boxAtEnd(g, "d", 0);
            g.computeLayout();
            g.addEquivalence(a, b, false);
            g.addEquivalence(b, c);
            g.addEquivalence(c, d, false);

            g.removeEquivalence(b, c);
            EXPECT_EQ(chainOf(g, a), (std::vector<std::string>{ "a", "b" }));
            EXPECT_EQ(chainOf(g, d), (std::vector<std::string>{ "c", "d" }));
            EXPECT_FALSE(g.isEquivalenceContinuous(a, b));
            EXPECT_FALSE(g.isEquivalenceContinuous(c, d));
            EXPECT_FALSE(g.canRemoveEquivalence(a, c));
        }

        TEST(Equivalences, DeletingAMemberKeepsTheOthersEquivalent) {
            Graph g("delete");
            auto a = boxAtEnd(g, "a", 0);
            auto b = boxAtEnd(g, "b", 0);
            auto c = boxAtEnd(g, "c", 0);
            g.computeLayout();
            g.addEquivalence(a, b, false);
            g.addEquivalence(b, c);

            g.removeNode(b);
            EXPECT_EQ(chainOf(g, a), (std::vector<std::string>{ "a", "c" }));
            EXPECT_TRUE(g.isEquivalenceContinuous(a, c)); // only one of the two was dashed
            expectChainsTogether(g);

            g.setEquivalenceContinuous(a, c, false);
            g.removeNode(c);
            EXPECT_TRUE(g.getEquivalences().empty());
        }

        TEST(Equivalences, TheGapIsDashedOnlyIfBothLinesWere) {
            Graph g("dashed");
            auto a = boxAtEnd(g, "a", 0);
            auto b = boxAtEnd(g, "b", 0);
            auto c = boxAtEnd(g, "c", 0);
            g.computeLayout();
            g.addEquivalence(a, b, false);
            g.addEquivalence(b, c, false);

            g.removeNode(b);
            EXPECT_FALSE(g.isEquivalenceContinuous(a, c));
        }

        TEST(Equivalences, DeletingWhatHeldTheChainDownLetsItGoUp) {
            Graph g("held");
            auto a = box(g, "a", 0);
            auto p = box(g, "p", 0);
            auto b = box(g, "b", 1, p);
            g.computeLayout();
            g.addEquivalence(a, b);
            ASSERT_EQ(a->getLayer(), 1);

            g.removeNode(p);
            EXPECT_EQ(a->getLayer(), 0);
            EXPECT_EQ(b->getLayer(), 0);
            expectChainsTogether(g);
        }

        TEST(Equivalences, DeletingTheMemberThatHeldTheOthersDownLetsThemGoUp) {
            Graph g("held by a member");
            auto p = box(g, "p", 0);
            auto b = box(g, "b", 1, p);
            auto a = boxAtEnd(g, "a", 0);
            auto c = boxAtEnd(g, "c", 0);
            g.computeLayout();
            g.addEquivalence(a, c);
            g.computeLayout();
            g.addEquivalence(b, a);
            ASSERT_EQ(c->getLayer(), 1);
            ASSERT_EQ(chainOf(g, a).size(), 3u);

            g.removeNode(b);
            EXPECT_EQ(a->getLayer(), 0);
            EXPECT_EQ(c->getLayer(), 0);
            EXPECT_EQ(chainOf(g, a), (std::vector<std::string>{ "a", "c" }));
            expectChainsTogether(g);
        }

        // ── Layers ────────────────────────────────────────────────────────────

        TEST(Equivalences, RelocatingAMemberTakesTheChainAlong) {
            Graph g("relocate");
            auto r = box(g, "r", 0);
            auto r1 = box(g, "r1", 1, r);
            auto r2 = box(g, "r2", 2, r1);
            auto a = boxAtEnd(g, "a", 0);
            auto b = boxAtEnd(g, "b", 0);
            auto b1 = box(g, "b1", 1, b);
            g.computeLayout();
            g.addEquivalence(a, b);
            g.computeLayout();

            g.Hypergraph::relocateNodeToLayer(a, 1);
            EXPECT_EQ(a->getLayer(), 1);
            EXPECT_EQ(b->getLayer(), 1);
            EXPECT_EQ(b1->getLayer(), 2);
            EXPECT_EQ(a->getDesiredLayer(), 1);
            EXPECT_EQ(b->getDesiredLayer(), -1);
            expectChainsTogether(g);
            expectLayering(g);

            // Back up: b's old override does not hold it.
            g.computeLayout();
            g.Hypergraph::relocateNodeToLayer(b, 0);
            EXPECT_EQ(a->getLayer(), 0);
            EXPECT_EQ(b->getLayer(), 0);
            expectChainsTogether(g);
            expectLayering(g);
        }

        TEST(Equivalences, NoMemberCanGoAboveItsParents) {
            Graph g("parents");
            auto p = box(g, "p", 0);
            auto q = box(g, "q", 1, p);
            auto b = box(g, "b", 2, q);
            auto a = box(g, "a", 0);
            g.computeLayout();
            g.addEquivalence(a, b);
            ASSERT_EQ(a->getLayer(), 2);
            g.computeLayout();

            EXPECT_THROW(g.Hypergraph::relocateNodeToLayer(a, 1), std::logic_error);
            EXPECT_EQ(a->getLayer(), 2);
        }

        TEST(Equivalences, ANewParentOfAMemberPushesTheWholeChain) {
            Graph g("push");
            auto a = boxAtEnd(g, "a", 0);
            auto b = boxAtEnd(g, "b", 0);
            auto c = boxAtEnd(g, "c", 0);
            g.computeLayout();
            g.addEquivalence(a, b);

            g.addConnection(c, b);
            EXPECT_EQ(a->getLayer(), 1);
            EXPECT_EQ(b->getLayer(), 1);
            expectChainsTogether(g);
            expectLayering(g);

            g.removeConnection(c, b);
            EXPECT_EQ(a->getLayer(), 0);
            EXPECT_EQ(b->getLayer(), 0);
            expectChainsTogether(g);
        }

        TEST(Equivalences, ADescendantsChainFollowsItDown) {
            // r -> s; s == t (t under u). A new layer above r pushes s, and so t too.
            Graph g("descendant");
            auto r = box(g, "r", 0);
            auto s = box(g, "s", 1, r);
            auto u = box(g, "u", 0);
            auto t = box(g, "t", 1, u);
            auto t1 = box(g, "t1", 2, t);
            g.computeLayout();
            g.addEquivalence(s, t);
            g.computeLayout();

            g.createParent(NodeAttributes("top"), r);
            EXPECT_EQ(s->getLayer(), 2);
            EXPECT_EQ(t->getLayer(), 2);
            EXPECT_EQ(t1->getLayer(), 3);
            expectChainsTogether(g);
            expectLayering(g);
        }

        // ── Within a layer ────────────────────────────────────────────────────

        TEST(Equivalences, DraggingAMemberBetweenTheOthersReordersTheChain) {
            Graph g("reorder");
            auto a = boxAtEnd(g, "a", 0);
            auto b = boxAtEnd(g, "b", 0);
            auto c = boxAtEnd(g, "c", 0);
            g.computeLayout();
            g.addEquivalence(a, b, false);
            g.addEquivalence(b, c);
            g.computeLayout();

            g.relocateNodeInLayer(a, (g.getX(b) + g.getX(c)) / 2.0);
            EXPECT_EQ(row(g, 0), (std::vector<std::string>{ "b", "a", "c" }));
            EXPECT_EQ(chainOf(g, a), (std::vector<std::string>{ "b", "a", "c" }));
            // Styles stay with the gaps.
            EXPECT_FALSE(g.isEquivalenceContinuous(b, a));
            EXPECT_TRUE(g.isEquivalenceContinuous(a, c));
            expectChainsTogether(g);
        }

        TEST(Equivalences, DraggingAMemberOutsideMovesTheWholeChain) {
            Graph g("whole");
            auto x = boxAtEnd(g, "x", 0);
            auto a = boxAtEnd(g, "a", 0);
            auto b = boxAtEnd(g, "b", 0);
            auto y = boxAtEnd(g, "y", 0);
            auto z = boxAtEnd(g, "z", 0);
            g.computeLayout();
            g.addEquivalence(a, b);
            g.computeLayout();

            // b dragged past y (by as much as b moves, b's right end passes y only).
            g.relocateNodeInLayer(b, g.getX(y) + 1.0);
            EXPECT_EQ(row(g, 0), (std::vector<std::string>{ "x", "y", "a", "b", "z" }));
            g.computeLayout();
            g.relocateNodeInLayer(a, g.getX(x) - 1.0);
            EXPECT_EQ(row(g, 0), (std::vector<std::string>{ "a", "b", "x", "y", "z" }));
            EXPECT_EQ(chainOf(g, a), (std::vector<std::string>{ "a", "b" }));
            expectChainsTogether(g);
        }

        TEST(Equivalences, AnotherBoxDroppedInsideAChainGoesToItsNearerEnd) {
            Graph g("intruder");
            auto a = boxAtEnd(g, "a", 0);
            auto b = boxAtEnd(g, "b", 0);
            auto c = boxAtEnd(g, "c", 0);
            auto x = boxAtEnd(g, "x", 0);
            g.computeLayout();
            g.addEquivalence(a, b);
            g.addEquivalence(b, c);
            g.computeLayout();

            // Between b and c: nearer the right end, where it already was.
            EXPECT_THROW(g.relocateNodeInLayer(x, (g.getX(b) + g.getX(c)) / 2.0), std::invalid_argument);
            EXPECT_EQ(row(g, 0), (std::vector<std::string>{ "a", "b", "c", "x" }));
            // Between a and b: nearer the left end.
            g.relocateNodeInLayer(x, (g.getX(a) + g.getX(b)) / 2.0);
            EXPECT_EQ(row(g, 0), (std::vector<std::string>{ "x", "a", "b", "c" }));
            expectChainsTogether(g);
        }

        TEST(Equivalences, ANewBoxNextToAMemberGoesBeyondTheChain) {
            Graph g("next to");
            auto a = boxAtEnd(g, "a", 0);
            auto b = boxAtEnd(g, "b", 0);
            g.computeLayout();
            g.addEquivalence(a, b);

            g.createNodeNextTo(NodeAttributes("r"), a, false);
            g.createNodeNextTo(NodeAttributes("l"), b, true);
            EXPECT_EQ(row(g, 0), (std::vector<std::string>{ "l", "a", "b", "r" }));
            g.createNode(NodeAttributes("m"), 0, 2, nullptr); // asks for a place inside
            expectChainsTogether(g);
        }

        TEST(Equivalences, CrossingMinimizationKeepsChainsTogether) {
            // Two parents whose children cross; the chain sits among the children.
            Graph g("sifting");
            auto p = box(g, "p", 0);
            auto q = box(g, "q", 0);
            auto c1 = childAtEnd(g, "c1", q);
            auto c2 = childAtEnd(g, "c2", p);
            auto c3 = childAtEnd(g, "c3", q);
            auto c4 = childAtEnd(g, "c4", p);
            g.computeLayout();
            ASSERT_EQ(row(g, 1), (std::vector<std::string>{ "c1", "c2", "c3", "c4" }));
            g.addEquivalence(c2, c3);
            g.addEquivalence(c3, c4);
            g.computeLayout();

            g.minimizeCrossings();
            expectChainsTogether(g);
            EXPECT_EQ(chainOf(g, c2), (std::vector<std::string>{ "c2", "c3", "c4" }));
        }

        // ── Fusion ────────────────────────────────────────────────────────────

        TEST(Equivalences, FusingTwoMembersOfAChainIsFine) {
            Graph g("fuse same");
            auto a = boxAtEnd(g, "a", 0);
            auto b = boxAtEnd(g, "b", 0);
            auto c = boxAtEnd(g, "c", 0);
            g.computeLayout();
            g.addEquivalence(a, b);
            g.addEquivalence(b, c, false);

            ASSERT_TRUE(g.canFuseNodes(a, c));
            g.fuseNodes(a, c, NodeAttributes("ac"));
            EXPECT_EQ(chainOf(g, a), (std::vector<std::string>{ "ac", "b" }));
            expectChainsTogether(g);
        }

        TEST(Equivalences, FusingMembersOfTwoChainsMergesThem) {
            Graph g("fuse two");
            auto a = boxAtEnd(g, "a", 0);
            auto b = boxAtEnd(g, "b", 0);
            auto c = boxAtEnd(g, "c", 0);
            auto d = boxAtEnd(g, "d", 0);
            g.computeLayout();
            g.addEquivalence(a, b, false);
            g.addEquivalence(c, d);
            g.computeLayout();

            g.fuseNodes(b, c, NodeAttributes("bc"));
            EXPECT_EQ(chainOf(g, a), (std::vector<std::string>{ "a", "bc", "d" }));
            EXPECT_FALSE(g.isEquivalenceContinuous(a, b));
            EXPECT_TRUE(g.isEquivalenceContinuous(b, d));
            EXPECT_EQ(g.getEquivalences().size(), 1u);
            expectChainsTogether(g);
        }

        TEST(Equivalences, FusingAnOutsiderWithAMemberPutsItInTheChain) {
            Graph g("fuse outsider");
            auto p = box(g, "p", 0);
            auto x = box(g, "x", 1, p);
            auto a = box(g, "a", 0);
            auto b = box(g, "b", 0);
            g.computeLayout();
            g.addEquivalence(a, b);
            g.computeLayout();

            // a is the shallower one, so it is the box that stays.
            g.fuseNodes(x, a, NodeAttributes("xa"));
            EXPECT_EQ(chainOf(g, a).size(), 2u);
            EXPECT_EQ(a->getLayer(), 1); // the fused box hangs from p
            EXPECT_EQ(b->getLayer(), 1);
            expectChainsTogether(g);
            expectLayering(g);
        }

        TEST(Equivalences, AMemberAbsorbedByAnOutsiderLeavesItInItsPlace) {
            Graph g("fuse into outsider");
            auto o = boxAtEnd(g, "o", 0);
            auto a = boxAtEnd(g, "a", 0);
            auto b = boxAtEnd(g, "b", 0);
            g.computeLayout();
            g.addEquivalence(a, b, false);
            g.computeLayout();

            g.fuseNodes(o, a, NodeAttributes("oa"));
            EXPECT_EQ(chainOf(g, o), (std::vector<std::string>{ "oa", "b" }));
            EXPECT_FALSE(g.isEquivalenceContinuous(o, b)); // a's line, kept
            expectChainsTogether(g);
        }

        TEST(Equivalences, FeasibilityMatchesTheOperation) {
            Graph g("feasibility");
            auto r = box(g, "r", 0);
            auto s = childAtEnd(g, "s", r);
            auto t = childAtEnd(g, "t", s);
            auto u = boxAtEnd(g, "u", 0);
            auto v = childAtEnd(g, "v", u);
            auto w = boxAtEnd(g, "w", 1);
            auto x = boxAtEnd(g, "x", 1);
            g.computeLayout();
            g.addEquivalence(w, x);
            g.computeLayout();

            std::vector<NodePtr> boxes;
            for (const auto& n : g.getAllNodes()) if (!n->isDummy()) boxes.push_back(n);
            int yes = 0, no = 0;
            for (size_t i = 0; i < boxes.size(); ++i) {
                for (size_t j = 0; j < boxes.size(); ++j) {
                    Graph c = g.clone();
                    const auto copies = c.getAllNodes();
                    bool ok = true;
                    try { c.addEquivalence(copies[i], copies[j]); }
                    catch (const std::exception&) { ok = false; }
                    EXPECT_EQ(g.canAddEquivalence(boxes[i], boxes[j]), ok)
                        << boxes[i]->getName() << " / " << boxes[j]->getName();
                    (ok ? yes : no)++;
                    if (ok) expectChainsTogether(c);
                }
            }
            EXPECT_GT(yes, 0);
            EXPECT_GT(no, 0);
        }

        TEST(Equivalences, AFusionThatClosesACycleThroughAChainIsRefused) {
            // x -> y, z -> w, y == z: fusing w and x would close a cycle.
            Graph g("fuse cycle");
            auto x = box(g, "x", 0);
            auto y = box(g, "y", 1, x);
            auto z = box(g, "z", 0);
            auto w = box(g, "w", 1, z);
            g.computeLayout();
            g.addEquivalence(y, z);

            EXPECT_FALSE(g.canFuseNodes(w, x));
            EXPECT_THROW(g.fuseNodes(w, x, NodeAttributes("wx")), std::logic_error);
            EXPECT_EQ(g.getAllNodes().size(), 4u);
        }

        // ── Blocks and copies ─────────────────────────────────────────────────

        TEST(Equivalences, AChainIsOneBlockAndCopiesWithTheGraph) {
            Graph g("blocks");
            auto a = boxAtEnd(g, "a", 0);
            auto x = boxAtEnd(g, "x", 0);
            auto b = boxAtEnd(g, "b", 0);
            g.computeLayout();
            g.addEquivalence(a, b, false);
            g.computeLayout();

            const auto block = g.getComponentNodes(a.get());
            EXPECT_TRUE(block.count(b.get()));
            EXPECT_FALSE(block.count(x.get()));
            EXPECT_THROW(g.copyOf({ a.get() }), std::invalid_argument);

            Graph copy = g.clone();
            ASSERT_EQ(copy.getEquivalences().size(), 1u);
            const auto& chain = copy.getEquivalences().front();
            EXPECT_EQ(chain.members.front()->getName(), "a");
            EXPECT_NE(chain.members.front(), a);
            EXPECT_FALSE(chain.continuous.front());

            g.moveComponent(a.get(), g.getX(x) + 1000.0);
            EXPECT_EQ(row(g, 0), (std::vector<std::string>{ "x", "a", "b" }));
            g.removeComponent(a.get());
            EXPECT_TRUE(g.getEquivalences().empty());
        }

    } // namespace equivalence_tests
} // namespace hypergraph_logic
