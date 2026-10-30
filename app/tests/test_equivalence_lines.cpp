#include "HypergraphEditor.h"
#include "JointHypergraphEditor.h"
#include "Project.h"
#include "HypergraphRenderer.h"
#include <gtest/gtest.h>

#include <QApplication>
#include <QGraphicsScene>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>

// ============================================================================
// Equivalences in the editors and on screen
//
// Editor: adding and removing an equivalence is undoable like any structural
// step; restyling a line is a local change that puts the whole chain's
// certainty groups back on undo. Saving and opening a project keeps the chains.
// Renderer: one line per pair of neighbours, at their middle height, from one
// box's side to the other's, dashed when they are not certainly equivalent,
// hopping over the dummies between them.
// ============================================================================

namespace app_logic {
    namespace equivalence_line_tests {

        using ui::HypergraphRenderer;

        void ensureApplication() {
            if (QCoreApplication::instance()) return;
            static int argc = 1;
            static char name[] = "app_logic_tests";
            static char* argv[] = { name, nullptr };
            qputenv("QT_QPA_PLATFORM", "offscreen");
            static QApplication app(argc, argv);
        }

        template <typename Graph>
        NodePtr named(const Graph& g, const std::string& name) {
            for (const auto& n : g.getAllNodes())
                if (!n->isDummy() && n->getName() == name) return n;
            return nullptr;
        }

        // The names of a chain's members, left to right (empty if none).
        std::vector<std::string> chainOf(const GraphicalHypergraph& g, const std::string& name) {
            std::vector<std::string> names;
            if (const EquivalenceChain* c = g.getEquivalenceChain(named(g, name).get()))
                for (const auto& m : c->members) names.push_back(m->getName());
            return names;
        }

        bool continuous(const GraphicalHypergraph& g, const std::string& a, const std::string& b) {
            return g.isEquivalenceContinuous(named(g, a), named(g, b));
        }

        // Three boxes side by side in layer 0.
        void threeInARow(HypergraphEditor& ed) {
            ed.createNode("a", 0, 0, nullptr);
            ed.createNode("b", 0, 1, nullptr);
            ed.createNode("c", 0, 2, nullptr);
        }

        // ── Editor and history ──────────────────────────────────────────────────

        TEST(EquivalenceEditing, AddingAndRemovingAreUndoable) {
            HypergraphEditor ed(GraphicalHypergraph("g"));
            threeInARow(ed);
            ed.addEquivalence(named(ed, "a"), named(ed, "b"));
            ed.addEquivalence(named(ed, "b"), named(ed, "c"), false);
            EXPECT_EQ(chainOf(ed.getGraph(), "a"), (std::vector<std::string>{ "a", "b", "c" }));

            ed.removeEquivalence(named(ed, "b"), named(ed, "c"));
            EXPECT_EQ(chainOf(ed.getGraph(), "a"), (std::vector<std::string>{ "a", "b" }));

            ed.undo();
            EXPECT_EQ(chainOf(ed.getGraph(), "c"), (std::vector<std::string>{ "a", "b", "c" }));
            EXPECT_FALSE(continuous(ed.getGraph(), "b", "c"));
            ed.undo(); ed.undo();
            EXPECT_TRUE(ed.getGraph().getEquivalences().empty());
            ed.redo();
            EXPECT_TRUE(continuous(ed.getGraph(), "a", "b"));
        }

        TEST(EquivalenceEditing, ARefusedEquivalenceRecordsNothing) {
            HypergraphEditor ed(GraphicalHypergraph("g"));
            threeInARow(ed);
            ed.addEquivalence(named(ed, "a"), named(ed, "b"));
            EXPECT_THROW(ed.addEquivalence(named(ed, "a"), named(ed, "b")), std::logic_error);
            EXPECT_THROW(ed.removeEquivalence(named(ed, "a"), named(ed, "c")), std::logic_error);
            ed.undo();
            EXPECT_TRUE(ed.getGraph().getEquivalences().empty());
        }

