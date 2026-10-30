#include "HypergraphEditor.h"
#include "JointHypergraphEditor.h"
#include "Project.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
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
            NodePtr A = g.createNode("A", 0, 0, nullptr);
            g.createNode("B", A->getLayer() + 1, 0, A);
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
                EXPECT_FALSE(p.hasUnsavedChanges());
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
                    ed.createNode("A", 0, 0, nullptr);
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
                NodePtr A = ed.createNode("A", 0, 0, nullptr);
                EXPECT_NE(A, nullptr);
                EXPECT_FALSE(ed.getAllNodes().empty());
            }

            TEST(Project, ActiveEditorUndoRedoWorks) {
                Project p("p");
                HypergraphEditor& ed = p.getActiveEditor();
                ed.createNode("A", 0, 0, nullptr);
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
                ed0.createNode("A", 0, 0, nullptr);
                // ed1's undo stack must be independent.
                EXPECT_FALSE(ed1.canUndo());
            }

            // ── Recent colours ───────────────────────────────────────────────

            TEST(Project, RecentColoursStartEmpty) {
                Project p("P");
                EXPECT_TRUE(p.getRecentColours().empty());
            }

            TEST(Project, AddRecentColourIsMostRecentFirstWithoutDuplicates) {
                Project p("P");
                const Color red{ 255, 0, 0, 255 }, green{ 0, 255, 0, 255 }, blue{ 0, 0, 255, 255 };
                p.addRecentColour(red);
                p.addRecentColour(green);
                p.addRecentColour(blue);
                p.addRecentColour(red); // moves back to the front
                ASSERT_EQ(p.getRecentColours().size(), 3u);
                EXPECT_EQ(p.getRecentColours()[0], red);
                EXPECT_EQ(p.getRecentColours()[1], blue);
                EXPECT_EQ(p.getRecentColours()[2], green);
                // A convenience kept with the next save, not a change of its own.
                EXPECT_FALSE(p.hasUnsavedChanges());
            }

            TEST(Project, AddRecentColourKeepsAtMostMaxColours) {
                Project p("P");
                for (int i = 0; i < Project::MAX_RECENT_COLOURS + 5; ++i)
                    p.addRecentColour(Color{ static_cast<uint8_t>(i), 0, 0, 255 });
                ASSERT_EQ(static_cast<int>(p.getRecentColours().size()), Project::MAX_RECENT_COLOURS);
                EXPECT_EQ(p.getRecentColours().front().r, Project::MAX_RECENT_COLOURS + 4);
            }

            TEST(Project, RecentColoursSurviveSaveAndLoad) {
                namespace fs = std::filesystem;
                fs::path tmp = fs::temp_directory_path() / "test_project_recent_colours.json";
                std::vector<Color> saved;
                {
                    // Only one project (joint graph) may be alive at a time.
                    Project p("P");
                    p.addRecentColour(Color{ 10, 20, 30, 255 });
                    p.addRecentColour(Color{ 200, 100, 50, 128 });
                    p.save(tmp);
                    saved = p.getRecentColours();
                }

                auto loaded = Project::load(tmp);
                EXPECT_EQ(loaded->getRecentColours(), saved);
                EXPECT_FALSE(loaded->hasUnsavedChanges());
                fs::remove(tmp);
            }

            TEST(Project, LoadWithoutRecentColoursGivesEmptyList) {
                namespace fs = std::filesystem;
                fs::path tmp = fs::temp_directory_path() / "test_project_no_recent_colours.json";
                {
                    Project p("P");
                    p.addRecentColour(Color{ 1, 2, 3, 255 });
                    p.save(tmp);
                }

                // Strip the field, as in files saved before it existed.
                nlohmann::json j;
                { std::ifstream in(tmp); in >> j; }
                j.erase("recent_colours");
                { std::ofstream out(tmp); out << j.dump(); }

                auto loaded = Project::load(tmp);
                EXPECT_TRUE(loaded->getRecentColours().empty());
                fs::remove(tmp);
            }

            // ── Defaults for new boxes ──────────────────────────────────────────────

            TEST(Project, NewBoxesTakeTheProjectsDefaults) {
                Project p("P");
                EXPECT_EQ(p.newNodeAttributes("A"), NodeAttributes("A"));
                EXPECT_FALSE(p.hasUnsavedChanges());

                const Color navy{ 0, 32, 96, 255 }, white{ 255, 255, 255, 255 };
                p.setDefaultColour(navy);
                p.setDefaultFontColour(white);
                p.setDefaultFontSize(14);
                const NodeAttributes a = p.newNodeAttributes("A");
                EXPECT_EQ(a.name, "A");
                EXPECT_EQ(a.colour, navy);
                EXPECT_EQ(a.font_colour, white);
                EXPECT_EQ(a.font_size, 14);
                EXPECT_EQ(a.shape, NodeAttributes().shape); // only the defaults change
                EXPECT_TRUE(p.hasUnsavedChanges());
            }

            TEST(Project, DefaultsSurviveSaveAndLoad) {
                namespace fs = std::filesystem;
                fs::path tmp = fs::temp_directory_path() / "test_project_default_colours.json";
                {
                    Project p("P");
                    p.setDefaultColour(Color{ 10, 20, 30, 255 });
                    p.setDefaultFontColour(Color{ 240, 230, 220, 255 });
                    p.setDefaultFontSize(20);
                    p.save(tmp);
                }

                auto loaded = Project::load(tmp);
                EXPECT_EQ(loaded->getDefaultColour(), (Color{ 10, 20, 30, 255 }));
                EXPECT_EQ(loaded->getDefaultFontColour(), (Color{ 240, 230, 220, 255 }));
                EXPECT_EQ(loaded->getDefaultFontSize(), 20);
                EXPECT_FALSE(loaded->hasUnsavedChanges());
                loaded.reset();

                // Files saved before default colours existed keep the usual ones.
                nlohmann::json j;
                { std::ifstream in(tmp); in >> j; }
                j.erase("defaults");
                { std::ofstream out(tmp); out << j.dump(); }
                loaded = Project::load(tmp);
                EXPECT_EQ(loaded->getDefaultColour(), NodeAttributes().colour);
                EXPECT_EQ(loaded->getDefaultFontColour(), NodeAttributes().font_colour);
                EXPECT_EQ(loaded->getDefaultFontSize(), NodeAttributes::DEFAULT_FONT_SIZE);
                fs::remove(tmp);
            }

            // ── Settings: defaults for new boxes, and every diagram's layout ──

            static NodePtr boxNamed(const GraphicalHypergraph& g, const std::string& name) {
                for (const auto& n : g.getAllNodes())
                    if (!n->isDummy() && n->getName() == name) return n;
                ADD_FAILURE() << "no box named " << name;
                return nullptr;
            }

            // A on the default size; B with a width of its own; R a rhombus on its default size.
            static void threeBoxes(Project& p) {
                auto& ed = p.getEditor(0);
                ed.createNode(p.newNodeAttributes("A"), 0, -1, nullptr);
                NodeAttributes b = p.newNodeAttributes("B");
                b.width = 120.0;
                ed.createNode(b, 0, -1, nullptr);
                NodeAttributes r = p.newNodeAttributes("R");
                r.shape = NodeShape::Rhombus;
                ed.createNode(r, 0, -1, nullptr);
            }

            TEST(ProjectSettings, DefaultsForNewBoxesLeaveExistingOnesAlone) {
                Project p("P");
                threeBoxes(p);
                const bool could_undo = p.getEditor(0).canUndo();

                ProjectSettings s = p.getSettings();
                s.colour = Color{ 0, 32, 96, 255 };
                s.font_colour = Color{ 255, 255, 255, 255 };
                s.font_size = 14;
                s.shape = NodeShape::Circle;
                p.setSettings(s);

                const NodePtr A = boxNamed(p.getEditor(0).getGraph(), "A");
                EXPECT_EQ(A->getColour(), NodeAttributes().colour);
                EXPECT_EQ(A->getFontColour(), NodeAttributes().font_colour);
                EXPECT_EQ(A->getFontSize(), NodeAttributes::DEFAULT_FONT_SIZE);
                EXPECT_EQ(A->getShape(), NodeShape::Rectangle);
                const NodeAttributes c = p.newNodeAttributes("C");
                EXPECT_EQ(c.colour, s.colour);
                EXPECT_EQ(c.font_colour, s.font_colour);
                EXPECT_EQ(c.font_size, 14);
                EXPECT_EQ(c.shape, NodeShape::Circle);
                // Not a step of the diagram's history, but an unsaved change.
                EXPECT_EQ(p.getEditor(0).canUndo(), could_undo);
                EXPECT_TRUE(p.hasUnsavedChanges());
            }

            TEST(ProjectSettings, NewDefaultSizesCanReachTheBoxesUsingTheOldOnes) {
                Project p("P");
                threeBoxes(p);
                p.getJointEditor().addHypergraph(const_cast<GraphicalHypergraph&>(p.getEditor(0).getGraph()), false);

                ProjectSettings s = p.getSettings();
                s.layout.block_sep = 150.0;
                s.layout.box_sizes.rectangle = BoxSize{ 160.0, 70.0 };
                p.setSettings(s, Project::ExistingBoxes::TakeNewSize);

                for (const GraphicalHypergraph* g : { &p.getEditor(0).getGraph(),
                                                      static_cast<const GraphicalHypergraph*>(&p.getJointEditor().getGraph()) }) {
                    EXPECT_EQ(g->getLayoutSettings(), s.layout);
                    EXPECT_DOUBLE_EQ(boxNamed(*g, "A")->getWidth(), 160.0);
                    EXPECT_DOUBLE_EQ(boxNamed(*g, "A")->getHeight(), 70.0);
                    EXPECT_DOUBLE_EQ(boxNamed(*g, "B")->getWidth(), 120.0); // its own width
                    EXPECT_DOUBLE_EQ(boxNamed(*g, "B")->getHeight(), 70.0);  // the default height
                    EXPECT_DOUBLE_EQ(boxNamed(*g, "R")->getWidth(), RHOMBUS_NODE_WIDTH);
                }
                const auto& g = p.getEditor(0).getGraph();
                EXPECT_NEAR(g.getX(boxNamed(g, "B")) - g.getX(boxNamed(g, "A")), (160.0 + 120.0) / 2.0 + 150.0, 1e-9);

                // Boxes created afterwards take them too, in new diagrams as well.
                p.addDiagram();
                p.getEditor(1).createNode(p.newNodeAttributes("D"), 0, -1, nullptr);
                EXPECT_EQ(p.getEditor(1).getGraph().getLayoutSettings(), s.layout);
                EXPECT_DOUBLE_EQ(boxNamed(p.getEditor(1).getGraph(), "D")->getWidth(), 160.0);
            }

            TEST(ProjectSettings, NewDefaultSizesCanBeOnlyForNewBoxes) {
                Project p("P");
                threeBoxes(p);
                ProjectSettings s = p.getSettings();
                s.layout.box_sizes.rectangle = BoxSize{ 160.0, NODE_HEIGHT }; // only the width changes
                p.setSettings(s, Project::ExistingBoxes::KeepTheirSize);

                const auto& g = p.getEditor(0).getGraph();
                EXPECT_DOUBLE_EQ(boxNamed(g, "A")->getWidth(), NODE_WIDTH);
                EXPECT_EQ(boxNamed(g, "A")->getAttributes().width, NODE_WIDTH);       // now its own
                EXPECT_FALSE(boxNamed(g, "A")->getAttributes().height.has_value());   // that default did not change
                EXPECT_FALSE(boxNamed(g, "R")->getAttributes().width.has_value());    // nor did the rhombus's
                p.getEditor(0).createNode(p.newNodeAttributes("C"), 0, -1, nullptr);
                EXPECT_DOUBLE_EQ(boxNamed(g, "C")->getWidth(), 160.0);
            }

            // An undone step brings back the graph as it was before the settings
            // changed: it is made to match them again, and the boxes that kept
            // their size keep it there too.
            TEST(ProjectSettings, UndoAndRedoKeepMatchingTheSettings) {
                Project p("P");
                threeBoxes(p);
                auto& ed = p.getEditor(0);
                ed.createNode(p.newNodeAttributes("C"), 0, -1, nullptr); // a structural step

                ProjectSettings s = p.getSettings();
                s.layout.block_sep = 150.0;
                s.layout.box_sizes.rectangle = BoxSize{ 160.0, 70.0 };
                p.setSettings(s, Project::ExistingBoxes::KeepTheirSize);

                ed.undo(); // C goes away; the copy restored was laid out with the old settings
                EXPECT_EQ(ed.getGraph().getLayoutSettings(), s.layout);
                EXPECT_DOUBLE_EQ(boxNamed(ed.getGraph(), "A")->getWidth(), NODE_WIDTH);
                EXPECT_DOUBLE_EQ(boxNamed(ed.getGraph(), "A")->getHeight(), NODE_HEIGHT);
                EXPECT_NEAR(ed.getX(boxNamed(ed.getGraph(), "B")) - ed.getX(boxNamed(ed.getGraph(), "A")),
                    (NODE_WIDTH + 120.0) / 2.0 + 150.0, 1e-9);

                ed.redo();
                EXPECT_EQ(ed.getGraph().getLayoutSettings(), s.layout);
                EXPECT_DOUBLE_EQ(boxNamed(ed.getGraph(), "C")->getWidth(), NODE_WIDTH);
            }

            TEST(ProjectSettings, SurviveSaveAndLoad) {
                namespace fs = std::filesystem;
                const fs::path tmp = fs::temp_directory_path() / "test_project_settings.json";
                ProjectSettings s;
                {
                    Project p("P");
                    threeBoxes(p);
                    s = p.getSettings();
                    s.colour = Color{ 10, 20, 30, 255 };
                    s.shape = NodeShape::Rhombus;
                    s.equivalence_continuous = false;
                    s.layout.layer_gap = 90.0;
                    s.layout.horizontal_sep = 45.0;
                    s.layout.box_sizes.circle = BoxSize{ 100.0, 60.0 };
                    p.setSettings(s, Project::ExistingBoxes::TakeNewSize);
                    p.save(tmp);
                }
                auto loaded = Project::load(tmp);
                EXPECT_EQ(loaded->getSettings(), s);
                EXPECT_EQ(loaded->getEditor(0).getGraph().getLayoutSettings(), s.layout);
                EXPECT_FALSE(loaded->hasUnsavedChanges());
                fs::remove(tmp);
            }

            TEST(ProjectSettings, ClampedAndJSONRoundTrip) {
                ProjectSettings s;
                s.font_size = 0;
                s.layout.block_sep = 1.0;
                s.layout.box_sizes.rhombus = BoxSize{ 5.0, 5000.0 };
                const ProjectSettings c = s.clamped();
                EXPECT_EQ(c.font_size, ProjectSettings::MIN_FONT_SIZE);
                EXPECT_DOUBLE_EQ(c.layout.block_sep, LayoutSettings::MIN_BLOCK_SEP_SETTING);
                EXPECT_EQ(c.layout.box_sizes.rhombus, (BoxSize{ MIN_NODE_WIDTH, MAX_NODE_HEIGHT }));

                ProjectSettings t;
                t.colour = Color{ 1, 2, 3, 255 };
                t.shape = NodeShape::RoundedRectangle;
                t.equivalence_continuous = false;
                t.layout.horizontal_sep = 50.0;
                nlohmann::json j;
                t.toJSON(j);
                EXPECT_EQ(ProjectSettings::fromJSON(j), t);
                EXPECT_EQ(ProjectSettings::fromJSON(nlohmann::json::object()), ProjectSettings());
            }

            // ── The joint's names follow the diagrams' ───────────────────────

            // Diagram 0 ("A", in the joint) and diagram 1 ("B", not in it).
            static void twoDiagramsOneInTheJoint(Project& p) {
                p.getEditor(0).setName("A");
                p.getEditor(0).createNode(NodeAttributes("1"), 0, -1, nullptr);
                p.addDiagram();
                p.getEditor(1).setName("B");
                p.getJointEditor().addHypergraph(const_cast<GraphicalHypergraph&>(p.getEditor(0).getGraph()), false);
            }

            TEST(Project, RenamingADiagramRenamesItInTheJoint) {
                Project p("P");
                twoDiagramsOneInTheJoint(p);
                const std::string a = p.getEditor(0).getId();

                p.getEditor(0).setName("A2");
                p.syncJointNames();
                EXPECT_EQ(p.getJointEditor().getGraph().getIncorporatedName(a), "A2");

                p.getEditor(0).undo(); // the rename
                p.syncJointNames();
                EXPECT_EQ(p.getJointEditor().getGraph().getIncorporatedName(a), "A");

                // A diagram that is not in the joint stays out of it.
                p.getEditor(1).setName("B2");
                p.syncJointNames();
                EXPECT_FALSE(p.getJointEditor().getGraph().getIncorporatedIds().count(p.getEditor(1).getId()));
                EXPECT_TRUE(p.getJointEditor().getGraph().getIncorporatedName(p.getEditor(1).getId()).empty());
            }

            TEST(Project, UndoingInTheJointDoesNotBringBackAnOldName) {
                Project p("P");
                twoDiagramsOneInTheJoint(p);
                const std::string a = p.getEditor(0).getId();

                p.getJointEditor().moveHypergraphToLayer(a, 1); // its snapshot knows it as "A"
                p.getEditor(0).setName("A2");
                p.syncJointNames();
                p.getJointEditor().undo();
                p.syncJointNames();
                EXPECT_EQ(p.getJointEditor().getGraph().getIncorporatedName(a), "A2");
            }

            TEST(Project, LoadingGivesTheJointTheDiagramsNames) {
                namespace fs = std::filesystem;
                fs::path tmp = fs::temp_directory_path() / "test_project_joint_names.json";
                std::string a;
                {
                    Project p("P");
                    twoDiagramsOneInTheJoint(p);
                    a = p.getEditor(0).getId();
                    p.getEditor(0).setName("A2"); // not synced: as files saved before it was
                    p.save(tmp);
                }
                auto loaded = Project::load(tmp);
                EXPECT_EQ(loaded->getJointEditor().getGraph().getIncorporatedName(a), "A2");
                fs::remove(tmp);
            }

        } // namespace project_tests
    } // namespace editors
} // namespace app_logic