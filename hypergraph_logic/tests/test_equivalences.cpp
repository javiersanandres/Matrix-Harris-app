#include "GraphicalHypergraph.h"
#include "GlobalSifting.h"
#include "ILPBackendTestHook.h"
#include "ILPCancellationToken.h"
#include <gtest/gtest.h>

#include <random>
#include <stdexcept>
#include <string>
#include <vector>

// The hooks the next exact solve takes (defined in minimizeCrossingsILP.cpp).
namespace sifting_internal {
    extern hypergraph_logic::ILPCancellationToken* g_active_cancel_token;
    extern ILPBackendOverride g_ilp_backend_override;
}

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
            auto members = chainOf(g, c2); // the global minimization may reorder it
            std::sort(members.begin(), members.end());
            EXPECT_EQ(members, (std::vector<std::string>{ "c2", "c3", "c4" }));
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

        // ── Crossing minimization ─────────────────────────────────────────────
        //
        // The layers set by hand, then sifted as the editing operations do (only the
        // layers given; the one below is read, never changed) or by the sifter itself.

        struct Sifted : Graph {
            using Graph::Graph;
            std::map<int, LayerData>& layers() { return layers_; }
            int sift(int rounds, int start, int end) { return Hypergraph::minimizeCrossings(rounds, start, end); }
            void syncChains() { syncEquivalenceOrder(); } // after the sifter itself reordered one
            int siftFor(const std::vector<Node*>& nodes, int start, int end) {
                return Graph::minimizeCrossingsForNodes(nodes, start, end);
            }
            // The crossings of the whole drawing, the chains' lines included or not.
            int crossings(bool with_lines) {
                sifting_internal::GlobalSifter s(0, layers_.rbegin()->first, layers_, false, {}, {},
                    with_lines ? equivalences_ : std::vector<EquivalenceChain>{});
                return s.countCrossings();
            }
        };

        // Parents fixed in layer 0 (sifting starts at layer 1); every edge has a hub of its
        // own between the layers, so a box can only gain by moving where its hub already is.
        struct Anchored {
            Sifted g{ "anchored" };
            std::vector<NodePtr> top, bottom;
            NodePtr parent(const char* name) { top.push_back(boxAtEnd(g, name, 0)); return top.back(); }
            NodePtr child(const char* name, const NodePtr& p) { bottom.push_back(childAtEnd(g, name, p)); return bottom.back(); }
            void rows(std::vector<NodePtr> upper, std::vector<NodePtr> lower) {
                g.layers().at(0).nodes = std::move(upper);
                g.layers().at(1).nodes = std::move(lower);
            }
        };

        // P -> b and Q -> a, with a ≡ b: in this order their connections cross.
        struct Crossed : Anchored {
            NodePtr P = parent("P"), Q = parent("Q");
            NodePtr a = child("a", Q), b = child("b", P);
            Crossed() {
                g.computeLayout();
                g.addEquivalence(a, b);
                g.computeLayout();
                rows({ P, Q }, { a, b });
            }
        };

        // P -> {a, b} and Q -> C two layers down, its dummy d at layer 1 between a ≡ b.
        struct DummyInside {
            Sifted g{ "dummy inside" };
            NodePtr P = box(g, "P", 0), Q = boxAtEnd(g, "Q", 0);
            NodePtr a = childAtEnd(g, "a", P), b = childAtEnd(g, "b", P), C = childAtEnd(g, "C", a);
            NodePtr d;
            DummyInside() {
                g.addConnection(Q, C);
                for (const auto& n : g.getNodesAt(1)) if (n->isDummy()) d = n;
                g.computeLayout();
                g.addEquivalence(a, b);
                g.computeLayout();
                g.layers().at(0).nodes = { P, Q };
                g.layers().at(1).nodes = { a, d, b };
            }
            std::vector<std::string> fullRow() {
                std::vector<std::string> names;
                for (const auto& n : g.getNodesAt(1)) names.push_back(n->isDummy() ? "." : n->getName());
                return names;
            }
        };

        TEST(EquivalenceSifting, ADummyBetweenMembersCrossesTheirLine) {
            DummyInside t;
            ASSERT_TRUE(t.d);
            ASSERT_EQ(t.fullRow(), (std::vector<std::string>{ "a", ".", "b" }));
            EXPECT_EQ(t.g.crossings(true), t.g.crossings(false) + 1);
        }

        TEST(EquivalenceSifting, TheDummyLeavesTheChainWhenThatRemovesCrossings) {
            DummyInside t;
            ASSERT_EQ(t.g.crossings(true), 2); // the line, and Q's connection with P -> b
            t.g.sift(3, 1, 1);
            EXPECT_EQ(t.fullRow(), (std::vector<std::string>{ "a", "b", "." }));
            EXPECT_EQ(t.g.crossings(true), 0);
            expectChainsTogether(t.g);
        }

        // P1, P2 -> a and R1, R2 -> b, with Q -> C two layers down in between: its dummy d,
        // right of a ≡ b, crosses both of b's connections, but only the line between them.
        struct DummyWanted : DummyInside {
            DummyWanted() {
                // DummyInside's P -> b is replaced by two connections from the right.
                g.removeConnection(P, b);
                R1 = boxAtEnd(g, "R1", 0);
                R2 = boxAtEnd(g, "R2", 0);
                P2 = box(g, "P2", 0);
                g.addConnection(P2, a);
                g.addConnection(R1, b);
                g.addConnection(R2, b);
                g.computeLayout();
                g.layers().at(0).nodes = { P, P2, Q, R1, R2 };
                g.layers().at(1).nodes = { a, b, d };
            }
            NodePtr P2, R1, R2;
        };

        struct StepSifter : sifting_internal::GlobalSifter {
            using GlobalSifter::GlobalSifter;
            using GlobalSifter::chainStep;
            // x's block right after y's in B, the hubs staying where they are.
            void putAfter(Node* x, Node* y) {
                const int bx = S_.g1_nodes[S_.node_to_g1.at(x)].block_id;
                const int by = S_.g1_nodes[S_.node_to_g1.at(y)].block_id;
                B_.erase(std::find(B_.begin(), B_.end(), bx));
                B_.insert(std::find(B_.begin(), B_.end(), by) + 1, bx);
                orderLayersByBlockOrder();
                sortAdjacencies();
            }
        };

        TEST(EquivalenceSifting, AChainStepCanTakeADummyBetweenItsMembers) {
            // The hubs are sorted for a . b, then d goes right of b: the chain, moving as one
            // block, only gets back to a . b by stopping around d.
            DummyWanted t;
            t.g.layers().at(1).nodes = { t.a, t.d, t.b };
            StepSifter s(1, 1, t.g.layers(), false, {}, {}, t.g.getEquivalences());
            s.putAfter(t.d.get(), t.b.get());
            const int before = s.countCrossings();
            s.chainStep(0, false, false);
            EXPECT_LT(s.countCrossings(), before);
            s.writeBack();
            EXPECT_EQ(t.fullRow(), (std::vector<std::string>{ "a", ".", "b" }));
        }

        TEST(EquivalenceSifting, PlacingAChainCanTakeADummyBetweenItsMembers) {
            DummyWanted t;
            const int before = t.g.crossings(true);
            t.g.siftFor({ t.a.get() }, 1, 1);
            EXPECT_EQ(t.fullRow(), (std::vector<std::string>{ "a", ".", "b" }));
            EXPECT_LT(t.g.crossings(true), before);
            expectChainsTogether(t.g);
        }

        TEST(EquivalenceSifting, ARealBoxNeverStopsInsideAChain) {
            // Q -> a, P -> x, R -> b with P between Q and R: x would cross nothing between
            // a and b, which it may not take.
            Anchored t;
            auto Q = t.parent("Q"), P = t.parent("P"), R = t.parent("R");
            auto a = t.child("a", Q), b = t.child("b", R), x = t.child("x", P);
            t.g.computeLayout();
            t.g.addEquivalence(a, b);
            t.g.computeLayout();
            t.rows({ Q, P, R }, { a, b, x });
            ASSERT_EQ(t.g.crossings(true), 1);

            t.g.sift(5, 1, 1);
            const auto names = row(t.g, 1);
            EXPECT_NE(names, (std::vector<std::string>{ "a", "x", "b" }));
            EXPECT_EQ(t.g.crossings(true), 1);
            EXPECT_EQ(chainOf(t.g, a), (std::vector<std::string>{ "a", "b" }));
            expectChainsTogether(t.g);
        }

        TEST(EquivalenceSifting, TheLightCallsKeepTheChainsOrder) {
            Crossed t;
            ASSERT_EQ(t.g.crossings(true), 1);
            t.g.sift(5, 1, 1);
            EXPECT_EQ(row(t.g, 1), (std::vector<std::string>{ "a", "b" }));
        }

        TEST(EquivalenceSifting, TheGlobalSiftingReordersAChain) {
            Crossed t;
            sifting_internal::GlobalSifter s(1, 1, t.g.layers(), false, {}, {}, t.g.getEquivalences());
            s.runSifting(3, false, true);
            EXPECT_EQ(s.countCrossings(), 0);
            s.writeBack();
            EXPECT_EQ(row(t.g, 1), (std::vector<std::string>{ "b", "a" }));
        }

        TEST(EquivalenceSifting, TheBarycenterSortsAChainAsOneUnitAndThenItsMembers) {
            Crossed t;
            sifting_internal::GlobalSifter s(1, 1, t.g.layers(), true, {}, {}, t.g.getEquivalences());
            EXPECT_EQ(s.countCrossings(), 0);
            s.writeBack();
            EXPECT_EQ(row(t.g, 1), (std::vector<std::string>{ "b", "a" }));
        }

        TEST(EquivalenceSifting, TheBarycenterNeverPutsABoxInsideAChain) {
            // x's own barycenter falls between b's and a's.
            Anchored t;
            auto P = t.parent("P"), R = t.parent("R"), Q = t.parent("Q");
            auto a = t.child("a", Q), b = t.child("b", P), x = t.child("x", R);
            t.g.computeLayout();
            t.g.addEquivalence(a, b);
            t.g.computeLayout();
            t.rows({ P, R, Q }, { a, b, x });

            sifting_internal::GlobalSifter s(1, 1, t.g.layers(), true, {}, {}, t.g.getEquivalences());
            s.writeBack();
            t.g.syncChains();
            EXPECT_NE(row(t.g, 1), (std::vector<std::string>{ "b", "x", "a" }));
            expectChainsTogether(t.g);
        }

        TEST(EquivalenceSifting, PlacingANodesChainMovesItWholeInItsOrder) {
            // x crosses both of the chain's connections; the chain moves past it as one.
            Anchored t;
            auto X = t.parent("X"), P = t.parent("P"), Q = t.parent("Q");
            auto a = t.child("a", P), b = t.child("b", Q), x = t.child("x", X);
            t.g.computeLayout();
            t.g.addEquivalence(a, b);
            t.g.computeLayout();
            t.rows({ X, P, Q }, { a, b, x });
            const int before = t.g.crossings(true);

            t.g.siftFor({ a.get() }, 1, 1);
            EXPECT_LE(t.g.crossings(true), before);
            EXPECT_EQ(chainOf(t.g, a), (std::vector<std::string>{ "a", "b" }));
            expectChainsTogether(t.g);
        }

        TEST(EquivalenceSifting, AChainFollowsAMovedParentAsOne) {
            // A was moved past B; c hangs from A and d, its equivalent, from B.
            Sifted g("follow");
            auto A = boxAtEnd(g, "A", 0), B = boxAtEnd(g, "B", 0);
            auto a1 = childAtEnd(g, "a1", A), c = childAtEnd(g, "c", A);
            auto d = childAtEnd(g, "d", B), b1 = childAtEnd(g, "b1", B);
            g.computeLayout();
            ASSERT_EQ(row(g, 1), (std::vector<std::string>{ "a1", "c", "d", "b1" }));
            g.addEquivalence(c, d);
            g.computeLayout();
            g.layers().at(0).nodes = { B, A };
            g.layers().at(1).nodes = { a1, c, d, b1 };

            sifting_internal::GlobalSifter s(1, 1, g.layers(), false, { A.get() }, {}, g.getEquivalences());
            EXPECT_EQ(row(g, 1), (std::vector<std::string>{ "b1", "c", "d", "a1" }));
        }

        TEST(EquivalenceSifting, TheGlobalMinimizationCountsTheLinesAndKeepsTheChainsInOrder) {
            DummyInside t;
            const int crossings = t.g.minimizeCrossings();
            expectChainsTogether(t.g);
            EXPECT_EQ(crossings, t.g.crossings(true));
        }

        TEST(EquivalenceSifting, RandomEditsKeepEveryChainTogetherAndInItsOrder) {
            auto orders = [](const Graph& g) {
                std::vector<std::vector<Node*>> all;
                for (const auto& chain : g.getEquivalences()) {
                    all.emplace_back();
                    for (const auto& m : chain.members) all.back().push_back(m.get());
                }
                return all;
            };
            for (unsigned seed = 1; seed <= 25; ++seed) {
                std::mt19937 rng(seed);
                auto pick = [&](const std::vector<NodePtr>& v) { return v[rng() % v.size()]; };
                Graph g("random");
                std::vector<NodePtr> boxes;
                for (int i = 0; i < 4; ++i) boxes.push_back(boxAtEnd(g, ("r" + std::to_string(i)).c_str(), 0));
                for (int i = 0; i < 12; ++i)
                    boxes.push_back(childAtEnd(g, ("n" + std::to_string(i)).c_str(), pick(boxes)));
                g.computeLayout();
                for (int k = 0; k < 40 && g.getEquivalences().size() < 3; ++k) {
                    auto a = pick(boxes), b = pick(boxes);
                    if (!g.canAddEquivalence(a, b)) continue;
                    g.addEquivalence(a, b);
                    g.computeLayout();
                    expectChainsTogether(g);
                }
                // Each connection sifts the layers it touches, lightly.
                for (int k = 0; k < 20; ++k) {
                    auto a = pick(boxes), b = pick(boxes);
                    if (!g.canAddConnection(a, b)) continue;
                    const auto before = orders(g);
                    g.addConnection(a, b);
                    g.computeLayout();
                    expectChainsTogether(g);
                    EXPECT_EQ(orders(g), before) << "seed " << seed;
                }
                g.minimizeCrossings();
                expectChainsTogether(g);
                expectLayering(g);
            }
        }

        // ── Exact solve ───────────────────────────────────────────────────────
        //
        // The whole drawing, solved by the ILP itself (not the heuristic baseline it is
        // compared with), then written back and the chains synced, as the global
        // minimization does.

        int solveExactly(Sifted& g, bool with_equivalences = true) {
            const int last = g.layers().rbegin()->first;
            sifting_internal::GlobalSifter s(0, last, g.layers(), true, {}, {},
                with_equivalences ? g.getEquivalences() : std::vector<EquivalenceChain>{});
            const int crossings = s.runCrossingILP(30.0);
            if (crossings >= 0) {
                s.writeBackFromG1Order();
                g.syncChains();
            }
            return crossings;
        }

        // A -> {a, x}, B -> {x, b}: a x b crosses nothing, but a ≡ b keeps x out.
        struct Between {
            Sifted g{ "between" };
            NodePtr A = boxAtEnd(g, "A", 0), B = boxAtEnd(g, "B", 0);
            NodePtr a = childAtEnd(g, "a", A), x = childAtEnd(g, "x", A), b = childAtEnd(g, "b", B);
            Between() {
                g.addConnection(B, x);
                g.computeLayout();
            }
            bool xInside() {
                const auto names = row(g, 1);
                return names == std::vector<std::string>{ "a", "x", "b" } || names == std::vector<std::string>{ "b", "x", "a" };
            }
        };

        TEST(EquivalenceILP, ARealBoxNeverEndsInsideAChain) {
            Between free;
            EXPECT_EQ(solveExactly(free.g), 0);
            EXPECT_TRUE(free.xInside());

            Between t;
            t.g.addEquivalence(t.a, t.b);
            t.g.computeLayout();
            const int crossings = solveExactly(t.g);
            EXPECT_EQ(crossings, 1);
            EXPECT_FALSE(t.xInside());
            expectChainsTogether(t.g);
            EXPECT_EQ(crossings, t.g.crossings(true));
        }

        TEST(EquivalenceILP, ADummyGoesInsideAChainWhenThatIsCheapest) {
            // L, L2, M -> a and R, R2, M -> b, a ≡ b, and M -> C two layers down. Out of
            // the chain, M's dummy d crosses two connections; between a and b, only the line.
            Sifted g("dummy inside");
            auto L = boxAtEnd(g, "L", 0), L2 = boxAtEnd(g, "L2", 0), M = boxAtEnd(g, "M", 0);
            auto R = boxAtEnd(g, "R", 0), R2 = boxAtEnd(g, "R2", 0);
            auto a = childAtEnd(g, "a", L), b = childAtEnd(g, "b", R), C = boxAtEnd(g, "C", 2);
            for (const auto& [p, c] : std::vector<std::pair<NodePtr, NodePtr>>{ { L2, a }, { M, a }, { R2, b }, { M, b }, { M, C } }) {
                ASSERT_TRUE(g.canAddConnection(p, c)) << p->getName() << " -> " << c->getName();
                g.addConnection(p, c);
            }
            g.computeLayout();
            g.addEquivalence(a, b);
            g.computeLayout();

            const int crossings = solveExactly(g);
            EXPECT_EQ(crossings, 1);
            std::vector<std::string> names;
            for (const auto& n : g.getNodesAt(1)) names.push_back(n->isDummy() ? "." : n->getName());
            EXPECT_TRUE(names == (std::vector<std::string>{ "a", ".", "b" }) || names == (std::vector<std::string>{ "b", ".", "a" }))
                << ::testing::PrintToString(names);
            expectChainsTogether(g);
            EXPECT_EQ(crossings, g.crossings(true));
        }

        TEST(EquivalenceILP, OnRandomDrawingsItCountsWhatItDrawsAndKeepsTheChainsTogether) {
            // Chains of two members and of more (the two ways of counting the line), on
            // drawings with long connections.
            int longer_chains = 0;
            for (unsigned seed = 1; seed <= 25; ++seed) {
                std::mt19937 rng(seed);
                auto pick = [&](const std::vector<NodePtr>& v) { return v[rng() % v.size()]; };
                Sifted g("random");
                std::vector<NodePtr> boxes;
                for (int i = 0; i < 3; ++i) boxes.push_back(boxAtEnd(g, ("r" + std::to_string(i)).c_str(), 0));
                for (int i = 0; i < 11; ++i)
                    boxes.push_back(childAtEnd(g, ("n" + std::to_string(i)).c_str(), pick(boxes)));
                for (int k = 0; k < 8; ++k) {
                    auto p = pick(boxes), c = pick(boxes);
                    if (g.canAddConnection(p, c)) g.addConnection(p, c);
                }
                g.computeLayout();
                for (int k = 0; k < 60; ++k) {
                    auto a = pick(boxes), b = pick(boxes);
                    if (!g.canAddEquivalence(a, b)) continue;
                    g.addEquivalence(a, b);
                    g.computeLayout();
                }
                for (const auto& chain : g.getEquivalences())
                    if (chain.members.size() >= 3) ++longer_chains;

                const int last = g.layers().rbegin()->first;
                sifting_internal::GlobalSifter s(0, last, g.layers(), true, {}, {}, g.getEquivalences());
                s.runSifting(3, false, true);
                const int heuristic = s.countCrossings();
                const int crossings = s.runCrossingILP(30.0);
                ASSERT_GE(crossings, 0) << "seed " << seed;
                EXPECT_LE(crossings, heuristic) << "seed " << seed;
                s.writeBackFromG1Order();
                g.syncChains();
                expectChainsTogether(g);
                EXPECT_EQ(crossings, g.crossings(true)) << "seed " << seed;
            }
            EXPECT_GT(longer_chains, 0);
        }

        TEST(EquivalenceILP, TwinsKeepTheirOrderWhileTheRestIsSolved) {
            // P -> {x, y, z} is one connection, so x, y and z share their hub: twins. Q -> w
            // starts left of them and crosses all three; w ≡ v, Q -> v, keeps a chain in
            // the row, which is never fixed.
            Sifted g("twins");
            auto P = boxAtEnd(g, "P", 0), Q = boxAtEnd(g, "Q", 0);
            auto w = boxAtEnd(g, "w", 1), v = boxAtEnd(g, "v", 1);
            auto x = boxAtEnd(g, "x", 1), y = boxAtEnd(g, "y", 1), z = boxAtEnd(g, "z", 1);
            auto edge = g.addConnection(P, x);
            g.addTargetToEdge(edge, y);
            g.addTargetToEdge(edge, z);
            g.addConnection(Q, w);
            g.addConnection(Q, v);
            g.computeLayout();
            g.addEquivalence(w, v);
            g.computeLayout();
            g.layers().at(0).nodes = { P, Q };
            g.layers().at(1).nodes = { w, v, x, y, z };

            const int last = g.layers().rbegin()->first;
            sifting_internal::GlobalSifter s(0, last, g.layers(), false, {}, {}, g.getEquivalences());
            const int crossings = s.runCrossingILP(30.0);
            ASSERT_EQ(crossings, 0);
            s.writeBackFromG1Order();
            g.syncChains();

            std::vector<std::string> twins;
            for (const auto& name : row(g, 1))
                if (name == "x" || name == "y" || name == "z") twins.push_back(name);
            EXPECT_EQ(twins, (std::vector<std::string>{ "x", "y", "z" }));
            expectChainsTogether(g);
            EXPECT_EQ(crossings, g.crossings(true));
        }

        TEST(ILPHooks, AMinimizationThatNeverSolvesLeavesNoHookBehind) {
            using sifting_internal::ILPBackendOverride;
            auto arm = [](ILPCancellationToken& token) {
                beginInterruptibleILP(token);
                sifting_internal::setILPBackendOverrideForTesting(ILPBackendOverride::kForceGurobi);
            };
            auto expectDropped = [] {
                EXPECT_EQ(sifting_internal::g_active_cancel_token, nullptr);
                EXPECT_EQ(sifting_internal::g_ilp_backend_override, ILPBackendOverride::kAuto);
            };

            // No crossings: it returns before the exact solve.
            {
                Graph g("no crossings");
                auto A = boxAtEnd(g, "A", 0), B = boxAtEnd(g, "B", 0);
                childAtEnd(g, "a", A);
                childAtEnd(g, "b", B);
                g.computeLayout();
                ILPCancellationToken token;
                arm(token);
                EXPECT_EQ(g.minimizeCrossings(), 0);
                expectDropped();
            }
            // Nothing to minimize: it throws.
            {
                Graph g("one connection");
                childAtEnd(g, "a", boxAtEnd(g, "A", 0));
                g.computeLayout();
                ILPCancellationToken token;
                arm(token);
                EXPECT_THROW(g.minimizeCrossings(), std::logic_error);
                expectDropped();
            }
        }

    } // namespace equivalence_tests
} // namespace hypergraph_logic
