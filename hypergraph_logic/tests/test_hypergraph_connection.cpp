#include <gtest/gtest.h>
#include "Hypergraph.h"
#include <algorithm>
#include <unordered_set>

namespace hypergraph_logic::hypergraph_tests::add_connection_tests {

    // =============================================================================
    // TestableHypergraph — exposes every protected method for white-box testing.
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
        void pub_removeHyperedgeFromLayer(int layer, const HyperedgePtr& e) {
            removeHyperedgeFromLayer(layer, e);
        }
        void pub_removeHyperedgeFromLayer(int layer, const std::unordered_set<Hyperedge*>& es) {
            removeHyperedgeFromLayer(layer, es);
        }
        void pub_splitLongEdge(const HyperedgePtr& edge,
            const std::vector<NodePtr>& sources,
            const std::vector<NodePtr>& targets) {
            splitLongEdge(edge, sources, targets);
        }
        void pub_dissolveSegments(const std::unordered_set<Hyperedge*>& long_edges) {
            dissolveSegments(long_edges);
        }
        void pub_applyRelocationAndPropagate(const NodePtr& node, int new_layer) {
            applyRelocationAndPropagate(node, new_layer);
        }
        void pub_removeTransitiveConnections(const NodePtr& parent, const NodePtr& child) {
            removeTransitiveConnections(parent, child);
        }
        bool pub_parentIsInAncestors(const NodePtr& child, const NodePtr& parent) {
            return parentIsInAncestors(child, parent);
        }
		bool pub_checkCycles(const NodePtr& node) {
			return checkCycles(node);
		}

        std::vector<NodePtr>& rawNodes() { return all_nodes_; }
        std::vector<HyperedgePtr>& rawEdges() { return all_hyperedges_; }
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

    // Does a layer contain any real (non-dummy) node that is absent from all_nodes_?
    static bool layersAreConsistentWithAllNodes(const TestableHypergraph& g) {
        std::unordered_set<Node*> in_all;
        for (const auto& n : g.getAllNodes()) in_all.insert(n.get());
        for (const auto& [l, data] : g.getLayers())
            for (const auto& n : data.nodes)
                if (!in_all.count(n.get())) return false;
        return true;
    }

    // Are all short edges in the layer they correspond to with the ?
    static bool shortEdgesAreConsistentWithAdjacency(TestableHypergraph& g) {
        for (auto& e : g.getAllHyperedges()) {
			int k = g.edgeIsShort(e);
            if (k < 0) continue;
			if (k != e->getLayer()) return false;
            const auto& layer_outgoing_edges = g.getLayerData(k).outgoing_edges;
			if (std::find(layer_outgoing_edges.begin(), layer_outgoing_edges.end(), e) == layer_outgoing_edges.end())
                return false;
        }
        return true;
	}



    // =============================================================================
    // Test fixture: fresh graph for each test
    // =============================================================================
    class HypergraphAddConnectionTest : public ::testing::Test {
    protected:
        TestableHypergraph g{ "test" };
    };

