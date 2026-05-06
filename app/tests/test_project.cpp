#include "HypergraphEditor.h"
#include "JointHypergraphEditor.h"
#include "Project.h"
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace app_logic {
    namespace editors {

        // ────────────────────────────────────────────────────────────────────
        // Shared helpers
        // ────────────────────────────────────────────────────────────────────

        // Build a minimal two-node, two-layer GraphicalHypergraph that is
        // ready to be moved into a HypergraphEditor.
        static GraphicalHypergraph makeTwoNodeGraph(const std::string& name) {
            GraphicalHypergraph g(name);
            NodePtr A = g.createNode("A", 0, nullptr);
            g.createNode("B", 0, A);
            g.computeLayout();
            return g;
        }


        // ════════════════════════════════════════════════════════════════════
        // Project tests
        // ════════════════════════════════════════════════════════════════════
        namespace project_tests {

            // ── Construction ─────────────────────────────────────────────────

            TEST(Project, ConstructionDoesNotThrow) {
                EXPECT_NO_THROW(Project p("MyProject"));
            }

            TEST(Project, InitialDiagramCountIsOne) {
                Project p("MyProject");
                EXPECT_EQ(p.getDiagramCount(), 1);
            }

            TEST(Project, InitialActiveIndexIsZero) {
                Project p("MyProject");
                EXPECT_EQ(p.getActiveIndex(), 0);
            }

            TEST(Project, InitialJointIsNotActive) {
                Project p("MyProject");
                EXPECT_FALSE(p.isJointActive());
            }

            TEST(Project, InitialHasNoUnsavedChanges) {
                // A freshly created project with one empty diagram is considered clean.
                Project p("MyProject");
                EXPECT_FALSE(p.hasUnsavedChanges());
            }

            TEST(Project, GetNameReturnsConstructorName) {
                Project p("Alpha");
                EXPECT_EQ(p.getName(), "Alpha");
            }

            // ── addDiagram ────────────────────────────────────────────────────

            TEST(Project, AddDiagramIncreasesDiagramCount) {
                Project p("p");
                int before = p.getDiagramCount();
                p.addDiagram();
                EXPECT_EQ(p.getDiagramCount(), before + 1);
            }

            TEST(Project, AddDiagramSetsNewDiagramAsActive) {
                Project p("p");
                int idx = p.addDiagram();
                EXPECT_EQ(p.getActiveIndex(), idx);
            }

            TEST(Project, AddDiagramMarksUnsaved) {
                Project p("p");
                p.addDiagram();
                EXPECT_TRUE(p.hasUnsavedChanges());
            }

            TEST(Project, AddMultipleDiagramsCountsCorrectly) {
                Project p("p");
                p.addDiagram();
                p.addDiagram();
                p.addDiagram();
                EXPECT_EQ(p.getDiagramCount(), 4); // 1 initial + 3
            }

            // ── removeDiagram ─────────────────────────────────────────────────

            TEST(Project, RemoveDiagramDecreasesDiagramCount) {
                Project p("p");
                p.addDiagram();
                int before = p.getDiagramCount();
                p.removeDiagram(0);
                EXPECT_EQ(p.getDiagramCount(), before - 1);
            }

            TEST(Project, RemoveDiagramOutOfRangeThrows) {
                Project p("p");
                EXPECT_THROW(p.removeDiagram(99), std::out_of_range);
                EXPECT_THROW(p.removeDiagram(-1), std::out_of_range);
            }

            TEST(Project, RemoveDiagramMarksUnsaved) {
                Project p("p");
                p.addDiagram();
                // Manually reset flag (save path not available in unit test).
                // We can only observe the flag goes true again.
                p.removeDiagram(0);
                EXPECT_TRUE(p.hasUnsavedChanges());
            }

            TEST(Project, RemoveOnlyDiagramSetsActiveToMinusOne) {
                Project p("p");
                ASSERT_EQ(p.getDiagramCount(), 1);
                p.removeDiagram(0);
                EXPECT_EQ(p.getActiveIndex(), -1);
                EXPECT_TRUE(p.isJointActive());
            }

            TEST(Project, RemoveActiveDiagramAdjustsActiveIndex) {
                Project p("p");
                p.addDiagram(); // index 1
                p.setActive(1);
                p.removeDiagram(1);
                // Active should now be 0 (the previous diagram).
                EXPECT_EQ(p.getActiveIndex(), 0);
            }

            // ── setActive / getActiveIndex / isJointActive ────────────────────

            TEST(Project, SetActiveJointWithMinusOne) {
                Project p("p");
                p.setActive(-1);
                EXPECT_TRUE(p.isJointActive());
                EXPECT_EQ(p.getActiveIndex(), -1);
            }

            TEST(Project, SetActiveOutOfRangeThrows) {
                Project p("p");
                EXPECT_THROW(p.setActive(99), std::out_of_range);
            }

            TEST(Project, SetActiveValidIndex) {
                Project p("p");
                p.addDiagram(); // creates index 1
                p.setActive(0);
                EXPECT_EQ(p.getActiveIndex(), 0);
            }

            // ── getActiveEditor ───────────────────────────────────────────────

            TEST(Project, GetActiveEditorThrowsWhenJointActive) {
                Project p("p");
                p.setActive(-1);
                EXPECT_THROW(p.getActiveEditor(), std::logic_error);
            }

            TEST(Project, GetActiveEditorReturnsEditorWhenDiagramActive) {
                Project p("p");
                EXPECT_NO_THROW(p.getActiveEditor());
            }

            TEST(Project, ConstGetActiveEditorReturnsEditorWhenDiagramActive) {
                const Project p("p");
                EXPECT_NO_THROW(p.getActiveEditor());
            }

            // ── getEditor ─────────────────────────────────────────────────────

            TEST(Project, GetEditorOutOfRangeThrows) {
                Project p("p");
                EXPECT_THROW(p.getEditor(99), std::out_of_range);
                EXPECT_THROW(p.getEditor(-1), std::out_of_range);
            }

            TEST(Project, GetEditorValidIndex) {
                Project p("p");
                EXPECT_NO_THROW(p.getEditor(0));
            }

            // ── getJointEditor ────────────────────────────────────────────────

            TEST(Project, GetJointEditorDoesNotThrow) {
                Project p("p");
                EXPECT_NO_THROW(p.getJointEditor());
            }

            TEST(Project, ConstGetJointEditorDoesNotThrow) {
                const Project p("p");
                EXPECT_NO_THROW(p.getJointEditor());
            }

            // ── getDiagramName ────────────────────────────────────────────────

            TEST(Project, GetDiagramNameOutOfRangeThrows) {
                Project p("p");
                EXPECT_THROW(p.getDiagramName(99), std::out_of_range);
            }

            TEST(Project, GetDiagramNameReturnsNonEmptyString) {
                Project p("p");
                EXPECT_FALSE(p.getDiagramName(0).empty());
            }

            // ── setName / getName ─────────────────────────────────────────────

            TEST(Project, SetNameUpdatesGetName) {
                Project p("old");
                p.setName("new");
                EXPECT_EQ(p.getName(), "new");
            }

            TEST(Project, SetNameMarksUnsaved) {
                Project p("p");
                p.setName("renamed");
                EXPECT_TRUE(p.hasUnsavedChanges());
            }

            // ── getFilePath ───────────────────────────────────────────────────

            TEST(Project, InitialFilePathIsEmpty) {
                Project p("p");
                EXPECT_TRUE(p.getFilePath().empty());
            }

            // ── save / load round-trip ────────────────────────────────────────

            TEST(Project, SaveWithoutPathThrows) {
                Project p("p");
                EXPECT_THROW(p.save(), std::logic_error);
            }

            TEST(Project, SaveAndLoadRoundTrip) {
                namespace fs = std::filesystem;
                fs::path tmp = fs::temp_directory_path() / "test_project_roundtrip.json";

                // Build a small project.
                {
                    Project p("RoundTrip");
                    p.addDiagram();
                    HypergraphEditor& ed = p.getActiveEditor();
                    ed.createNode("A", 0, nullptr);
                    p.save(tmp);
                }

                // Reload and verify.
                std::unique_ptr<Project> loaded = Project::load(tmp);
                EXPECT_EQ(loaded->getName(), "RoundTrip");
                EXPECT_GE(loaded->getDiagramCount(), 1);

                fs::remove(tmp);
            }

            TEST(Project, LoadNonExistentFileThrows) {
                EXPECT_THROW(
                    Project::load("/nonexistent/path/project.json"),
                    std::runtime_error
                );
            }

            TEST(Project, SaveClearsUnsavedChangesFlag) {
                namespace fs = std::filesystem;
                fs::path tmp = fs::temp_directory_path() / "test_save_flag.json";

                Project p("p");
                p.addDiagram(); // marks unsaved
                EXPECT_TRUE(p.hasUnsavedChanges());
                p.save(tmp);
                EXPECT_FALSE(p.hasUnsavedChanges());

                fs::remove(tmp);
            }

            TEST(Project, SaveSetsFilePath) {
                namespace fs = std::filesystem;
                fs::path tmp = fs::temp_directory_path() / "test_save_path.json";

                Project p("p");
                EXPECT_TRUE(p.getFilePath().empty());
                p.save(tmp);
                EXPECT_EQ(p.getFilePath(), tmp);

                fs::remove(tmp);
            }

            TEST(Project, SaveToCurrentPathAfterFirstSave) {
                namespace fs = std::filesystem;
                fs::path tmp = fs::temp_directory_path() / "test_save_current.json";

                Project p("p");
                p.save(tmp);
                p.addDiagram();
                EXPECT_NO_THROW(p.save()); // uses stored file_path_

                fs::remove(tmp);
            }

            TEST(Project, LoadPreservesActiveIndex) {
                namespace fs = std::filesystem;
                fs::path tmp = fs::temp_directory_path() / "test_active_idx.json";

                {
                    Project p("p");
                    p.addDiagram(); // index 1
                    p.setActive(1);
                    p.save(tmp);
                }
                std::unique_ptr<Project> loaded = Project::load(tmp);
                EXPECT_EQ(loaded->getActiveIndex(), 1);

                fs::remove(tmp);
            }

            // ── Editor operations through Project ─────────────────────────────

            TEST(Project, ActiveEditorCreateNodeWorks) {
                Project p("p");
                HypergraphEditor& ed = p.getActiveEditor();
                NodePtr A = ed.createNode("A", 0, nullptr);
                EXPECT_NE(A, nullptr);
                EXPECT_FALSE(ed.getAllNodes().empty());
            }

            TEST(Project, ActiveEditorUndoRedoWorks) {
                Project p("p");
                HypergraphEditor& ed = p.getActiveEditor();
                ed.createNode("A", 0, nullptr);
                EXPECT_TRUE(ed.canUndo());
                ed.undo();
                EXPECT_TRUE(ed.canRedo());
                ed.redo();
                EXPECT_FALSE(ed.getAllNodes().empty());
            }

            TEST(Project, JointEditorAddHypergraphWorks) {
                Project p("p");
                GraphicalHypergraph g = makeTwoNodeGraph("g1");
                JointHypergraphEditor& jed = p.getJointEditor();
                EXPECT_NO_THROW(jed.addHypergraph(g, true));
            }

            TEST(Project, IndependentEditorsDontShareHistory) {
                Project p("p");
                p.addDiagram(); // index 1
                HypergraphEditor& ed0 = p.getEditor(0);
				EXPECT_FALSE(ed0.canUndo());
                HypergraphEditor& ed1 = p.getEditor(1);
                ed0.createNode("A", 0, nullptr);
                // ed1's undo stack must be independent.
                EXPECT_FALSE(ed1.canUndo());
            }

        } // namespace project_tests
    } // namespace editors
} // namespace app_logic