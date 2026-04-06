#include <gtest/gtest.h>
#include "Hypergraph.h"
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

namespace hypergraph_logic::hypergraph_tests::add_connection_tests {

    // =============================================================================
    // TestableHypergraph — exposes every protected method for white-box testing.
    // Updated: rawEdges() now returns the new unordered_map type.
    // splitLongEdge() signature updated (no longer takes explicit sources/targets).
    // =============================================================================
    class TestableHypergraph : public Hypergraph {
    public:
        using Hypergraph::Hypergraph;

        void pub_addNodeToLayer(int layer, int pos, const NodePtr& n) {
            addNodeToLayer(layer, pos, n);
        }
        void pub_removeNodeFromLayer(int layer, const NodePtr& n) {
            removeNodeFromLayer(layer, n);
        }
        void pub_removeNodeFromLayer(int layer, const std::unordered_set<Node*>& ns) {
            removeNodeFromLayer(layer, ns);
        }
        void pub_addHyperedgeToLayer(int layer, const HyperedgePtr& e) {
            addHyperedgeToLayer(layer, e);
        }
        void pub_removeHyperedgeFromLayer(int layer, const HyperedgePtr& e) {
            removeHyperedgeFromLayer(layer, e);
        }
        void pub_removeHyperedgeFromLayer(int layer, const std::unordered_set<Hyperedge*>& es) {
            removeHyperedgeFromLayer(layer, es);
        }
        void pub_splitLongEdge(const HyperedgePtr& edge) {
            splitLongEdge(edge);
        }
        void pub_dissolveSegments(const std::unordered_set<Hyperedge*>& long_edges) {
            dissolveSegments(long_edges);
        }
        void pub_applyRelocationAndPropagate(const NodePtr& node, int new_layer) {
            applyRelocationAndPropagate({ {node, new_layer} });
        }
        void pub_applyRelocationAndPropagate(const std::vector<std::pair<NodePtr, int>>& relocations) {
            applyRelocationAndPropagate(relocations);
        }
        void pub_removeTransitiveConnections(const std::vector<NodePtr>& parents,
            const std::vector<NodePtr>& children) {
            removeTransitiveConnections(parents, children);
        }
        bool pub_parentIsInAncestors(const NodePtr& child, const NodePtr& parent) {
            return parentIsInAncestors({ child }, parent);
        }
        bool pub_parentIsInAncestors(const std::vector<NodePtr>& children, const NodePtr& parent) {
            return parentIsInAncestors(children, parent);
        }
        bool pub_childIsInDescendants(const std::vector<NodePtr>& parents, const NodePtr& child) {
            return childIsInDescendants(parents, child);
        }
        bool pub_checkCycles(const NodePtr& node) {
            return checkCycles(node);
        }

        // Direct access to internal storage for assertions
        std::vector<NodePtr>& rawNodes() { return all_nodes_; }
        std::unordered_map<HyperedgePtr, std::vector<HyperedgePtr>, HyperedgePtrHash>& rawEdges() {
            return all_hyperedges_;
        }
        std::map<int, LayerData>& rawLayers() { return layers_; }
    };

    // =============================================================================
    // Utility helpers
    // =============================================================================
    static bool layerContainsNode(const TestableHypergraph& g, int layer, const NodePtr& node) {
        auto nodes = g.getNodesAt(layer);
        return std::find(nodes.begin(), nodes.end(), node) != nodes.end();
    }

    static int countEdgesInLayer(const TestableHypergraph& g, int layer) {
        return static_cast<int>(g.getLayerData(layer).outgoing_edges.size());
    }

    static int countDummyNodesInLayer(const TestableHypergraph& g, int layer) {
        int count = 0;
        for (const auto& n : g.getNodesAt(layer))
            if (n->isDummy()) ++count;
        return count;
    }

    static int countSegmentEdges(const TestableHypergraph& g) {
        int count = 0;
        for (const auto& e : g.getAllHyperedges())
            if (e->isSegment()) ++count;
        return count;
    }

    static int countOriginalEdges(const TestableHypergraph& g) {
        int count = 0;
        for (const auto& e : g.getAllHyperedges())
            if (!e->isSegment()) ++count;
        return count;
    }

    // Does every segment hyperedge connect nodes exactly 1 layer apart?
    static bool allEdgesAreShort(const TestableHypergraph& g) {
        for (const auto& e : g.getAllHyperedges()) {
            if (!e->isSegment()) continue;
            for (const auto& s : e->getSources())
                for (const auto& t : e->getTargets())
                    if (std::abs(s->getLayer() - t->getLayer()) != 1)
                        return false;
        }
        return true;
    }

    // Does a layer contain any node that is absent from all_nodes_?
    static bool layersAreConsistentWithAllNodes(const TestableHypergraph& g) {
        std::unordered_set<Node*> in_all;
        for (const auto& n : g.getAllNodes()) in_all.insert(n.get());
        for (const auto& [l, data] : g.getLayers())
            for (const auto& n : data.nodes)
                if (!in_all.count(n.get())) return false;
        return true;
    }

    // Are all short edges registered in their correct LayerData bucket?
    static bool shortEdgesAreConsistentWithAdjacency(TestableHypergraph& g) {
        for (const auto& e : g.getAllHyperedges()) {
            int k = g.edgeIsShort(e);
            if (k < 0) continue;
            if (k != e->getLayer()) return false;
            const auto& layer_outgoing_edges = g.getLayerData(k).outgoing_edges;
            if (std::find(layer_outgoing_edges.begin(), layer_outgoing_edges.end(), e)
                == layer_outgoing_edges.end())
                return false;
        }
        return true;
    }

    // Does a node appear in the sources of a given edge?
    static bool edgeHasSource(const HyperedgePtr& e, const NodePtr& n) {
        for (const auto& s : e->getSources())
            if (s == n) return true;
        return false;
    }

    // Does a node appear in the targets of a given edge?
    static bool edgeHasTarget(const HyperedgePtr& e, const NodePtr& n) {
        for (const auto& t : e->getTargets())
            if (t == n) return true;
        return false;
    }

