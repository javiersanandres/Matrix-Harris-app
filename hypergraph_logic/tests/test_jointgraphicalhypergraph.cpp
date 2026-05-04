#include <gtest/gtest.h>
#include "JointGraphicalHypergraph.h"

#include <algorithm>
#include <unordered_set>

namespace hypergraph_logic {
    namespace jointgraphicalhypergraph_tests {

        // =============================================================================
        // TestableJoint
        // Exposes the protected members of GraphicalHypergraph that we need for
        // deep-inspection assertions.
        // =============================================================================
        class TestableJoint : public JointGraphicalHypergraph {
        public:
            // JointGraphicalHypergraph is non-copyable and non-movable by design, so
            // we cannot wrap an existing instance. Instead, TestableJoint replaces the
            // factory: it calls the private JointGraphicalHypergraph constructor via
            // friendship declared below, and is itself the object under test.
            //
            // Because JointGraphicalHypergraph::create() is the only way to construct
            // the object, and it returns a unique_ptr, tests that need raw access call
            // createTestable() instead.
            static std::unique_ptr<TestableJoint> createTestable(const std::string& name) {
                // Delegate to the real singleton guard.
                if (JointGraphicalHypergraph::instance_exists_) {
                    throw std::logic_error("TestableJoint: instance already exists");
                }
                JointGraphicalHypergraph::instance_exists_ = true;
                return std::unique_ptr<TestableJoint>(new TestableJoint(name));
            }

            const std::vector<NodePtr>& rawNodes() const { return all_nodes_; }
            const std::unordered_map<HyperedgePtr, std::vector<HyperedgePtr>, HyperedgePtrHash>&
                rawEdges() const { return all_hyperedges_; }
            const std::map<int, LayerData>& rawLayers() const { return layers_; }

        private:
            explicit TestableJoint(const std::string& name)
                : JointGraphicalHypergraph(name) {
            }
        };

        // =============================================================================
        // Helpers
        // =============================================================================

        static bool layerContainsNodeNamed(
            const JointGraphicalHypergraph& j, int layer, const std::string& name)
        {
            for (const auto& n : j.getNodesAt(layer))
                if (!n->isDummy() && n->getName() == name) return true;
            return false;
        }

        static int countRealNodes(const TestableJoint& j) {
            int c = 0;
            for (const auto& n : j.rawNodes()) if (!n->isDummy()) ++c;
            return c;
        }

        static int countDummies(const TestableJoint& j) {
            int c = 0;
            for (const auto& n : j.rawNodes()) if (n->isDummy()) ++c;
            return c;
        }

        static int countOriginalEdges(const TestableJoint& j) {
            return static_cast<int>(j.rawEdges().size());
        }

        static int countSegmentEdges(const TestableJoint& j) {
            int c = 0;
            for (const auto& [orig, segs] : j.rawEdges()) c += static_cast<int>(segs.size());
            return c;
        }

        // Returns the names of real nodes in layer order (left to right).
        static std::vector<std::string> realNodeNamesInLayer(
            const JointGraphicalHypergraph& j, int layer)
        {
            std::vector<std::string> names;
            for (const auto& n : j.getNodesAt(layer))
                if (!n->isDummy()) names.push_back(n->getName());
            return names;
        }

        // Builds a simple two-node graph A->B.
        static GraphicalHypergraph makeSimpleGraph(const std::string& name) {
            GraphicalHypergraph g(name);
            auto A = g.createNode("A", -1, nullptr);
            g.createNode("B", -1, A);
            g.computeLayout();
            return g;
        }

        // Builds a three-layer chain P->Q->R with a long edge S->R (skips layer 1).
        static GraphicalHypergraph makeGraphWithLongEdge(const std::string& name) {
            GraphicalHypergraph g(name);
            auto P = g.createNode("P", -1, nullptr);
            auto Q = g.createNode("Q", -1, P);
            auto R = g.createNode("R", -1, Q);
            auto S = g.createNode("S", -1, nullptr);
            g.addConnection(S, R);
            g.computeLayout();
            return g;
        }

