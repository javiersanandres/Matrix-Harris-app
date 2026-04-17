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
            int k = g.edgeIsShort(e);
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
        auto n = g.createNode("n", 0, nullptr);
        EXPECT_EQ(n->getLayer(), 0);
        EXPECT_TRUE(layerContainsNode(g, 0, n));
        EXPECT_TRUE(nodeInAllNodes(g, n));
    }

    TEST_F(ConnectionManagementTest, CreateNode_WithParent_PlacedAtParentLayerPlusOne) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_EQ(c->getLayer(), 1);
        EXPECT_TRUE(layerContainsNode(g, 1, c));
    }

    TEST_F(ConnectionManagementTest, CreateNode_WithParent_EdgeCreated) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_TRUE(edgeHasTarget(edge, c));
    }

    TEST_F(ConnectionManagementTest, CreateNode_WithParent_ParentChildLinksSet) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto children = p->getChildren();
        auto parents = c->getParents();
        EXPECT_NE(std::find(children.begin(), children.end(), c), children.end());
        EXPECT_NE(std::find(parents.begin(), parents.end(), p), parents.end());
    }

    TEST_F(ConnectionManagementTest, CreateNode_ChainPropagatesLayers) {
        auto a = g.createNode("a", 0, nullptr);
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
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);

        auto mid = g.createNode("mid", edge);

        ASSERT_NE(mid, nullptr);
        EXPECT_TRUE(nodeInAllNodes(g, mid));
        // New node must be between p and c
        EXPECT_EQ(mid->getLayer(), 1);
        EXPECT_EQ(c->getLayer(), 2);
    }

    TEST_F(ConnectionManagementTest, CreateNodeOnEdge_NullEdgeReturnsNull) {
        EXPECT_EQ(g.createNode("x", nullptr), nullptr);
    }

    // =============================================================================
    // 3. createSource / createTarget
    // =============================================================================

    TEST_F(ConnectionManagementTest, CreateSource_AddedToEdgeAtLayer0) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);

        auto src = g.createSource("src", 0, edge);

        ASSERT_NE(src, nullptr);
        EXPECT_EQ(src->getLayer(), 0);
        EXPECT_TRUE(edgeHasSource(edge, src));
        EXPECT_TRUE(nodeInAllNodes(g, src));
    }

    TEST_F(ConnectionManagementTest, CreateSource_NullEdgeReturnsNull) {
        EXPECT_EQ(g.createSource("x", 0, nullptr), nullptr);
    }

    TEST_F(ConnectionManagementTest, CreateTarget_AddedToEdgeAtCorrectLayer) {
        auto p = g.createNode("p", 0, nullptr);
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
        auto p = g.createNode("p", 0, nullptr);
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
        auto c = g.createNode("c", 0, nullptr);
        EXPECT_NO_THROW(g.addConnection(nullptr, c));
        EXPECT_TRUE(layerContainsNode(g, 0, c));
    }

    TEST_F(ConnectionManagementTest, AddConnection_NullChildIgnored) {
        auto p = g.createNode("p", 0, nullptr);
        EXPECT_NO_THROW(g.addConnection(p, nullptr));
    }

    TEST_F(ConnectionManagementTest, AddConnection_SelfConnectionThrows) {
        auto n = g.createNode("n", 0, nullptr);
        EXPECT_THROW(g.addConnection(n, n), std::invalid_argument);
    }

    TEST_F(ConnectionManagementTest, AddConnection_DuplicateDirectConnectionThrows) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_THROW(g.addConnection(p, c), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddConnection_TransitiveAncestorThrows) {
        auto p = g.createNode("p", 0, nullptr);
        auto m = g.createNode("m", 0, p);
        auto c = g.createNode("c", 0, m);
        EXPECT_THROW(g.addConnection(p, c), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddConnection_DirectCycleThrows) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_THROW(g.addConnection(c, p), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddConnection_LongCycleThrows) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);
        EXPECT_THROW(g.addConnection(d, a), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddConnection_ExceptionSafety_StateUnchangedOnCycle) {
        auto a = g.createNode("a", 0, nullptr);
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
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        auto c = g.createNode("c", 0, a);
        g.addConnection(b, c);
        EXPECT_TRUE(layerContainsNode(g, 1, c));
        EXPECT_EQ(countDummyNodesInLayer(g, 1), 0);
        EXPECT_EQ(countSegmentEdges(g), 0);
    }

    TEST_F(ConnectionManagementTest, AddConnection_ParentChildLinksSet) {
        auto p = g.createNode("p", 0, nullptr);
        auto q = g.createNode("q", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        g.addConnection(q, c);
        auto parents = c->getParents();
        auto children = q->getChildren();
        EXPECT_NE(std::find(parents.begin(), parents.end(), q), parents.end());
        EXPECT_NE(std::find(children.begin(), children.end(), c), children.end());
    }

    TEST_F(ConnectionManagementTest, AddConnection_ChildMovesDown) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        g.addConnection(a, b);
        EXPECT_EQ(b->getLayer(), 1);
        EXPECT_FALSE(layerContainsNode(g, 0, b));
        EXPECT_TRUE(layerContainsNode(g, 1, b));
    }

    TEST_F(ConnectionManagementTest, AddConnection_DescendantsPropagateOnChildMove) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);
        g.addConnection(a, b);
        EXPECT_EQ(b->getLayer(), 1);
        EXPECT_EQ(c->getLayer(), 2);
        EXPECT_EQ(d->getLayer(), 3);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, AddConnection_LongEdgeSplitsWithDummies) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2);
        EXPECT_GE(countDummyNodesInLayer(g, 1), 1);
        EXPECT_GE(countSegmentEdges(g), 2);
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    TEST_F(ConnectionManagementTest, AddConnection_LongEdge_RealNodesNoDirectDummyLinks) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2);
        for (const auto& p : n2->getParents())
            EXPECT_FALSE(p->isDummy()) << "n2 should not have dummy parents";
        for (const auto& ch : r2->getChildren())
            EXPECT_FALSE(ch->isDummy()) << "r2 should not have dummy children";
    }

    TEST_F(ConnectionManagementTest, AddConnection_TransitiveEdgeRemovedAfterAdd) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
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
        auto a = g.createNode("a", 0, nullptr);
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
        auto leaf = g.createNode("leaf", 0, nullptr);
        for (int i = 0; i < 4; i++)
            g.addConnection(g.createNode("r" + std::to_string(i), 0, nullptr), leaf);
        EXPECT_EQ(leaf->getLayer(), 1);
        EXPECT_EQ(countDummyNodesInLayer(g, 1), 0);
        EXPECT_EQ(countSegmentEdges(g), 0);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, AddConnection_FullBinaryTree_FiveLevels) {
        auto root = g.createNode("root", 0, nullptr);
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
        auto n0 = g.createNode("n0", 0, nullptr);
        auto p1 = g.createNode("p1", 0, nullptr);
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
        auto n = g.createNode("n", 0, nullptr);
        EXPECT_NO_THROW(g.addSourceToEdge(nullptr, n));
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_NullSourceIgnored) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_NO_THROW(g.addSourceToEdge(edge, nullptr));
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_SegmentEdgeIgnored) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2);
        HyperedgePtr seg = nullptr;
        for (const auto& e : g.getAllHyperedges()) if (e->isSegment()) { seg = e; break; }
        ASSERT_NE(seg, nullptr);
        auto x = g.createNode("x", 0, nullptr);
        EXPECT_NO_THROW(g.addSourceToEdge(seg, x));
        EXPECT_FALSE(edgeHasSource(seg, x));
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_SourceIsTargetThrows) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_THROW(g.addSourceToEdge(edge, c), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_TransitiveAncestorThrows) {
        auto q = g.createNode("q", 0, nullptr);
        auto p = g.createNode("p", 0, q);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_THROW(g.addSourceToEdge(edge, q), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_AdjacentLayer_SourceAdded) {
        auto p = g.createNode("p", 0, nullptr);
        auto q = g.createNode("q", 0, nullptr);
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
        auto p = g.createNode("p", 0, nullptr);
        auto q = g.createNode("q", 0, nullptr);
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
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto r0 = g.createNode("r0", 0, nullptr);
        auto r1 = g.createNode("r1", 0, r0);
        auto r = g.createNode("r", 0, r1);  // layer 2
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addSourceToEdge(edge, r);
        EXPECT_EQ(c->getLayer(), 3);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, AddSourceToEdge_MultipleTargetsAllGetNewParent) {
        auto p = g.createNode("p", 0, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto c2 = g.createNode("c2", 0, nullptr);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addTargetToEdge(edge, c2);
        auto q = g.createNode("q", 0, nullptr);
        g.addSourceToEdge(edge, q);
        auto q_children = q->getChildren();
        EXPECT_NE(std::find(q_children.begin(), q_children.end(), c1), q_children.end());
        EXPECT_NE(std::find(q_children.begin(), q_children.end(), c2), q_children.end());
    }

    // =============================================================================
    // 7. addTargetToEdge
    // =============================================================================

    TEST_F(ConnectionManagementTest, AddTargetToEdge_NullEdgeIgnored) {
        auto n = g.createNode("n", 0, nullptr);
        EXPECT_NO_THROW(g.addTargetToEdge(nullptr, n));
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_NullTargetIgnored) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_NO_THROW(g.addTargetToEdge(edge, nullptr));
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_SegmentEdgeIgnored) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2);
        HyperedgePtr seg = nullptr;
        for (const auto& e : g.getAllHyperedges()) if (e->isSegment()) { seg = e; break; }
        ASSERT_NE(seg, nullptr);
        auto x = g.createNode("x", 0, nullptr);
        EXPECT_NO_THROW(g.addTargetToEdge(seg, x));
        EXPECT_FALSE(edgeHasTarget(seg, x));
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_TargetIsSourceThrows) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_THROW(g.addTargetToEdge(edge, p), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_TransitiveDescendantThrows) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto d = g.createNode("d", 0, c);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_THROW(g.addTargetToEdge(edge, d), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_AdjacentLayer_TargetAdded) {
        auto p = g.createNode("p", 0, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto c2 = g.createNode("c2", 0, nullptr);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addTargetToEdge(edge, c2);
        EXPECT_TRUE(edgeHasTarget(edge, c1));
        EXPECT_TRUE(edgeHasTarget(edge, c2));
        EXPECT_EQ(c2->getLayer(), 1);
        EXPECT_EQ(countSegmentEdges(g), 0);
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_ParentChildLinksUpdated) {
        auto p = g.createNode("p", 0, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto c2 = g.createNode("c2", 0, nullptr);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addTargetToEdge(edge, c2);
        auto p_children = p->getChildren();
        auto c2_parents = c2->getParents();
        EXPECT_NE(std::find(p_children.begin(), p_children.end(), c2), p_children.end());
        EXPECT_NE(std::find(c2_parents.begin(), c2_parents.end(), p), c2_parents.end());
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_ShallowTarget_MovesDown) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto t = g.createNode("t", 0, nullptr);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addTargetToEdge(edge, t);
        EXPECT_EQ(t->getLayer(), 1);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, AddTargetToEdge_DeepTarget_EdgeSplit) {
        auto p = g.createNode("p", 0, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto root = g.createNode("root", 0, nullptr);
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
        auto p = g.createNode("p", 0, nullptr);
        auto q = g.createNode("q", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addSourceToEdge(edge, q);
        auto c2 = g.createNode("c2", 0, nullptr);
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
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        g.removeNode(c);
        EXPECT_FALSE(nodeInAllNodes(g, c));
        EXPECT_FALSE(layerContainsNode(g, 1, c));
    }

    TEST_F(ConnectionManagementTest, RemoveNode_LeafNode_ParentChildLinkSevered) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        g.removeNode(c);
        auto children = p->getChildren();
        EXPECT_EQ(std::find(children.begin(), children.end(), c), children.end());
    }

    TEST_F(ConnectionManagementTest, RemoveNode_LeafNode_EdgeRemovedFromGraph) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        g.removeNode(c);
        for (const auto& e : g.getAllHyperedges())
            EXPECT_FALSE(edgeHasTarget(e, c)) << "No edge should still target removed node";
    }

    TEST_F(ConnectionManagementTest, RemoveNode_RootWithChildren_ChildrenBecomeRoots) {
        auto root = g.createNode("root", 0, nullptr);
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
        auto p = g.createNode("p", 0, nullptr);
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
        auto p = g.createNode("p", 0, nullptr);
        auto m = g.createNode("m", 0, p);
        auto c = g.createNode("c", 0, m);
        EXPECT_EQ(c->getLayer(), 2);
        g.removeNode(m);
        // c should now be at layer 1 (directly below p)
        EXPECT_EQ(c->getLayer(), 1);
    }

    TEST_F(ConnectionManagementTest, RemoveNode_GlobalInvariantsAfterRemoval) {
        auto a = g.createNode("a", 0, nullptr);
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
        auto c = g.createNode("c", 0, nullptr);
        EXPECT_NO_THROW(g.removeConnection(nullptr, c));
    }

    TEST_F(ConnectionManagementTest, RemoveConnection_NullChildIgnored) {
        auto p = g.createNode("p", 0, nullptr);
        EXPECT_NO_THROW(g.removeConnection(p, nullptr));
    }

    TEST_F(ConnectionManagementTest, RemoveConnection_DirectEdgeRemoved) {
        auto p = g.createNode("p", 0, nullptr);
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
        auto p = g.createNode("p", 0, nullptr);
        auto q = g.createNode("q", 0, nullptr);
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
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        int edges_before = static_cast<int>(g.getAllHyperedges().size());
        EXPECT_NO_THROW(g.removeConnection(a, b));
        EXPECT_EQ(static_cast<int>(g.getAllHyperedges().size()), edges_before);
    }

    // =============================================================================
    // 10. fuseNodes
    // =============================================================================

    TEST_F(ConnectionManagementTest, FuseNodes_NullArgIgnored) {
        auto n = g.createNode("n", 0, nullptr);
        EXPECT_NO_THROW(g.fuseNodes(nullptr, n, "x"));
        EXPECT_NO_THROW(g.fuseNodes(n, nullptr, "x"));
    }

    TEST_F(ConnectionManagementTest, FuseNodes_SameNodeThrows) {
        auto n = g.createNode("n", 0, nullptr);
        EXPECT_THROW(g.fuseNodes(n, n, "x"), std::invalid_argument);
    }

    TEST_F(ConnectionManagementTest, FuseNodes_CycleFusionThrows) {
        // p -> c; fusing p and c would create a self-loop
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_THROW(g.fuseNodes(p, c, "fused"), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, FuseNodes_Node2RemovedFromGraph) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        g.fuseNodes(a, b, "ab");
        EXPECT_FALSE(nodeInAllNodes(g, b));
        EXPECT_FALSE(layerContainsNode(g, 0, b));
    }

    TEST_F(ConnectionManagementTest, FuseNodes_NameUpdated) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        g.fuseNodes(a, b, "fused");
        EXPECT_EQ(a->getName(), "fused");
    }

    TEST_F(ConnectionManagementTest, FuseNodes_ConnectionsMerged) {
        // r->a, s->b; fuse a and b; fused node should have both r and s as parents
        auto r = g.createNode("r", 0, nullptr);
        auto s = g.createNode("s", 0, nullptr);
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
        auto r = g.createNode("r", 0, nullptr);
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
        auto r = g.createNode("r", 0, nullptr);
        auto root = g.createNode("root", 0, nullptr);
        auto mid = g.createNode("mid", 0, root);
        auto a = g.createNode("a", 0, r);     // layer 1
        auto b = g.createNode("b", 0, mid);   // layer 2
        g.fuseNodes(a, b, "ab");
        EXPECT_EQ(a->getLayer(), 2);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, FuseNodes_HyperedgesUpdated) {
        // edge p->b; fuse a and b; edge should now target a instead of b
        auto p = g.createNode("p", 0, nullptr);
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, p);
        g.fuseNodes(a, b, "ab");
        bool b_in_any_edge = false;
        for (const auto& e : g.getAllHyperedges())
            if (edgeHasSource(e, b) || edgeHasTarget(e, b)) { b_in_any_edge = true; break; }
        EXPECT_FALSE(b_in_any_edge) << "b (erased node) should not appear in any edge";
    }

    TEST_F(ConnectionManagementTest, FuseNodes_GlobalInvariantsAfterFusion) {
        auto r1 = g.createNode("r1", 0, nullptr);
        auto r2 = g.createNode("r2", 0, nullptr);
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
        auto r1 = g.createNode("r1", 0, nullptr);
        auto r2 = g.createNode("r2", 0, nullptr);
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
        auto r = g.createNode("r", 0, nullptr);
        auto a = g.createNode("a", 0, r);
        auto b = g.createNode("b", 0, r);
        auto c = g.createNode("c", 0, a);
        g.fuseNodes(a, b, "ab");
        g.removeNode(c);
        EXPECT_FALSE(nodeInAllNodes(g, c));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(ConnectionManagementTest, Mixed_LargeDAG_NoOrphanDummies) {
        auto r = g.createNode("r", 0, nullptr);
        std::vector<NodePtr> chain{ r };
        for (int i = 1; i <= 4; i++)
            chain.push_back(g.createNode("c" + std::to_string(i), 0, chain.back()));
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, chain[4]);
        g.addConnection(chain[2], g.createNode("side", 0, nullptr));
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
    // 12. INTENSIVE — complex topology and extreme cases
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
        auto z0 = g.createNode("z0", 0, nullptr);
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
        auto a0 = g.createNode("a0", 0, nullptr);
        auto a1 = g.createNode("a1", 0, a0);
        auto a2 = g.createNode("a2", 0, a1);
        auto a3 = g.createNode("a3", 0, a2);
        auto b0 = g.createNode("b0", 0, nullptr);
        auto b1 = g.createNode("b1", 0, b0);
        auto b2 = g.createNode("b2", 0, b1);
        auto b3 = g.createNode("b3", 0, b2);
        auto sink = g.createNode("sink", 0, nullptr);
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
        NodePtr prev = g.createNode("n0", 0, nullptr);
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
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        auto c = g.createNode("c", 0, nullptr);

        g.addConnection(a, b);   // b->1
        g.addConnection(b, c);   // c->2

        auto d = g.createNode("d", 0, nullptr);
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
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto n4 = g.createNode("n4", 0, n3);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n4);  // long edge, gap = 4

        auto deep_root = g.createNode("dr", 0, nullptr);
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
        auto r0 = g.createNode("r0", 0, nullptr);
        auto r1 = g.createNode("r1", 0, r0);
        auto r2 = g.createNode("r2", 0, r1);
        auto chain_root = g.createNode("cr", 0, nullptr);
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
        auto p = g.createNode("p", 0, nullptr);
        auto q = g.createNode("q", 0, nullptr);
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
        auto p = g.createNode("p", 0, nullptr);
        auto t1 = g.createNode("t1", 0, p);   // layer 1

        auto chain_root = g.createNode("cr", 0, nullptr);
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
        auto p = g.createNode("p", 0, nullptr);
        auto t1 = g.createNode("t1", 0, p);
        auto t2 = g.createNode("t2", 0, nullptr);
        auto q = g.createNode("q", 0, nullptr);

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
        NodePtr prev = g.createNode("n0", 0, nullptr);
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
        auto p1 = g.createNode("p1", 0, nullptr);
        auto p2 = g.createNode("p2", 0, nullptr);
        auto p3 = g.createNode("p3", 0, nullptr);
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
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr);
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
        auto root = g.createNode("root", 0, nullptr);
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
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr);
        auto r3 = g.createNode("r3", 0, nullptr);
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
        auto p1 = g.createNode("p1", 0, nullptr);
        auto p2 = g.createNode("p2", 0, nullptr);
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
        auto r = g.createNode("r", 0, nullptr);
        auto a = g.createNode("a", 0, r);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);

        // b and c are in a direct parent-child relationship -> fusing would create cycle
        EXPECT_THROW(g.fuseNodes(b, c, "bc"), std::logic_error);
    }

    TEST_F(ConnectionManagementTest, Stress_FuseNodes_TwoNodesWithDescendants_LayerCorrect) {
        // r1->a->leaf1, r2->b->leaf2; fuse a and b -> merged node below max(r1,r2)
        auto r1 = g.createNode("r1", 0, nullptr);
        auto r2 = g.createNode("r2", 0, nullptr);
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
        auto r = g.createNode("r", 0, nullptr);
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
        auto top = g.createNode("top", 0, nullptr);
        auto left = g.createNode("left", 0, top);
        auto right = g.createNode("right", 0, top);
        auto bottom = g.createNode("bottom", 0, left);
        g.addConnection(right, bottom);

        // left and right have a common parent (top) and common child (bottom)
        g.fuseNodes(left, right, "mid");
        EXPECT_EQ(g.getAllHyperedges().size(), 2u);
   
        // Instead: add an unrelated pair and fuse them
        auto x = g.createNode("x", 0, nullptr);
        auto y = g.createNode("y", 0, nullptr);
        g.fuseNodes(x, y, "xy");
        g.addConnection(g.getAllNodes().back(), bottom);  // connect fused to bottom

        g.removeNode(bottom);

        EXPECT_FALSE(nodeInAllNodes(g, bottom));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    TEST_F(ConnectionManagementTest, Stress_EndToEnd_AddSourceTargetRemoveNode) {
        // Build a large hyperedge, then remove a source and a target via removeNode
        auto s1 = g.createNode("s1", 0, nullptr);
        auto s2 = g.createNode("s2", 0, nullptr);
        auto s3 = g.createNode("s3", 0, nullptr);
        auto t1 = g.createNode("t1", 0, s1);

        auto edge = findEdgeWithSourceAndTarget(g, s1, t1);
        ASSERT_NE(edge, nullptr);
        g.addSourceToEdge(edge, s2);
        g.addSourceToEdge(edge, s3);

        auto t2 = g.createNode("t2", 0, nullptr);
        auto t3 = g.createNode("t3", 0, nullptr);
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
        auto ra = g.createNode("ra", 0, nullptr);
        auto ma = g.createNode("ma", 0, ra);
        auto la = g.createNode("la", 0, ma);

        auto rb = g.createNode("rb", 0, nullptr);
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
        auto r = g.createNode("r", 0, nullptr);
        std::vector<NodePtr> chain{ r };
        for (int i = 1; i <= 6; i++)
            chain.push_back(g.createNode("n" + std::to_string(i), 0, chain.back()));

        // Add extra parents to several chain nodes
        auto x = g.createNode("x", 0, nullptr);
        auto y = g.createNode("y", 0, nullptr);
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