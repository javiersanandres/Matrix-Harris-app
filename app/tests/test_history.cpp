#include "HypergraphEditor.h"
#include "JointHypergraphEditor.h"
#include "Project.h"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

// ============================================================================
// Undo/redo history
//
// Cosmetic changes (names, box attributes, line styles) are recorded as local
// changes instead of copies of the whole graph. They must still undo and redo
// correctly when mixed with the structural steps, which swap the whole graph.
//
// Saved state: the project has unsaved changes only while some diagram is not
// in the state it was saved in (or started in), whatever steps led there.
// ============================================================================

namespace app_logic {
    namespace history_tests {

        // The box called name in the editor's live graph.
        template <typename Editor>
        NodePtr named(const Editor& ed, const std::string& name) {
            for (const auto& n : ed.getAllNodes())
                if (!n->isDummy() && n->getName() == name) return n;
            return nullptr;
        }

        template <typename Editor>
        HyperedgePtr edgeBetween(const Editor& ed, const std::string& s, const std::string& t) {
            for (const auto& e : ed.getAllHyperedges())
                if (!e->isSegment() && e->containsSource(named(ed, s)) && e->containsTarget(named(ed, t))) return e;
            return nullptr;
        }

        // Everything that defines the drawing: every layer's boxes and bars in
        // order, with their coordinates.
        std::string drawing(const GraphicalHypergraph& g) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(3);
            for (const auto& [layer, data] : g.getLayers()) {
                out << "L" << layer << ":";
                for (const auto& n : data.nodes)
                    out << " " << (n->isDummy() ? std::string("~") : n->getName()) << "@" << g.getNodeLayout().at(n.get()).x;
                out << " |";
                for (const auto& e : data.outgoing_edges) {
                    out << " [";
                    for (const auto& s : e->getSources()) out << s->getName() << ",";
                    out << ">";
                    for (const auto& t : e->getTargets()) out << t->getName() << ",";
                    auto y = g.getEdgeLayout().find(e.get());
                    if (y != g.getEdgeLayout().end()) out << "y" << y->second;
                    out << "]";
                }
                out << "\n";
            }
            return out.str();
        }

        // ── Local changes mixed with structural steps ───────────────────────────

        TEST(History, LocalChangesSurviveStructuralUndoAndRedo) {
            HypergraphEditor ed(GraphicalHypergraph("g"));
            ed.createNode("A", 0, 0, nullptr);
            ed.createNode("B", 1, 0, named(ed, "A"));
            ed.renameNode(named(ed, "A"), "X");                            // local
            ed.createNode("C", 1, 1, named(ed, "X"));                      // structural
            ed.setHyperedgeContinuous(edgeBetween(ed, "X", "B"), false);   // local

            while (ed.canUndo()) ed.undo();
            EXPECT_TRUE(ed.getAllNodes().empty());

            ed.redo(); ed.redo();
            ASSERT_NE(named(ed, "A"), nullptr);
            ed.redo();
            EXPECT_EQ(named(ed, "A"), nullptr);
            ASSERT_NE(named(ed, "X"), nullptr);
            ed.redo(); ed.redo();
            ASSERT_NE(named(ed, "C"), nullptr);
            EXPECT_FALSE(edgeBetween(ed, "X", "B")->isContinuous());
            EXPECT_FALSE(ed.canRedo());

            // And back down through the structural step.
            ed.undo(); ed.undo(); ed.undo();
            EXPECT_EQ(named(ed, "C"), nullptr);
            EXPECT_NE(named(ed, "A"), nullptr);
            EXPECT_TRUE(edgeBetween(ed, "A", "B")->isContinuous());
        }

        TEST(History, DoubtfulEndsUndoAndRedoAcrossStructuralSteps) {
            HypergraphEditor ed(GraphicalHypergraph("g"));
            ed.createNode("A", 0, 0, nullptr);
            ed.createNode("B", 1, 0, named(ed, "A"));
            ed.createNode("C", 1, 1, nullptr);
            ed.addTargetToEdge(edgeBetween(ed, "A", "B"), named(ed, "C"));
            ed.setConnectionEndUncertain(edgeBetween(ed, "A", "C"), named(ed, "C"), true);  // local
            ed.createNode("D", 0, 1, nullptr);                                               // structural

            ed.undo(); ed.undo();
            EXPECT_FALSE(edgeBetween(ed, "A", "C")->hasUncertainEnds());
            ed.redo(); ed.redo();
            EXPECT_TRUE(edgeBetween(ed, "A", "C")->isTargetUncertain(named(ed, "C").get()));
            EXPECT_FALSE(edgeBetween(ed, "A", "C")->isTargetUncertain(named(ed, "B").get()));
        }

