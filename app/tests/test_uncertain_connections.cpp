#include "HypergraphEditor.h"
#include "HypergraphRenderer.h"
#include <gtest/gtest.h>

#include <QApplication>
#include <QGraphicsScene>

// ============================================================================
// Doubtful connections ("conexión dudosa")
//
// Editor: marking an end of a connection is undoable, and a connection drawn
// discontinuous as a whole counts as doubted at every end.
// Renderer: only the part of the drawing that exists to reach doubted ports is
// dashed; the rest stays continuous.
// ============================================================================

namespace app_logic {
    namespace uncertain_connection_tests {

        using ui::HypergraphRenderer;

        // The renderer builds Qt graphics items, which need an application.
        void ensureApplication() {
            if (QCoreApplication::instance()) return;
            static int argc = 1;
            static char name[] = "app_logic_tests";
            static char* argv[] = { name, nullptr };
            qputenv("QT_QPA_PLATFORM", "offscreen");
            static QApplication app(argc, argv);
        }

        HyperedgePtr edgeBetween(const GraphicalHypergraph& g, const NodePtr& s, const NodePtr& t) {
            for (const auto& e : g.getAllHyperedges())
                if (!e->isSegment() && e->containsSource(s) && e->containsTarget(t)) return e;
            return nullptr;
        }

        // Length of the continuous and of the discontinuous part of edge's drawing.
        struct Drawn { double solid = 0.0; double dashed = 0.0; };

        Drawn drawn(GraphicalHypergraph& g, const HyperedgePtr& edge) {
            ensureApplication();
            g.computeLayout();
            QGraphicsScene scene;
            std::unordered_map<Node*, QGraphicsPathItem*> nodes;
            std::unordered_map<Hyperedge*, QGraphicsPathItem*> edges;
            HypergraphRenderer::render(g, &scene, nodes, edges);

            Drawn d;
            QGraphicsPathItem* item = edges.at(edge.get());
            d.solid = item->path().length();
            for (QGraphicsItem* child : item->childItems())
                if (auto* dashes = qgraphicsitem_cast<QGraphicsPathItem*>(child)) d.dashed += dashes->path().length();
            return d;
        }

        // ── Renderer ─────────────────────────────────────────────────────────

        TEST(UncertainConnectionDrawing, NothingDoubtedMeansNothingDashed) {
            GraphicalHypergraph g("none");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            const Drawn d = drawn(g, edgeBetween(g, a, b));
            EXPECT_GT(d.solid, 0.0);
            EXPECT_EQ(d.dashed, 0.0);
        }

        TEST(UncertainConnectionDrawing, DoubtingOneEndOfAOneToOneConnectionDashesAllOfIt) {
            GraphicalHypergraph g("one_to_one");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            edgeBetween(g, a, b)->setTargetUncertain(b, true);
            const Drawn d = drawn(g, edgeBetween(g, a, b));
            EXPECT_EQ(d.solid, 0.0);
            EXPECT_GT(d.dashed, 0.0);
        }

        TEST(UncertainConnectionDrawing, DoubtingOneBranchDashesOnlyThatBranch) {
            // a -> {b, c, d}: doubting c dashes c's stub (and at most the stretch
            // of bar only c needs); a's stub and the other branches stay solid.
            GraphicalHypergraph g("branch");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            auto edge = edgeBetween(g, a, b);
            auto c = g.createNode("c", 0, 1, nullptr);
            auto d = g.createNode("d", 0, 2, nullptr);
            g.addTargetToEdge(edge, c);
            g.addTargetToEdge(edge, d);
            edge->setTargetUncertain(c, true);

            const Drawn whole = drawn(g, edge);
            EXPECT_GT(whole.solid, 0.0);
            EXPECT_GT(whole.dashed, 0.0);
            EXPECT_LT(whole.dashed, whole.solid);
        }