        // Builds a single-layer graph with one root node.
        static GraphicalHypergraph makeSingleNodeGraph(
            const std::string& name, const std::string& node_name)
        {
            GraphicalHypergraph g(name);
            g.createNode(node_name, -1, nullptr);
            g.computeLayout();
            return g;
        }

        // =============================================================================
        // Fixture — creates a fresh JointGraphicalHypergraph for each test and
        // destroys it afterwards, releasing the singleton slot.
        // =============================================================================
        class JointTest : public ::testing::Test {
        protected:
            std::unique_ptr<TestableJoint> joint;

            void SetUp() override {
                joint = TestableJoint::createTestable("joint");
            }

            void TearDown() override {
                joint.reset(); // releases the singleton slot
            }
        };

        // =============================================================================
        // 1. Singleton lifecycle
        // =============================================================================

        TEST(JointSingletonTest, Create_ReturnsNonNull) {
            auto j = JointGraphicalHypergraph::create("j");
            ASSERT_NE(j, nullptr);
        }

        TEST(JointSingletonTest, Create_WhileAliveThrows) {
            auto j = JointGraphicalHypergraph::create("j");
            EXPECT_THROW(JointGraphicalHypergraph::create("j2"), std::logic_error);
        }

        TEST(JointSingletonTest, Destroy_AllowsNewInstance) {
            {
                auto j = JointGraphicalHypergraph::create("j");
                // j destroyed here
            }
            // Should not throw
            EXPECT_NO_THROW({
                auto j2 = JointGraphicalHypergraph::create("j2");
                });
        }

        TEST(JointSingletonTest, RecreateAfterDestroy_IsEmpty) {
            {
                auto j = JointGraphicalHypergraph::create("j");
                auto g = makeSimpleGraph("g");
                j->addHypergraph(g, false);
            }
            auto j2 = JointGraphicalHypergraph::create("j2");
            EXPECT_EQ(j2->getLayerCount(), 0);
            EXPECT_TRUE(j2->getAllNodes().empty());
        }

        // =============================================================================
        // 2. Disabled node-creation API
        // =============================================================================

        TEST_F(JointTest, CreateNode_WithParent_Throws) {
            EXPECT_THROW(joint->createNode("X", -1, nullptr), std::logic_error);
        }

        TEST_F(JointTest, CreateNode_IntoEdge_Throws) {
            // We need at least one edge to call this overload; use a graph first.
            auto g = makeSimpleGraph("g");
            joint->addHypergraph(g, false);
            auto edges = joint->getAllHyperedges();
            ASSERT_FALSE(edges.empty());
            EXPECT_THROW(joint->createNode("X", edges.front()), std::logic_error);
        }

        TEST_F(JointTest, CreateSource_Throws) {
            auto g = makeSimpleGraph("g");
            joint->addHypergraph(g, false);
            auto edges = joint->getAllHyperedges();
            ASSERT_FALSE(edges.empty());
            EXPECT_THROW(joint->createSource("X", -1, edges.front()), std::logic_error);
        }

        TEST_F(JointTest, CreateTarget_Throws) {
            auto g = makeSimpleGraph("g");
            joint->addHypergraph(g, false);
            auto edges = joint->getAllHyperedges();
            ASSERT_FALSE(edges.empty());
            EXPECT_THROW(joint->createTarget("X", -1, edges.front()), std::logic_error);
        }

        // =============================================================================
        // 3. addHypergraph — basic structural merge
        // =============================================================================

        TEST_F(JointTest, AddHypergraph_SimpleGraph_NodesAndEdgesIncorporated) {
            auto g = makeSimpleGraph("g");
            joint->addHypergraph(g, false);

            EXPECT_EQ(joint->getLayerCount(), 2);
            EXPECT_EQ(countRealNodes(*joint), 2);
            EXPECT_EQ(countOriginalEdges(*joint), 1);
        }

        TEST_F(JointTest, AddHypergraph_SimpleGraph_NodeNamesPresent) {
            auto g = makeSimpleGraph("g");
            joint->addHypergraph(g, false);

            EXPECT_TRUE(layerContainsNodeNamed(*joint, 0, "A"));
            EXPECT_TRUE(layerContainsNodeNamed(*joint, 1, "B"));
        }

