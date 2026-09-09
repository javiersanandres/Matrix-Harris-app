// =============================================================================
// test_connection_management.cpp
//
// Tests for all public connection-management API:
//   createNode (both overloads), createSource, createTarget,
//   addConnection, addSourceToEdge, addTargetToEdge,
//   removeNode, removeConnection, fuseNodes
// =============================================================================

#include <gtest/gtest.h>
#include "Hypergraph.h"
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

namespace hypergraph_logic::hypergraph_tests::connection_management {

    // =============================================================================
    // TestableHypergraph — exposes protected helpers needed for assertions
    // without polluting the public API under test.
    // =============================================================================
    class TestableHypergraph : public Hypergraph {
    public:
        using Hypergraph::Hypergraph;
        std::unordered_map<HyperedgePtr, std::vector<HyperedgePtr>, HyperedgePtrHash>& rawEdges() {
            return all_hyperedges_;
        }
        int pub_edgeIsShort(const HyperedgePtr& e) { return edgeIsShort(e); }
    };

    // =============================================================================
    // Shared utilities
    // =============================================================================
    static bool layerContainsNode(const TestableHypergraph& g, int layer, const NodePtr& node) {
        auto nodes = g.getNodesAt(layer);
        return std::find(nodes.begin(), nodes.end(), node) != nodes.end();
    }

    static bool layersAreConsistentWithAllNodes(const TestableHypergraph& g) {
        std::unordered_set<Node*> in_all;
        for (const auto& n : g.getAllNodes()) in_all.insert(n.get());
        for (const auto& [l, data] : g.getLayers())
            for (const auto& n : data.nodes)
                if (!in_all.count(n.get())) return false;
        return true;
    }

    static bool allSegmentEdgesAreShort(const TestableHypergraph& g) {
        for (const auto& e : g.getAllHyperedges()) {
            if (!e->isSegment()) continue;
            for (const auto& s : e->getSources())
                for (const auto& t : e->getTargets())
                    if (std::abs(s->getLayer() - t->getLayer()) != 1) return false;
        }
        return true;
    }

    static bool shortEdgesAreConsistentWithAdjacency(TestableHypergraph& g) {
        for (const auto& e : g.getAllHyperedges()) {
            int k = g.pub_edgeIsShort(e);
            if (k < 0) continue;
            if (k != e->getLayer()) return false;
            const auto& layer_edges = g.getLayerData(k).outgoing_edges;
            if (std::find(layer_edges.begin(), layer_edges.end(), e) == layer_edges.end())
                return false;
        }
        return true;
    }

    static int countSegmentEdges(const TestableHypergraph& g) {
        int n = 0;
        for (const auto& e : g.getAllHyperedges()) if (e->isSegment()) ++n;
        return n;
    }

    static int countDummyNodesInLayer(const TestableHypergraph& g, int layer) {
        int n = 0;
        for (const auto& node : g.getNodesAt(layer)) if (node->isDummy()) ++n;
        return n;
    }

    static bool edgeHasSource(const HyperedgePtr& e, const NodePtr& n) {
        for (const auto& s : e->getSources()) if (s == n) return true;
        return false;
    }

    static bool edgeHasTarget(const HyperedgePtr& e, const NodePtr& n) {
        for (const auto& t : e->getTargets()) if (t == n) return true;
        return false;
    }

    static HyperedgePtr findEdgeWithSource(const TestableHypergraph& g, const NodePtr& n) {
        for (const auto& e : g.getAllHyperedges()) {
            if (!e->isSegment() && edgeHasSource(e, n)) return e;
        }
        return nullptr;
    }

    static HyperedgePtr findEdgeWithSourceAndTarget(const TestableHypergraph& g,
        const NodePtr& s, const NodePtr& t) {
        for (const auto& e : g.getAllHyperedges()) {
            if (!e->isSegment() && edgeHasSource(e, s) && edgeHasTarget(e, t)) return e;
        }
        return nullptr;
    }

    static bool nodeInAllNodes(const TestableHypergraph& g, const NodePtr& node) {
        for (const auto& n : g.getAllNodes()) if (n == node) return true;
        return false;
    }

    // =============================================================================
    // Fixture
    // =============================================================================
    class ConnectionManagementTest : public ::testing::Test {
    protected:
        TestableHypergraph g{ "test" };
    };

    // =============================================================================
    // 1. createNode(label, position, parent)
    // =============================================================================

    TEST_F(ConnectionManagementTest, CreateNode_NoParent_PlacedAtLayer0) {
        auto n = g.createNode("n", 0, nullptr, nullptr);
        EXPECT_EQ(n->getLayer(), 0);
        EXPECT_TRUE(layerContainsNode(g, 0, n));
        EXPECT_TRUE(nodeInAllNodes(g, n));
    }