        TEST(UncertainConnectionDrawing, ABranchBuiltAcrossLayersIsDashedAllTheWay) {
            // x -> {y, c}, with c two layers down: reaching c needs a run through a
            // dummy at layer 1. Doubting c dashes that whole run, which is longer
            // than a single gap; x's stub and y's branch stay solid.
            GraphicalHypergraph g("long_branch");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            auto c = g.createNode("c", 2, 0, b);
            auto x = g.createNode("x", 0, 1, nullptr);
            auto y = g.createNode("y", 1, 1, x);
            auto edge = edgeBetween(g, x, y);
            g.addTargetToEdge(edge, c);
            edge = edgeBetween(g, x, c);
            ASSERT_NE(edge, nullptr);
            edge->setTargetUncertain(c, true);

            const Drawn d = drawn(g, edge);
            const double gap = g.getLayerLayout().at(0) - g.getLayerLayout().at(1);
            EXPECT_GT(d.solid, 0.0);
            EXPECT_GT(d.dashed, gap);
        }

        TEST(UncertainConnectionDrawing, DiscontinuousAsAWholeIsAllDashed) {
            GraphicalHypergraph g("whole");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            auto c = g.createNode("c", 1, 1, nullptr);
            auto edge = edgeBetween(g, a, b);
            g.addTargetToEdge(edge, c);
            edge->setContinuous(false);
            const Drawn d = drawn(g, edge);
            EXPECT_EQ(d.solid, 0.0);
            EXPECT_GT(d.dashed, 0.0);
        }

        // ── Editor ───────────────────────────────────────────────────────────

        GraphicalHypergraph fork() {
            // a -> {b, c}
            GraphicalHypergraph g("fork");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 1, 0, a);
            auto c = g.createNode("c", 1, 1, nullptr);
            g.addTargetToEdge(edgeBetween(g, a, b), c);
            g.computeLayout();
            return g;
        }

        NodePtr named(HypergraphEditor& ed, const std::string& name) {
            for (const auto& n : ed.getAllNodes())
                if (!n->isDummy() && n->getName() == name) return n;
            return nullptr;
        }

        HyperedgePtr theEdge(HypergraphEditor& ed) {
            for (const auto& e : ed.getAllHyperedges())
                if (!e->isSegment()) return e;
            return nullptr;
        }

        TEST(UncertainConnectionEditor, MarkingAnEndIsUndoable) {
            HypergraphEditor ed(fork());
            ed.setConnectionEndUncertain(theEdge(ed), named(ed, "c"), true);
            EXPECT_TRUE(theEdge(ed)->isTargetUncertain(named(ed, "c").get()));
            ed.undo();
            EXPECT_FALSE(theEdge(ed)->hasUncertainEnds());
            ed.redo();
            EXPECT_TRUE(theEdge(ed)->isTargetUncertain(named(ed, "c").get()));
        }

        TEST(UncertainConnectionEditor, UnmarkingOneEndOfAWhollyDashedConnectionKeepsTheOthers) {
            HypergraphEditor ed(fork());
            ed.setHyperedgeContinuous(theEdge(ed), false);
            ed.setConnectionEndUncertain(theEdge(ed), named(ed, "b"), false);

            auto e = theEdge(ed);
            EXPECT_TRUE(e->isContinuous());
            EXPECT_TRUE(e->isSourceUncertain(named(ed, "a").get()));
            EXPECT_FALSE(e->isTargetUncertain(named(ed, "b").get()));
            EXPECT_TRUE(e->isTargetUncertain(named(ed, "c").get()));
        }

        TEST(UncertainConnectionEditor, DoubtingEveryEndIsTheSameAsDashingTheWhole) {
            HypergraphEditor ed(fork());
            for (const char* name : { "a", "b", "c" })
                ed.setConnectionEndUncertain(theEdge(ed), named(ed, name), true);
            EXPECT_FALSE(theEdge(ed)->isContinuous());
            EXPECT_FALSE(theEdge(ed)->hasUncertainEnds());
            for (const char* name : { "a", "b", "c" })
                EXPECT_TRUE(HypergraphEditor::isConnectionEndUncertain(theEdge(ed), named(ed, name)));
        }

        TEST(UncertainConnectionEditor, TheWholeConnectionOverridesItsEnds) {
            HypergraphEditor ed(fork());
            ed.setConnectionEndUncertain(theEdge(ed), named(ed, "c"), true);
            ed.setHyperedgeContinuous(theEdge(ed), true);
            EXPECT_FALSE(theEdge(ed)->hasUncertainEnds());
            EXPECT_TRUE(ed.canUndo());
        }

    } // namespace uncertain_connection_tests
} // namespace app_logic