        TEST_F(JointTest, AddHypergraph_LayerCount_MatchesIncomingGraph) {
            auto g = makeGraphWithLongEdge("g");
            joint->addHypergraph(g, false);
            EXPECT_EQ(joint->getLayerCount(), g.getLayerCount());
        }

        TEST_F(JointTest, AddHypergraph_LongEdgeGraph_DummiesPresent) {
            auto g = makeGraphWithLongEdge("g");
            joint->addHypergraph(g, false);
            EXPECT_GT(countDummies(*joint), 0);
            EXPECT_GT(countSegmentEdges(*joint), 0);
        }

        TEST_F(JointTest, AddHypergraph_LongEdgeGraph_AllSegmentsShort) {
            auto g = makeGraphWithLongEdge("g");
            joint->addHypergraph(g, false);
            for (const auto& [orig, segs] : joint->rawEdges())
                for (const auto& seg : segs)
                    for (const auto& s : seg->getSources())
                        for (const auto& t : seg->getTargets())
                            EXPECT_EQ(std::abs(s->getLayer() - t->getLayer()), 1)
                            << "Segment edge is not short after merge";
        }

        TEST_F(JointTest, AddHypergraph_EmptyGraph_JointRemainsEmpty) {
            GraphicalHypergraph empty("empty");
            empty.computeLayout();
            joint->addHypergraph(empty, false);
            EXPECT_EQ(joint->getLayerCount(), 0);
            EXPECT_TRUE(joint->getAllNodes().empty());
        }

        // =============================================================================
        // 4. addHypergraph — left / right placement
        // =============================================================================

        TEST_F(JointTest, AddHypergraph_Right_NodesAppendedInLayer) {
            auto g1 = makeSingleNodeGraph("g1", "Left");
            auto g2 = makeSingleNodeGraph("g2", "Right");

            joint->addHypergraph(g1, false); // first — goes right (only graph, position irrelevant)
            joint->addHypergraph(g2, false); // also right — appended after Left

            auto names = realNodeNamesInLayer(*joint, 0);
            ASSERT_EQ(names.size(), 2u);
            EXPECT_EQ(names[0], "Left");
            EXPECT_EQ(names[1], "Right");
        }

        TEST_F(JointTest, AddHypergraph_Left_NodesPrependedInLayer) {
            auto g1 = makeSingleNodeGraph("g1", "Existing");
            auto g2 = makeSingleNodeGraph("g2", "NewLeft");

            joint->addHypergraph(g1, false); // appended first
            joint->addHypergraph(g2, true);  // prepended — should appear before Existing

            auto names = realNodeNamesInLayer(*joint, 0);
            ASSERT_EQ(names.size(), 2u);
            EXPECT_EQ(names[0], "NewLeft");
            EXPECT_EQ(names[1], "Existing");
        }

        TEST_F(JointTest, AddHypergraph_Right_MultiLayer_AllLayersAppended) {
            auto g1 = makeSimpleGraph("g1"); // A(0) -> B(1)
            auto g2 = makeSimpleGraph("g2"); // A(0) -> B(1) — different graph, same structure

            // Rename nodes to distinguish them
            GraphicalHypergraph gX("gX");
            auto X0 = gX.createNode("X0", -1, nullptr);
            gX.createNode("X1", -1, X0);
            gX.computeLayout();

            joint->addHypergraph(g1, false);
            joint->addHypergraph(gX, false);

            // Both layers should have two real nodes each, in insertion order
            auto layer0 = realNodeNamesInLayer(*joint, 0);
            auto layer1 = realNodeNamesInLayer(*joint, 1);
            ASSERT_EQ(layer0.size(), 2u);
            ASSERT_EQ(layer1.size(), 2u);
            EXPECT_EQ(layer0[0], "A");
            EXPECT_EQ(layer0[1], "X0");
            EXPECT_EQ(layer1[0], "B");
            EXPECT_EQ(layer1[1], "X1");
        }

