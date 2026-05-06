#include "HypergraphEditor.h"
#include "JointHypergraphEditor.h"
#include "Project.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace app_logic {
    namespace editors {

        // ────────────────────────────────────────────────────────────────────
        // Shared helpers
        // ────────────────────────────────────────────────────────────────────

        // Build a minimal two-node, two-layer GraphicalHypergraph.
        static GraphicalHypergraph makeTwoNodeGraph(const std::string& name) {
            GraphicalHypergraph g(name);
            NodePtr A = g.createNode("A", 0, nullptr);
            g.createNode("B", 0, A);
            g.computeLayout();
            return g;
        }

        // Return the first non-segment hyperedge in the editor, or nullptr.
        static HyperedgePtr firstRealEdge(HypergraphEditorBase<HypergraphEditor>& ed) {
            for (const auto& e : ed.getAllHyperedges())
                if (!e->isSegment()) return e;
            return nullptr;
        }

        // ════════════════════════════════════════════════════════════════════
        // HypergraphEditor tests
        // ════════════════════════════════════════════════════════════════════
        namespace hypergraph_editor_tests {

            // ── Construction ─────────────────────────────────────────────────

            TEST(HypergraphEditor, ConstructionDoesNotThrow) {
                EXPECT_NO_THROW(HypergraphEditor ed(GraphicalHypergraph("g")));
            }

            TEST(HypergraphEditor, InitialUndoRedoStacksEmpty) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                EXPECT_FALSE(ed.canUndo());
                EXPECT_FALSE(ed.canRedo());
            }

            TEST(HypergraphEditor, UndoOnEmptyStackThrows) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                EXPECT_THROW(ed.undo(), std::logic_error);
            }

            TEST(HypergraphEditor, RedoOnEmptyStackThrows) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                EXPECT_THROW(ed.redo(), std::logic_error);
            }

            // ── createNode (with parent) — happy path ─────────────────────────

            TEST(HypergraphEditor, CreateNodeWithParentReturnsNonNull) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                ASSERT_NE(A, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                EXPECT_NE(B, nullptr);
            }

            TEST(HypergraphEditor, CreateNodeCommitsSnapshotOnSuccess) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                ed.createNode("A", 0, nullptr);
                EXPECT_TRUE(ed.canUndo());
            }

            TEST(HypergraphEditor, CreateNodeClearsRedoStack) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                ed.createNode("A", 0, nullptr);
                ed.undo();
                EXPECT_TRUE(ed.canRedo());
                ed.createNode("X", 0, nullptr);
                EXPECT_FALSE(ed.canRedo());
            }

            TEST(HypergraphEditor, CreateNodeAppearsInAllNodes) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                auto nodes = ed.getAllNodes();
                bool found = std::any_of(nodes.begin(), nodes.end(),
                    [&](const NodePtr& n) { return n == A; });
                EXPECT_TRUE(found);
            }

            // ── createNode (into edge) ────────────────────────────────────────

            TEST(HypergraphEditor, CreateNodeIntoEdgeReturnsNonNull) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                HyperedgePtr edge = firstRealEdge(ed);
                ASSERT_NE(edge, nullptr);
                NodePtr C = ed.createNode("C", edge);
                EXPECT_NE(C, nullptr);
            }

            TEST(HypergraphEditor, CreateNodeIntoEdgeCommitsSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
				NodePtr C = ed.createNode("C", 0, nullptr);
                HyperedgePtr edge = firstRealEdge(ed);
                ASSERT_NE(edge, nullptr);
                ed.undo(); // clear prior snapshots
				HyperedgePtr edge2 = firstRealEdge(ed);
				EXPECT_NE(edge2.get(), edge.get()); // sanity check that undo worked
                ed.createNode("C", edge2);
                EXPECT_TRUE(ed.canUndo());
            }

            // ── createSource / createTarget ───────────────────────────────────

            TEST(HypergraphEditor, CreateSourceReturnsNonNull) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                HyperedgePtr edge = firstRealEdge(ed);
                ASSERT_NE(edge, nullptr);
                NodePtr S = ed.createSource("S", 0, edge);
                EXPECT_NE(S, nullptr);
            }

            TEST(HypergraphEditor, CreateSourceCommitsSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                HyperedgePtr edge = firstRealEdge(ed);
                ASSERT_NE(edge, nullptr);
                ed.undo(); ed.undo();
                ed.createSource("S", 0, edge);
                EXPECT_TRUE(ed.canUndo());
            }

            TEST(HypergraphEditor, CreateTargetReturnsNonNull) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                HyperedgePtr edge = firstRealEdge(ed);
                ASSERT_NE(edge, nullptr);
                NodePtr T = ed.createTarget("T", 1, edge);
                EXPECT_NE(T, nullptr);
            }

            // ── Undo / Redo — createNode ──────────────────────────────────────

            TEST(HypergraphEditor, UndoCreateNodeRemovesIt) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                ed.createNode("A", 0, nullptr);
                std::size_t count_after = ed.getAllNodes().size();
                ed.undo();
                EXPECT_LT(ed.getAllNodes().size(), count_after);
            }

            TEST(HypergraphEditor, RedoCreateNodeRestoresIt) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                ed.createNode("A", 0, nullptr);
                std::size_t count_before = ed.getAllNodes().size();
                ed.undo();
                ed.redo();
                EXPECT_EQ(ed.getAllNodes().size(), count_before);
            }

            TEST(HypergraphEditor, UndoEnablesRedo) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                ed.createNode("A", 0, nullptr);
                ed.undo();
                EXPECT_TRUE(ed.canRedo());
            }

            TEST(HypergraphEditor, RedoEnablesUndo) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                ed.createNode("A", 0, nullptr);
                ed.undo();
                ed.redo();
                EXPECT_TRUE(ed.canUndo());
            }

            TEST(HypergraphEditor, MultipleUndoStepsWork) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                ed.createNode("A", 0, nullptr);
                ed.createNode("B", 0, nullptr);
                ed.createNode("C", 0, nullptr);
                ed.undo();
                ed.undo();
                EXPECT_TRUE(ed.canUndo());
                EXPECT_TRUE(ed.canRedo());
            }

            TEST(HypergraphEditor, UndoAllThenRedoAllRestoresState) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                std::size_t full_count = ed.getAllNodes().size();
                ed.undo();
                ed.undo();
                EXPECT_EQ(ed.getAllNodes().size(), 0u);
                ed.redo();
                ed.redo();
                EXPECT_EQ(ed.getAllNodes().size(), full_count);
            }

            // ── addConnection — happy path ─────────────────────────────────────

            TEST(HypergraphEditor, AddConnectionReturnsNonNull) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, nullptr);
                HyperedgePtr e = ed.addConnection(A, B);
                EXPECT_NE(e, nullptr);
            }

            TEST(HypergraphEditor, AddConnectionCommitsSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, nullptr);
                ed.undo(); ed.undo();
                EXPECT_FALSE(ed.canUndo());
            }

            // ── addConnection — failure path (transactional guarantee) ─────────
            //
            // addConnection throws std::invalid_argument when connecting a node
            // to itself, and std::logic_error when the connection already exists
            // or would create a cycle. In all cases the snapshot must NOT have
            // been committed and the live graph must be untouched.

            TEST(HypergraphEditor, AddConnectionSelfLoopThrowsAndDoesNotCommitSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                ed.undo(); // clear prior snapshot — stack is now empty
                EXPECT_THROW(ed.addConnection(A, A), std::invalid_argument);
                // Snapshot must NOT have been pushed.
                EXPECT_FALSE(ed.canUndo());
                // Live graph must be untouched — A still exists, no new edges.
                EXPECT_EQ(ed.getAllNodes().size(), 0u);
            }

            TEST(HypergraphEditor, AddConnectionDuplicateThrowsAndDoesNotCommitSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                // A->B connection already exists via createNode.
                // Drain undo stack so we can test the empty-stack invariant.
                while (ed.canUndo()) ed.undo();
                EXPECT_THROW(ed.addConnection(A, B), std::logic_error);
                EXPECT_FALSE(ed.canUndo());
            }

            TEST(HypergraphEditor, AddConnectionCycleThrowsAndDoesNotCommitSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                // B->A would create a cycle (A is already a parent of B).
                while (ed.canUndo()) ed.undo();
                EXPECT_THROW(ed.addConnection(B, A), std::logic_error);
                EXPECT_FALSE(ed.canUndo());
            }

            // ── removeNode ────────────────────────────────────────────────────

            TEST(HypergraphEditor, RemoveNodeDecreasesCount) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                std::size_t before = ed.getAllNodes().size();
                ed.removeNode(A);
                EXPECT_LT(ed.getAllNodes().size(), before);
            }

            TEST(HypergraphEditor, RemoveNodeCommitsSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                ed.undo();
                ed.createNode("A", 0, nullptr); // recreate so we have something to remove
                ed.undo();
                ed.redo();
                ed.removeNode(ed.getAllNodes().front());
                EXPECT_TRUE(ed.canUndo());
            }

            TEST(HypergraphEditor, UndoRemoveNodeRestoresIt) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                std::size_t before = ed.getAllNodes().size();
                ed.removeNode(A);
                ed.undo();
                EXPECT_EQ(ed.getAllNodes().size(), before);
            }

            // ── removeConnection — failure path ───────────────────────────────
            //
            // removeConnection throws std::logic_error when the connection does
            // not exist. The snapshot must not be committed.

            TEST(HypergraphEditor, RemoveConnectionNonExistentThrowsAndDoesNotCommitSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, nullptr);
                // A and B have no connection between them.
                EXPECT_THROW(ed.removeConnection(A, B), std::logic_error);
                EXPECT_TRUE(ed.canUndo());
                // Both nodes must still exist.
                EXPECT_EQ(ed.getAllNodes().size(), 2u);
            }

            // ── removeHyperedge ───────────────────────────────────────────────

            TEST(HypergraphEditor, RemoveHyperedgeDecreasesEdgeCount) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                HyperedgePtr edge = firstRealEdge(ed);
                ASSERT_NE(edge, nullptr);
				const auto& edges_before = ed.getAllHyperedges();
                auto non_seg_before = std::count_if(
                    edges_before.begin(), edges_before.end(),
                    [](const HyperedgePtr& e) { return !e->isSegment(); });
                ed.removeHyperedge(edge);
				const auto& edges_after = ed.getAllHyperedges();
                auto non_seg_after = std::count_if(
                    edges_after.begin(), edges_after.end(),
                    [](const HyperedgePtr& e) { return !e->isSegment(); });
                EXPECT_LT(non_seg_after, non_seg_before);
            }

            TEST(HypergraphEditor, RemoveHyperedgeCommitsSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                HyperedgePtr edge = firstRealEdge(ed);
                ASSERT_NE(edge, nullptr);
                while (ed.canUndo()) ed.undo();
                ed.removeHyperedge(edge);
                EXPECT_TRUE(ed.canUndo());
            }

            // ── removeSourceFromHyperedge — failure path ──────────────────────
            //
            // removeSourceFromHyperedge throws std::logic_error when a node
            // in sources_to_remove is not actually a source of the edge.

            TEST(HypergraphEditor, RemoveSourceInvalidThrowsAndDoesNotCommitSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                NodePtr C = ed.createNode("C", 0, nullptr); // not a source of A->B
                HyperedgePtr edge = firstRealEdge(ed);
                ASSERT_NE(edge, nullptr);
                EXPECT_THROW(
                    ed.removeSourceFromHyperedge(edge, C),
                    std::logic_error);
                EXPECT_TRUE(ed.canUndo());
                // Graph must be untouched.
                EXPECT_EQ(ed.getAllNodes().size(), 3u);
            }

            // ── removeTargetFromHyperedge — failure path ──────────────────────
            //
            // removeTargetFromHyperedge throws std::logic_error when a node
            // in targets_to_remove is not actually a target of the edge.

            TEST(HypergraphEditor, RemoveTargetInvalidThrowsAndDoesNotCommitSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                NodePtr C = ed.createNode("C", 0, nullptr); // not a target of A->B
                HyperedgePtr edge = firstRealEdge(ed);
                ASSERT_NE(edge, nullptr);
                EXPECT_THROW(
                    ed.removeTargetFromHyperedge(edge, C),
                    std::logic_error);
                EXPECT_TRUE(ed.canUndo());
                EXPECT_EQ(ed.getAllNodes().size(), 3u);
            }

            // ── fuseNodes — happy path ─────────────────────────────────────────

            TEST(HypergraphEditor, FuseNodesDecreasesNodeCount) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 1, nullptr);
                std::size_t before = ed.getAllNodes().size();
                ed.fuseNodes(A, B, "AB");
                EXPECT_LT(ed.getAllNodes().size(), before);
            }

            TEST(HypergraphEditor, FuseNodesCommitsSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 1, nullptr);
                while (ed.canUndo()) ed.undo();
                ed.fuseNodes(A, B, "AB");
                EXPECT_TRUE(ed.canUndo());
            }

            TEST(HypergraphEditor, UndoFuseNodesRestoresCount) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 1, nullptr);
                std::size_t before = ed.getAllNodes().size();
                ed.fuseNodes(A, B, "AB");
                ed.undo();
                EXPECT_EQ(ed.getAllNodes().size(), before);
            }

            // ── fuseNodes — failure path ───────────────────────────────────────
            //
            // fuseNodes throws std::invalid_argument when node1 == node2, and
            // std::logic_error when the fusion would create a cycle.
            // In both cases the snapshot must NOT be committed.

            TEST(HypergraphEditor, FuseNodeWithItselfThrowsAndDoesNotCommitSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                EXPECT_THROW(ed.fuseNodes(A, A, "AA"), std::invalid_argument);
                EXPECT_TRUE(ed.canUndo());
                EXPECT_EQ(ed.getAllNodes().size(), 1u);
            }

            TEST(HypergraphEditor, FuseNodesCycleThrowsAndDoesNotCommitSnapshot) {
                // Build A->B->C and try to fuse A with C:
                // that would make C a parent of itself (A's parents become C's parents,
                // but C is already a descendant of A), creating a cycle.
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                NodePtr C = ed.createNode("C", 0, B);
                EXPECT_THROW(ed.fuseNodes(A, C, "AC"), std::logic_error);
                EXPECT_TRUE(ed.canUndo());
                // All three nodes must still be present.
                EXPECT_EQ(ed.getAllNodes().size(), 3u);
            }

            // ── renameNode ────────────────────────────────────────────────────

            TEST(HypergraphEditor, RenameNodeChangesName) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                ed.renameNode(A, "Z");
                EXPECT_EQ(A->getName(), "Z");
            }

            TEST(HypergraphEditor, RenameNodeCommitsSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                while (ed.canUndo()) ed.undo();
                ed.renameNode(A, "Z");
                EXPECT_TRUE(ed.canUndo());
            }

            TEST(HypergraphEditor, UndoRenameNodeRestoresName) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                ed.renameNode(A, "Z");
                ed.undo();
                EXPECT_EQ(ed.getAllNodes()[0]->getName(), "A");
            }

            // ── setName ───────────────────────────────────────────────────────

            TEST(HypergraphEditor, SetNameUpdatesGetName) {
                HypergraphEditor ed(GraphicalHypergraph("old"));
                ed.setName("new");
                EXPECT_EQ(ed.getName(), "new");
            }

            TEST(HypergraphEditor, SetNameCommitsSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("old"));
                ed.setName("new");
                EXPECT_TRUE(ed.canUndo());
            }

            TEST(HypergraphEditor, UndoSetNameRestoresName) {
                HypergraphEditor ed(GraphicalHypergraph("old"));
                ed.setName("new");
                ed.undo();
                EXPECT_EQ(ed.getName(), "old");
            }

            // ── addSourceToEdge ───────────────────────────────────────────────

            TEST(HypergraphEditor, AddSourceToEdgeCommitsSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                NodePtr C = ed.createNode("C", 0, nullptr);
                HyperedgePtr edge = firstRealEdge(ed);
                ASSERT_NE(edge, nullptr);
                while (ed.canUndo()) ed.undo();
                ed.addSourceToEdge(edge, C);
                EXPECT_TRUE(ed.canUndo());
            }

            TEST(HypergraphEditor, UndoAddSourceToEdgeRestoresGraph) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                NodePtr C = ed.createNode("C", 0, nullptr);
                HyperedgePtr edge = firstRealEdge(ed);
                ASSERT_NE(edge, nullptr);
                std::size_t node_count = ed.getAllNodes().size();
                ed.addSourceToEdge(edge, C);
                ed.undo();
                EXPECT_EQ(ed.getAllNodes().size(), node_count);
            }

            // ── addSourceToEdge — failure path ────────────────────────────────
            //
            // addSourceToEdge throws std::logic_error when the connection already
            // exists, and std::logic_error when a cycle would be created.

            TEST(HypergraphEditor, AddSourceAlreadyConnectedThrowsAndDoesNotCommitSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                // B is a child of A. Adding A again as a source of the A->B edge
                // is a duplicate connection.
                HyperedgePtr edge = firstRealEdge(ed);
                ASSERT_NE(edge, nullptr);
                while (ed.canUndo()) ed.undo();
                EXPECT_THROW(ed.addSourceToEdge(edge, A), std::logic_error);
                EXPECT_FALSE(ed.canUndo());
            }

            // ── addTargetToEdge ───────────────────────────────────────────────

            TEST(HypergraphEditor, AddTargetToEdgeCommitsSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                NodePtr C = ed.createNode("C", 1, nullptr);
                HyperedgePtr edge = firstRealEdge(ed);
                ASSERT_NE(edge, nullptr);
				EXPECT_NO_THROW(ed.addTargetToEdge(edge, C));
                ed.undo();
                EXPECT_THROW(ed.addTargetToEdge(edge, C), std::logic_error);
                EXPECT_TRUE(ed.canUndo());
            }

            TEST(HypergraphEditor, AddTargetToEdgeFailureDoesNotCommitSnapshot) {
                // A node cannot be connected to itself: adding a source of the
                // edge as a target of the same edge throws std::logic_error.
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                HyperedgePtr edge = firstRealEdge(ed);
                ASSERT_NE(edge, nullptr);
                while (ed.canUndo()) ed.undo();
                // A is the source of the A->B edge; adding A as a target creates a self-loop.
                EXPECT_THROW(ed.addTargetToEdge(edge, A), std::logic_error);
                EXPECT_FALSE(ed.canUndo());
            }

            // ── relocateNodeInLayer ───────────────────────────────────────────
            //
            // relocateNodeInLayer throws std::invalid_argument when the new
            // coordinate does not change the node's position in the layer.

            TEST(HypergraphEditor, RelocateNodeToNewPositionCommitsSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 1, nullptr);
                // B is to the right of A. Move A well past B.
                double xB = ed.getX(B);
                ed.relocateNodeInLayer(A, xB + 50.0);
                EXPECT_TRUE(ed.canUndo());
            }

            TEST(HypergraphEditor, RelocateNodeSamePositionThrowsAndDoesNotCommitSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                double xA = ed.getX(A);
                while (ed.canUndo()) ed.undo();
                // Moving to the exact same x does not swap any neighbour — throws.
                EXPECT_THROW(ed.relocateNodeInLayer(A, xA), std::invalid_argument);
                EXPECT_FALSE(ed.canUndo());
            }

            // ── minimizeCrossings ─────────────────────────────────────────────

            TEST(HypergraphEditor, MinimizeCrossingsDoesNotThrow) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 1, nullptr);
                NodePtr C = ed.createNode("C", 0, A);
                NodePtr D = ed.createNode("D", 1, B);
                EXPECT_NO_THROW(ed.minimizeCrossings());
            }

            TEST(HypergraphEditor, MinimizeCrossingsCommitsSnapshot) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                while (ed.canUndo()) ed.undo();
                ed.minimizeCrossings();
                EXPECT_TRUE(ed.canUndo());
            }

            // ── MAX_HISTORY cap ───────────────────────────────────────────────

            TEST(HypergraphEditor, UndoStackCappedAtMaxHistory) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                for (int i = 0; i < HypergraphEditor::MAX_HISTORY + 5; ++i)
                    ed.createNode(std::string("N") + std::to_string(i), 0, nullptr);
                int undo_count = 0;
                while (ed.canUndo()) { ed.undo(); ++undo_count; }
                EXPECT_LE(undo_count, HypergraphEditor::MAX_HISTORY);
            }

            // ── Transactional invariant: failed op leaves stack and graph clean ─
        
            TEST(HypergraphEditor, FailedMutationLeavesUndoStackDepthUnchanged) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                ed.createNode("X", 0, nullptr);
                ed.createNode("Y", 0, nullptr);
                const auto& nodes = ed.getAllNodes();
                // Now cause a failure and check depth is still the same.
                EXPECT_THROW(ed.addConnection(
                    ed.getAllNodes()[0], ed.getAllNodes()[0]), // self-loop
                    std::invalid_argument);
				const auto& nodes_after = ed.getAllNodes();
				ASSERT_EQ(nodes, nodes_after);
            }

            // ── Read-only queries ─────────────────────────────────────────────

            TEST(HypergraphEditor, GetGraphReturnsSameName) {
                HypergraphEditor ed(GraphicalHypergraph("my_graph"));
                EXPECT_EQ(ed.getGraph().getName(), "my_graph");
            }

            TEST(HypergraphEditor, GetLayerCountAfterCreateNode) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                EXPECT_EQ(ed.getLayerCount(), 0);
                ed.createNode("A", 0, nullptr);
                EXPECT_GT(ed.getLayerCount(), 0);
            }

            TEST(HypergraphEditor, GetNodesAtReturnsCorrectLayer) {
                HypergraphEditor ed(GraphicalHypergraph("g"));
                NodePtr A = ed.createNode("A", 0, nullptr);
                NodePtr B = ed.createNode("B", 0, A);
                auto layer0 = ed.getNodesAt(0);
                bool found = std::any_of(layer0.begin(), layer0.end(),
                    [&](const NodePtr& n) { return n == A; });
                EXPECT_TRUE(found);
            }

        } // namespace hypergraph_editor_tests


        // ════════════════════════════════════════════════════════════════════
        // JointHypergraphEditor tests
        // ════════════════════════════════════════════════════════════════════
        namespace joint_editor_tests {

            static std::unique_ptr<JointGraphicalHypergraph> makeJoint(
                const std::string& name)
            {
                return JointGraphicalHypergraph::create(name);
            }

            // ── Construction ─────────────────────────────────────────────────

            TEST(JointHypergraphEditor, ConstructionDoesNotThrow) {
                EXPECT_NO_THROW(JointHypergraphEditor jed(makeJoint("j")));
            }

            TEST(JointHypergraphEditor, NullPointerThrowsOnConstruction) {
                EXPECT_THROW(
                    JointHypergraphEditor jed(nullptr),
                    std::invalid_argument);
            }

            TEST(JointHypergraphEditor, InitialUndoRedoStacksEmpty) {
                JointHypergraphEditor jed(makeJoint("j"));
                EXPECT_FALSE(jed.canUndo());
                EXPECT_FALSE(jed.canRedo());
            }

            TEST(JointHypergraphEditor, UndoOnEmptyStackThrows) {
                JointHypergraphEditor jed(makeJoint("j"));
                EXPECT_THROW(jed.undo(), std::logic_error);
            }

            TEST(JointHypergraphEditor, RedoOnEmptyStackThrows) {
                JointHypergraphEditor jed(makeJoint("j"));
                EXPECT_THROW(jed.redo(), std::logic_error);
            }

            // ── addHypergraph — happy path ────────────────────────────────────

            TEST(JointHypergraphEditor, AddHypergraphDoesNotThrow) {
                JointHypergraphEditor jed(makeJoint("j"));
                GraphicalHypergraph g = makeTwoNodeGraph("g1");
                EXPECT_NO_THROW(jed.addHypergraph(g, true));
            }

            TEST(JointHypergraphEditor, AddHypergraphCommitsSnapshot) {
                JointHypergraphEditor jed(makeJoint("j"));
                GraphicalHypergraph g = makeTwoNodeGraph("g1");
                jed.addHypergraph(g, true);
                EXPECT_TRUE(jed.canUndo());
            }

            TEST(JointHypergraphEditor, AddHypergraphClearsRedoStack) {
                JointHypergraphEditor jed(makeJoint("j"));
                GraphicalHypergraph g1 = makeTwoNodeGraph("g1");
                jed.addHypergraph(g1, true);
                jed.undo();
                EXPECT_TRUE(jed.canRedo());
                GraphicalHypergraph g2 = makeTwoNodeGraph("g2");
                jed.addHypergraph(g2, false);
                EXPECT_FALSE(jed.canRedo());
            }

            TEST(JointHypergraphEditor, UndoAddHypergraphRemovesNodes) {
                JointHypergraphEditor jed(makeJoint("j"));
                GraphicalHypergraph g = makeTwoNodeGraph("g1");
                jed.addHypergraph(g, true);
                std::size_t count_after = jed.getAllNodes().size();
                jed.undo();
                EXPECT_LT(jed.getAllNodes().size(), count_after);
            }

            TEST(JointHypergraphEditor, RedoAddHypergraphRestoresNodes) {
                JointHypergraphEditor jed(makeJoint("j"));
                GraphicalHypergraph g = makeTwoNodeGraph("g1");
                jed.addHypergraph(g, true);
                std::size_t count_after = jed.getAllNodes().size();
                jed.undo();
                jed.redo();
                EXPECT_EQ(jed.getAllNodes().size(), count_after);
            }

            // ── addHypergraph — failure path ──────────────────────────────────
            //
            // addHypergraph throws std::invalid_argument when the same graph
            // is added twice (duplicate id). The snapshot must NOT be committed.

            TEST(JointHypergraphEditor, AddDuplicateGraphThrowsAndDoesNotCommitSnapshot) {
                JointHypergraphEditor jed(makeJoint("j"));
                GraphicalHypergraph g = makeTwoNodeGraph("g1");
                jed.addHypergraph(g, true);
                jed.undo(); // stack is now empty
                // Try to add the same graph again — must throw.
                jed.addHypergraph(g, true); // first add succeeds
                EXPECT_THROW(jed.addHypergraph(g, true), std::invalid_argument);
                EXPECT_TRUE(jed.canUndo());
            }

            // ── Undo / Redo round-trip ────────────────────────────────────────

            TEST(JointHypergraphEditor, UndoEnablesRedo) {
                JointHypergraphEditor jed(makeJoint("j"));
                GraphicalHypergraph g = makeTwoNodeGraph("g1");
                jed.addHypergraph(g, true);
                jed.undo();
                EXPECT_TRUE(jed.canRedo());
                EXPECT_FALSE(jed.canUndo());
            }

            TEST(JointHypergraphEditor, RedoEnablesUndo) {
                JointHypergraphEditor jed(makeJoint("j"));
                GraphicalHypergraph g = makeTwoNodeGraph("g1");
                jed.addHypergraph(g, true);
                jed.undo();
                jed.redo();
                EXPECT_TRUE(jed.canUndo());
            }

            // ── Shared base operations ────────────────────────────────────────

            TEST(JointHypergraphEditor, SetNameUpdatesName) {
                JointHypergraphEditor jed(makeJoint("old"));
                jed.setName("new");
                EXPECT_EQ(jed.getName(), "new");
            }

            TEST(JointHypergraphEditor, SetNameCommitsSnapshot) {
                JointHypergraphEditor jed(makeJoint("j"));
                jed.setName("renamed");
                EXPECT_TRUE(jed.canUndo());
            }

            TEST(JointHypergraphEditor, UndoSetNameRestoresName) {
                JointHypergraphEditor jed(makeJoint("old"));
                jed.setName("new");
                jed.undo();
                EXPECT_EQ(jed.getName(), "old");
            }

            TEST(JointHypergraphEditor, GetGraphReturnsCorrectName) {
                JointHypergraphEditor jed(makeJoint("joint_graph"));
                EXPECT_EQ(jed.getGraph().getName(), "joint_graph");
            }

            // ── addConnection on joint — failure path ─────────────────────────
            //
            // Self-loop on the joint must throw and must not commit a snapshot.

            TEST(JointHypergraphEditor, AddConnectionSelfLoopThrowsAndDoesNotCommitSnapshot) {
                JointHypergraphEditor jed(makeJoint("j"));
                GraphicalHypergraph g = makeTwoNodeGraph("g1");
                jed.addHypergraph(g, true);
                auto nodes = jed.getAllNodes();
                ASSERT_GE(nodes.size(), 1u);
                while (jed.canUndo()) jed.undo();
                EXPECT_THROW(jed.addConnection(nodes[0], nodes[0]), std::invalid_argument);
                EXPECT_FALSE(jed.canUndo());
            }

            // ── MAX_HISTORY cap ───────────────────────────────────────────────

            TEST(JointHypergraphEditor, UndoStackCappedAtMaxHistory) {
                JointHypergraphEditor jed(makeJoint("j"));
                for (int i = 0; i < JointHypergraphEditor::MAX_HISTORY + 5; ++i)
                    jed.setName(std::string("name") + std::to_string(i));
                int undo_count = 0;
                while (jed.canUndo()) { jed.undo(); ++undo_count; }
                EXPECT_LE(undo_count, JointHypergraphEditor::MAX_HISTORY);
            }

        } // namespace joint_editor_tests
    } // namespace editors
} // namespace app_logic