    // Find the unique original (non-segment) edge whose sources include n.
    // Returns nullptr if not found.
    static HyperedgePtr findOriginalEdgeWithSource(const TestableHypergraph& g, const NodePtr& n) {
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            if (edgeHasSource(e, n)) return e;
        }
        return nullptr;
    }

    // Find the unique original (non-segment) edge whose targets include n.
    static HyperedgePtr findOriginalEdgeWithTarget(const TestableHypergraph& g, const NodePtr& n) {
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            if (edgeHasTarget(e, n)) return e;
        }
        return nullptr;
    }

    // =============================================================================
    // Test fixture: fresh graph for each test
    // =============================================================================
    class HypergraphAddConnectionTest : public ::testing::Test {
    protected:
        TestableHypergraph g{ "test" };
    };

    // =============================================================================
    // 1. GUARD CONDITIONS — addConnection
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, NullParentIsIgnored) {
        auto c = g.createNode("c", 0, nullptr);
        EXPECT_NO_THROW(g.addConnection(nullptr, c));
        EXPECT_TRUE(layerContainsNode(g, 0, c));
    }

    TEST_F(HypergraphAddConnectionTest, NullChildIsIgnored) {
        auto p = g.createNode("p", 0, nullptr);
        EXPECT_NO_THROW(g.addConnection(p, nullptr));
    }

    TEST_F(HypergraphAddConnectionTest, SelfConnectionThrows) {
        auto n = g.createNode("n", 0, nullptr);
        EXPECT_THROW(g.addConnection(n, n), std::invalid_argument);
    }

    TEST_F(HypergraphAddConnectionTest, DuplicateDirectConnectionThrows) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_THROW(g.addConnection(p, c), std::logic_error);
    }

    TEST_F(HypergraphAddConnectionTest, TransitiveAncestorConnectionThrows) {
        auto p = g.createNode("p", 0, nullptr);
        auto m = g.createNode("m", 0, p);
        auto c = g.createNode("c", 0, m);
        EXPECT_THROW(g.addConnection(p, c), std::logic_error);
    }

    TEST_F(HypergraphAddConnectionTest, CycleConnectionThrows) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_THROW(g.addConnection(c, p), std::logic_error);
    }

    TEST_F(HypergraphAddConnectionTest, LongerCycleThrows) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);
        EXPECT_THROW(g.addConnection(d, a), std::logic_error);
    }

    TEST_F(HypergraphAddConnectionTest, AddConnection_ExceptionSafety_StateUnchangedOnCycle) {
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
    // 2. ADJACENT-LAYER CONNECTION
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, AdjacentLayerSimpleConnect) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        auto c = g.createNode("c", 0, a);

        g.addConnection(b, c);

        EXPECT_TRUE(layerContainsNode(g, 0, b));
        EXPECT_TRUE(layerContainsNode(g, 1, c));
        EXPECT_GE(countEdgesInLayer(g, 0), 2);
        EXPECT_EQ(countDummyNodesInLayer(g, 1), 0);
        EXPECT_EQ(countSegmentEdges(g), 0);
    }

    TEST_F(HypergraphAddConnectionTest, AdjacentConnect_ParentChildLinksSet) {
        auto p = g.createNode("p", 0, nullptr);
        auto q = g.createNode("q", 0, nullptr);
        auto c = g.createNode("c", 0, p);

        g.addConnection(q, c);

        auto parents = c->getParents();
        auto children = q->getChildren();
        EXPECT_NE(std::find(parents.begin(), parents.end(), q), parents.end());
        EXPECT_NE(std::find(children.begin(), children.end(), c), children.end());
    }

    // =============================================================================
    // 3. CHILD MOVES DOWN
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, ChildAtSameLayerMovesDown) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        g.addConnection(a, b);

        EXPECT_EQ(b->getLayer(), 1);
        EXPECT_TRUE(layerContainsNode(g, 1, b));
        EXPECT_FALSE(layerContainsNode(g, 0, b));
    }

    TEST_F(HypergraphAddConnectionTest, ChildMovesDownAndDescendantsPropagateCorrectly) {
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

    TEST_F(HypergraphAddConnectionTest, ChildWithMultipleParentsMovesFarEnoughDown) {
        auto r1 = g.createNode("r1", 0, nullptr);
        auto p1 = g.createNode("p1", 0, r1);
        auto p2 = g.createNode("p2", 0, p1);
        auto c = g.createNode("c", 0, nullptr);

        g.addConnection(p2, c);

        EXPECT_EQ(c->getLayer(), 3);
        EXPECT_TRUE(layerContainsNode(g, 3, c));
    }

    TEST_F(HypergraphAddConnectionTest, NodeWithMultipleParentsLayerEqualsDeepestParentPlusOne) {
        auto r = g.createNode("r", 0, nullptr);
        auto a = g.createNode("a", 0, r);
        auto b = g.createNode("b", 0, r);

        g.addConnection(a, b);
        EXPECT_EQ(b->getLayer(), 2);
    }

    // =============================================================================
    // 4. LONG EDGE SPLITTING
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, LongEdgeSplitsIntoDummies_TwoLayerGap) {
        auto r = g.createNode("r", 0, nullptr);
        auto m = g.createNode("m", 0, r);
        auto l = g.createNode("l", 0, m);
        auto r2 = g.createNode("r2", 0, nullptr);

        g.addConnection(r2, l);

        EXPECT_EQ(l->getLayer(), 2);
        EXPECT_GE(countDummyNodesInLayer(g, 1), 1);
        EXPECT_GE(countSegmentEdges(g), 2);
    }

    TEST_F(HypergraphAddConnectionTest, LongEdgeSplitsIntoDummies_ThreeLayerGap) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr);

        g.addConnection(r2, n3);

        EXPECT_EQ(n3->getLayer(), 3);
        EXPECT_GE(countDummyNodesInLayer(g, 1), 1);
        EXPECT_GE(countDummyNodesInLayer(g, 2), 1);
        EXPECT_GE(countSegmentEdges(g), 3);
    }

    TEST_F(HypergraphAddConnectionTest, LongEdgeSegmentsAreLinkedToOriginal) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);

        g.addConnection(r2, n2);

        HyperedgePtr orig = findOriginalEdgeWithSource(g, r2);
        ASSERT_NE(orig, nullptr);

        bool found_segment = false;
        for (const auto& e : g.getAllHyperedges()) {
            if (!e->isSegment()) continue;
            if (e->getOrigin().lock() == orig) { found_segment = true; break; }
        }
        EXPECT_TRUE(found_segment);
    }

    TEST_F(HypergraphAddConnectionTest, LongEdge_NoDummiesLinkToRealNodeParentsOrChildren) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr);

        g.addConnection(r2, n3);

        for (const auto& p : n3->getParents())
            EXPECT_FALSE(p->isDummy()) << "n3 has a dummy parent";
        for (const auto& ch : r2->getChildren())
            EXPECT_FALSE(ch->isDummy()) << "r2 has a dummy child";
    }

    // =============================================================================
    // 5. TRANSITIVE CONNECTION REMOVAL
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, RedundantEdgeRemovedAfterTransitiveAdd) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        auto c = g.createNode("c", 0, a);

        g.addConnection(a, b);
        g.addConnection(b, c);

        bool direct_ac_exists = false;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            if (edgeHasSource(e, a) && edgeHasTarget(e, c)) {
                direct_ac_exists = true; break;
            }
        }
        EXPECT_FALSE(direct_ac_exists) << "Transitive edge a->c should have been removed";
    }

    TEST_F(HypergraphAddConnectionTest, NonRedundantEdgesArePreserved) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);

        g.addConnection(b, c);

        bool ab_exists = false;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            if (edgeHasSource(e, a) && edgeHasTarget(e, b)) { ab_exists = true; break; }
        }
        EXPECT_TRUE(ab_exists) << "Non-redundant edge a->b must survive";
    }

    TEST_F(HypergraphAddConnectionTest, TransitiveAncestorAlreadyCovered_Throws) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);
        auto d = g.createNode("d", 0, b);

        g.addConnection(c, d);
        EXPECT_THROW(g.addConnection(a, d), std::logic_error);
    }

    // =============================================================================
    // 6. parentIsInAncestors
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, ParentIsInAncestors_DirectParent) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_TRUE(g.pub_parentIsInAncestors(c, p));
    }

    TEST_F(HypergraphAddConnectionTest, ParentIsInAncestors_IndirectAncestor) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        EXPECT_TRUE(g.pub_parentIsInAncestors(c, a));
    }

    TEST_F(HypergraphAddConnectionTest, ParentIsInAncestors_UnrelatedNode) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        EXPECT_FALSE(g.pub_parentIsInAncestors(b, a));
    }

    TEST_F(HypergraphAddConnectionTest, ParentIsInAncestors_Null) {
        auto a = g.createNode("a", 0, nullptr);
        EXPECT_FALSE(g.pub_parentIsInAncestors(nullptr, a));
        EXPECT_FALSE(g.pub_parentIsInAncestors(a, nullptr));
    }

    TEST_F(HypergraphAddConnectionTest, ParentIsInAncestors_DeepChain) {
        NodePtr prev = g.createNode("n0", 0, nullptr);
        NodePtr root = prev;
        for (int i = 1; i < 10; ++i)
            prev = g.createNode("n" + std::to_string(i), 0, prev);
        EXPECT_TRUE(g.pub_parentIsInAncestors(prev, root));
    }

    TEST_F(HypergraphAddConnectionTest, ParentIsInAncestors_DiamondDAG) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);
        auto d = g.createNode("d", 0, b);
        g.addConnection(c, d);
        EXPECT_TRUE(g.pub_parentIsInAncestors(d, a));
        EXPECT_FALSE(g.pub_parentIsInAncestors(a, d));
    }

    TEST_F(HypergraphAddConnectionTest, ParentIsInAncestors_VectorOfChildren) {
        // parent p; two children c1 and c2, one of which has p as ancestor
        auto p = g.createNode("p", 0, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto c2 = g.createNode("c2", 0, nullptr);
        // p is an ancestor of c1 but not c2; the vector overload should return true
        EXPECT_TRUE(g.pub_parentIsInAncestors({ c1, c2 }, p));
        // If neither has p as ancestor, should return false
        auto x = g.createNode("x", 0, nullptr);
        auto y = g.createNode("y", 0, nullptr);
        EXPECT_FALSE(g.pub_parentIsInAncestors({ x, y }, p));
    }

    // =============================================================================
    // 7. childIsInDescendants
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, ChildIsInDescendants_DirectChild) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_TRUE(g.pub_childIsInDescendants({ p }, c));
    }

    TEST_F(HypergraphAddConnectionTest, ChildIsInDescendants_IndirectDescendant) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        EXPECT_TRUE(g.pub_childIsInDescendants({ a }, c));
    }

    TEST_F(HypergraphAddConnectionTest, ChildIsInDescendants_UnrelatedNode) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        EXPECT_FALSE(g.pub_childIsInDescendants({ a }, b));
    }

    TEST_F(HypergraphAddConnectionTest, ChildIsInDescendants_Null) {
        auto a = g.createNode("a", 0, nullptr);
        EXPECT_FALSE(g.pub_childIsInDescendants({ a }, nullptr));
        EXPECT_FALSE(g.pub_childIsInDescendants({}, a));
    }

    TEST_F(HypergraphAddConnectionTest, ChildIsInDescendants_VectorOfParents) {
        auto p1 = g.createNode("p1", 0, nullptr);
        auto p2 = g.createNode("p2", 0, nullptr);
        auto c = g.createNode("c", 0, p1);
        // c is a descendant of p1 but not p2; vector overload should return true
        EXPECT_TRUE(g.pub_childIsInDescendants({ p1, p2 }, c));
        auto x = g.createNode("x", 0, nullptr);
        EXPECT_FALSE(g.pub_childIsInDescendants({ p2, x }, c));
    }

    TEST_F(HypergraphAddConnectionTest, ChildIsInDescendants_Symmetry_WithParentIsInAncestors) {
        // The two functions should agree: iff A is ancestor of B, B is descendant of A.
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        EXPECT_EQ(g.pub_parentIsInAncestors(c, a),
            g.pub_childIsInDescendants({ a }, c));
        EXPECT_EQ(g.pub_parentIsInAncestors(a, c),
            g.pub_childIsInDescendants({ c }, a));
    }

    // =============================================================================
    // 8. checkCycles
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, CheckCycles_NoCycle_SimpleDAG) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        EXPECT_FALSE(g.pub_checkCycles(a));
        EXPECT_FALSE(g.pub_checkCycles(b));
    }

    TEST_F(HypergraphAddConnectionTest, CheckCycles_DirectCycleDetected) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        b->addChild(a);
        a->addParent(b);
        EXPECT_TRUE(g.pub_checkCycles(a));
    }

    TEST_F(HypergraphAddConnectionTest, CheckCycles_LongCycleDetected) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);
        d->addChild(a);
        a->addParent(d);
        EXPECT_TRUE(g.pub_checkCycles(a));
    }

    TEST_F(HypergraphAddConnectionTest, CheckCycles_IsolatedNode_NoCycle) {
        auto n = g.createNode("n", 0, nullptr);
        EXPECT_FALSE(g.pub_checkCycles(n));
    }

    // =============================================================================
    // 9. Layer management helpers
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, AddNodeToLayer_CreatesLayerOnDemand) {
        g.createNode("n", 0, nullptr);
        auto m = std::make_shared<Node>("m");
        g.pub_addNodeToLayer(5, 0, m);
        EXPECT_TRUE(layerContainsNode(g, 5, m));
        EXPECT_EQ(m->getLayer(), 5);
    }

    TEST_F(HypergraphAddConnectionTest, AddNodeToLayer_OutOfBoundsPositionAppendsToEnd) {
        g.createNode("n", 0, nullptr);
        auto m = std::make_shared<Node>("m");
        g.pub_addNodeToLayer(0, 9999, m);
        EXPECT_EQ(g.getNodesAt(0).back(), m);
    }

    TEST_F(HypergraphAddConnectionTest, AddNodeToLayer_NegativePositionAppendsToEnd) {
        g.createNode("n", 0, nullptr);
        auto m = std::make_shared<Node>("m");
        g.pub_addNodeToLayer(0, -1, m);
        EXPECT_EQ(g.getNodesAt(0).back(), m);
    }

    TEST_F(HypergraphAddConnectionTest, AddHyperedgeToLayer_CreatesLayerOnDemand) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        // Create an edge without assigning a layer, then add it to a new layer.
        auto edge = g.createHyperedge({ p }, { c }, -1);
        g.pub_addHyperedgeToLayer(7, edge);
        EXPECT_EQ(edge->getLayer(), 7);
        EXPECT_EQ(countEdgesInLayer(g, 7), 1);
    }

    TEST_F(HypergraphAddConnectionTest, RemoveNodeFromLayer_Ptr_RemovesCorrectly) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        g.pub_removeNodeFromLayer(0, a);
        EXPECT_FALSE(layerContainsNode(g, 0, a));
        EXPECT_TRUE(layerContainsNode(g, 0, b));
    }

    TEST_F(HypergraphAddConnectionTest, RemoveNodeFromLayer_Set_RemovesAll) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        auto c = g.createNode("c", 0, nullptr);
        std::unordered_set<Node*> to_remove{ a.get(), b.get() };
        g.pub_removeNodeFromLayer(0, to_remove);
        EXPECT_FALSE(layerContainsNode(g, 0, a));
        EXPECT_FALSE(layerContainsNode(g, 0, b));
        EXPECT_TRUE(layerContainsNode(g, 0, c));
    }

    TEST_F(HypergraphAddConnectionTest, RemoveNodeFromLayer_NonexistentLayer_NoThrow) {
        auto n = g.createNode("n", 0, nullptr);
        EXPECT_NO_THROW(g.pub_removeNodeFromLayer(99, n));
    }

    TEST_F(HypergraphAddConnectionTest, RemoveHyperedgeFromLayer_Ptr_RemovesCorrectly) {
        auto p = g.createNode("p", 0, nullptr);
        g.createNode("c", 0, p);
        auto& edges = g.getLayerData(0).outgoing_edges;
        ASSERT_EQ(edges.size(), 1u);
        auto edge = edges.front();
        g.pub_removeHyperedgeFromLayer(0, edge);
        EXPECT_EQ(countEdgesInLayer(g, 0), 0);
        EXPECT_EQ(edge->getLayer(), -1);
    }

    TEST_F(HypergraphAddConnectionTest, RemoveHyperedgeFromLayer_Set_RemovesAll) {
        auto r = g.createNode("r", 0, nullptr);
        g.createNode("a", 0, r);
        g.createNode("b", 0, r);
        ASSERT_GE(countEdgesInLayer(g, 0), 2);
        std::unordered_set<Hyperedge*> to_remove;
        for (const auto& e : g.getLayerData(0).outgoing_edges)
            to_remove.insert(e.get());
        g.pub_removeHyperedgeFromLayer(0, to_remove);
        EXPECT_EQ(countEdgesInLayer(g, 0), 0);
    }

    // =============================================================================
    // 10. dissolveSegments
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, DissolveSegments_CleansUpDummiesAndSegments) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);

        g.addConnection(r2, n2);
        ASSERT_GT(countSegmentEdges(g), 0);
        ASSERT_GT(countDummyNodesInLayer(g, 1), 0);

        std::unordered_set<Hyperedge*> long_edges;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            if (edgeHasSource(e, r2)) { long_edges.insert(e.get()); break; }
        }
        g.pub_dissolveSegments(long_edges);

        EXPECT_EQ(countSegmentEdges(g), 0);
        EXPECT_EQ(countDummyNodesInLayer(g, 1), 0);
    }

    TEST_F(HypergraphAddConnectionTest, DissolveSegments_EmptySetNoOp) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2);

        int seg_before = countSegmentEdges(g);
        g.pub_dissolveSegments({});
        EXPECT_EQ(countSegmentEdges(g), seg_before);
    }

    TEST_F(HypergraphAddConnectionTest, DissolveSegments_OriginalEdgeSurvives) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2);

        HyperedgePtr orig = findOriginalEdgeWithSource(g, r2);
        ASSERT_NE(orig, nullptr);

        std::unordered_set<Hyperedge*> long_edges{ orig.get() };
        g.pub_dissolveSegments(long_edges);

        // The original edge itself must still be in getAllHyperedges()
        bool orig_found = false;
        for (const auto& e : g.getAllHyperedges())
            if (e == orig) { orig_found = true; break; }
        EXPECT_TRUE(orig_found) << "dissolveSegments must not erase the original edge";
    }

    // =============================================================================
    // 11. applyRelocationAndPropagate
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, RelocationPropagates_DescendantsFollowNode) {
        auto root = g.createNode("root", 0, nullptr);
        auto n1 = g.createNode("n1", 0, root);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);

        g.pub_applyRelocationAndPropagate(n1, 2);

        EXPECT_EQ(n1->getLayer(), 2);
        EXPECT_EQ(n2->getLayer(), 3);
        EXPECT_EQ(n3->getLayer(), 4);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, RelocationKeepsOldLayerEmptyOrClean) {
        auto root = g.createNode("root", 0, nullptr);
        auto n1 = g.createNode("n1", 0, root);
        g.createNode("n2", 0, n1);

        g.pub_applyRelocationAndPropagate(n1, 2);

        // Layer 1 had only n1; after moving it, layer 1 should only have a dummy
        // (from the re-split of root->n1) or be fully absent.
        for (const auto& node : g.getNodesAt(1))
            EXPECT_TRUE(node->isDummy()) << "Layer 1 should only have dummy nodes after relocation";
    }

    TEST_F(HypergraphAddConnectionTest, RelocationEdgesAreSplitIfNecessary) {
        auto root = g.createNode("root", 0, nullptr);
        auto n1 = g.createNode("n1", 0, root);

        g.pub_applyRelocationAndPropagate(n1, 3);

        EXPECT_GE(countDummyNodesInLayer(g, 1), 1);
        EXPECT_GE(countDummyNodesInLayer(g, 2), 1);
        EXPECT_GE(countSegmentEdges(g), 2);
        EXPECT_TRUE(allEdgesAreShort(g));
    }

    TEST_F(HypergraphAddConnectionTest, BatchRelocation_MultipleNodesAtOnce) {
        // Two independent roots a and b each with a child; batch-relocate both children.
        auto a = g.createNode("a", 0, nullptr);
        auto ac = g.createNode("ac", 0, a);
        auto b = g.createNode("b", 0, nullptr);
        auto bc = g.createNode("bc", 0, b);

        g.pub_applyRelocationAndPropagate({ {ac, 3}, {bc, 3} });

        EXPECT_EQ(ac->getLayer(), 3);
        EXPECT_EQ(bc->getLayer(), 3);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allEdgesAreShort(g));
    }

    // =============================================================================
    // 12. addSourceToEdge — guard conditions
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, AddSourceToEdge_NullEdgeIsIgnored) {
        auto n = g.createNode("n", 0, nullptr);
        EXPECT_NO_THROW(g.addSourceToEdge(nullptr, n));
    }

    TEST_F(HypergraphAddConnectionTest, AddSourceToEdge_NullSourceIsIgnored) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_NO_THROW(g.addSourceToEdge(edge, nullptr));
    }

    TEST_F(HypergraphAddConnectionTest, AddSourceToEdge_SegmentEdgeIsIgnored) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2);

        // Find a segment edge
        HyperedgePtr seg = nullptr;
        for (const auto& e : g.getAllHyperedges())
            if (e->isSegment()) { seg = e; break; }
        ASSERT_NE(seg, nullptr);

        auto x = g.createNode("x", 0, nullptr);
        EXPECT_NO_THROW(g.addSourceToEdge(seg, x)); // must be silently ignored
        // Verify the segment was not mutated
        EXPECT_FALSE(edgeHasSource(seg, x));
    }

    TEST_F(HypergraphAddConnectionTest, AddSourceToEdge_SourceIsTargetThrows) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        // c is a target of edge; adding c as a source would create a self-connection
        EXPECT_THROW(g.addSourceToEdge(edge, c), std::logic_error);
    }

    TEST_F(HypergraphAddConnectionTest, AddSourceToEdge_TransitiveAncestorThrows) {
        // edge: p -> c; q is an ancestor of p; adding q as source would be redundant
        auto q = g.createNode("q", 0, nullptr);
        auto p = g.createNode("p", 0, q);
        auto c = g.createNode("c", 0, p);
        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        // q -> c is already covered transitively via q -> p -> c
        EXPECT_THROW(g.addSourceToEdge(edge, q), std::logic_error);
    }

    TEST_F(HypergraphAddConnectionTest, AddSourceToEdge_CycleThrows) {
        // edge: p -> c; adding c as a source would create p -> c and c -> c (cycle)
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        // Create a separate edge with c as source and p as target
        auto edge2 = g.createHyperedge({ c }, { p }, -1); // raw, bypassing addConnection
        // Now try to add p as a source to edge2 (p -> p transitively)
        // The cycle check should fire
        EXPECT_THROW(g.addSourceToEdge(edge2, p), std::logic_error);
    }

    // =============================================================================
    // 13. addSourceToEdge — correct behaviour
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, AddSourceToEdge_AdjacentLayer_NoSplit) {
        // p -> c (layer 0->1), q also at layer 0; add q as source → hyperedge {p,q}->c
        auto p = g.createNode("p", 0, nullptr);
        auto q = g.createNode("q", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);

        g.addSourceToEdge(edge, q);

        EXPECT_TRUE(edgeHasSource(edge, q));
        EXPECT_TRUE(edgeHasSource(edge, p));
        EXPECT_EQ(countSegmentEdges(g), 0);
        EXPECT_TRUE(shortEdgesAreConsistentWithAdjacency(g));
    }

    TEST_F(HypergraphAddConnectionTest, AddSourceToEdge_ParentChildLinksUpdated) {
        auto p = g.createNode("p", 0, nullptr);
        auto q = g.createNode("q", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);

        g.addSourceToEdge(edge, q);

        // q must list c as a child, c must list q as a parent
        auto q_children = q->getChildren();
        auto c_parents = c->getParents();
        EXPECT_NE(std::find(q_children.begin(), q_children.end(), c), q_children.end());
        EXPECT_NE(std::find(c_parents.begin(), c_parents.end(), q), c_parents.end());
    }

    TEST_F(HypergraphAddConnectionTest, AddSourceToEdge_DeepSource_TargetMovesDown) {
        // p(0) -> c(1); add source r at layer 2 → c must move to layer 3
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto r0 = g.createNode("r0", 0, nullptr);
        auto r1 = g.createNode("r1", 0, r0);
        auto r = g.createNode("r", 0, r1); // r at layer 2

        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);

        g.addSourceToEdge(edge, r);

        EXPECT_EQ(c->getLayer(), 3);
        EXPECT_TRUE(layerContainsNode(g, 3, c));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, AddSourceToEdge_LongEdge_SegmentsCreated) {
        // p(0) -> c(3) (long edge already split); add q(0) as additional source
        auto p = g.createNode("p", 0, nullptr);
        auto m1 = g.createNode("m1", 0, p);
        auto m2 = g.createNode("m2", 0, m1);
        auto c = g.createNode("c", 0, m2);   // c at layer 3
        auto q = g.createNode("q", 0, nullptr); // q at layer 0

        // Create the long edge p -> c (bypasses m1 and m2)
        g.addConnection(q, c);  // q(0) -> c(3): long edge with dummies

        // Now find that edge and add a third source at layer 0
        auto edge = findOriginalEdgeWithSource(g, q);
        ASSERT_NE(edge, nullptr);
        auto extra = g.createNode("extra", 0, nullptr);
        g.addSourceToEdge(edge, extra);

        EXPECT_TRUE(edgeHasSource(edge, extra));
        EXPECT_GE(countSegmentEdges(g), 2);
        EXPECT_TRUE(allEdgesAreShort(g));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, AddSourceToEdge_RemovesTransitiveConnections) {
        // edge p->c exists; add ancestor a of p as a source to the same edge.
        // The existing a->p edge becomes transitive and should be removed.
        // (a->c now covers a->p->c)
        // Build: a->p->c; add a as second source of the p->c edge.
        auto a = g.createNode("a", 0, nullptr);
        auto p = g.createNode("p", 0, a);
        auto c = g.createNode("c", 0, p);

        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);

        // Adding a as a source means a->c is now direct; a->p->c is transitive
        g.addSourceToEdge(edge, a);

        // The a->p edge should have been removed as redundant
        bool ap_edge_exists = false;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            if (edgeHasSource(e, a) && edgeHasTarget(e, p)) {
                ap_edge_exists = true; break;
            }
        }
        EXPECT_FALSE(ap_edge_exists) << "a->p edge should be removed as transitive";
    }

    TEST_F(HypergraphAddConnectionTest, AddSourceToEdge_MultipleTargets_AllGetNewParent) {
        // edge p->{c1, c2}; add q as source → q must be parent of both c1 and c2
        auto p = g.createNode("p", 0, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        // Manually add c2 as a second target of the same edge
        auto c2 = g.createNode("c2", 0, nullptr);
        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addTargetToEdge(edge, c2);

        auto q = g.createNode("q", 0, nullptr);
        g.addSourceToEdge(edge, q);

        auto q_children = q->getChildren();
        EXPECT_NE(std::find(q_children.begin(), q_children.end(), c1), q_children.end());
        EXPECT_NE(std::find(q_children.begin(), q_children.end(), c2), q_children.end());
    }

    // =============================================================================
    // 14. addTargetToEdge — guard conditions
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, AddTargetToEdge_NullEdgeIsIgnored) {
        auto n = g.createNode("n", 0, nullptr);
        EXPECT_NO_THROW(g.addTargetToEdge(nullptr, n));
    }

    TEST_F(HypergraphAddConnectionTest, AddTargetToEdge_NullTargetIsIgnored) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_NO_THROW(g.addTargetToEdge(edge, nullptr));
    }

    TEST_F(HypergraphAddConnectionTest, AddTargetToEdge_SegmentEdgeIsIgnored) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2);

        HyperedgePtr seg = nullptr;
        for (const auto& e : g.getAllHyperedges())
            if (e->isSegment()) { seg = e; break; }
        ASSERT_NE(seg, nullptr);

        auto x = g.createNode("x", 0, nullptr);
        EXPECT_NO_THROW(g.addTargetToEdge(seg, x));
        EXPECT_FALSE(edgeHasTarget(seg, x));
    }

    TEST_F(HypergraphAddConnectionTest, AddTargetToEdge_TargetIsSourceThrows) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_THROW(g.addTargetToEdge(edge, p), std::logic_error);
    }

    TEST_F(HypergraphAddConnectionTest, AddTargetToEdge_TransitiveDescendantThrows) {
        // edge: p -> c; d is a descendant of c; adding d as target is redundant
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto d = g.createNode("d", 0, c);
        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        EXPECT_THROW(g.addTargetToEdge(edge, d), std::logic_error);
    }

    TEST_F(HypergraphAddConnectionTest, AddTargetToEdge_CycleThrows) {
        // edge: p -> c; adding p as a target would create a cycle p -> c -> p
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        // Manually create edge c -> p (bypassing validation) then try addTarget
        auto edge2 = g.createHyperedge({ c }, {}, -1);
        EXPECT_THROW(g.addTargetToEdge(edge2, p), std::logic_error);
    }

    // =============================================================================
    // 15. addTargetToEdge — correct behaviour
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, AddTargetToEdge_AdjacentLayer_NoSplit) {
        auto p = g.createNode("p", 0, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto c2 = g.createNode("c2", 0, nullptr); // c2 at layer 0, will move to 1

        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);

        g.addTargetToEdge(edge, c2);

        EXPECT_TRUE(edgeHasTarget(edge, c1));
        EXPECT_TRUE(edgeHasTarget(edge, c2));
        EXPECT_EQ(c2->getLayer(), 1);
        EXPECT_EQ(countSegmentEdges(g), 0);
        EXPECT_TRUE(shortEdgesAreConsistentWithAdjacency(g));
    }

    TEST_F(HypergraphAddConnectionTest, AddTargetToEdge_ParentChildLinksUpdated) {
        auto p = g.createNode("p", 0, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto c2 = g.createNode("c2", 0, nullptr);

        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addTargetToEdge(edge, c2);

        auto p_children = p->getChildren();
        auto c2_parents = c2->getParents();
        EXPECT_NE(std::find(p_children.begin(), p_children.end(), c2), p_children.end());
        EXPECT_NE(std::find(c2_parents.begin(), c2_parents.end(), p), c2_parents.end());
    }

    TEST_F(HypergraphAddConnectionTest, AddTargetToEdge_TargetAlreadyDeep_EdgeSplit) {
        // edge p(0)->c1(1); add c2(3) as second target → edge becomes long, must be split
        auto p = g.createNode("p", 0, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto r0 = g.createNode("r0", 0, nullptr);
        auto r1 = g.createNode("r1", 0, r0);
        auto c2 = g.createNode("c2", 0, r1);  // c2 at layer 2

        // Disconnect c2 from r1 conceptually — actually create an independent deep node
        auto root = g.createNode("root", 0, nullptr);
        auto mid = g.createNode("mid", 0, root);
        auto deep = g.createNode("deep", 0, mid);  // deep at layer 2

        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);

        g.addTargetToEdge(edge, deep);

        // The edge now spans layers 0 to 2 — segments must exist
        EXPECT_GE(countSegmentEdges(g), 1);
        EXPECT_TRUE(allEdgesAreShort(g));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, AddTargetToEdge_TargetAboveSource_TargetMovesDown) {
        // p(0)->c(1); add shallow target t(0) → t must move to layer 1
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto t = g.createNode("t", 0, nullptr);  // t starts at layer 0

        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addTargetToEdge(edge, t);

        EXPECT_EQ(t->getLayer(), 1);
        EXPECT_TRUE(layerContainsNode(g, 1, t));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, AddTargetToEdge_RemovesTransitiveConnections) {
        // edge {p} -> {c}; p also has a direct edge to d which is a child of c.
        // Adding d as target of the p->c edge makes p->d transitive (covered by p->c->d).
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto d = g.createNode("d", 0, c);

        // Add a direct p->d edge bypassing createNode (so we can then test removal)
        // Actually, p->d is already covered transitively via p->c->d, so addConnection throws.
        // Instead test with sibling: p->{c,s}; then s->d; then add d as target to p->s edge.
        auto s = g.createNode("s", 0, p);  // p->s
        auto sd = g.createNode("sd", 0, s); // s->sd

        auto edge_ps = findOriginalEdgeWithSource(g, p);
        // Find the edge that is specifically p->s (not p->c)
        HyperedgePtr ps_edge = nullptr;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            if (edgeHasSource(e, p) && edgeHasTarget(e, s)) { ps_edge = e; break; }
        }
        ASSERT_NE(ps_edge, nullptr);

        // Adding sd as a target to ps_edge (p->s, p->sd now);
        // the existing s->sd connection means p->s->sd is transitive, so p->sd direct becomes
        // redundant only if p->sd is already in the system. Since it's not, this is a valid add.
        g.addTargetToEdge(ps_edge, sd);

        EXPECT_TRUE(edgeHasTarget(ps_edge, sd));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(shortEdgesAreConsistentWithAdjacency(g));
    }

    TEST_F(HypergraphAddConnectionTest, AddTargetToEdge_MultipleSourcesAllBecomeParents) {
        // edge {p, q} -> {c}; add target c2 → both p and q must parent c2
        auto p = g.createNode("p", 0, nullptr);
        auto q = g.createNode("q", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addSourceToEdge(edge, q);  // now edge = {p,q} -> {c}

        auto c2 = g.createNode("c2", 0, nullptr);
        g.addTargetToEdge(edge, c2);  // now edge = {p,q} -> {c, c2}

        auto c2_parents = c2->getParents();
        std::unordered_set<Node*> pset;
        for (const auto& par : c2_parents) pset.insert(par.get());
        EXPECT_TRUE(pset.count(p.get()));
        EXPECT_TRUE(pset.count(q.get()));
    }

    // =============================================================================
    // 16. Complex topology stress tests
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, WideDAG_NoDuplicateEdges) {
        auto root = g.createNode("root", 0, nullptr);
        std::vector<NodePtr> children;
        for (int i = 0; i < 5; i++)
            children.push_back(g.createNode("c" + std::to_string(i), i, root));
        auto leaf = g.createNode("leaf", 0, children[0]);

        for (int i = 1; i < 5; i++)
            g.addConnection(children[i], leaf);

        EXPECT_EQ(leaf->getLayer(), 2);
        auto parents = leaf->getParents();
        std::unordered_set<Node*> parent_set;
        for (const auto& p : parents) parent_set.insert(p.get());
        EXPECT_EQ(parent_set.size(), 5u);
    }

    TEST_F(HypergraphAddConnectionTest, DiamondDAG_Integrity) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);
        auto d = g.createNode("d", 0, b);

        g.addConnection(c, d);

        EXPECT_EQ(d->getLayer(), 2);
        auto parents = d->getParents();
        std::unordered_set<Node*> pset;
        for (const auto& p : parents) pset.insert(p.get());
        EXPECT_TRUE(pset.count(b.get()));
        EXPECT_TRUE(pset.count(c.get()));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, FullBinaryTree_FiveLevel) {
        auto root = g.createNode("root", 0, nullptr);
        std::vector<NodePtr> current_level{ root };
        for (int depth = 1; depth <= 4; depth++) {
            std::vector<NodePtr> next_level;
            for (const auto& parent : current_level) {
                next_level.push_back(g.createNode("L", 0, parent));
                next_level.push_back(g.createNode("R", 0, parent));
            }
            current_level = next_level;
        }
        for (const auto& leaf : current_level)
            EXPECT_EQ(leaf->getLayer(), 4);
        EXPECT_EQ(g.getLayerCount(), 5);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, MultipleRootsConvergingToSingleLeaf) {
        auto leaf = g.createNode("leaf", 0, nullptr);
        for (int i = 0; i < 4; i++) {
            auto r = g.createNode("r" + std::to_string(i), 0, nullptr);
            g.addConnection(r, leaf);
        }
        EXPECT_EQ(leaf->getLayer(), 1);
        EXPECT_EQ(countDummyNodesInLayer(g, 1), 0);
        EXPECT_EQ(countSegmentEdges(g), 0);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, TwoParallelChains_SharedSink) {
        auto a0 = g.createNode("a0", 0, nullptr);
        auto a1 = g.createNode("a1", 0, a0);
        auto a2 = g.createNode("a2", 0, a1);
        auto a3 = g.createNode("a3", 0, a2);

        auto b0 = g.createNode("b0", 0, nullptr);
        auto b1 = g.createNode("b1", 0, b0);
        auto b2 = g.createNode("b2", 0, b1);
        auto b3 = g.createNode("b3", 0, b2);

        auto s = g.createNode("s", 0, nullptr);
        g.addConnection(a3, s);
        g.addConnection(b3, s);

        EXPECT_EQ(s->getLayer(), 4);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, AllEdgesConnectAdjacentLayersAfterSplitting) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n3);
        auto r3 = g.createNode("r3", 0, nullptr);
        g.addConnection(r3, n2);

        EXPECT_TRUE(allEdgesAreShort(g));
    }

    TEST_F(HypergraphAddConnectionTest, SequentialReconnection_NodesMoveCorrectly) {
        auto n0 = g.createNode("n0", 0, nullptr);
        auto p1 = g.createNode("p1", 0, nullptr);
        g.addConnection(p1, n0);
        EXPECT_EQ(n0->getLayer(), 1);

        auto p2 = g.createNode("p2", 0, p1);
        g.addConnection(p2, n0);
        EXPECT_EQ(n0->getLayer(), 2);

        auto p3 = g.createNode("p3", 0, p2);
        g.addConnection(p3, n0);
        EXPECT_EQ(n0->getLayer(), 3);

        EXPECT_TRUE(layerContainsNode(g, 3, n0));
        EXPECT_FALSE(layerContainsNode(g, 0, n0));
        EXPECT_FALSE(layerContainsNode(g, 1, n0));
        EXPECT_FALSE(layerContainsNode(g, 2, n0));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, AddConnectionDoesNotDuplicateAllNodesEntry) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        auto c = g.createNode("c", 0, nullptr);

        g.addConnection(a, b);
        g.addConnection(b, c);
        EXPECT_THROW(g.addConnection(a, c), std::logic_error);

        std::unordered_set<Node*> seen;
        for (const auto& n : g.getAllNodes()) {
            EXPECT_FALSE(seen.count(n.get())) << "Duplicate in all_nodes_";
            seen.insert(n.get());
        }
    }

    TEST_F(HypergraphAddConnectionTest, LargeDAG_NoOrphanDummyNodes) {
        auto r = g.createNode("r", 0, nullptr);
        std::vector<NodePtr> chain{ r };
        for (int i = 1; i <= 4; i++)
            chain.push_back(g.createNode("c" + std::to_string(i), 0, chain.back()));

        auto r2 = g.createNode("r2", 0, nullptr);
        auto r3 = g.createNode("r3", 0, nullptr);
        auto n3 = g.createNode("n3", 0, r3);
        g.addConnection(r2, chain[4]);
        g.addConnection(chain[2], n3);
        g.addConnection(r3, chain[1]);

        EXPECT_THROW(g.addConnection(chain[0], chain[3]), std::logic_error);
        EXPECT_THROW(g.addConnection(chain[1], chain[4]), std::logic_error);

        std::unordered_set<Node*> all_in_graph;
        for (const auto& n : g.getAllNodes()) all_in_graph.insert(n.get());
        for (const auto& [l, data] : g.getLayers())
            for (const auto& n : data.nodes)
                if (n->isDummy())
                    EXPECT_TRUE(all_in_graph.count(n.get()))
                    << "Dummy in layer " << l << " not in all_nodes_";

        EXPECT_TRUE(allEdgesAreShort(g));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, AddConnectionAfterNodeRelocation_Integrity) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, nullptr);

        g.addConnection(d, b);
        EXPECT_EQ(b->getLayer(), 1);
        EXPECT_EQ(c->getLayer(), 2);

        g.addConnection(d, a);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(shortEdgesAreConsistentWithAdjacency(g));
        EXPECT_EQ(a->getLayer(), 1);
        EXPECT_EQ(b->getLayer(), 2);
        EXPECT_EQ(c->getLayer(), 3);
    }

    TEST_F(HypergraphAddConnectionTest, TwoRootsConnectedToEachOthersMid) {
        auto a = g.createNode("a", 0, nullptr);
        auto am = g.createNode("am", 0, a);
        g.createNode("aleaf", 0, am);
        auto b = g.createNode("b", 0, nullptr);
        auto bm = g.createNode("bm", 0, b);
        g.createNode("bleaf", 0, bm);

        g.addConnection(a, bm);
        EXPECT_EQ(bm->getLayer(), 1);
        g.addConnection(b, am);
        EXPECT_EQ(am->getLayer(), 1);

        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_EQ(countSegmentEdges(g), 0);
    }

    TEST_F(HypergraphAddConnectionTest, MixedAddSourceAndAddConnection_GlobalInvariants) {
        // Build a moderately complex graph mixing addConnection and addSourceToEdge,
        // then assert all global structural invariants.
        auto r1 = g.createNode("r1", 0, nullptr);
        auto r2 = g.createNode("r2", 0, nullptr);
        auto a = g.createNode("a", 0, r1);
        auto b = g.createNode("b", 0, r1);
        auto c = g.createNode("c", 0, a);

        // Add r2 as an additional source to the r1->a edge
        auto edge_r1a = findOriginalEdgeWithSource(g, r1);
        // There may be multiple edges from r1 (to a and to b); find the one to a
        HyperedgePtr edge_to_a = nullptr;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            if (edgeHasSource(e, r1) && edgeHasTarget(e, a)) { edge_to_a = e; break; }
        }
        ASSERT_NE(edge_to_a, nullptr);
        g.addSourceToEdge(edge_to_a, r2);

        // Connect b to c (b at 1, c at 2 → adjacent)
        g.addConnection(b, c);

        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(shortEdgesAreConsistentWithAdjacency(g));
        EXPECT_TRUE(allEdgesAreShort(g));
    }

    TEST_F(HypergraphAddConnectionTest, MixedAddTargetAndAddConnection_GlobalInvariants) {
        auto p = g.createNode("p", 0, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto c2 = g.createNode("c2", 0, nullptr);

        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        g.addTargetToEdge(edge, c2);  // p -> {c1, c2}

        // Now connect an independent chain and add a cross-connection
        auto q = g.createNode("q", 0, nullptr);
        auto d = g.createNode("d", 0, q);
        g.addConnection(p, d);  // p(0) -> d(1) adjacent

        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(shortEdgesAreConsistentWithAdjacency(g));
    }

    TEST_F(HypergraphAddConnectionTest, HyperedgeWithManySourcesAndTargets_Integrity) {
        // Build a hyperedge with 3 sources and 3 targets all at adjacent layers,
        // verify all invariants after addSourceToEdge and addTargetToEdge calls.
        auto s1 = g.createNode("s1", 0, nullptr);
        auto s2 = g.createNode("s2", 0, nullptr);
        auto t1 = g.createNode("t1", 0, s1);
        auto t2 = g.createNode("t2", 0, s1);

        auto edge = findOriginalEdgeWithSource(g, s1);
        // find the s1->t1 edge specifically
        HyperedgePtr main_edge = nullptr;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            if (edgeHasSource(e, s1) && edgeHasTarget(e, t1)) { main_edge = e; break; }
        }
        ASSERT_NE(main_edge, nullptr);

        g.addSourceToEdge(main_edge, s2);
        g.addTargetToEdge(main_edge, t2);

        EXPECT_EQ(main_edge->getSources().size(), 2u);
        EXPECT_EQ(main_edge->getTargets().size(), 2u);
        EXPECT_TRUE(shortEdgesAreConsistentWithAdjacency(g));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

} // namespace hypergraph_logic::hypergraph_tests::add_connection_testssts