        TEST_F(JointTest, AddHypergraph_Left_MultiLayer_AllLayersPrepended) {
            GraphicalHypergraph g1("g1");
            auto a = g1.createNode("A", -1, nullptr);
            g1.createNode("B", -1, a);
            g1.computeLayout();

            GraphicalHypergraph g2("g2");
            auto x = g2.createNode("X", -1, nullptr);
            g2.createNode("Y", -1, x);
            g2.computeLayout();

            joint->addHypergraph(g1, false); // A(0), B(1)
            joint->addHypergraph(g2, true);  // X prepended before A, Y before B

            auto layer0 = realNodeNamesInLayer(*joint, 0);
            auto layer1 = realNodeNamesInLayer(*joint, 1);
            ASSERT_EQ(layer0.size(), 2u);
            ASSERT_EQ(layer1.size(), 2u);
            EXPECT_EQ(layer0[0], "X");
            EXPECT_EQ(layer0[1], "A");
            EXPECT_EQ(layer1[0], "Y");
            EXPECT_EQ(layer1[1], "B");
        }

        // =============================================================================
        // 5. addHypergraph — duplicate rejection
        // =============================================================================

        TEST_F(JointTest, AddHypergraph_SameGraphTwice_Throws) {
            auto g = makeSimpleGraph("g");
            joint->addHypergraph(g, false);
            EXPECT_THROW(joint->addHypergraph(g, false), std::invalid_argument);
        }

        TEST_F(JointTest, AddHypergraph_CloneOfAlreadyAdded_Throws) {
            auto g = makeSimpleGraph("g");
            joint->addHypergraph(g, false);
            // clone() preserves the ID, so the joint must reject it
            auto clone = g.clone();
            EXPECT_THROW(joint->addHypergraph(clone, false), std::invalid_argument);
        }

        TEST_F(JointTest, AddHypergraph_DifferentGraphs_BothAccepted) {
            auto g1 = makeSimpleGraph("g1");
            auto g2 = makeSimpleGraph("g2"); // different object, different ID
            EXPECT_NO_THROW(joint->addHypergraph(g1, false));
            EXPECT_NO_THROW(joint->addHypergraph(g2, false));
            EXPECT_EQ(joint->getIncorporatedIds().size(), 2u);
        }

        TEST_F(JointTest, AddHypergraph_AfterRejection_JointUnchanged) {
            auto g = makeSimpleGraph("g");
            joint->addHypergraph(g, false);
            int node_count_before = countRealNodes(*joint);

            try { joint->addHypergraph(g, false); }
            catch (...) {}

            EXPECT_EQ(countRealNodes(*joint), node_count_before);
        }

        // =============================================================================
        // 6. addHypergraph — snapshot isolation
        // =============================================================================

        TEST_F(JointTest, Snapshot_MutatingOriginalDoesNotAffectJoint) {
            auto g = makeSimpleGraph("g");
            joint->addHypergraph(g, false);
            int node_count_before = countRealNodes(*joint);

            // Mutate the original — add a new node
            g.createNode("Extra", -1, nullptr);

            EXPECT_EQ(countRealNodes(*joint), node_count_before)
                << "Joint was affected by a mutation on the original graph";
        }

        TEST_F(JointTest, Snapshot_MutatingJointDoesNotAffectOriginal) {
            auto g = makeSimpleGraph("g");
            int original_node_count = static_cast<int>(g.getAllNodes().size());
            joint->addHypergraph(g, false);

            // Mutate the joint — remove a node
            auto nodes = joint->getAllNodes();
            NodePtr real_node;
            for (const auto& n : nodes)
                if (!n->isDummy()) { real_node = n; break; }
            ASSERT_NE(real_node, nullptr);
            joint->removeNode(real_node);

            EXPECT_EQ(static_cast<int>(g.getAllNodes().size()), original_node_count)
                << "Original graph was affected by a mutation on the joint";
        }

        TEST_F(JointTest, Snapshot_NodePointersAreDistinct) {
            auto g = makeSimpleGraph("g");
            joint->addHypergraph(g, false);

            std::unordered_set<Node*> original_ptrs;
            for (const auto& n : g.getAllNodes()) original_ptrs.insert(n.get());

            for (const auto& n : joint->rawNodes())
                EXPECT_EQ(original_ptrs.count(n.get()), 0u)
                << "Joint shares a Node pointer with the original graph";
        }