    // =============================================================================
    // 1. GUARD CONDITIONS
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, NullParentIsIgnored) {
        auto c = g.createNode("c", 0, nullptr);
        EXPECT_NO_THROW(g.addConnection(nullptr, c));
        // child must still be at layer 0, unchanged
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
        auto c = g.createNode("c", 0, p);       // already connected via createNode
        EXPECT_THROW(g.addConnection(p, c), std::logic_error);
    }

    TEST_F(HypergraphAddConnectionTest, TransitiveAncestorConnectionThrows) {
        // p -> m -> c already; adding p -> c must be rejected
        auto p = g.createNode("p", 0, nullptr);
        auto m = g.createNode("m", 0, p);
        auto c = g.createNode("c", 0, m);
        EXPECT_THROW(g.addConnection(p, c), std::logic_error);
    }

    TEST_F(HypergraphAddConnectionTest, CycleConnectionThrows) {
        // p -> c; then c -> p would create a cycle
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_THROW(g.addConnection(c, p), std::logic_error);
    }

    TEST_F(HypergraphAddConnectionTest, LongerCycleThrows) {
        // a -> b -> c -> d; then d -> a would create a 4-cycle
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);
        EXPECT_THROW(g.addConnection(d, a), std::logic_error);
    }

    // =============================================================================
    // 2. SIMPLE ADJACENT-LAYER CONNECTION  (parent_layer == child_layer - 1)
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, AdjacentLayerSimpleConnect) {
        // Two independent roots, then connect them
        auto a = g.createNode("a", 0, nullptr); // layer 0
        auto b = g.createNode("b", 0, nullptr); // layer 0
        auto c = g.createNode("c", 0, a);       // layer 1

        // b is at layer 0, c is at layer 1 → adjacent, no splitting needed
        g.addConnection(b, c);

        EXPECT_TRUE(layerContainsNode(g, 0, b));
        EXPECT_TRUE(layerContainsNode(g, 1, c));

        // Exactly one new edge from layer 0 (on top of the one createNode made)
        int edges_L0 = countEdgesInLayer(g, 0);
        EXPECT_GE(edges_L0, 2); // at least the original a->c edge + the new b->c edge

        // No dummy nodes should have been created
        EXPECT_EQ(countDummyNodesInLayer(g, 1), 0);
    }

    TEST_F(HypergraphAddConnectionTest, AdjacentConnect_ParentChildLinksSet) {
        auto p = g.createNode("p", 0, nullptr);
        auto q = g.createNode("q", 0, nullptr);
        auto c = g.createNode("c", 0, p);

        g.addConnection(q, c);

        // c must list q as a parent
        auto parents = c->getParents();
        EXPECT_NE(std::find(parents.begin(), parents.end(), q), parents.end());
        // q must list c as a child
        auto children = q->getChildren();
        EXPECT_NE(std::find(children.begin(), children.end(), c), children.end());
    }

    // =============================================================================
    // 3. CHILD NEEDS TO MOVE DOWN  (parent_layer >= child_layer)
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, ChildAtSameLayerMovesDown) {
        // Two roots at layer 0; connect the first to the second
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        // a=0, b=0; after addConnection(a,b), b must move to layer 1
        g.addConnection(a, b);

        EXPECT_EQ(b->getLayer(), 1);
        EXPECT_TRUE(layerContainsNode(g, 1, b));
        EXPECT_FALSE(layerContainsNode(g, 0, b));
    }

    TEST_F(HypergraphAddConnectionTest, ChildMovesDownAndDescendantsPropagateCorrectly) {
        // Chain at layer 0: a-b-c-d all roots initially, then connect a->b
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        auto c = g.createNode("c", 0, b);  // b->c, c at layer 1
        auto d = g.createNode("d", 0, c);  // c->d, d at layer 2

        // Now connect a->b. b is at layer 0, so b must move to 1, c to 2, d to 3.
        g.addConnection(a, b);

        EXPECT_EQ(b->getLayer(), 1);
        EXPECT_EQ(c->getLayer(), 2);
        EXPECT_EQ(d->getLayer(), 3);

        EXPECT_TRUE(layerContainsNode(g, 1, b));
        EXPECT_TRUE(layerContainsNode(g, 2, c));
        EXPECT_TRUE(layerContainsNode(g, 3, d));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, ChildWithMultipleParentsMovesFarEnoughDown) {
        // p1 at layer 1 (created under root r1), p2 at layer 2 (created under r1->p1)
        // c is an isolated root at layer 0.
        // Connect p2 -> c: c must move to layer 3.
        auto r1 = g.createNode("r1", 0, nullptr);
        auto p1 = g.createNode("p1", 0, r1);      // layer 1
        auto p2 = g.createNode("p2", 0, p1);      // layer 2
        auto c = g.createNode("c", 0, nullptr); // layer 0

        g.addConnection(p2, c);

        EXPECT_EQ(c->getLayer(), 3);
        EXPECT_TRUE(layerContainsNode(g, 3, c));
        EXPECT_FALSE(layerContainsNode(g, 0, c));
    }

    TEST_F(HypergraphAddConnectionTest, NodeWithMultipleParentsLayerEqualsDeepestParentPlusOne) {
        // r -> a (layer 1), r -> b (layer 1)
        // After connecting a -> b, b must be at layer 2
        auto r = g.createNode("r", 0, nullptr);
        auto a = g.createNode("a", 0, r);   // layer 1
        auto b = g.createNode("b", 0, r);   // layer 1 initially

        g.addConnection(a, b);

        EXPECT_EQ(b->getLayer(), 2);
    }

    // =============================================================================
    // 4. SPLIT LONG EDGE (parent_layer < child_layer - 1)
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, LongEdgeSplitsIntoDummies_TwoLayerGap) {
        // Manually build: root r at 0, leaf l at layer 2, then connect r->l.
        // r->m (layer 1), m->l (layer 2) via createNode, then add r->l which skips m.
        auto r = g.createNode("r", 0, nullptr); // layer 0
        auto m = g.createNode("m", 0, r);       // layer 1
        auto l = g.createNode("l", 0, m);       // layer 2

        // Now add a new root r2 at layer 0 and connect it directly to l (gap of 2)
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, l);

        // l stays at layer 2; a dummy must have been inserted at layer 1
        EXPECT_EQ(l->getLayer(), 2);
        EXPECT_GE(countDummyNodesInLayer(g, 1), 1);
        EXPECT_GE(countSegmentEdges(g), 2); // at least 2 segments for the split
    }

    TEST_F(HypergraphAddConnectionTest, LongEdgeSplitsIntoDummies_ThreeLayerGap) {
        // root at 0, leaf at layer 3 (built via chain), then add a second root
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);     // layer 1
        auto n2 = g.createNode("n2", 0, n1);    // layer 2
        auto n3 = g.createNode("n3", 0, n2);    // layer 3

        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n3); // gap of 3

        EXPECT_EQ(n3->getLayer(), 3);
        // Dummies at layers 1 and 2
        EXPECT_GE(countDummyNodesInLayer(g, 1), 1);
        EXPECT_GE(countDummyNodesInLayer(g, 2), 1);
        EXPECT_GE(countSegmentEdges(g), 3);
    }

    TEST_F(HypergraphAddConnectionTest, LongEdgeSegmentsAreLinkedToOriginal) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);

        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2); // r2 at 0, n2 at 2 → gap 2

        // Find the original (non-segment) edge whose source is r2
        HyperedgePtr orig = nullptr;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            for (const auto& s : e->getSources())
                if (s == r2) { orig = e; break; }
            if (orig) break;
        }
        ASSERT_NE(orig, nullptr);

        // Every segment that references this edge must have it as origin
        bool found_segment = false;
        for (const auto& e : g.getAllHyperedges()) {
            if (!e->isSegment()) continue;
            auto o = e->getOrigin().lock();
            if (o == orig) { found_segment = true; break; }
        }
        EXPECT_TRUE(found_segment);
    }

    TEST_F(HypergraphAddConnectionTest, LongEdge_NoDummiesLinkToRealNodeParents) {
        // Real nodes must NOT know about dummy intermediaries
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);

        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n3);

        // n3's parents should be real nodes only (r2 and n2)
        for (const auto& p : n3->getParents())
            EXPECT_FALSE(p->isDummy()) << "n3 has a dummy parent";

        // r2's children should be real nodes only (n3)
        for (const auto& ch : r2->getChildren())
            EXPECT_FALSE(ch->isDummy()) << "r2 has a dummy child";
    }

    // =============================================================================
    // 5. TRANSITIVE CONNECTION REMOVAL
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, RedundantEdgeRemovedAfterTransitiveAdd) {
        // Build: a -> c (direct), a -> b -> c (indirect)
        // Initially a->c exists; after we add a->b and b->c, the direct a->c
        // should be detected as redundant by removeTransitiveConnections.

        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        auto c = g.createNode("c", 0, a);  // a->c at layer 1

        // add b at layer 0 and connect a->b (b moves to layer 1), then b->c (c moves to 2)
        g.addConnection(a, b);  // b now at layer 1
        g.addConnection(b, c);  // c now at layer 2; the old a->c edge (a[0]->c[1]) becomes transitive

        // a->c is now transitively covered by a->b->c; the direct edge should be gone
        bool direct_ac_exists = false;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            bool has_a = false, has_c = false;
            for (const auto& s : e->getSources()) if (s == a) has_a = true;
            for (const auto& t : e->getTargets()) if (t == c) has_c = true;
            if (has_a && has_c) { direct_ac_exists = true; break; }
        }
        EXPECT_FALSE(direct_ac_exists) << "Transitive edge a->c should have been removed";
    }

    TEST_F(HypergraphAddConnectionTest, NonRedundantEdgesArePreserved) {
        // a -> b, a -> c: adding b -> c should not remove a -> b.
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);

        g.addConnection(b, c);

        bool ab_exists = false;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            bool has_a = false, has_b = false;
            for (const auto& s : e->getSources()) if (s == a) has_a = true;
            for (const auto& t : e->getTargets()) if (t == b) has_b = true;
            if (has_a && has_b) { ab_exists = true; break; }
        }
        EXPECT_TRUE(ab_exists) << "Non-redundant edge a->b must survive";
    }

    TEST_F(HypergraphAddConnectionTest, MultipleRedundantEdgesRemovedAtOnce) {
        // Diamond: a->b, a->c, b->d, c->d; then add a->d which is transitively covered twice.
        // After a->d is added, it should not exist as a direct edge (since b->d and c->d cover it).
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);
        auto d = g.createNode("d", 0, b);

        g.addConnection(c, d);
        // a->d would be transitive through a->b->d and a->c->d; this should throw
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
        // Build a chain of 10 nodes and check that the root is in ancestors of the last
        NodePtr prev = g.createNode("n0", 0, nullptr);
        NodePtr root = prev;
        for (int i = 1; i < 10; ++i) {
            prev = g.createNode("n" + std::to_string(i), 0, prev);
        }
        EXPECT_TRUE(g.pub_parentIsInAncestors(prev, root));
    }

    TEST_F(HypergraphAddConnectionTest, ParentIsInAncestors_DiamondDAG) {
        // a -> b, a -> c, b -> d, c -> d
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);
        auto d = g.createNode("d", 0, b);
        g.addConnection(c, d);
        // a is ancestor of d through both b and c
        EXPECT_TRUE(g.pub_parentIsInAncestors(d, a));
        EXPECT_FALSE(g.pub_parentIsInAncestors(a, d));
    }

    // =============================================================================
    // 7. checkCycles
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
        // Manually wire a cycle (bypassing addConnection validation)
        b->addChild(a);
        a->addParent(b);
        EXPECT_TRUE(g.pub_checkCycles(a));
    }

    TEST_F(HypergraphAddConnectionTest, CheckCycles_LongCycleDetected) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);
        // Wire d -> a
        d->addChild(a);
        a->addParent(d);
        EXPECT_TRUE(g.pub_checkCycles(a));
    }

    TEST_F(HypergraphAddConnectionTest, CheckCycles_IsolatedNode_NoCycle) {
        auto n = g.createNode("n", 0, nullptr);
        EXPECT_FALSE(g.pub_checkCycles(n));
    }

    // =============================================================================
    // 8. addNodeToLayer / removeNodeFromLayer / removeHyperedgeFromLayer
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, AddNodeToLayer_CreatesLayerOnDemand) {
        auto n = g.createNode("n", 0, nullptr); // layer 0 exists now
        auto m = std::make_shared<Node>("m");
        g.pub_addNodeToLayer(5, 0, m);
        EXPECT_TRUE(layerContainsNode(g, 5, m));
        EXPECT_EQ(m->getLayer(), 5);
    }

    TEST_F(HypergraphAddConnectionTest, AddNodeToLayer_OutOfBoundsPositionAppendsToEnd) {
        auto n = g.createNode("n", 0, nullptr);
        auto m = std::make_shared<Node>("m");
        // position 9999 is out of bounds; should append
        g.pub_addNodeToLayer(0, 9999, m);
        auto nodes = g.getNodesAt(0);
        EXPECT_EQ(nodes.back(), m);
    }

    TEST_F(HypergraphAddConnectionTest, AddNodeToLayer_NegativePositionAppendsToEnd) {
        auto n = g.createNode("n", 0, nullptr);
        auto m = std::make_shared<Node>("m");
        g.pub_addNodeToLayer(0, -1, m);
        auto nodes = g.getNodesAt(0);
        EXPECT_EQ(nodes.back(), m);
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
        auto c = g.createNode("c", 0, p);
        auto& edges = g.getLayerData(0).outgoing_edges;
        ASSERT_EQ(edges.size(), 1u);
        auto& edge = edges.front();
        g.pub_removeHyperedgeFromLayer(0, edge);
        EXPECT_EQ(countEdgesInLayer(g, 0), 0);
    }

    TEST_F(HypergraphAddConnectionTest, RemoveHyperedgeFromLayer_Set_RemovesAll) {
        auto r = g.createNode("r", 0, nullptr);
        auto a = g.createNode("a", 0, r);
        auto b = g.createNode("b", 0, r);
        // Layer 0 now has 2 outgoing edges
        ASSERT_GE(countEdgesInLayer(g, 0), 2);
        std::unordered_set<Hyperedge*> to_remove;
        for (const auto& e : g.getLayerData(0).outgoing_edges)
            to_remove.insert(e.get());
        g.pub_removeHyperedgeFromLayer(0, to_remove);
        EXPECT_EQ(countEdgesInLayer(g, 0), 0);
    }

    // =============================================================================
    // 9. dissolveSegments
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, DissolveSegments_CleansUpDummies) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);

        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2); // creates a long edge with dummies

        int seg_before = countSegmentEdges(g);
        int dummy_before = countDummyNodesInLayer(g, 1);
        ASSERT_GT(seg_before, 0);
        ASSERT_GT(dummy_before, 0);

        // Now collect all original non-segment edges involving r2
        std::unordered_set<Hyperedge*> long_edges;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            for (const auto& s : e->getSources())
                if (s == r2) { long_edges.insert(e.get()); break; }
        }

        g.pub_dissolveSegments(long_edges);

        // Segments should be gone
        EXPECT_EQ(countSegmentEdges(g), 0);
        // Dummies in layer 1 coming from this long edge should be gone
        // (Note: layer 1 still has n1 as a real node)
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
        EXPECT_EQ(countSegmentEdges(g), seg_before); // unchanged
    }

    // =============================================================================
    // 10. applyRelocationAndPropagate
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, RelocationPropagates_DescendantsFollowNode) {
        // Build: root at 0, chain n1->n2->n3 under it.
        // Manually relocate n1 to layer 2 and check n2 ends at 3, n3 at 4.
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

    TEST_F(HypergraphAddConnectionTest, RelocationKeepsOldLayerEmptyClean) {
        auto root = g.createNode("root", 0, nullptr);
        auto n1 = g.createNode("n1", 0, root);
        auto n2 = g.createNode("n2", 0, n1);

        // After relocation the empty layer 1 must be erased
        g.pub_applyRelocationAndPropagate(n1, 2);

        // Layer 1 should either be absent or empty (depending on implementation)

        EXPECT_TRUE(g.getNodesAt(1)[0]->isDummy()) << "Old layer should have a dummy node";
    }

    TEST_F(HypergraphAddConnectionTest, RelocationEdgesAreSplitIfNecessary) {
        // After moving a node further away from its parents, the connecting edge
        // may become a long edge and must be re-split.
        auto root = g.createNode("root", 0, nullptr);
        auto n1 = g.createNode("n1", 0, root); // root(0) -> n1(1)

        g.pub_applyRelocationAndPropagate(n1, 3); // move n1 to layer 3

        // The edge root->n1 now spans 3 layers; it must have been split with dummies
        EXPECT_GE(countDummyNodesInLayer(g, 1), 1);
        EXPECT_GE(countDummyNodesInLayer(g, 2), 1);
        EXPECT_GE(countSegmentEdges(g), 2);
    }

    // =============================================================================
    // 11. COMPLEX TOPOLOGIES
    // =============================================================================

    TEST_F(HypergraphAddConnectionTest, WideDAG_NoDuplicateEdges) {
        // One root, 5 direct children, then each child connects to a single grandchild
        auto root = g.createNode("root", 0, nullptr);
        std::vector<NodePtr> children;
        for (int i = 0; i < 5; i++)
            children.push_back(g.createNode("c" + std::to_string(i), i, root));
        auto leaf = g.createNode("leaf", 0, children[0]);

        for (int i = 1; i < 5; i++)
            g.addConnection(children[i], leaf);

        // leaf should be at layer 2
        EXPECT_EQ(leaf->getLayer(), 2);

        // Count distinct parents of leaf (should be all 5 children)
        auto parents = leaf->getParents();
        std::unordered_set<Node*> parent_set;
        for (const auto& p : parents) parent_set.insert(p.get());
        EXPECT_EQ(parent_set.size(), 5u);
    }

    TEST_F(HypergraphAddConnectionTest, DiamondDAG_Integrity) {
        // Classic diamond: a -> b, a -> c, b -> d, c -> d
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);
        auto d = g.createNode("d", 0, b);

        g.addConnection(c, d); // c is at layer 1, d at layer 2 → adjacent

        EXPECT_EQ(d->getLayer(), 2);
        EXPECT_TRUE(layerContainsNode(g, 2, d));

        // d must have exactly b and c as parents
        auto parents = d->getParents();
        std::unordered_set<Node*> pset;
        for (const auto& p : parents) pset.insert(p.get());
        EXPECT_TRUE(pset.count(b.get()));
        EXPECT_TRUE(pset.count(c.get()));

        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, FullBinaryTree_FiveLevel) {
        // Build a 5-level perfect binary tree (1+2+4+8+16 = 31 nodes)
        auto root = g.createNode("root", 0, nullptr);
        std::vector<NodePtr> current_level{ root };
        for (int depth = 1; depth <= 4; depth++) {
            std::vector<NodePtr> next_level;
            for (const auto& parent : current_level) {
                next_level.push_back(g.createNode("n" + std::to_string(depth) + "L", 0, parent));
                next_level.push_back(g.createNode("n" + std::to_string(depth) + "R", 0, parent));
            }
            current_level = next_level;
        }
        // All leaves at layer 4
        for (const auto& leaf : current_level)
            EXPECT_EQ(leaf->getLayer(), 4);

        EXPECT_EQ(g.getLayerCount(), 5);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, MultipleRootsConvergingToSingleLeaf) {
        // 4 independent roots all connecting to the same leaf (which starts at layer 0)
        auto leaf = g.createNode("leaf", 0, nullptr);
        std::vector<NodePtr> roots;
        for (int i = 0; i < 4; i++) {
            auto r = g.createNode("r" + std::to_string(i), 0, nullptr);
            roots.push_back(r);
        }

        for (const auto& r : roots)
            g.addConnection(r, leaf);

        // leaf must be at layer 1 (one level below any root)
        EXPECT_EQ(leaf->getLayer(), 1);
        EXPECT_TRUE(layerContainsNode(g, 1, leaf));

        // No dummies should be needed (all roots at layer 0, leaf at layer 1)
        EXPECT_EQ(countDummyNodesInLayer(g, 1), 0);
        EXPECT_EQ(countSegmentEdges(g), 0);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, AddConnectionAfterNodeRelocation_Integrity) {
        // Build: a -> b -> c. Then add d as new root and connect d -> b,
        // which should move b (and c) down.
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, nullptr);

        g.addConnection(d, b); // b now has two parents at layer 0; b moves to layer 1 (already there)

        // a(0) -> b(1) -> c(2), d(0) -> b(1) — no movement needed, b stays at 1
        EXPECT_EQ(b->getLayer(), 1);
        EXPECT_EQ(c->getLayer(), 2);

		// Now connect d -> a, which should move a to layer 1, b to 2, c to 3 and remove the old 
        g.addConnection(d, a);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(shortEdgesAreConsistentWithAdjacency(g));
        EXPECT_EQ(a->getLayer(), 1);
        EXPECT_EQ(b->getLayer(), 2);
        EXPECT_EQ(c->getLayer(), 3);
    }

    TEST_F(HypergraphAddConnectionTest, StarGraphWithDeepCenterNode) {
        // Center node c at layer 0 connected to 5 branches; then
        // connect all leaf nodes to a single sink (which must move deep)
        auto center = g.createNode("center", 0, nullptr);
        std::vector<NodePtr> mids, leaves;
        for (int i = 0; i < 5; i++) {
            auto m = g.createNode("m" + std::to_string(i), 0, center);  // layer 1
            auto l = g.createNode("l" + std::to_string(i), 0, m);       // layer 2
            mids.push_back(m);
            leaves.push_back(l);
        }

        auto sink = g.createNode("sink", 0, nullptr); // starts at layer 0
        for (const auto& l : leaves)
            g.addConnection(l, sink);

        // sink must now be at layer 3
        EXPECT_EQ(sink->getLayer(), 3);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, ZigZagGraph_CrossLayerEdges) {
        // z0(0) -> z1(1) -> z2(2) -> z3(3)
        // Also add z0 -> z2 (skip 2) and z1 -> z3 (skip 2)
        auto z0 = g.createNode("z0", 0, nullptr);
        auto z1 = g.createNode("z1", 0, z0);
        auto z2 = g.createNode("z2", 0, z1);
        auto z3 = g.createNode("z3", 0, z2);

        // Layers must be unchanged for real nodes
        EXPECT_EQ(z0->getLayer(), 0);
        EXPECT_EQ(z1->getLayer(), 1);
        EXPECT_EQ(z2->getLayer(), 2);
        EXPECT_EQ(z3->getLayer(), 3);

        // Dummies required at intermediate layers for both long edges
        EXPECT_EQ(countDummyNodesInLayer(g, 1), 0); // for z0->z2
        EXPECT_EQ(countDummyNodesInLayer(g, 2), 0); // for z1->z3
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, TwoParallelChains_SharedSink) {
        // Chain A: a0 -> a1 -> a2 -> a3
        // Chain B: b0 -> b1 -> b2 -> b3
        // Shared sink s connected to a3 and b3
        auto a0 = g.createNode("a0", 0, nullptr);
        auto a1 = g.createNode("a1", 0, a0);
        auto a2 = g.createNode("a2", 0, a1);
        auto a3 = g.createNode("a3", 0, a2);

        auto b0 = g.createNode("b0", 0, nullptr);
        auto b1 = g.createNode("b1", 0, b0);
        auto b2 = g.createNode("b2", 0, b1);
        auto b3 = g.createNode("b3", 0, b2);

        auto s = g.createNode("s", 0, nullptr); // starts at 0
        g.addConnection(a3, s);
        g.addConnection(b3, s);

        EXPECT_EQ(s->getLayer(), 4);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, LayerIntegrityAfterManyConnections) {
        // Build a random-ish DAG with 15 nodes in sequential layers,
        // then assert invariants hold throughout.
        std::vector<NodePtr> nodes;
        nodes.push_back(g.createNode("n0", 0, nullptr));
        nodes.push_back(g.createNode("n1", 0, nullptr));

        g.addConnection(nodes[0], nodes[1]);
        nodes.push_back(g.createNode("n2", 0, nodes[1]));
        nodes.push_back(g.createNode("n3", 0, nodes[0]));

        g.addConnection(nodes[3], nodes[2]);

        nodes.push_back(g.createNode("n4", 0, nodes[2]));
        nodes.push_back(g.createNode("n5", 0, nodes[3]));

        g.addConnection(nodes[4], nodes[5]);

        // Every real node must appear in exactly one layer
        std::unordered_map<Node*, int> layer_count;
        for (const auto& [l, data] : g.getLayers())
            for (const auto& n : data.nodes)
                layer_count[n.get()]++;

        for (const auto& n : nodes) {
            EXPECT_EQ(layer_count.count(n.get()), 1u) << n->getName() << " not in any layer";
            EXPECT_EQ(layer_count[n.get()], 1) << n->getName() << " in multiple layers";
        }

        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, AllEdgesConnectAdjacentLayersAfterSplitting) {
        // After any sequence of addConnection calls, every edge (segment or not)
        // must connect nodes exactly 1 layer apart.
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);


        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n3);

        auto r3 = g.createNode("r3", 0, nullptr);
        g.addConnection(r3, n2);

        EXPECT_TRUE(allEdgesAreShort(g))
            << "Some edge connects non-adjacent layers after splitting";
    }

    TEST_F(HypergraphAddConnectionTest, SequentialReconnection_NodesMoveCorrectly) {
        // Build n0 standalone, then keep adding parents to push it deeper.
        auto n0 = g.createNode("n0", 0, nullptr); // layer 0
        auto p1 = g.createNode("p1", 0, nullptr); // layer 0

        g.addConnection(p1, n0); // n0 -> layer 1
        EXPECT_EQ(n0->getLayer(), 1);

        auto p2 = g.createNode("p2", 0, p1); // layer 1
        g.addConnection(p2, n0);             // n0 -> layer 2
        EXPECT_EQ(n0->getLayer(), 2);

        auto p3 = g.createNode("p3", 0, p2); // layer 2
        g.addConnection(p3, n0);             // n0 -> layer 3
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

        // Even if the above throws, all_nodes_ must not have duplicates
        std::unordered_set<Node*> seen;
        for (const auto& n : g.getAllNodes()) {
            EXPECT_FALSE(seen.count(n.get())) << "Duplicate in all_nodes_";
            seen.insert(n.get());
        }
    }

    TEST_F(HypergraphAddConnectionTest, LargeDAG_NoOrphanDummyNodes) {
        // After all splitting and dissolving, no dummy node should be unreachable
        // from any real node via segment edges.
        auto r = g.createNode("r", 0, nullptr);
        std::vector<NodePtr> chain{ r };
        for (int i = 1; i <= 4; i++)
            chain.push_back(g.createNode("c" + std::to_string(i), 0, chain.back()));

        auto r2 = g.createNode("r2", 0, nullptr);
		auto r3 = g.createNode("r3", 0, nullptr);
        auto n3 = g.createNode("n3", 0, r3);
		g.addConnection(r2, chain[4]); // creates a long edge with dummies
        g.addConnection(chain[2], n3);
        g.addConnection(r3, chain[1]);

        // Add skip edges
        EXPECT_THROW(g.addConnection(chain[0], chain[3]), std::logic_error);
        EXPECT_THROW(g.addConnection(chain[1], chain[4]), std::logic_error);

        // Every dummy must appear in exactly one layer and in all_nodes_
        std::unordered_set<Node*> all_in_graph;
        for (const auto& n : g.getAllNodes()) all_in_graph.insert(n.get());

        for (const auto& [l, data] : g.getLayers()) {
            for (const auto& n : data.nodes) {
                if (!n->isDummy()) continue;
                EXPECT_TRUE(all_in_graph.count(n.get()))
                    << "Dummy in layer " << l << " not in all_nodes_";
            }
        }
        EXPECT_TRUE(allEdgesAreShort(g));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphAddConnectionTest, AddConnection_ExceptionSafety_StateUnchangedOnCycle) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);

        int nodes_before = static_cast<int>(g.getAllNodes().size());
        int edges_before = static_cast<int>(g.getAllHyperedges().size());
        int layers_before = g.getLayerCount();

        EXPECT_THROW(g.addConnection(b, a), std::logic_error);

        // State must be rolled back
        EXPECT_EQ(static_cast<int>(g.getAllNodes().size()), nodes_before);
        EXPECT_EQ(static_cast<int>(g.getAllHyperedges().size()), edges_before);
        EXPECT_EQ(g.getLayerCount(), layers_before);
    }

    TEST_F(HypergraphAddConnectionTest, TwoRootsConnectedToEachOthersMid) {
        // a -> am -> aleaf
        // b -> bm -> bleaf
        // Cross-connect: a -> bm and b -> am (neither creates a cycle)
        auto a = g.createNode("a", 0, nullptr);
        auto am = g.createNode("am", 0, a);
        auto aleaf = g.createNode("aleaf", 0, am);

        auto b = g.createNode("b", 0, nullptr);
        auto bm = g.createNode("bm", 0, b);
        auto bleaf = g.createNode("bleaf", 0, bm);

        // a -> bm: a at 0, bm at 1 → adjacent, no split
        g.addConnection(a, bm);
        EXPECT_EQ(bm->getLayer(), 1);

        // b -> am: b at 0, am at 1 → adjacent, no split
        g.addConnection(b, am);
        EXPECT_EQ(am->getLayer(), 1);

        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_EQ(countSegmentEdges(g), 0) << "No long edges, no segments expected";
    }

} // namespace hypergraph_logic::hypergraph_tests