        TEST(EquivalenceEditing, RestylingALineUndoesTheWholeChainsGroups) {
            // b a c with a uncertain with both, and b certain with c: making a-c
            // continuous joins a to that group, so b-a turns continuous too.
            HypergraphEditor ed(GraphicalHypergraph("g"));
            threeInARow(ed);
            ed.addEquivalence(named(ed, "a"), named(ed, "b"), false);
            ed.addEquivalence(named(ed, "b"), named(ed, "c"));
            const auto& layout = ed.getGraph().getNodeLayout();
            const double between = (layout.at(named(ed, "b").get()).x + layout.at(named(ed, "c").get()).x) / 2.0;
            ed.relocateNode(named(ed, "a"), between, ed.getGraph().getLayerLayout().at(0));
            ASSERT_EQ(chainOf(ed.getGraph(), "a"), (std::vector<std::string>{ "b", "a", "c" }));

            ed.setEquivalenceContinuous(named(ed, "a"), named(ed, "c"), true);
            EXPECT_TRUE(continuous(ed.getGraph(), "b", "a"));

            ed.undo();
            EXPECT_FALSE(continuous(ed.getGraph(), "b", "a"));
            EXPECT_FALSE(continuous(ed.getGraph(), "a", "c"));
            EXPECT_TRUE(ed.getGraph().isEquivalenceCertain(named(ed, "b"), named(ed, "c")));
            ed.redo();
            EXPECT_TRUE(continuous(ed.getGraph(), "b", "a"));
            EXPECT_TRUE(continuous(ed.getGraph(), "a", "c"));
        }

        TEST(EquivalenceEditing, RestylingSurvivesStructuralSteps) {
            HypergraphEditor ed(GraphicalHypergraph("g"));
            threeInARow(ed);
            ed.addEquivalence(named(ed, "a"), named(ed, "b"));
            ed.setEquivalenceContinuous(named(ed, "a"), named(ed, "b"), false); // local
            ed.createNode("d", 1, 0, named(ed, "c"));                           // structural

            ed.undo(); ed.undo();
            EXPECT_TRUE(continuous(ed.getGraph(), "a", "b"));
            ed.redo(); ed.redo();
            EXPECT_FALSE(continuous(ed.getGraph(), "a", "b"));
            EXPECT_NE(named(ed, "d"), nullptr);
        }

        TEST(EquivalenceEditing, AStyleItAlreadyHasRecordsNothing) {
            HypergraphEditor ed(GraphicalHypergraph("g"));
            threeInARow(ed);
            ed.addEquivalence(named(ed, "a"), named(ed, "b"));
            ed.setEquivalenceContinuous(named(ed, "a"), named(ed, "b"), true);
            ed.undo();
            EXPECT_TRUE(ed.getGraph().getEquivalences().empty()); // only the equivalence was there
            EXPECT_THROW(ed.setEquivalenceContinuous(named(ed, "a"), named(ed, "b"), true), std::logic_error);
        }

        TEST(EquivalenceEditing, BoxesOfTwoDiagramsCanBeEquivalentInTheJoint) {
            HypergraphEditor one(GraphicalHypergraph("one"));
            one.createNode("x", 0, 0, nullptr);
            HypergraphEditor two(GraphicalHypergraph("two"));
            two.createNode("y", 0, 0, nullptr);

            JointHypergraphEditor joint(JointGraphicalHypergraph::create("j"));
            GraphicalHypergraph a = one.getGraph().clone(), b = two.getGraph().clone();
            joint.addHypergraph(a, true);
            joint.addHypergraph(b, false);
            joint.addEquivalence(named(joint, "x"), named(joint, "y"), false);
            EXPECT_FALSE(joint.getGraph().isSeparable(a.getId()));
            EXPECT_TRUE(joint.getGraph().isRemovable(a.getId()));

            joint.setEquivalenceContinuous(named(joint, "x"), named(joint, "y"), true);
            joint.undo();
            EXPECT_FALSE(continuous(joint.getGraph(), "x", "y"));
            joint.undo();
            EXPECT_TRUE(joint.getGraph().getEquivalences().empty());
            EXPECT_TRUE(joint.getGraph().isSeparable(a.getId()));
        }

        // ── Saving and opening ──────────────────────────────────────────────────

        TEST(EquivalenceEditing, AProjectOpensWithItsChains) {
            namespace fs = std::filesystem;
            const fs::path tmp = fs::temp_directory_path() / "test_equivalence_lines_project.json";
            {
                Project p("P");
                auto& ed = p.getEditor(0);
                ed.createNode("a", 0, 0, nullptr);
                ed.createNode("b", 0, 1, nullptr);
                ed.createNode("c", 0, 2, nullptr);
                ed.addEquivalence(named(ed, "a"), named(ed, "b"));
                ed.addEquivalence(named(ed, "b"), named(ed, "c"), false);
                GraphicalHypergraph copy = ed.getGraph().clone();
                p.getJointEditor().addHypergraph(copy, true);
                p.save(tmp);
            }
            auto loaded = Project::load(tmp);
            for (const GraphicalHypergraph* g : { &loaded->getEditor(0).getGraph(),
                    static_cast<const GraphicalHypergraph*>(&loaded->getJointEditor().getGraph()) }) {
                EXPECT_EQ(chainOf(*g, "a"), (std::vector<std::string>{ "a", "b", "c" }));
                EXPECT_TRUE(continuous(*g, "a", "b"));
                EXPECT_FALSE(continuous(*g, "b", "c"));
            }
            EXPECT_FALSE(loaded->hasUnsavedChanges());
            fs::remove(tmp);
        }