        TEST_F(JointTest, Snapshot_EdgePointersAreDistinct) {
            auto g = makeSimpleGraph("g");
            joint->addHypergraph(g, false);

            std::unordered_set<Hyperedge*> original_ptrs;
            for (const auto& e : g.getAllHyperedges()) original_ptrs.insert(e.get());

            for (const auto& [orig, segs] : joint->rawEdges()) {
                EXPECT_EQ(original_ptrs.count(orig.get()), 0u)
                    << "Joint shares an original edge pointer with the source graph";
                for (const auto& seg : segs)
                    EXPECT_EQ(original_ptrs.count(seg.get()), 0u)
                    << "Joint shares a segment pointer with the source graph";
            }
        }

        // =============================================================================
        // 7. addHypergraph — multiple graphs, layer creation on demand
        // =============================================================================

        TEST_F(JointTest, AddMultipleGraphs_TotalNodeCountIsSum) {
            auto g1 = makeSimpleGraph("g1");          // 2 real nodes
            auto g2 = makeGraphWithLongEdge("g2");    // 4 real nodes (P, Q, R, S)
            joint->addHypergraph(g1, false);
            joint->addHypergraph(g2, false);
            EXPECT_EQ(countRealNodes(*joint), 6);
        }

        TEST_F(JointTest, AddMultipleGraphs_TotalOriginalEdgeCountIsSum) {
            auto g1 = makeSimpleGraph("g1");       // 1 original edge
            auto g2 = makeSimpleGraph("g2");       // 1 original edge
            joint->addHypergraph(g1, false);
            joint->addHypergraph(g2, false);
            EXPECT_EQ(countOriginalEdges(*joint), 2);
        }

        TEST_F(JointTest, AddGraph_WithDeeperLayers_CreatesNewLayersInJoint) {
            // g1 has 2 layers (0,1); g2 has 3 layers (0,1,2)
            auto g1 = makeSimpleGraph("g1");
            auto g2 = makeGraphWithLongEdge("g2");
            joint->addHypergraph(g1, false);
            int layers_before = joint->getLayerCount();
            joint->addHypergraph(g2, false);
            EXPECT_GT(joint->getLayerCount(), layers_before);
        }

        TEST_F(JointTest, AddGraph_ShallowerThanExisting_DoesNotReduceLayerCount) {
            auto g1 = makeGraphWithLongEdge("g1"); // 3 layers
            auto g2 = makeSimpleGraph("g2");        // 2 layers
            joint->addHypergraph(g1, false);
            int layers_after_first = joint->getLayerCount();
            joint->addHypergraph(g2, false);
            EXPECT_EQ(joint->getLayerCount(), layers_after_first);
        }

        // =============================================================================
        // 8. addHypergraph — long edges and dummies survive the merge
        // =============================================================================

        TEST_F(JointTest, LongEdge_SegmentOriginsStillValidAfterMerge) {
            auto g = makeGraphWithLongEdge("g");
            joint->addHypergraph(g, false);

            // Collect all original edge raw pointers that are in the joint.
            std::unordered_set<Hyperedge*> joint_originals;
            for (const auto& [orig, segs] : joint->rawEdges())
                joint_originals.insert(orig.get());

            // Every segment's origin must resolve to a joint-owned original.
            for (const auto& [orig, segs] : joint->rawEdges())
                for (const auto& seg : segs) {
                    auto locked = seg->getOrigin().lock();
                    ASSERT_TRUE(locked) << "Segment origin expired after merge";
                    EXPECT_TRUE(joint_originals.count(locked.get()))
                        << "Segment origin resolves to an edge outside the joint";
                }
        }