        TEST(History, NothingIsRecordedForAChangeThatChangesNothing) {
            HypergraphEditor ed(GraphicalHypergraph("g"));
            ed.createNode("A", 0, 0, nullptr);
            ed.undo(); ed.redo();
            ed.renameNode(named(ed, "A"), "A");
            ed.setNodeAttributes(named(ed, "A"), named(ed, "A")->getAttributes());
            ed.setName("g");
            ed.undo();
            EXPECT_FALSE(ed.canUndo()); // only the creation was there
        }

        TEST(History, RenamingTheDiagramIsUndoable) {
            HypergraphEditor ed(GraphicalHypergraph("g"));
            ed.setName("Planta");
            ed.undo();
            EXPECT_EQ(ed.getName(), "g");
            ed.redo();
            EXPECT_EQ(ed.getName(), "Planta");
        }

        TEST(History, ABoxNotInTheDiagramIsRejected) {
            HypergraphEditor ed(GraphicalHypergraph("g"));
            NodePtr a = ed.createNode("A", 0, 0, nullptr);
            ed.undo(); // a no longer belongs to the live graph
            EXPECT_THROW(ed.renameNode(a, "Z"), std::invalid_argument);
            EXPECT_FALSE(ed.canUndo());
        }

        // ── A new shape re-lays out; undo brings back the exact drawing ─────────

        TEST(History, UndoingAShapeChangeRestoresTheExactDrawing) {
            GraphicalHypergraph g("g");
            auto a = g.createNode("a", 0, 0, nullptr);
            auto b = g.createNode("b", 0, 1, nullptr);
            auto c = g.createNode("c", 0, 2, nullptr);
            auto d = g.createNode("d", 1, 0, a);
            auto e = g.createNode("e", 1, 1, b);
            auto f = g.createNode("f", 1, 2, c);
            g.addConnection(a, e);
            g.addConnection(c, d);
            g.addConnection(b, f);
            g.addConnection(a, f);
            HypergraphEditor ed(std::move(g));
            const std::string before = drawing(ed.getGraph());

            NodeAttributes wide = named(ed, "b")->getAttributes();
            wide.shape = NodeShape::Circle;
            ed.setNodeAttributes(named(ed, "b"), wide);
            const std::string after = drawing(ed.getGraph());
            EXPECT_NE(before, after); // the new shape moves things

            ed.undo();
            EXPECT_EQ(drawing(ed.getGraph()), before);
            EXPECT_EQ(named(ed, "b")->getShape(), NodeShape::Rectangle);
            ed.redo();
            EXPECT_EQ(drawing(ed.getGraph()), after);
            EXPECT_EQ(named(ed, "b")->getShape(), NodeShape::Circle);
        }

        // ── Joint diagram ───────────────────────────────────────────────────────

        TEST(History, JointLocalChangesSurviveItsStructuralSteps) {
            JointHypergraphEditor jed(JointGraphicalHypergraph::create("j"));
            GraphicalHypergraph g("g1");
            auto a = g.createNode("A", 0, 0, nullptr);
            g.createNode("B", 1, 0, a);
            jed.addHypergraph(g, true);                    // structural
            jed.renameNode(named(jed, "A"), "X");          // local

            jed.undo(); jed.undo();
            EXPECT_TRUE(jed.getAllNodes().empty());
            jed.redo(); jed.redo();
            EXPECT_NE(named(jed, "X"), nullptr);
            EXPECT_EQ(named(jed, "A"), nullptr);
        }

        // ── Saved state ─────────────────────────────────────────────────────────

        TEST(SavedState, UndoingEveryChangeGoesBackToTheSavedState) {
            HypergraphEditor ed(GraphicalHypergraph("g"));
            EXPECT_TRUE(ed.isAtSavedState());
            ed.createNode("A", 0, 0, nullptr);
            ed.renameNode(named(ed, "A"), "B");
            EXPECT_FALSE(ed.isAtSavedState());
            ed.undo();
            EXPECT_FALSE(ed.isAtSavedState());
            ed.undo();
            EXPECT_TRUE(ed.isAtSavedState());
            ed.redo();
            EXPECT_FALSE(ed.isAtSavedState());
        }

        TEST(SavedState, SavingInTheMiddleOfTheHistory) {
            HypergraphEditor ed(GraphicalHypergraph("g"));
            ed.createNode("A", 0, 0, nullptr);
            ed.createNode("B", 0, 1, nullptr);
            ed.undo();
            ed.markSaved();                // saved with only A
            ed.redo();
            EXPECT_FALSE(ed.isAtSavedState());
            ed.undo();
            EXPECT_TRUE(ed.isAtSavedState());
            ed.undo();
            EXPECT_FALSE(ed.isAtSavedState());
            ed.redo();
            EXPECT_TRUE(ed.isAtSavedState());
        }