    TEST_F(ConnectionManagementTest, CreateNode_WithParent_PlacedAtParentLayerPlusOne) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_EQ(c->getLayer(), 1);
        EXPECT_TRUE(layerContainsNode(g, 1, c));
    }

    TEST_F(ConnectionManagementTest, CreateNode_WithParent_EdgeCreated) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_TRUE(edgeHasTarget(edge, c));
    }

    TEST_F(ConnectionManagementTest, CreateNode_WithParent_ParentChildLinksSet) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto children = p->getChildren();
        auto parents = c->getParents();
        EXPECT_NE(std::find(children.begin(), children.end(), c), children.end());
        EXPECT_NE(std::find(parents.begin(), parents.end(), p), parents.end());
    }

    TEST_F(ConnectionManagementTest, CreateNode_ChainPropagatesLayers) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        EXPECT_EQ(a->getLayer(), 0);
        EXPECT_EQ(b->getLayer(), 1);
        EXPECT_EQ(c->getLayer(), 2);
    }

    // =============================================================================
    // 2. createNode(label, edge) — insert node into edge
    // =============================================================================

    TEST_F(ConnectionManagementTest, CreateNodeOnEdge_SplitsEdgeIntoTwo) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);

        auto mid = g.createNodeInEdge("mid", edge);

        ASSERT_NE(mid, nullptr);
        EXPECT_TRUE(nodeInAllNodes(g, mid));
        // New node must be between p and c
        EXPECT_EQ(mid->getLayer(), 1);
        EXPECT_EQ(c->getLayer(), 2);
    }

    TEST_F(ConnectionManagementTest, CreateNodeOnEdge_NullEdgeReturnsNull) {
        EXPECT_EQ(g.createNodeInEdge("x", nullptr), nullptr);
    }

    // =============================================================================
    // 3. createSource / createTarget
    // =============================================================================

    TEST_F(ConnectionManagementTest, CreateSource_PlacedOneLayerAboveShallowestTarget) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p); // c at layer 1
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);

        auto src = g.createSource("src", 0, edge);

        ASSERT_NE(src, nullptr);
        EXPECT_EQ(src->getLayer(), 0) << "One layer above c (1) happens to be 0 here";
        EXPECT_TRUE(edgeHasSource(edge, src));
        EXPECT_TRUE(nodeInAllNodes(g, src));
    }

    TEST_F(ConnectionManagementTest, CreateSource_DeepTarget_PlacedAtDeepestLegalLayerNotLayerZero) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto mid1 = g.createNode("mid1", 0, p);
        auto mid2 = g.createNode("mid2", 0, mid1);
        auto c = g.createNode("c", 0, nullptr, nullptr); // c at layer 3

        auto deep_edge = g.addConnection(p, c); // p->c directly;
        g.relocateNodeToLayer(c, 3);
        auto src = g.createSource("src", -1, deep_edge);

        ASSERT_NE(src, nullptr);
        EXPECT_EQ(src->getLayer(), 2) << "One layer above c's layer (3), not layer 0";
        EXPECT_TRUE(edgeHasSource(deep_edge, src));
    }

    TEST_F(ConnectionManagementTest, CreateSource_MultipleTargetsAtDifferentLayers_UsesShallowestMinusOne) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto near = g.createNode("near", 0, p);           // layer 1
        auto mid = g.createNode("mid", 0, near);
        auto far = g.createNode("far", 0, nullptr);
        g.relocateNodeToLayer(far, 3);
        HyperedgePtr edge = nullptr;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->containsSource(p) && e->containsTarget(near)) {
                edge = e;
            }
        }
        g.addTargetToEdge(edge, far);                       // edge now targets {near(1), far(3)}

        auto src = g.createSource("src", -1, edge);
        ASSERT_NE(src, nullptr);
        EXPECT_EQ(src->getLayer(), 0) << "One layer above the shallowest target (near, layer 1)";
    }

    TEST_F(ConnectionManagementTest, CreateSource_NullEdgeReturnsNull) {
        EXPECT_EQ(g.createSource("x", 0, nullptr), nullptr);
    }

    TEST_F(ConnectionManagementTest, CreateTarget_AddedToEdgeAtCorrectLayer) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);

        auto tgt = g.createTarget("tgt", 0, edge);

        ASSERT_NE(tgt, nullptr);
        EXPECT_EQ(tgt->getLayer(), 1);
        EXPECT_TRUE(edgeHasTarget(edge, tgt));
        EXPECT_TRUE(nodeInAllNodes(g, tgt));
    }

    TEST_F(ConnectionManagementTest, CreateTarget_NullEdgeReturnsNull) {
        EXPECT_EQ(g.createTarget("x", 0, nullptr), nullptr);
    }

    TEST_F(ConnectionManagementTest, CreateTarget_ParentChildLinksSet) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);

        auto tgt = g.createTarget("tgt", 0, edge);

        auto p_children = p->getChildren();
        auto tgt_parents = tgt->getParents();
        EXPECT_NE(std::find(p_children.begin(), p_children.end(), tgt), p_children.end());
        EXPECT_NE(std::find(tgt_parents.begin(), tgt_parents.end(), p), tgt_parents.end());
    }

    // =============================================================================
    // 4. addConnection — guard conditions
    // =============================================================================

    TEST_F(ConnectionManagementTest, AddConnection_NullParentIgnored) {
        auto c = g.createNode("c", 0, nullptr, nullptr);
        EXPECT_NO_THROW(g.addConnection(nullptr, c));
        EXPECT_TRUE(layerContainsNode(g, 0, c));
    }

    TEST_F(ConnectionManagementTest, AddConnection_NullChildIgnored) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        EXPECT_NO_THROW(g.addConnection(p, nullptr));
    }

    TEST_F(ConnectionManagementTest, AddConnection_SelfConnectionThrows) {
        auto n = g.createNode("n", 0, nullptr, nullptr);
        EXPECT_THROW(g.addConnection(n, n), std::invalid_argument);
    }

    TEST_F(ConnectionManagementTest, AddConnection_DuplicateDirectConnectionThrows) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_THROW(g.addConnection(p, c), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddConnection_TransitiveAncestorThrows) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto m = g.createNode("m", 0, p);
        auto c = g.createNode("c", 0, m);
        EXPECT_THROW(g.addConnection(p, c), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddConnection_DirectCycleThrows) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_THROW(g.addConnection(c, p), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddConnection_LongCycleThrows) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);
        EXPECT_THROW(g.addConnection(d, a), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddConnection_ExceptionSafety_StateUnchangedOnCycle) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        int nodes_before = static_cast<int>(g.getAllNodes().size());
        int edges_before = static_cast<int>(g.getAllHyperedges().size());
        int layers_before = g.getLayerCount();
        EXPECT_THROW(g.addConnection(b, a), std::logic_error);
        EXPECT_EQ(static_cast<int>(g.getAllNodes().size()), nodes_before);
        EXPECT_EQ(static_cast<int>(g.getAllHyperedges().size()), edges_before);
        EXPECT_EQ(g.getLayerCount(), layers_before);
    }

    // =============================================================================
    // 5. addConnection — correct behaviour
    // =============================================================================

    TEST_F(ConnectionManagementTest, AddConnection_AdjacentLayer_NoSplit) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, a);
        g.addConnection(b, c);
        EXPECT_TRUE(layerContainsNode(g, 1, c));
        EXPECT_EQ(countDummyNodesInLayer(g, 1), 0);
        EXPECT_EQ(countSegmentEdges(g), 0);
    }

    TEST_F(ConnectionManagementTest, AddConnection_ParentChildLinksSet) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto q = g.createNode("q", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        g.addConnection(q, c);
        auto parents = c->getParents();
        auto children = q->getChildren();
        EXPECT_NE(std::find(parents.begin(), parents.end(), q), parents.end());
        EXPECT_NE(std::find(children.begin(), children.end(), c), children.end());
    }

    TEST_F(ConnectionManagementTest, AddConnection_ChildMovesDown) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        g.addConnection(a, b);
        EXPECT_EQ(b->getLayer(), 1);
        EXPECT_FALSE(layerContainsNode(g, 0, b));
        EXPECT_TRUE(layerContainsNode(g, 1, b));
    }

    TEST_F(ConnectionManagementTest, AddConnection_DescendantsPropagateOnChildMove) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);
        g.addConnection(a, b);
        EXPECT_EQ(b->getLayer(), 1);
        EXPECT_EQ(c->getLayer(), 2);
        EXPECT_EQ(d->getLayer(), 3);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, AddConnection_LongEdgeSplitsWithDummies) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        g.addConnection(r2, n2);
        EXPECT_GE(countDummyNodesInLayer(g, 1), 1);
        EXPECT_GE(countSegmentEdges(g), 2);
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    TEST_F(ConnectionManagementTest, AddConnection_LongEdge_RealNodesNoDirectDummyLinks) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        g.addConnection(r2, n2);
        for (const auto& p : n2->getParents())
            EXPECT_FALSE(p->isDummy()) << "n2 should not have dummy parents";
        for (const auto& ch : r2->getChildren())
            EXPECT_FALSE(ch->isDummy()) << "r2 should not have dummy children";
    }

    TEST_F(ConnectionManagementTest, AddConnection_TransitiveEdgeRemovedAfterAdd) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, a);
        g.addConnection(a, b);
        g.addConnection(b, c);
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            EXPECT_FALSE(edgeHasSource(e, a) && edgeHasTarget(e, c))
                << "Transitive edge a->c should have been removed";
        }
    }

    TEST_F(ConnectionManagementTest, AddConnection_DiamondDAG_BothParentsPresent) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);
        auto d = g.createNode("d", 0, b);
        g.addConnection(c, d);
        auto parents = d->getParents();
        std::unordered_set<Node*> pset;
        for (const auto& p : parents) pset.insert(p.get());
        EXPECT_TRUE(pset.count(b.get()));
        EXPECT_TRUE(pset.count(c.get()));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, AddConnection_MultipleRootsToSingleLeaf) {
        auto leaf = g.createNode("leaf", 0, nullptr, nullptr);
        for (int i = 0; i < 4; i++)
            g.addConnection(g.createNode("r" + std::to_string(i), 0, nullptr), leaf);
        EXPECT_EQ(leaf->getLayer(), 1);
        EXPECT_EQ(countDummyNodesInLayer(g, 1), 0);
        EXPECT_EQ(countSegmentEdges(g), 0);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, AddConnection_FullBinaryTree_FiveLevels) {
        auto root = g.createNode("root", 0, nullptr, nullptr);
        std::vector<NodePtr> current{ root };
        for (int depth = 1; depth <= 4; depth++) {
            std::vector<NodePtr> next;
            for (const auto& par : current) {
                next.push_back(g.createNode("L", 0, par));
                next.push_back(g.createNode("R", 0, par));
            }
            current = next;
        }
        for (const auto& leaf : current)
            EXPECT_EQ(leaf->getLayer(), 4);
        EXPECT_EQ(g.getLayerCount(), 5);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, AddConnection_SequentialParentsDeepensNode) {
        auto n0 = g.createNode("n0", 0, nullptr, nullptr);
        auto p1 = g.createNode("p1", 0, nullptr, nullptr);
        g.addConnection(p1, n0); EXPECT_EQ(n0->getLayer(), 1);
        auto p2 = g.createNode("p2", 0, p1);
        g.addConnection(p2, n0); EXPECT_EQ(n0->getLayer(), 2);
        auto p3 = g.createNode("p3", 0, p2);
        g.addConnection(p3, n0); EXPECT_EQ(n0->getLayer(), 3);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    // =============================================================================
    // 6. addSourceToEdge
    // =============================================================================

    TEST_F(ConnectionManagementTest, AddSourceToEdge_NullEdgeIgnored) {
        auto n = g.createNode("n", 0, nullptr, nullptr);
        EXPECT_NO_THROW(g.addSourceToEdge(nullptr, n));
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_NullSourceIgnored) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_NO_THROW(g.addSourceToEdge(edge, nullptr));
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_SegmentEdgeIgnored) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        g.addConnection(r2, n2);
        HyperedgePtr seg = nullptr;
        for (const auto& e : g.getAllHyperedges()) if (e->isSegment()) { seg = e; break; }
        ASSERT_NE(seg, nullptr);
        auto x = g.createNode("x", 0, nullptr, nullptr);
        EXPECT_NO_THROW(g.addSourceToEdge(seg, x));
        EXPECT_FALSE(edgeHasSource(seg, x));
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_SourceIsTargetThrows) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_THROW(g.addSourceToEdge(edge, c), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_TransitiveAncestorThrows) {
        auto q = g.createNode("q", 0, nullptr, nullptr);
        auto p = g.createNode("p", 0, q);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_THROW(g.addSourceToEdge(edge, q), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_AdjacentLayer_SourceAdded) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto q = g.createNode("q", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addSourceToEdge(edge, q);
        EXPECT_TRUE(edgeHasSource(edge, q));
        EXPECT_TRUE(edgeHasSource(edge, p));
        EXPECT_EQ(countSegmentEdges(g), 0);
        EXPECT_TRUE(shortEdgesAreConsistentWithAdjacency(g));
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_ParentChildLinksUpdated) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto q = g.createNode("q", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addSourceToEdge(edge, q);
        auto q_children = q->getChildren();
        auto c_parents = c->getParents();
        EXPECT_NE(std::find(q_children.begin(), q_children.end(), c), q_children.end());
        EXPECT_NE(std::find(c_parents.begin(), c_parents.end(), q), c_parents.end());
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_DeepSource_TargetMovesDown) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto r0 = g.createNode("r0", 0, nullptr, nullptr);
        auto r1 = g.createNode("r1", 0, r0);
        auto r = g.createNode("r", 0, r1);  // layer 2
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addSourceToEdge(edge, r);
        EXPECT_EQ(c->getLayer(), 3);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_MultipleTargetsAllGetNewParent) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto c2 = g.createNode("c2", 0, nullptr, nullptr);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addTargetToEdge(edge, c2);
        auto q = g.createNode("q", 0, nullptr, nullptr);
        g.addSourceToEdge(edge, q);
        auto q_children = q->getChildren();
        EXPECT_NE(std::find(q_children.begin(), q_children.end(), c1), q_children.end());
        EXPECT_NE(std::find(q_children.begin(), q_children.end(), c2), q_children.end());
    }

    // =============================================================================
    // 7. addTargetToEdge
    // =============================================================================

    TEST_F(ConnectionManagementTest, AddTargetToEdge_NullEdgeIgnored) {
        auto n = g.createNode("n", 0, nullptr, nullptr);
        EXPECT_NO_THROW(g.addTargetToEdge(nullptr, n));
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_NullTargetIgnored) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_NO_THROW(g.addTargetToEdge(edge, nullptr));
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_SegmentEdgeIgnored) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        g.addConnection(r2, n2);
        HyperedgePtr seg = nullptr;
        for (const auto& e : g.getAllHyperedges()) if (e->isSegment()) { seg = e; break; }
        ASSERT_NE(seg, nullptr);
        auto x = g.createNode("x", 0, nullptr, nullptr);
        EXPECT_NO_THROW(g.addTargetToEdge(seg, x));
        EXPECT_FALSE(edgeHasTarget(seg, x));
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_TargetIsSourceThrows) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_THROW(g.addTargetToEdge(edge, p), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_TransitiveDescendantThrows) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto d = g.createNode("d", 0, c);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_THROW(g.addTargetToEdge(edge, d), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_AdjacentLayer_TargetAdded) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto c2 = g.createNode("c2", 0, nullptr, nullptr);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addTargetToEdge(edge, c2);
        EXPECT_TRUE(edgeHasTarget(edge, c1));
        EXPECT_TRUE(edgeHasTarget(edge, c2));
        EXPECT_EQ(c2->getLayer(), 1);
        EXPECT_EQ(countSegmentEdges(g), 0);
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_ParentChildLinksUpdated) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto c2 = g.createNode("c2", 0, nullptr, nullptr);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addTargetToEdge(edge, c2);
        auto p_children = p->getChildren();
        auto c2_parents = c2->getParents();
        EXPECT_NE(std::find(p_children.begin(), p_children.end(), c2), p_children.end());
        EXPECT_NE(std::find(c2_parents.begin(), c2_parents.end(), p), c2_parents.end());
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_ShallowTarget_MovesDown) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto t = g.createNode("t", 0, nullptr, nullptr);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addTargetToEdge(edge, t);
        EXPECT_EQ(t->getLayer(), 1);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_DeepTarget_EdgeSplit) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto mid = g.createNode("mid", 0, root);
        auto deep = g.createNode("deep", 0, mid);   // layer 2
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addTargetToEdge(edge, deep);
        EXPECT_GE(countSegmentEdges(g), 1);
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_MultipleSourcesAllBecomeParents) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto q = g.createNode("q", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addSourceToEdge(edge, q);
        auto c2 = g.createNode("c2", 0, nullptr, nullptr);
        g.addTargetToEdge(edge, c2);
        auto c2_parents = c2->getParents();
        std::unordered_set<Node*> pset;
        for (const auto& par : c2_parents) pset.insert(par.get());
        EXPECT_TRUE(pset.count(p.get()));
        EXPECT_TRUE(pset.count(q.get()));
    }

    // =============================================================================
    // 8. removeNode
    // =============================================================================

    TEST_F(ConnectionManagementTest, RemoveNode_Null_NoOp) {
        EXPECT_NO_THROW(g.removeNode(nullptr));
    }

    TEST_F(ConnectionManagementTest, RemoveNode_LeafNode_RemovedFromGraphAndLayer) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        g.removeNode(c);
        EXPECT_FALSE(nodeInAllNodes(g, c));
        EXPECT_FALSE(layerContainsNode(g, 1, c));
    }

    TEST_F(ConnectionManagementTest, RemoveNode_LeafNode_ParentChildLinkSevered) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        g.removeNode(c);
        auto children = p->getChildren();
        EXPECT_EQ(std::find(children.begin(), children.end(), c), children.end());
    }

    TEST_F(ConnectionManagementTest, RemoveNode_LeafNode_EdgeRemovedFromGraph) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        g.removeNode(c);
        for (const auto& e : g.getAllHyperedges())
            EXPECT_FALSE(edgeHasTarget(e, c)) << "No edge should still target removed node";
    }

    TEST_F(ConnectionManagementTest, RemoveNode_RootWithChildren_ChildrenBecomeRoots) {
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto c1 = g.createNode("c1", 0, root);
        auto c2 = g.createNode("c2", 0, root);
        g.removeNode(root);
        EXPECT_FALSE(nodeInAllNodes(g, root));
        // Children should have no parents referencing root
        for (const auto& p : c1->getParents())
            EXPECT_NE(p, root);
        for (const auto& p : c2->getParents())
            EXPECT_NE(p, root);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, RemoveNode_MiddleNode_ParentsWiredToChildren) {
        // p -> m -> c; remove m; p should now connect to c
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto m = g.createNode("m", 0, p);
        auto c = g.createNode("c", 0, m);
        g.removeNode(m);
        EXPECT_FALSE(nodeInAllNodes(g, m));
        // There should be an edge from p to c
        bool pc_connected = false;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            if (edgeHasSource(e, p) && edgeHasTarget(e, c)) { pc_connected = true; break; }
        }
        EXPECT_TRUE(pc_connected) << "p should now connect to c after m is removed";
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, RemoveNode_MiddleNode_ChildLayerAdjusted) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto m = g.createNode("m", 0, p);
        auto c = g.createNode("c", 0, m);
        EXPECT_EQ(c->getLayer(), 2);
        g.removeNode(m);
        // c should now be at layer 1 (directly below p)
        EXPECT_EQ(c->getLayer(), 1);
    }

    TEST_F(ConnectionManagementTest, RemoveNode_GlobalInvariantsAfterRemoval) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, b);
        g.removeNode(b);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    // =============================================================================
    // 9. removeConnection
    // =============================================================================

    TEST_F(ConnectionManagementTest, RemoveConnection_NullParentIgnored) {
        auto c = g.createNode("c", 0, nullptr, nullptr);
        EXPECT_NO_THROW(g.removeConnection(nullptr, c));
    }

    TEST_F(ConnectionManagementTest, RemoveConnection_NullChildIgnored) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        EXPECT_NO_THROW(g.removeConnection(p, nullptr));
    }

    TEST_F(ConnectionManagementTest, RemoveConnection_DirectEdgeRemoved) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        g.removeConnection(p, c);
        // No edge should connect p to c
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            EXPECT_FALSE(edgeHasSource(e, p) && edgeHasTarget(e, c));
        }
    }

    TEST_F(ConnectionManagementTest, RemoveConnection_CoSourcePreservesConnectionToChild) {
        // Edge {p, q} -> c; remove p from that edge;
        // remaining edge {q} -> c should still exist
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto q = g.createNode("q", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addSourceToEdge(edge, q);  // edge now {p,q}->c

        g.removeConnection(p, c);

        bool qc_exists = false;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            if (edgeHasSource(e, q) && edgeHasTarget(e, c)) { qc_exists = true; break; }
        }
        EXPECT_TRUE(qc_exists) << "q->c connection must survive after p is removed from edge";
    }

    TEST_F(ConnectionManagementTest, RemoveConnection_NonExistentConnectionIgnored) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        int edges_before = static_cast<int>(g.getAllHyperedges().size());
        EXPECT_THROW(g.removeConnection(a, b), std::logic_error);
        EXPECT_EQ(static_cast<int>(g.getAllHyperedges().size()), edges_before);
    }

    // =============================================================================
    // 10. fuseNodes
    // =============================================================================

    TEST_F(ConnectionManagementTest, FuseNodes_NullArgIgnored) {
        auto n = g.createNode("n", 0, nullptr, nullptr);
        EXPECT_NO_THROW(g.fuseNodes(nullptr, n, "x"));
        EXPECT_NO_THROW(g.fuseNodes(n, nullptr, "x"));
    }

    TEST_F(ConnectionManagementTest, FuseNodes_SameNodeThrows) {
        auto n = g.createNode("n", 0, nullptr, nullptr);
        EXPECT_THROW(g.fuseNodes(n, n, "x"), std::invalid_argument);
    }

    TEST_F(ConnectionManagementTest, FuseNodes_CycleFusionThrows) {
        // p -> c; fusing p and c would create a self-loop
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_THROW(g.fuseNodes(p, c, "fused"), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, FuseNodes_Node2RemovedFromGraph) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        g.fuseNodes(a, b, "ab");
        EXPECT_FALSE(nodeInAllNodes(g, b));
        EXPECT_FALSE(layerContainsNode(g, 0, b));
    }

    TEST_F(ConnectionManagementTest, FuseNodes_NameUpdated) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        g.fuseNodes(a, b, "fused");
        EXPECT_EQ(a->getName(), "fused");
    }

    TEST_F(ConnectionManagementTest, FuseNodes_ConnectionsMerged) {
        // r->a, s->b; fuse a and b; fused node should have both r and s as parents
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto s = g.createNode("s", 0, nullptr, nullptr);
        auto a = g.createNode("a", 0, r);
        auto b = g.createNode("b", 0, s);
        g.fuseNodes(a, b, "ab");
        auto parents = a->getParents();
        std::unordered_set<Node*> pset;
        for (const auto& p : parents) pset.insert(p.get());
        EXPECT_TRUE(pset.count(r.get()));
        EXPECT_TRUE(pset.count(s.get()));
    }

    TEST_F(ConnectionManagementTest, FuseNodes_SharedParent_NoDuplicates) {
        // r->a, r->b; fuse a and b; r must appear only once as parent
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto a = g.createNode("a", 0, r);
        auto b = g.createNode("b", 0, r);
        g.fuseNodes(a, b, "ab");
        auto parents = a->getParents();
        std::unordered_set<Node*> pset;
        for (const auto& p : parents) pset.insert(p.get());
        EXPECT_EQ(pset.count(r.get()), 1u) << "r must appear only once as parent";
    }

    TEST_F(ConnectionManagementTest, FuseNodes_LayerUpdatedAfterFusion) {
        // r->a (layer 1), deep->b (layer 2); fuse a and b — fused node should be at layer 2
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto mid = g.createNode("mid", 0, root);
        auto a = g.createNode("a", 0, r);     // layer 1
        auto b = g.createNode("b", 0, mid);   // layer 2
        g.fuseNodes(a, b, "ab");
        EXPECT_EQ(a->getLayer(), 2);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, FuseNodes_HyperedgesUpdated) {
        // edge p->b; fuse a and b; edge should now target a instead of b
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, p);
        g.fuseNodes(a, b, "ab");
        bool b_in_any_edge = false;
        for (const auto& e : g.getAllHyperedges())
            if (edgeHasSource(e, b) || edgeHasTarget(e, b)) { b_in_any_edge = true; break; }
        EXPECT_FALSE(b_in_any_edge) << "b (erased node) should not appear in any edge";
    }

    TEST_F(ConnectionManagementTest, FuseNodes_GlobalInvariantsAfterFusion) {
        auto r1 = g.createNode("r1", 0, nullptr, nullptr);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        auto a = g.createNode("a", 0, r1);
        auto b = g.createNode("b", 0, r2);
        auto c = g.createNode("c", 0, a);
        g.fuseNodes(a, b, "ab");
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    // =============================================================================
    // 11. Mixed-API stress tests
    // =============================================================================

    TEST_F(ConnectionManagementTest, Mixed_AddSourceAndConnection_Invariants) {
        auto r1 = g.createNode("r1", 0, nullptr, nullptr);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        auto a = g.createNode("a", 0, r1);
        auto b = g.createNode("b", 0, r1);
        auto c = g.createNode("c", 0, a);
        auto edge = findEdgeWithSourceAndTarget(g, r1, a);
        ASSERT_NE(edge, nullptr);
        g.addSourceToEdge(edge, r2);
        g.addConnection(b, c);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(shortEdgesAreConsistentWithAdjacency(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    TEST_F(ConnectionManagementTest, Mixed_RemoveNodeAfterFuse_Invariants) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto a = g.createNode("a", 0, r);
        auto b = g.createNode("b", 0, r);
        auto c = g.createNode("c", 0, a);
        g.fuseNodes(a, b, "ab");
        g.removeNode(c);
        EXPECT_FALSE(nodeInAllNodes(g, c));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, Mixed_LargeDAG_NoOrphanDummies) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        std::vector<NodePtr> chain{ r };
        for (int i = 1; i <= 4; i++)
            chain.push_back(g.createNode("c" + std::to_string(i), 0, chain.back()));
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        g.addConnection(r2, chain[4]);
        g.addConnection(chain[2], g.createNode("side", 0, nullptr, nullptr));
        std::unordered_set<Node*> all_in_graph;
        for (const auto& n : g.getAllNodes()) all_in_graph.insert(n.get());
        for (const auto& [l, data] : g.getLayers())
            for (const auto& n : data.nodes)
                if (n->isDummy())
                    EXPECT_TRUE(all_in_graph.count(n.get()))
                    << "Dummy in layer " << l << " not in all_nodes_";
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    // =============================================================================
    // 12. createParent
    // =============================================================================

    TEST_F(ConnectionManagementTest, CreateParent_PlacedAtLayerZero) {
        auto c = g.createNode("c", 0, nullptr, nullptr);
        auto p = g.createParent("p", c);
        EXPECT_EQ(p->getLayer(), 0);
        EXPECT_TRUE(layerContainsNode(g, 0, p));
        EXPECT_TRUE(nodeInAllNodes(g, p));
    }

    TEST_F(ConnectionManagementTest, CreateParent_EdgeCreatedToChild) {
        auto c = g.createNode("c", 0, nullptr, nullptr);
        auto p = g.createParent("p", c);
        auto edge = findEdgeWithSourceAndTarget(g, p, c);
        EXPECT_NE(edge, nullptr);
    }

    TEST_F(ConnectionManagementTest, CreateParent_ParentChildLinksSet) {
        auto c = g.createNode("c", 0, nullptr, nullptr);
        auto p = g.createParent("p", c);
        auto children = p->getChildren();
        EXPECT_NE(std::find(children.begin(), children.end(), c), children.end());
        auto parents = c->getParents();
        EXPECT_NE(std::find(parents.begin(), parents.end(), p), parents.end());
    }

    TEST_F(ConnectionManagementTest, CreateParent_ChildWasAtLayerZero_ChildRelocatesDown) {
        // c starts as a root at layer 0; giving it a new parent forces c down, and the new
        // parent must take layer 0 for itself.
        auto c = g.createNode("c", 0, nullptr, nullptr);
        ASSERT_EQ(c->getLayer(), 0);
        auto p = g.createParent("p", c);
        EXPECT_EQ(p->getLayer(), 0);
        EXPECT_EQ(c->getLayer(), 1);
        EXPECT_EQ(p->getDesiredLayer(), -1) << "p's natural depth rule (0, parentless) already matches";
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, CreateParent_ChildAtLayerOne_ParentLandsAtZeroNoOverride) {
        // child at exactly layer 1 is the boundary case: the new parent's natural placement
        // (child_layer - 1 = 0) coincides with its own depth rule (0, since it's parentless),
        // so no desired_layer override should be recorded — this is the exact case the
        // desired_layer correctness fix targets.
        auto other = g.createNode("other", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, other); // c at layer 1
        auto p = g.createParent("p", c);
        EXPECT_EQ(p->getLayer(), 0);
        EXPECT_EQ(p->getDesiredLayer(), -1) << "0 == p's natural depth rule; must not be recorded as an override";
        EXPECT_EQ(c->getLayer(), 1) << "No relocation needed, c already had a deep-enough parent";
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, CreateParent_DeepChild_ParentPlacedDirectlyAboveWithOverride) {
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto mid = g.createNode("mid", 0, root);
        auto c = g.createNode("c", 0, mid); // c at layer 2

        auto p = g.createParent("p", c);

        EXPECT_EQ(p->getLayer(), 1) << "Directly one layer above c (2), not layer 0";
        EXPECT_EQ(p->getDesiredLayer(), 1) << "1 > p's natural depth rule (0), so it's a genuine override";
        EXPECT_EQ(c->getLayer(), 2) << "c does not move; the new parent fits in beside its existing one";
        EXPECT_TRUE(edgeHasSource(findEdgeWithSourceAndTarget(g, p, c), p));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, CreateParent_NullChild_Throws) {
        EXPECT_THROW(g.createParent("p", nullptr), std::invalid_argument);
    }

    // =============================================================================
    // 13. removeSourcesFromHyperedge / removeTargetsFromHyperedge (direct public calls)
    // =============================================================================

    TEST_F(ConnectionManagementTest, RemoveSourcesFromHyperedge_RemovesLinkAndEdge) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, nullptr, nullptr);
        auto edge = g.addConnection(a, c);
        g.addSourceToEdge(edge, b); // edge now has sources {a, b}, target {c}
        g.removeSourcesFromHyperedge(edge, { a.get() }, true);
        auto children = a->getChildren();
        EXPECT_EQ(std::find(children.begin(), children.end(), c), children.end())
            << "a->c link should be gone";
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, RemoveSourcesFromHyperedge_AllSourcesRemoved_DissolvesEdge) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, a);
        auto edge = findEdgeWithSourceAndTarget(g, a, c);
        ASSERT_NE(edge, nullptr);
        g.removeSourcesFromHyperedge(edge, { a.get() }, true);
        EXPECT_TRUE(g.rawEdges().find(edge) == g.rawEdges().end());
    }

    TEST_F(ConnectionManagementTest, RemoveSourcesFromHyperedge_WithRelocation_ChildMovesUp) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        g.relocateNodeToLayer(b, 3); // isolated, valid override
        auto c = g.createNode("c", 0, nullptr, nullptr);
        g.addConnection(a, c);
        auto edge = g.addConnection(b, c); // c's depth rule now driven by b (layer 1) -> c at 2
        ASSERT_EQ(c->getLayer(), 2);
        g.removeSourcesFromHyperedge(edge, { b.get() }, true);
        EXPECT_EQ(c->getLayer(), 1) << "c should move back up now that only 'a' remains as a parent";
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, RemoveSourcesFromHyperedge_NoGapsLeftBehind) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        g.relocateNodeToLayer(b, 3);
        auto c = g.createNode("c", 0, nullptr, nullptr);
        g.addConnection(a, c);
        auto edge = g.addConnection(b, c);
        g.removeSourcesFromHyperedge(edge, { b.get() }, true);
        int expected = 0;
        for (const auto& [layer, data] : g.getLayers())
            EXPECT_EQ(layer, expected++) << "Layer numbers must stay dense from 0";
    }

    TEST_F(ConnectionManagementTest, RemoveTargetsFromHyperedge_RemovesLinkAndEdge) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, nullptr, nullptr);
        auto edge = g.addConnection(a, b);
        g.addTargetToEdge(edge, c); // edge now has target {b, c}
        g.removeTargetsFromHyperedge(edge, { b.get() }, true);
        auto parents = b->getParents();
        EXPECT_EQ(std::find(parents.begin(), parents.end(), a), parents.end())
            << "a->b link should be gone";
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, RemoveTargetsFromHyperedge_AllTargetsRemoved_DissolvesEdge) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        auto edge = findEdgeWithSourceAndTarget(g, a, b);
        ASSERT_NE(edge, nullptr);
        g.removeTargetsFromHyperedge(edge, { b.get() }, true);
        EXPECT_TRUE(g.rawEdges().find(edge) == g.rawEdges().end());
    }

    TEST_F(ConnectionManagementTest, RemoveTargetsFromHyperedge_ThrowsOnNonExistentConnection) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, nullptr, nullptr);
        auto edge = g.addConnection(a, b);
        EXPECT_THROW(g.removeTargetsFromHyperedge(edge, { c.get() }, false), std::logic_error);
    }

    // =============================================================================
    // 14. getName / setName
    // =============================================================================

    TEST_F(ConnectionManagementTest, GetName_ReturnsConstructorName) {
        EXPECT_EQ(g.getName(), "test");
    }

    TEST_F(ConnectionManagementTest, SetName_UpdatesName) {
        g.setName("renamed");
        EXPECT_EQ(g.getName(), "renamed");
    }

    // =============================================================================
    // 15. relocateNodeToLayer
    // =============================================================================

    // ---- null / no-op ---------------------------------------------------------

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_NullNode_NoThrowNoOp) {
        EXPECT_NO_THROW(g.relocateNodeToLayer(nullptr, 3));
    }

    // ---- plain branch: desired_layer between depth_rule_layer and last_layer --

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_BelowDepthRule_Throws) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p); // depth rule 1
        EXPECT_THROW(g.relocateNodeToLayer(c, 0), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_ExactlyAtCurrentLayer_NoOp) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_NO_THROW(g.relocateNodeToLayer(c, 1));
        EXPECT_EQ(c->getLayer(), 1);
        EXPECT_EQ(c->getDesiredLayer(), -1);
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_DeeperThanDepthRule_SetsOverrideAndMoves) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p); // depth rule 1
        g.relocateNodeToLayer(c, 4);
        EXPECT_EQ(c->getLayer(), 2);
        EXPECT_EQ(c->getDesiredLayer(), 2);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_CreatesDummyChainForMultiLayerJump) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        g.relocateNodeToLayer(c, 4); // last_layer=1 -> 4>1, normalizes to 2
        g.relocateNodeToLayer(c, 4); // last_layer=2 -> 4>2, normalizes to 3
        g.relocateNodeToLayer(c, 4); // last_layer=3 -> 4>3, normalizes to 4
        EXPECT_EQ(c->getLayer(), 4);
        EXPECT_GE(countDummyNodesInLayer(g, 1), 1);
        EXPECT_GE(countDummyNodesInLayer(g, 2), 1);
        EXPECT_GE(countDummyNodesInLayer(g, 3), 1);
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_PropagatesToDescendantsWithNoOtherAnchor) {
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, root);
        auto n2 = g.createNode("n2", 0, n1);
        g.relocateNodeToLayer(n1, 3);
        EXPECT_EQ(n1->getLayer(), 3);
        EXPECT_EQ(n2->getLayer(), 4);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_DescendantWithOwnDeeperOverride_NotForcedBack) {
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, root);
        auto n2 = g.createNode("n2", 0, n1);
        g.relocateNodeToLayer(n2, 6); // n2 has its own override, well past n1's depth rule
        g.relocateNodeToLayer(n2, 6); // n2 has its own override, well past n1's depth rule
        g.relocateNodeToLayer(n1, 2); // n1 moves deeper, but not deep enough to threaten n2's override
        EXPECT_EQ(n1->getLayer(), 2);
        EXPECT_EQ(n2->getLayer(), 4) << "n2's own valid override should be left alone";
        EXPECT_EQ(n2->getDesiredLayer(), 4);
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_OverrideClearedWhenMovedBackToDepthRule) {
        // Using a parentless node deliberately: with a real parent, ANY override at all forces
        // at least one dummy hop, making the node's immediate parent a dummy rather than the
        // original real one — so "the depth rule" computed from current parents becomes relative
        // to that dummy, not the original ancestor, and a second relocateNodeToLayer call can no
        // longer walk it back to the ORIGINAL natural value in one step. A parentless node's depth
        // rule is fixed at 0 regardless of where it currently sits, avoiding that confound entirely.
        auto iso = g.createNode("iso", 0, nullptr, nullptr);
        auto other = g.createNode("other", 0, nullptr, nullptr); // keeps layer 0 from being solely iso's
        // Scaffold depth so 3 is within [depth_rule, last_layer] (the plain branch), not beyond it.
        auto scaffold = g.createNode("scaffold", 0, nullptr, nullptr);
        for (int i = 0; i < 3; ++i) scaffold = g.createNode("s" + std::to_string(i), 0, scaffold);

        g.relocateNodeToLayer(iso, 3);
        ASSERT_EQ(iso->getLayer(), 3);
        ASSERT_EQ(iso->getDesiredLayer(), 3);

        g.relocateNodeToLayer(iso, 0); // back to the plain (parentless) depth rule
        EXPECT_EQ(iso->getLayer(), 0);
        EXPECT_EQ(iso->getDesiredLayer(), -1);
    }

    // ---- desired_layer == -1 branch: new shallowest layer ----------------------

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_MinusOne_NodeWithParents_Throws) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_THROW(g.relocateNodeToLayer(c, -1), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_MinusOne_SoleOccupantOfLayerZero_Throws) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        EXPECT_THROW(g.relocateNodeToLayer(a, -1), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_MinusOne_NotSoleOccupant_ShiftsEverythingAndPlacesNodeAtZero) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        g.relocateNodeToLayer(a, -1);
        EXPECT_EQ(a->getLayer(), 0);
        EXPECT_EQ(b->getLayer(), 1);
        EXPECT_TRUE(layerContainsNode(g, 0, a));
        EXPECT_TRUE(layerContainsNode(g, 1, b));
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_MinusOne_ChildrenFollowIfNoOtherAnchor) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto child = g.createNode("child", 0, a);
        auto other = g.createNode("other", 0, nullptr, nullptr); // keeps layer 0 from being sole-occupied by 'a'
        g.relocateNodeToLayer(a, -1);
        EXPECT_EQ(a->getLayer(), 0);
        EXPECT_EQ(child->getLayer(), 1) << "child must still be exactly below 'a'";
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_MinusOne_NoGapsLeftBehind) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto child = g.createNode("child", 0, a);
        auto other = g.createNode("other", 0, nullptr, nullptr);
        g.relocateNodeToLayer(a, -1);
        int expected = 0;
        for (const auto& [layer, data] : g.getLayers())
            EXPECT_EQ(layer, expected++) << "Layer numbers must stay dense from 0";
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_MinusOne_OtherRootsReceiveExplicitOverride) {
        // A uniform shift preserves "current layer == depth rule" for any node WITH parents (they
        // shift too, so the relative distance is unchanged) — but a parentless node's depth rule
        // is fixed at 0 regardless of the shift, so a root dragged along to layer 1 must carry an
        // explicit override recording that it no longer sits at its natural position.
        auto other = g.createNode("other", 0, nullptr, nullptr);
        auto a = g.createNode("a", 0, nullptr, nullptr);
        g.relocateNodeToLayer(a, -1);
        EXPECT_EQ(other->getLayer(), 1);
        EXPECT_EQ(other->getDesiredLayer(), 1)
            << "other is parentless but no longer at its natural depth rule (0)";
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_MinusOne_MovedNodeDesiredLayerNotClobbered) {
        // Regression test for an ordering bug: the loop that re-anchors OTHER shifted roots to
        // desired_layer 1 must not run after (and overwrite) the moved node's own setDesiredLayer(-1)
        // — 'a' itself was also one of the shifted layer-0 roots before landing back at the new 0.
        auto other = g.createNode("other", 0, nullptr, nullptr);
        auto a = g.createNode("a", 0, nullptr, nullptr);
        ASSERT_EQ(a->getLayer(), 0);
        g.relocateNodeToLayer(a, -1);
        EXPECT_EQ(a->getLayer(), 0) << "If clobbered, a would end up stuck at layer 1 instead";
        EXPECT_EQ(a->getDesiredLayer(), -1) << "a is back at its natural depth rule (0)";
    }

    // ---- desired_layer beyond last_layer branch: new deepest layer -------------
    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_BeyondLastLayer_SoleOccupantNoParents_Throws) {
        auto a = g.createNode("a", 0, nullptr, nullptr); // only node, layer 0 is also last_layer
        EXPECT_THROW(g.relocateNodeToLayer(a, 5), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_BeyondLastLayer_NotSoleOccupant_Succeeds) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr); // shares layer 0 (the current last_layer) with a
        g.relocateNodeToLayer(a, 5);
        EXPECT_EQ(a->getLayer(), 1); // normalized to last_layer+1, not the literal 5
        EXPECT_TRUE(layerContainsNode(g, 0, b));
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_BeyondLastLayer_WithParent_CreatesDummyChainNoGap) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto leaf = g.createNode("leaf", 0, p); // leaf at layer 1, last_layer == 1
        g.relocateNodeToLayer(leaf, 10);
        EXPECT_EQ(leaf->getLayer(), 2) << "Normalized to last_layer+1";
        EXPECT_GE(countDummyNodesInLayer(g, 1), 1) << "p->leaf now spans two layers, needs a dummy";
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        int expected = 0;
        for (const auto& [layer, data] : g.getLayers())
            EXPECT_EQ(layer, expected++) << "Layer numbers must stay dense from 0";
    }

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_BeyondLastLayer_IsolatedNode_NoCrash) {
        // Regression test: an isolated node (no parents, no children) sent past last_layer must
        // not crash minimizeCrossingsAfterRelocation with an INT_MAX sentinel.
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        EXPECT_NO_THROW(g.relocateNodeToLayer(a, 5));
        EXPECT_EQ(a->getLayer(), 1);
    }

    // ---- combined / gap-safety regression ---------------------------------------

    TEST_F(ConnectionManagementTest, RelocateNodeToLayer_Stress_SequenceOfRelocations_NoGapsEver) {
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, root);
        auto n2 = g.createNode("n2", 0, n1);
        auto iso = g.createNode("iso", 0, nullptr, nullptr);

        g.relocateNodeToLayer(n1, 4);     // n1 jumps deep; n2 cascades to 5; dummies fill 1-3
        g.relocateNodeToLayer(iso, 100);  // normalizes to last_layer+1, stays adjacent, no gap
        g.removeConnection(root, n1);     // orphans n1; its override (4) survives with no parents;
        // the dummy chain through 1-3 dissolves, opening a real gap

        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        int expected = 0;
        for (const auto& [layer, data] : g.getLayers()) {
            EXPECT_FALSE(data.nodes.empty() && data.outgoing_edges.empty())
                << "Layer " << layer << " should have been cleaned up if empty";
            EXPECT_EQ(layer, expected++) << "Layer numbers must stay dense from 0";
        }
    }

    // =============================================================================
    // 16. out_altered_layers reporting — public API
    // =============================================================================

    TEST_F(ConnectionManagementTest, AddConnection_ShortEdge_ReportsItsLayer) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, nullptr, nullptr); // c also at layer 0 (unrelated root)
        std::set<int> altered;
        g.addConnection(p, c, &altered); // c must relocate to 1; edge lands at layer 0
        EXPECT_TRUE(altered.count(0) > 0);
    }

    TEST_F(ConnectionManagementTest, AddConnection_LongEdge_ReportsSegmentLayers) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto mid = g.createNode("mid", 0, p);
        auto far = g.createNode("far", 0, mid); // far at layer 2
        auto x = g.createNode("x", 0, nullptr, nullptr); // x at layer 0
        std::set<int> altered;
        g.addConnection(x, far, &altered); // x(0)->far(2): needs a dummy at layer 1
        EXPECT_TRUE(altered.count(0) > 0);
        EXPECT_TRUE(altered.count(1) > 0) << "The new segment/dummy layer must be reported";
    }

    TEST_F(ConnectionManagementTest, AddConnection_RelocationCascade_ReportsDescendantLayers) {
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto dx1 = g.createNode("dx1", 0, root);
        auto dx2 = g.createNode("dx2", 0, dx1);      // dx2 at layer 2
        auto c = g.createNode("c", 0, nullptr, nullptr);      // c at layer 0
        auto gc = g.createNode("gc", 0, c);          // gc at layer 1

        std::set<int> altered;
        g.addConnection(dx2, c, &altered); // dx2(2)->c: forces c to 3, gc cascades to 4
        ASSERT_EQ(c->getLayer(), 3);
        ASSERT_EQ(gc->getLayer(), 4);
        EXPECT_TRUE(altered.count(3) > 0) << "c's new layer, where the resettled c->gc edge now sits";
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_StaysShortSameLayer_StillReports) {
        auto p1 = g.createNode("p1", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p1);        // p1->c, edge at layer 0
        auto p2 = g.createNode("p2", 0, nullptr, nullptr); // p2 also at layer 0
        auto edge = findEdgeWithSourceAndTarget(g, p1, c);
        ASSERT_NE(edge, nullptr);
        ASSERT_EQ(edge->getLayer(), 0);

        std::set<int> altered;
        g.addSourceToEdge(edge, p2, &altered); // p2 at the same layer: edge stays short at 0
        ASSERT_EQ(edge->getLayer(), 0) << "Confirms the dedup-defeat scenario actually occurred";
        EXPECT_TRUE(altered.count(0) > 0)
            << "addHyperedgeToLayer's own dedup would otherwise silently swallow this";
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_StaysShortSameLayer_StillReports) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c1 = g.createNode("c1", 0, p); // p->c1, edge at layer 0
        auto edge = findEdgeWithSourceAndTarget(g, p, c1);
        ASSERT_NE(edge, nullptr);
        ASSERT_EQ(edge->getLayer(), 0);

        auto other_root = g.createNode("other_root", 0, nullptr, nullptr);
        auto c2 = g.createNode("c2", 0, other_root); // c2 already at layer 1 via an unrelated parent

        std::set<int> altered;
        g.addTargetToEdge(edge, c2, &altered); // c2 already at the right layer; no relocation needed
        ASSERT_EQ(edge->getLayer(), 0) << "Confirms the dedup-defeat scenario actually occurred";
        EXPECT_TRUE(altered.count(0) > 0);
    }

    TEST_F(ConnectionManagementTest, CreateSource_OnShortEdge_AlwaysStaysAtSameLayer_StillReports) {
        // Adding a source at min(target layers) - 1 to an already-short edge always lands exactly
        // where the existing sources already are, by construction — this is the common case, not
        // a corner case, so it's worth confirming it always triggers the dedup-defeat guard.
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p); // edge at layer 0
        auto edge = findEdgeWithSourceAndTarget(g, p, c);
        ASSERT_NE(edge, nullptr);
        ASSERT_EQ(edge->getLayer(), 0);

        std::set<int> altered;
        auto src = g.createSource("src", -1, edge, &altered);
        ASSERT_NE(src, nullptr);
        EXPECT_EQ(src->getLayer(), 0);
        EXPECT_EQ(edge->getLayer(), 0);
        EXPECT_TRUE(altered.count(0) > 0);
    }

    TEST_F(ConnectionManagementTest, RemoveNode_InternalNode_ReportsNewParentChildEdgeLayer) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto mid = g.createNode("mid", 0, p);   // p->mid, layer 0
        auto c = g.createNode("c", 0, mid);     // mid->c, layer 1

        std::set<int> altered;
        g.removeNode(mid, &altered); // p inherits c directly: new p->c edge, short at layer 0
        auto new_edge = findEdgeWithSourceAndTarget(g, p, c);
        ASSERT_NE(new_edge, nullptr);
        EXPECT_TRUE(altered.count(0) > 0);
    }

    TEST_F(ConnectionManagementTest, RemoveSourcesFromHyperedge_RemovalFromSurvivingEdge_StillReports) {
        // Per the corrected reporting rule: removing a source from an edge that survives with
        // other sources intact counts as an alteration, not just adding one.
        auto p1 = g.createNode("p1", 0, nullptr, nullptr);
        auto p2 = g.createNode("p2", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p1); // p1->c, edge at layer 0
        auto edge = findEdgeWithSourceAndTarget(g, p1, c);
        ASSERT_NE(edge, nullptr);
        g.addSourceToEdge(edge, p2); // edge now has sources {p1, p2}

        std::set<int> altered;
        g.removeSourcesFromHyperedge(edge, { p2.get() }, false, &altered); // p1 remains
        EXPECT_TRUE(edgeHasSource(edge, p1));
        EXPECT_FALSE(edgeHasSource(edge, p2));
        EXPECT_TRUE(altered.count(edge->getLayer()) > 0);
    }

    TEST_F(ConnectionManagementTest, FuseNodes_MergesEdgeMembership_Reports) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        auto shared_target = g.createNode("t", 0, a); // a->t, layer 0
        g.addConnection(b, shared_target);            // b->t, a separate edge, also layer 0

        std::set<int> altered;
        g.fuseNodes(a, b, "ab", &altered);
        EXPECT_FALSE(altered.empty())
            << "Merging edge membership between a and b's edges must report the affected layer(s)";
    }

    // =============================================================================
    // 17. INTENSIVE — complex topology and extreme cases
    // =============================================================================

    // Helper: every node appears in exactly one layer
    static bool eachNodeInExactlyOneLayer(const TestableHypergraph& g) {
        std::unordered_map<Node*, int> count;
        for (const auto& [l, data] : g.getLayers())
            for (const auto& n : data.nodes)
                count[n.get()]++;
        for (const auto& [n, c] : count)
            if (c != 1) return false;
        return true;
    }

    // Helper: no node with a valid parent has layer <= that parent's layer
    static bool layerOrderIsConsistent(const TestableHypergraph& g) {
        for (const auto& n : g.getAllNodes()) {
            for (const auto& p : n->getParents()) {
                if (!p->isDummy() && !n->isDummy())
                    if (n->getLayer() <= p->getLayer()) return false;
            }
        }
        return true;
    }

    // Helper: no duplicate entries in all_nodes_
    static bool allNodesHasNoDuplicates(const TestableHypergraph& g) {
        std::unordered_set<Node*> seen;
        for (const auto& n : g.getAllNodes()) {
            if (!seen.insert(n.get()).second) return false;
        }
        return true;
    }

    // ---- addConnection: extreme topologies ----------------------------------------

    TEST_F(ConnectionManagementTest, Stress_ZigZagCrossEdges_GlobalInvariants) {
        // z0->z1->z2->z3->z4; then cross-connect z0->z2, z0->z3, z1->z3, z1->z4
        auto z0 = g.createNode("z0", 0, nullptr, nullptr);
        auto z1 = g.createNode("z1", 0, z0);
        auto z2 = g.createNode("z2", 0, z1);
        auto z3 = g.createNode("z3", 0, z2);
        auto z4 = g.createNode("z4", 0, z3);

        EXPECT_THROW(g.addConnection(z0, z2), std::logic_error);
        EXPECT_THROW(g.addConnection(z0, z3), std::logic_error);
        EXPECT_THROW(g.addConnection(z1, z3), std::logic_error);
        EXPECT_THROW(g.addConnection(z1, z4), std::logic_error);

        EXPECT_EQ(z0->getLayer(), 0);
        EXPECT_EQ(z1->getLayer(), 1);
        EXPECT_EQ(z2->getLayer(), 2);
        EXPECT_EQ(z3->getLayer(), 3);
        EXPECT_EQ(z4->getLayer(), 4);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
        EXPECT_TRUE(layerOrderIsConsistent(g));
    }

    TEST_F(ConnectionManagementTest, Stress_TwoParallelChainsWithSharedSink) {
        auto a0 = g.createNode("a0", 0, nullptr, nullptr);
        auto a1 = g.createNode("a1", 0, a0);
        auto a2 = g.createNode("a2", 0, a1);
        auto a3 = g.createNode("a3", 0, a2);
        auto b0 = g.createNode("b0", 0, nullptr, nullptr);
        auto b1 = g.createNode("b1", 0, b0);
        auto b2 = g.createNode("b2", 0, b1);
        auto b3 = g.createNode("b3", 0, b2);
        auto sink = g.createNode("sink", 0, nullptr, nullptr);
        g.addConnection(a3, sink);
        g.addConnection(b3, sink);

        EXPECT_EQ(sink->getLayer(), 4);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
    }

    TEST_F(ConnectionManagementTest, Stress_WideDAG_FiveRootsOneLeaf_ThenSkipEdges) {
        std::vector<NodePtr> roots;
        for (int i = 0; i < 5; i++) roots.push_back(g.createNode("r" + std::to_string(i), 0, nullptr));
        auto mid1 = g.createNode("m1", 0, roots[0]);
        auto mid2 = g.createNode("m2", 0, roots[1]);
        auto leaf = g.createNode("leaf", 0, mid1);
        g.addConnection(mid2, leaf);
        // Now add remaining roots to mid1 and mid2 (all at layer 0, adjacent)
        for (int i = 2; i < 5; i++) g.addConnection(roots[i], leaf);

        EXPECT_EQ(leaf->getLayer(), 2);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    TEST_F(ConnectionManagementTest, Stress_DeepChain20Nodes_AllLayersCorrect) {
        NodePtr prev = g.createNode("n0", 0, nullptr, nullptr);
        std::vector<NodePtr> chain{ prev };
        for (int i = 1; i < 20; i++) {
            prev = g.createNode("n" + std::to_string(i), 0, prev);
            chain.push_back(prev);
        }
        for (int i = 0; i < 20; i++)
            EXPECT_EQ(chain[i]->getLayer(), i) << "Node n" << i << " at wrong layer";
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allNodesHasNoDuplicates(g));
    }

    TEST_F(ConnectionManagementTest, Stress_AddConnectionAfterMultipleRelocations) {
        // Build two chains and then repeatedly add parents to force deep relocation.
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, nullptr, nullptr);

        g.addConnection(a, b);   // b->1
        g.addConnection(b, c);   // c->2

        auto d = g.createNode("d", 0, nullptr, nullptr);
        auto e = g.createNode("e", 0, d);  // e->1
        g.addConnection(e, b);             // b->2, c->3

        auto f = g.createNode("f", 0, e); // f->2
        g.addConnection(f, c);            // c->3 (still), no change

        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(layerOrderIsConsistent(g));
    }

    TEST_F(ConnectionManagementTest, Stress_SkipEdgesThenRelocation_AllInvariants) {
        // Chain r->n1->n2->n3->n4; add r2->n4 (skip 4); then add new parent to r2
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto n4 = g.createNode("n4", 0, n3);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        g.addConnection(r2, n4);  // long edge, gap = 4

        auto deep_root = g.createNode("dr", 0, nullptr, nullptr);
        auto deep_mid = g.createNode("dm", 0, deep_root);
        auto deep_mid2 = g.createNode("dm2", 0, deep_mid);
        g.addConnection(deep_mid2, r2);  // r2 moves to layer 3, n4 to layer 4

        EXPECT_EQ(r2->getLayer(), 3);
        EXPECT_EQ(n4->getLayer(), 4);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
    }

    // ---- addSourceToEdge: complex cases -------------------------------------------

    TEST_F(ConnectionManagementTest, Stress_AddMultipleSourcesToLongEdge_Invariants) {
        // Build: r0->r1->r2 (chain), target t3 at layer 3 under another chain.
        // Create a long edge r0->t3, then add r1 and r2 as additional sources.
        auto r0 = g.createNode("r0", 0, nullptr, nullptr);
        auto r1 = g.createNode("r1", 0, r0);
        auto r2 = g.createNode("r2", 0, r1);
        auto chain_root = g.createNode("cr", 0, nullptr, nullptr);
        auto cn1 = g.createNode("cn1", 0, chain_root);
        auto cn2 = g.createNode("cn2", 0, cn1);
        auto t3 = g.createNode("t3", 0, cn2);  // layer 3

        g.addConnection(r0, t3);  // long edge gap=3

        auto edge = findEdgeWithSourceAndTarget(g, r0, t3);
        ASSERT_NE(edge, nullptr);

        g.addSourceToEdge(edge, r1);
        g.addSourceToEdge(edge, r2);

        EXPECT_FALSE(edgeHasSource(edge, r0));
        EXPECT_FALSE(edgeHasSource(edge, r1));
        EXPECT_TRUE(edgeHasSource(edge, r2));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
    }

    TEST_F(ConnectionManagementTest, Stress_AddSourceGrouping_TwoEdgesMergedIntoOne) {
        // {p}->c and {q}->c independently; then add q to p's edge -> merge groupings
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto q = g.createNode("q", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        // Also create an independent q->c edge
        g.addConnection(q, c);  // q already connects to c

        // Now find the p->c edge and try adding q (q->c already exists as separate edge)
        auto edge = findEdgeWithSourceAndTarget(g, p, c);
        ASSERT_NE(edge, nullptr);

        // This should be allowed as a regrouping (q has only one edge to c)
        EXPECT_NO_THROW(g.addSourceToEdge(edge, q));

        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    // ---- addTargetToEdge: complex cases -------------------------------------------

    TEST_F(ConnectionManagementTest, Stress_AddTargetToEdge_MultipleTargetsAtDifferentLayers) {
        // Sources at layer 0; add targets at layers 1, 2, 3 — edge becomes long
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto t1 = g.createNode("t1", 0, p);   // layer 1

        auto chain_root = g.createNode("cr", 0, nullptr, nullptr);
        auto cm1 = g.createNode("cm1", 0, chain_root);
        auto t2 = g.createNode("t2", 0, cm1);  // layer 2

        auto edge = findEdgeWithSourceAndTarget(g, p, t1);
        ASSERT_NE(edge, nullptr);
        g.addTargetToEdge(edge, t2);

        EXPECT_TRUE(edgeHasTarget(edge, t1));
        EXPECT_TRUE(edgeHasTarget(edge, t2));
        EXPECT_GE(countSegmentEdges(g), 1);
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, Stress_AddTarget_Then_AddSource_ThenRemoveTarget) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto t1 = g.createNode("t1", 0, p);
        auto t2 = g.createNode("t2", 0, nullptr, nullptr);
        auto q = g.createNode("q", 0, nullptr, nullptr);

        auto edge = findEdgeWithSourceAndTarget(g, p, t1);
        ASSERT_NE(edge, nullptr);

        g.addTargetToEdge(edge, t2);  // {p}->{t1, t2}
        g.addSourceToEdge(edge, q);   // {p,q}->{t1, t2}

        // Now remove the connection p->t1 specifically
        g.removeConnection(p, t1);

        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
    }

    // ---- removeNode: complex cases ------------------------------------------------

    TEST_F(ConnectionManagementTest, Stress_RemoveNode_FromMiddleOfDeepChain_Invariants) {
        // Chain of 8: n0->n1->...->n7; remove n3 (middle)
        NodePtr prev = g.createNode("n0", 0, nullptr, nullptr);
        std::vector<NodePtr> chain{ prev };
        for (int i = 1; i <= 7; i++) {
            prev = g.createNode("n" + std::to_string(i), 0, prev);
            chain.push_back(prev);
        }
        g.removeNode(chain[3]);
        EXPECT_FALSE(nodeInAllNodes(g, chain[3]));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
        // Connectivity: chain[2] should now link to chain[4]
        bool connected = false;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            if (edgeHasSource(e, chain[2]) && edgeHasTarget(e, chain[4])) { connected = true; break; }
        }
        EXPECT_TRUE(connected) << "n2 should now connect to n4 after n3 removed";
    }

    TEST_F(ConnectionManagementTest, Stress_RemoveNode_WithMultipleParentsAndChildren) {
        // Three parents, three children; remove the hub node
        auto p1 = g.createNode("p1", 0, nullptr, nullptr);
        auto p2 = g.createNode("p2", 0, nullptr, nullptr);
        auto p3 = g.createNode("p3", 0, nullptr, nullptr);
        auto hub = g.createNode("hub", 0, p1);
        g.addConnection(p2, hub);
        g.addConnection(p3, hub);
        auto c1 = g.createNode("c1", 0, hub);
        auto c2 = g.createNode("c2", 0, hub);
        auto c3 = g.createNode("c3", 0, hub);

        g.removeNode(hub);

        EXPECT_FALSE(nodeInAllNodes(g, hub));
        // All children should now have all parents
        for (const auto& child : { c1, c2, c3 }) {
            auto parents = child->getParents();
            std::unordered_set<Node*> pset;
            for (const auto& p : parents) pset.insert(p.get());
            EXPECT_TRUE(pset.count(p1.get()) || pset.count(p2.get()) || pset.count(p3.get()))
                << "Child should still have at least one of hub's parents";
        }
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    TEST_F(ConnectionManagementTest, Stress_RemoveNode_LeafOfLongEdge_DummiesCleaned) {
        // r->n1->n2->n3; add r2->n3 (long edge); then remove n3
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        g.addConnection(r2, n3);
        ASSERT_GT(countSegmentEdges(g), 0);

        g.removeNode(n3);

        EXPECT_FALSE(nodeInAllNodes(g, n3));
        // After removing n3, the long edge to n3 should be gone along with its dummies
        for (const auto& e : g.getAllHyperedges())
            EXPECT_FALSE(edgeHasTarget(e, n3)) << "n3 should not appear as target in any edge";
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
    }

    TEST_F(ConnectionManagementTest, Stress_RemoveMultipleNodesSequentially) {
        // Build a 4-level binary tree then remove all level-2 nodes
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto l1 = g.createNode("l1", 0, root);
        auto r1 = g.createNode("r1", 0, root);
        auto ll = g.createNode("ll", 0, l1);
        auto lr = g.createNode("lr", 0, l1);
        auto rl = g.createNode("rl", 0, r1);
        auto rr = g.createNode("rr", 0, r1);

        g.removeNode(l1);
        g.removeNode(r1);

        EXPECT_FALSE(nodeInAllNodes(g, l1));
        EXPECT_FALSE(nodeInAllNodes(g, r1));
        EXPECT_TRUE(nodeInAllNodes(g, ll));
        EXPECT_TRUE(nodeInAllNodes(g, lr));
        EXPECT_TRUE(nodeInAllNodes(g, rl));
        EXPECT_TRUE(nodeInAllNodes(g, rr));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
    }

    // ---- removeConnection: complex cases ------------------------------------------

    TEST_F(ConnectionManagementTest, Stress_RemoveConnection_FromLongEdge_SegmentsRebuilt) {
        // r->n1->n2->n3; r2->n3 (long edge); then remove connection r2->n3
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        auto r3 = g.createNode("r3", 0, nullptr, nullptr);
        g.addConnection(r2, n3);
        g.addConnection(r3, n3);

        g.removeConnection(r2, n3);

        // r2->n3 should no longer exist
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            EXPECT_FALSE(edgeHasSource(e, r2) && edgeHasTarget(e, n3));
        }
        // r3->n3 must still exist
        bool r3n3 = false;
        for (const auto& e : g.getAllHyperedges()) {
            if (!e->isSegment() && edgeHasSource(e, r3) && edgeHasTarget(e, n3)) { r3n3 = true; break; }
        }
        EXPECT_TRUE(r3n3);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, Stress_RemoveAllConnectionsOfNode_BecomesIsolated) {
        auto p1 = g.createNode("p1", 0, nullptr, nullptr);
        auto p2 = g.createNode("p2", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p1);
        g.addConnection(p2, c);

        g.removeConnection(p1, c);
        g.removeConnection(p2, c);

        // c should now be isolated (no parents in any edge)
        for (const auto& e : g.getAllHyperedges())
            EXPECT_FALSE(edgeHasTarget(e, c)) << "c should have no remaining edges";
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    // ---- fuseNodes: complex cases -------------------------------------------------

    TEST_F(ConnectionManagementTest, Stress_FuseNodes_LongChainIntermediates) {
        // r->a->b->c->d; fuse b and c (adjacent)
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto a = g.createNode("a", 0, r);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);

        // b and c are in a direct parent-child relationship -> fusing would create cycle
        EXPECT_THROW(g.fuseNodes(b, c, "bc"), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, Stress_FuseNodes_TwoNodesWithDescendants_LayerCorrect) {
        // r1->a->leaf1, r2->b->leaf2; fuse a and b -> merged node below max(r1,r2)
        auto r1 = g.createNode("r1", 0, nullptr, nullptr);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        auto deep = g.createNode("deep", 0, r2);  // r2 at 0, deep at 1
        auto a = g.createNode("a", 0, r1);   // layer 1
        auto b = g.createNode("b", 0, deep); // layer 2
        auto leaf1 = g.createNode("leaf1", 0, a);
        auto leaf2 = g.createNode("leaf2", 0, b);

        g.fuseNodes(a, b, "ab");

        EXPECT_FALSE(nodeInAllNodes(g, b));
        EXPECT_EQ(a->getLayer(), 2);  // max parent layer is deep(1) -> layer 2
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
    }

    TEST_F(ConnectionManagementTest, Stress_FuseNodes_SharedChildrenDeduplication) {
        // r->a->sink, r->b->sink; fuse a and b; sink must have only one parent (fused)
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto a = g.createNode("a", 0, r);
        auto b = g.createNode("b", 0, r);
        auto sink = g.createNode("sink", 0, a);
        g.addConnection(b, sink);  // b also connects to sink

        g.fuseNodes(a, b, "ab");

        auto parents = sink->getParents();
        std::unordered_set<Node*> pset;
        for (const auto& p : parents) pset.insert(p.get());
        EXPECT_EQ(pset.size(), 1u) << "sink should have exactly one parent (the fused node)";
        EXPECT_TRUE(pset.count(a.get()));
        EXPECT_FALSE(nodeInAllNodes(g, b));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    // ---- end-to-end mixed stress tests --------------------------------------------

    TEST_F(ConnectionManagementTest, Stress_EndToEnd_BuildFuseThenRemove) {
        // Build diamond, fuse two middle nodes, then remove the fused node
        auto top = g.createNode("top", 0, nullptr, nullptr);
        auto left = g.createNode("left", 0, top);
        auto right = g.createNode("right", 0, top);
        auto bottom = g.createNode("bottom", 0, left);
        g.addConnection(right, bottom);

        // left and right have a common parent (top) and common child (bottom)
        g.fuseNodes(left, right, "mid");
        EXPECT_EQ(g.getAllHyperedges().size(), 2u);

        // Instead: add an unrelated pair and fuse them
        auto x = g.createNode("x", 0, nullptr, nullptr);
        auto y = g.createNode("y", 0, nullptr, nullptr);
        g.fuseNodes(x, y, "xy");
        g.addConnection(g.getAllNodes().back(), bottom);  // connect fused to bottom

        g.removeNode(bottom);

        EXPECT_FALSE(nodeInAllNodes(g, bottom));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    TEST_F(ConnectionManagementTest, Stress_EndToEnd_AddSourceTargetRemoveNode) {
        // Build a large hyperedge, then remove a source and a target via removeNode
        auto s1 = g.createNode("s1", 0, nullptr, nullptr);
        auto s2 = g.createNode("s2", 0, nullptr, nullptr);
        auto s3 = g.createNode("s3", 0, nullptr, nullptr);
        auto t1 = g.createNode("t1", 0, s1);

        auto edge = findEdgeWithSourceAndTarget(g, s1, t1);
        ASSERT_NE(edge, nullptr);
        g.addSourceToEdge(edge, s2);
        g.addSourceToEdge(edge, s3);

        auto t2 = g.createNode("t2", 0, nullptr, nullptr);
        auto t3 = g.createNode("t3", 0, nullptr, nullptr);
        g.addTargetToEdge(edge, t2);
        g.addTargetToEdge(edge, t3);

        // Now remove one source node and one target node
        g.removeNode(s2);
        g.removeNode(t1);

        EXPECT_FALSE(nodeInAllNodes(g, s2));
        EXPECT_FALSE(nodeInAllNodes(g, t1));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
        EXPECT_TRUE(allNodesHasNoDuplicates(g));
    }

    TEST_F(ConnectionManagementTest, Stress_EndToEnd_AddConnectionsCreateLongEdgesRemoveNodes) {
        // Build: two separate 3-level trees; cross-connect their tops to each other's bottoms
        auto ra = g.createNode("ra", 0, nullptr, nullptr);
        auto ma = g.createNode("ma", 0, ra);
        auto la = g.createNode("la", 0, ma);

        auto rb = g.createNode("rb", 0, nullptr, nullptr);
        auto mb = g.createNode("mb", 0, rb);
        auto lb = g.createNode("lb", 0, mb);

        // Cross: ra -> lb (skip 2 layers), rb -> la (skip 2 layers)
        g.addConnection(ra, lb);
        g.addConnection(rb, la);

        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));

        // Remove the mid-level nodes and check that the graph stays consistent
        g.removeNode(ma);
        g.removeNode(mb);

        auto edge = findEdgeWithSourceAndTarget(g, ra, lb);

        EXPECT_FALSE(nodeInAllNodes(g, ma));
        EXPECT_FALSE(nodeInAllNodes(g, mb));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
    }

    TEST_F(ConnectionManagementTest, Stress_UniqueLayerMembership_AfterManyOps) {
        // Perform a long sequence of operations and assert no node is in multiple layers
        auto r = g.createNode("r", 0, nullptr, nullptr);
        std::vector<NodePtr> chain{ r };
        for (int i = 1; i <= 6; i++)
            chain.push_back(g.createNode("n" + std::to_string(i), 0, chain.back()));

        // Add extra parents to several chain nodes
        auto x = g.createNode("x", 0, nullptr, nullptr);
        auto y = g.createNode("y", 0, nullptr, nullptr);
        g.addConnection(x, chain[3]);
        g.addConnection(y, chain[5]);

        // Add source to an edge
        auto edge = findEdgeWithSourceAndTarget(g, chain[2], chain[3]);
        if (edge) g.addSourceToEdge(edge, x);

        // Remove a middle node
        g.removeNode(chain[2]);

        // Fuse two roots
        g.fuseNodes(x, y, "xy");

        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allNodesHasNoDuplicates(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(layerOrderIsConsistent(g));
    }

} // namespace hypergraph_logic::hypergraph_tests::connection_management