        TEST_F(JointTest, LongEdge_DummyLinksCorrect_DummyToDummy) {
            auto g = makeGraphWithLongEdge("g");
            joint->addHypergraph(g, false);

            for (const auto& [orig, segs] : joint->rawEdges())
                for (const auto& seg : segs)
                    for (const auto& src : seg->getSources())
                        for (const auto& tgt : seg->getTargets()) {
                            if (!src->isDummy() || !tgt->isDummy()) continue;
                            auto src_children = src->getChildren();
                            auto tgt_parents = tgt->getParents();
                            EXPECT_NE(std::find(src_children.begin(), src_children.end(), tgt),
                                src_children.end())
                                << "dummy->dummy: src missing child link";
                            EXPECT_NE(std::find(tgt_parents.begin(), tgt_parents.end(), src),
                                tgt_parents.end())
                                << "dummy->dummy: tgt missing parent link";
                        }
        }

        TEST_F(JointTest, LongEdge_RealNodesUnawareOfDummies) {
            auto g = makeGraphWithLongEdge("g");
            joint->addHypergraph(g, false);

            for (const auto& n : joint->rawNodes()) {
                if (n->isDummy()) continue;
                for (const auto& p : n->getParents())
                    EXPECT_FALSE(p->isDummy())
                    << "Real node '" << n->getName() << "' has a dummy parent in joint";
                for (const auto& c : n->getChildren())
                    EXPECT_FALSE(c->isDummy())
                    << "Real node '" << n->getName() << "' has a dummy child in joint";
            }
        }

        // =============================================================================
        // 9. Enabled mutation API
        // =============================================================================

        TEST_F(JointTest, AddConnection_BetweenNodesFromDifferentGraphs) {
            auto g1 = makeSingleNodeGraph("g1", "N1");
            auto g2 = makeSingleNodeGraph("g2", "N2");
            joint->addHypergraph(g1, false);
            joint->addHypergraph(g2, false);

            // Both N1 and N2 are in layer 0 — addConnection will push N2 to layer 1.
            NodePtr n1, n2;
            for (const auto& n : joint->getNodesAt(0)) {
                if (!n->isDummy() && n->getName() == "N1") n1 = n;
                if (!n->isDummy() && n->getName() == "N2") n2 = n;
            }
            ASSERT_NE(n1, nullptr);
            ASSERT_NE(n2, nullptr);

            EXPECT_NO_THROW(joint->addConnection(n1, n2));
            EXPECT_EQ(n2->getLayer(), 1);
        }

        TEST_F(JointTest, RemoveNode_LeafNode_Succeeds) {
            auto g = makeSimpleGraph("g");
            joint->addHypergraph(g, false);

            // B is the leaf (layer 1)
            NodePtr leaf;
            for (const auto& n : joint->getNodesAt(1))
                if (!n->isDummy()) { leaf = n; break; }
            ASSERT_NE(leaf, nullptr);

            int node_count_before = countRealNodes(*joint);
            EXPECT_NO_THROW(joint->removeNode(leaf));
            EXPECT_EQ(countRealNodes(*joint), node_count_before - 1);
        }

        TEST_F(JointTest, RemoveConnection_BetweenTwoNodes_Succeeds) {
            auto g = makeSimpleGraph("g");
            joint->addHypergraph(g, false);

            NodePtr parent, child;
            for (const auto& n : joint->getNodesAt(0))
                if (!n->isDummy()) { parent = n; break; }
            for (const auto& n : joint->getNodesAt(1))
                if (!n->isDummy()) { child = n; break; }
            ASSERT_NE(parent, nullptr);
            ASSERT_NE(child, nullptr);

            int edge_count_before = countOriginalEdges(*joint);
            EXPECT_NO_THROW(joint->removeConnection(parent, child));
            EXPECT_LT(countOriginalEdges(*joint), edge_count_before);
        }

        TEST_F(JointTest, FuseNodes_TwoRootNodes_Succeeds) {
            auto g1 = makeSingleNodeGraph("g1", "N1");
            auto g2 = makeSingleNodeGraph("g2", "N2");
            joint->addHypergraph(g1, false);
            joint->addHypergraph(g2, false);

            NodePtr n1, n2;
            for (const auto& n : joint->getNodesAt(0)) {
                if (!n->isDummy() && n->getName() == "N1") n1 = n;
                if (!n->isDummy() && n->getName() == "N2") n2 = n;
            }
            ASSERT_NE(n1, nullptr);
            ASSERT_NE(n2, nullptr);

            int node_count_before = countRealNodes(*joint);
            EXPECT_NO_THROW(joint->fuseNodes(n1, n2, "Fused"));
            EXPECT_EQ(countRealNodes(*joint), node_count_before - 1);
            EXPECT_TRUE(layerContainsNodeNamed(*joint, 0, "Fused"));
        }