        // ── Drawing ─────────────────────────────────────────────────────────────

        struct Drawing {
            bool has_application = (ensureApplication(), true); // before the scene
            QGraphicsScene scene;
            std::unordered_map<Node*, QGraphicsPathItem*> nodes;
            std::unordered_map<Hyperedge*, QGraphicsPathItem*> edges;
            std::vector<QGraphicsPathItem*> lines;
        };

        void draw(GraphicalHypergraph& g, Drawing& d) {
            g.computeLayout();
            HypergraphRenderer::render(g, &d.scene, d.nodes, d.edges, &d.lines);
        }

        TEST(EquivalenceDrawing, OneLinePerPairOfNeighboursAtTheirMiddleHeight) {
            GraphicalHypergraph g("row");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 0, 1, nullptr);
            auto c = g.createNode("c", 0, 2, nullptr);
            g.computeLayout();
            g.addEquivalence(a, b);
            g.addEquivalence(b, c, false);

            Drawing d;
            draw(g, d);
            ASSERT_EQ(d.lines.size(), 2u);
            std::sort(d.lines.begin(), d.lines.end(), [](auto* l, auto* r) {
                return l->path().boundingRect().left() < r->path().boundingRect().left(); });

            const QRectF box_a = d.nodes.at(a.get())->path().boundingRect();
            const QRectF box_b = d.nodes.at(b.get())->path().boundingRect();
            const QRectF line = d.lines[0]->path().boundingRect();
            EXPECT_NEAR(line.height(), 0.0, 1e-6);                 // straight: nothing to hop over
            EXPECT_NEAR(line.top(), box_a.center().y(), 1e-6);
            EXPECT_NEAR(line.left(), box_a.right(), 1e-6);
            EXPECT_NEAR(line.right(), box_b.left(), 1e-6);

            EXPECT_EQ(d.lines[0]->pen().style(), Qt::SolidLine);
            EXPECT_NE(d.lines[1]->pen().style(), Qt::SolidLine);  // b and c are not certainly equivalent
        }

        TEST(EquivalenceDrawing, TheLineHopsOverADummyBetweenTheBoxes) {
            // r -> q runs through layer 1, between p's children and s's: the boxes
            // on both sides of its dummy there are made equivalent.
            GraphicalHypergraph g("hop");
            auto p = g.createNode("p", 0, 0, nullptr);
            auto r = g.createNode("r", 0, 1, nullptr);
            auto m = g.createNode("m", 1, 0, p);
            for (const char* name : { "a", "b", "c", "d" }) g.createNode(name, 1, 1, p);
            auto s = g.createNode("s", 0, 2, nullptr);
            g.createNode("t", 1, 5, s);
            auto q = g.createNode("q", 2, 0, m);
            g.addConnection(r, q);
            g.computeLayout();

            const auto row = g.getNodesAt(1);
            const auto at = std::find_if(row.begin(), row.end(), [](const NodePtr& n) { return n->isDummy(); });
            ASSERT_NE(at, row.end());
            ASSERT_NE(at, row.begin()) << "the test needs a box left of the dummy";
            ASSERT_NE(std::next(at), row.end()) << "the test needs a box right of the dummy";
            const NodePtr left = *std::prev(at), right = *std::next(at);
            ASSERT_FALSE(left->isDummy());
            ASSERT_FALSE(right->isDummy());
            Node* dummy = at->get();
            g.addEquivalence(left, right);

            Drawing d;
            draw(g, d);
            ASSERT_EQ(d.lines.size(), 1u);
            const QRectF line = d.lines[0]->path().boundingRect();
            const double dummy_x = g.getNodeLayout().at(dummy).x;
            EXPECT_GT(line.height(), 4.0);   // the hop rises above the line
            EXPECT_LT(line.left(), dummy_x);
            EXPECT_GT(line.right(), dummy_x);
            // Right above the dummy, the line is up in its hop, not on the layer.
            const double layer_y = -g.getLayerLayout().at(1);
            const QPainterPath& path = d.lines[0]->path();
            bool lifted = false;
            for (double t = 0.0; t <= 1.0; t += 0.001) {
                const QPointF pt = path.pointAtPercent(t);
                if (std::abs(pt.x() - dummy_x) < 0.5) lifted = lifted || pt.y() < layer_y - 3.0;
            }
            EXPECT_TRUE(lifted);
        }