        TEST(SavedState, ADifferentChangeAtTheSameDepthIsNotTheSavedState) {
            HypergraphEditor ed(GraphicalHypergraph("g"));
            ed.createNode("A", 0, 0, nullptr);
            ed.markSaved();
            ed.undo();
            ed.createNode("B", 0, 0, nullptr); // same number of steps, different diagram
            EXPECT_FALSE(ed.isAtSavedState());
        }

        TEST(SavedState, ProjectIsCleanOnceEveryDiagramIsBackWhereItWas) {
            namespace fs = std::filesystem;
            Project p("P");
            EXPECT_FALSE(p.hasUnsavedChanges());
            p.getEditor(0).createNode("A", 0, 0, nullptr);
            EXPECT_TRUE(p.hasUnsavedChanges());
            p.getEditor(0).undo();
            EXPECT_FALSE(p.hasUnsavedChanges());

            // After saving, undoing back to what was saved is clean again.
            p.getEditor(0).redo();
            const fs::path tmp = fs::temp_directory_path() / "test_history_saved_state.json";
            p.save(tmp);
            EXPECT_FALSE(p.hasUnsavedChanges());
            p.getEditor(0).renameNode(named(p.getEditor(0), "A"), "B");
            p.getEditor(0).createNode("C", 0, 1, nullptr);
            EXPECT_TRUE(p.hasUnsavedChanges());
            p.getEditor(0).undo();
            p.getEditor(0).undo();
            EXPECT_FALSE(p.hasUnsavedChanges());
            // ...and so is undoing past it, then redoing up to it.
            p.getEditor(0).undo();
            EXPECT_TRUE(p.hasUnsavedChanges());
            p.getEditor(0).redo();
            EXPECT_FALSE(p.hasUnsavedChanges());
            fs::remove(tmp);
        }

        TEST(SavedState, ChangesOutsideTheHistoryStayUnsavedUntilSaved) {
            Project p("P");
            p.addDiagram();
            EXPECT_TRUE(p.hasUnsavedChanges()); // cannot be undone
        }

        // ── Opening a project keeps the saved layout ────────────────────────────

        TEST(OpenProject, DiagramsOpenExactlyAsSaved) {
            namespace fs = std::filesystem;
            const fs::path tmp = fs::temp_directory_path() / "test_open_keeps_layout.json";
            std::string saved_diagram, saved_joint;
            {
                Project p("P");
                auto& ed = p.getEditor(0);
                ed.createNode("A", 0, 0, nullptr);
                ed.createNode("B", 0, 1, nullptr);
                ed.createNode("C", 1, 0, named(ed, "A"));
                ed.addSourceToEdge(edgeBetween(ed, "A", "C"), named(ed, "B"));
                ed.createNode("D", 2, 0, named(ed, "B"));
                GraphicalHypergraph copy = ed.getGraph().clone();
                p.getJointEditor().addHypergraph(copy, true);
                saved_diagram = drawing(ed.getGraph());
                saved_joint = drawing(p.getJointEditor().getGraph());
                p.save(tmp);
            }
            auto loaded = Project::load(tmp);
            EXPECT_EQ(drawing(loaded->getEditor(0).getGraph()), saved_diagram);
            EXPECT_EQ(drawing(loaded->getJointEditor().getGraph()), saved_joint);
            EXPECT_FALSE(loaded->hasUnsavedChanges());
            fs::remove(tmp);
        }

        TEST(OpenProject, TheLayoutIsReadNotRecomputed) {
            // A coordinate changed in the file must survive opening it: laying
            // the diagram out again would put the box back where it belongs.
            namespace fs = std::filesystem;
            const fs::path tmp = fs::temp_directory_path() / "test_open_reads_layout.json";
            {
                Project p("P");
                p.getEditor(0).createNode("A", 0, 0, nullptr);
                p.getEditor(0).createNode("B", 0, 1, nullptr);
                p.save(tmp);
            }
            json j;
            { std::ifstream in(tmp); in >> j; }
            auto& entry = j.at("diagrams").at(0).at("layout").at("node_layout").at(0);
            const double moved_x = entry.at("x").get<double>() + 1234.5;
            entry["x"] = moved_x;
            { std::ofstream out(tmp); out << j.dump(); }

            auto loaded = Project::load(tmp);
            bool found = false;
            for (const auto& [node, layout] : loaded->getEditor(0).getGraph().getNodeLayout())
                found = found || layout.x == moved_x;
            EXPECT_TRUE(found);
            fs::remove(tmp);
        }

    } // namespace history_tests
} // namespace app_logic