        TEST_F(JointTest, RemoveSourcesFromHyperedge_Succeeds) {
            // Build a graph where an edge has two sources, then remove one.
            GraphicalHypergraph g("g");
            auto s1 = g.createNode("S1", -1, nullptr);
            auto s2 = g.createNode("S2", -1, nullptr);
            auto t = g.createNode("T", -1, s1);
            g.addSourceToEdge(g.getLayerData(0).outgoing_edges.front(), s2);
            g.computeLayout();

            joint->addHypergraph(g, false);

            // Find the original edge and one of its sources.
            HyperedgePtr edge;
            for (const auto& [orig, segs] : joint->rawEdges())
                if (!orig->isSegment()) { edge = orig; break; }
            ASSERT_NE(edge, nullptr);

            NodePtr src_to_remove;
            for (const auto& s : edge->getSources())
                if (!s->isDummy() && s->getName() == "S2") { src_to_remove = s; break; }
            ASSERT_NE(src_to_remove, nullptr);

            EXPECT_NO_THROW(
                joint->removeSourcesFromHyperedge(edge, { src_to_remove.get() }, false));
            EXPECT_FALSE(edge->containsSource(src_to_remove));
        }

        TEST_F(JointTest, RemoveTargetsFromHyperedge_Succeeds) {
            GraphicalHypergraph g("g");
            auto s = g.createNode("S", -1, nullptr);
            auto t1 = g.createNode("T1", -1, s);
            auto t2 = g.createNode("T2", -1, nullptr);
            g.addTargetToEdge(g.getLayerData(0).outgoing_edges.front(), t2);
            g.computeLayout();

            joint->addHypergraph(g, false);

            HyperedgePtr edge;
            for (const auto& [orig, segs] : joint->rawEdges())
                if (!orig->isSegment()) { edge = orig; break; }
            ASSERT_NE(edge, nullptr);

            NodePtr tgt_to_remove;
            for (const auto& t : edge->getTargets())
                if (!t->isDummy() && t->getName() == "T2") { tgt_to_remove = t; break; }
            ASSERT_NE(tgt_to_remove, nullptr);

            EXPECT_NO_THROW(
                joint->removeTargetsFromHyperedge(edge, { tgt_to_remove.get() }, false));
            EXPECT_FALSE(edge->containsTarget(tgt_to_remove));
        }

        // =============================================================================
        // 10. getIncorporatedIds
        // =============================================================================

        TEST_F(JointTest, GetIncorporatedIds_EmptyInitially) {
            EXPECT_TRUE(joint->getIncorporatedIds().empty());
        }

        TEST_F(JointTest, GetIncorporatedIds_ContainsIdAfterAdd) {
            auto g = makeSimpleGraph("g");
            const std::string id = g.getId();
            joint->addHypergraph(g, false);
            EXPECT_TRUE(joint->getIncorporatedIds().count(id));
        }

        TEST_F(JointTest, GetIncorporatedIds_CountMatchesNumberOfGraphsAdded) {
            auto g1 = makeSimpleGraph("g1");
            auto g2 = makeSimpleGraph("g2");
            auto g3 = makeGraphWithLongEdge("g3");
            joint->addHypergraph(g1, false);
            joint->addHypergraph(g2, false);
            joint->addHypergraph(g3, false);
            EXPECT_EQ(joint->getIncorporatedIds().size(), 3u);
        }

        TEST_F(JointTest, GetIncorporatedIds_RejectedGraphNotRecorded) {
            auto g = makeSimpleGraph("g");
            joint->addHypergraph(g, false);
            size_t count_before = joint->getIncorporatedIds().size();

            try { joint->addHypergraph(g, false); }
            catch (...) {}

            EXPECT_EQ(joint->getIncorporatedIds().size(), count_before);
        }

    } // namespace jointgraphicalhypergraph_tests
} // namespace hypergraph_logic