        TEST(EquivalenceDrawing, AfterMinimizingEveryDummyInsideAChainIsHoppedOver) {
            // Random drawings, minimized as the "Minimizar cruces" entry does: each line
            // rises over every dummy between its two boxes, and nowhere else.
            int hops = 0;
            for (unsigned seed = 1; seed <= 15; ++seed) {
                std::mt19937 rng(seed);
                auto pick = [&](const std::vector<NodePtr>& v) { return v[rng() % v.size()]; };
                GraphicalHypergraph g("random");
                std::vector<NodePtr> boxes;
                for (int i = 0; i < 3; ++i) boxes.push_back(g.createNode("r" + std::to_string(i), 0, i, nullptr));
                for (int i = 0; i < 11; ++i) {
                    const NodePtr parent = pick(boxes);
                    boxes.push_back(g.createNode("n" + std::to_string(i), parent->getLayer() + 1, 1000, parent));
                }
                for (int k = 0; k < 8; ++k) {
                    auto p = pick(boxes), c = pick(boxes);
                    if (g.canAddConnection(p, c)) g.addConnection(p, c);
                }
                g.computeLayout();
                for (int k = 0; k < 60; ++k) {
                    auto a = pick(boxes), b = pick(boxes);
                    if (!g.canAddEquivalence(a, b)) continue;
                    g.addEquivalence(a, b, rng() % 2 == 0);
                    g.computeLayout();
                }
                g.minimizeCrossings();

                Drawing d;
                draw(g, d);
                size_t next = 0;
                for (const auto& chain : g.getEquivalences()) {
                    const int layer = chain.members.front()->getLayer();
                    const double layer_y = -g.getLayerLayout().at(layer);
                    const auto nodes = g.getNodesAt(layer);
                    for (size_t k = 0; k + 1 < chain.members.size(); ++k) {
                        ASSERT_LT(next, d.lines.size()) << "seed " << seed;
                        const QPainterPath& path = d.lines[next++]->path();
                        EXPECT_EQ(d.lines[next - 1]->pen().style() == Qt::SolidLine,
                            g.isEquivalenceContinuous(chain.members[k], chain.members[k + 1])) << "seed " << seed;

                        std::vector<double> dummies;
                        auto it = std::find(nodes.begin(), nodes.end(), chain.members[k]);
                        const auto end = std::find(nodes.begin(), nodes.end(), chain.members[k + 1]);
                        ASSERT_LT(it, end) << "seed " << seed;
                        for (++it; it != end; ++it) {
                            ASSERT_TRUE((*it)->isDummy()) << "seed " << seed << ": a box inside a chain";
                            dummies.push_back(g.getNodeLayout().at(it->get()).x);
                        }
                        hops += static_cast<int>(dummies.size());

                        // Up over every dummy. With one dummy at most (no arch joining
                        // several), back on the layer everywhere else.
                        std::vector<bool> lifted(dummies.size(), false);
                        bool rises_elsewhere = false;
                        for (int i = 0; i <= 4000; ++i) {
                            const QPointF pt = path.pointAtPercent(i / 4000.0);
                            bool near_dummy = false;
                            for (size_t j = 0; j < dummies.size(); ++j) {
                                if (std::abs(pt.x() - dummies[j]) < 0.75 && pt.y() < layer_y - 2.0) lifted[j] = true;
                                near_dummy = near_dummy || std::abs(pt.x() - dummies[j]) < 6.0;
                            }
                            rises_elsewhere = rises_elsewhere || (pt.y() < layer_y - 0.5 && !near_dummy);
                        }
                        for (size_t j = 0; j < dummies.size(); ++j)
                            EXPECT_TRUE(lifted[j]) << "seed " << seed << ": a dummy the line does not hop over";
                        if (dummies.size() < 2)
                            EXPECT_FALSE(rises_elsewhere) << "seed " << seed << ": the line rises where there is no dummy";
                    }
                }
                EXPECT_EQ(next, d.lines.size()) << "seed " << seed;
            }
            EXPECT_GT(hops, 0); // some drawing really had a dummy inside a chain
        }

    } // namespace equivalence_line_tests
} // namespace app_logic
