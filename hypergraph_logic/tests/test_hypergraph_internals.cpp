// =============================================================================
// test_hypergraph_internals.cpp
//
// Tests for:
//   - Constructor
//   - Node / hyperedge management (createHyperedge, getAllHyperedges, edgeIsShort)
//   - Layer queries (getLayerCount, getLayers, getLayerData, getNodesAt, getAllNodes)
//   - Protected methods exposed via TestableHypergraph:
//       addNodeToLayer, removeNodeFromLayer (ptr + set overloads)
//       addHyperedgeToLayer, removeHyperedgeFromLayer (ptr + set overloads)
//       splitLongEdge, dissolveSegments
//       applyRelocationAndPropagate (single + batch)
//       removeTransitiveConnections
//       parentIsInAncestors, childIsInDescendants
//       checkCycles
//       getAllAncestors, getAllDescendants
//       relocateNodes
// =============================================================================

#include <gtest/gtest.h>
#include "Hypergraph.h"
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

namespace hypergraph_logic::hypergraph_tests::internals {

    // =============================================================================
    // TestableHypergraph
    // =============================================================================
    class TestableHypergraph : public Hypergraph {
    public:
        using Hypergraph::Hypergraph;

        // Layer management
        void pub_addNodeToLayer(int layer, int pos, const NodePtr& n) { addNodeToLayer(layer, pos, n); }
        void pub_removeNodeFromLayer(int layer, const NodePtr& n) { removeNodeFromLayer(layer, n); }
        void pub_removeNodeFromLayer(int layer, const std::unordered_set<Node*>& ns) { removeNodeFromLayer(layer, ns); }
        void pub_addHyperedgeToLayer(int layer, const HyperedgePtr& e) { addHyperedgeToLayer(layer, e); }
        void pub_removeHyperedgeFromLayer(int layer, const HyperedgePtr& e) { removeHyperedgeFromLayer(layer, e); }
        void pub_removeHyperedgeFromLayer(int layer, const std::unordered_set<Hyperedge*>& es) { removeHyperedgeFromLayer(layer, es); }

        // Edge management
        void pub_splitLongEdge(const HyperedgePtr& e) { splitLongEdge(e); }
        void pub_dissolveSegments(const std::unordered_set<Hyperedge*>& es) { dissolveSegments(es); }

        // Relocation
        void pub_applyRelocationAndPropagate(const NodePtr& n, int layer) { applyRelocationAndPropagate({ {n, layer} }); }
        void pub_applyRelocationAndPropagate(const std::vector<std::pair<NodePtr, int>>& r) { applyRelocationAndPropagate(r); }
        bool pub_relocateNodes(const std::vector<NodePtr>& nodes) { return relocateNodes(nodes); }

        // Transitive connections
        void pub_removeTransitiveConnections(const std::vector<NodePtr>& parents, const std::vector<NodePtr>& children) {
            removeTransitiveConnections(parents, children);
        }

        // Traversal / search
        bool pub_parentIsInAncestors(const NodePtr& child, const NodePtr& parent) { return parentIsInAncestors({ child }, parent); }
        bool pub_parentIsInAncestors(const std::vector<NodePtr>& children, const NodePtr& parent) { return parentIsInAncestors(children, parent); }
        bool pub_childIsInDescendants(const std::vector<NodePtr>& parents, const NodePtr& child) { return childIsInDescendants(parents, child); }
        bool pub_checkCycles(const NodePtr& n) { return checkCycles(n); }
        std::unordered_set<Node*> pub_getAllAncestors(const std::vector<NodePtr>& nodes) { return getAllAncestors(nodes); }
        std::unordered_set<Node*> pub_getAllDescendants(const std::vector<NodePtr>& nodes) { return getAllDescendants(nodes); }

        // Raw access for assertions
        std::vector<NodePtr>& rawNodes() { return all_nodes_; }
        std::unordered_map<HyperedgePtr, std::vector<HyperedgePtr>, HyperedgePtrHash>& rawEdges() { return all_hyperedges_; }
        std::map<int, LayerData>& rawLayers() { return layers_; }
    };

    // =============================================================================
    // Helpers
    // =============================================================================
    static bool layerContainsNode(const TestableHypergraph& g, int layer, const NodePtr& n) {
        auto nodes = g.getNodesAt(layer);
        return std::find(nodes.begin(), nodes.end(), n) != nodes.end();
    }

    static int countEdgesInLayer(const TestableHypergraph& g, int layer) {
        return static_cast<int>(g.getLayerData(layer).outgoing_edges.size());
    }

    static int countDummyNodesInLayer(const TestableHypergraph& g, int layer) {
        int c = 0;
        for (const auto& n : g.getNodesAt(layer)) if (n->isDummy()) ++c;
        return c;
    }

    static int countSegmentEdges(const TestableHypergraph& g) {
        int c = 0;
        for (const auto& e : g.getAllHyperedges()) if (e->isSegment()) ++c;
        return c;
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

    static bool edgeHasSource(const HyperedgePtr& e, const NodePtr& n) {
        for (const auto& s : e->getSources()) if (s == n) return true;
        return false;
    }

    static bool edgeHasTarget(const HyperedgePtr& e, const NodePtr& n) {
        for (const auto& t : e->getTargets()) if (t == n) return true;
        return false;
    }

    static HyperedgePtr findOriginalEdgeWithSource(const TestableHypergraph& g, const NodePtr& n) {
        for (const auto& e : g.getAllHyperedges())
            if (!e->isSegment() && edgeHasSource(e, n)) return e;
        return nullptr;
    }

    // =============================================================================
    // Fixture
    // =============================================================================
    class HypergraphInternalsTest : public ::testing::Test {
    protected:
        TestableHypergraph g{ "test" };
    };

    // =============================================================================
    // 1. Constructor
    // =============================================================================

    TEST_F(HypergraphInternalsTest, Constructor_EmptyGraph) {
        EXPECT_EQ(g.getLayerCount(), 0);
        EXPECT_TRUE(g.getAllNodes().empty());
        EXPECT_TRUE(g.getAllHyperedges().empty());
    }

    // =============================================================================
    // 2. Node management — addNodeToLayer / removeNodeFromLayer
    // =============================================================================

    TEST_F(HypergraphInternalsTest, AddNodeToLayer_CreatesLayerOnDemand) {
        auto m = std::make_shared<Node>("m");
        g.pub_addNodeToLayer(5, 0, m);
        EXPECT_TRUE(layerContainsNode(g, 5, m));
        EXPECT_EQ(m->getLayer(), 5);
        EXPECT_EQ(g.getLayerCount(), 1);
    }

    TEST_F(HypergraphInternalsTest, AddNodeToLayer_NullNodeIgnored) {
        EXPECT_NO_THROW(g.pub_addNodeToLayer(0, 0, nullptr));
        EXPECT_EQ(g.getLayerCount(), 0);
    }

    TEST_F(HypergraphInternalsTest, AddNodeToLayer_OutOfBoundsPositionAppendsToEnd) {
        g.createNode("a", 0, nullptr);
        auto m = std::make_shared<Node>("m");
        g.pub_addNodeToLayer(0, 9999, m);
        EXPECT_EQ(g.getNodesAt(0).back(), m);
    }

    TEST_F(HypergraphInternalsTest, AddNodeToLayer_NegativePositionAppendsToEnd) {
        g.createNode("a", 0, nullptr);
        auto m = std::make_shared<Node>("m");
        g.pub_addNodeToLayer(0, -1, m);
        EXPECT_EQ(g.getNodesAt(0).back(), m);
    }

    TEST_F(HypergraphInternalsTest, AddNodeToLayer_DuplicateIgnored) {
        auto n = g.createNode("n", 0, nullptr);
        int count_before = static_cast<int>(g.getNodesAt(0).size());
        g.pub_addNodeToLayer(0, 0, n);  // already there
        EXPECT_EQ(static_cast<int>(g.getNodesAt(0).size()), count_before);
    }

    TEST_F(HypergraphInternalsTest, RemoveNodeFromLayer_Ptr_RemovesCorrectNode) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        g.pub_removeNodeFromLayer(0, a);
        EXPECT_FALSE(layerContainsNode(g, 0, a));
        EXPECT_TRUE(layerContainsNode(g, 0, b));
    }

    TEST_F(HypergraphInternalsTest, RemoveNodeFromLayer_Ptr_NonexistentLayerNoThrow) {
        auto n = g.createNode("n", 0, nullptr);
        EXPECT_NO_THROW(g.pub_removeNodeFromLayer(99, n));
    }

    TEST_F(HypergraphInternalsTest, RemoveNodeFromLayer_Set_RemovesAll) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        auto c = g.createNode("c", 0, nullptr);
        std::unordered_set<Node*> to_remove{ a.get(), b.get() };
        g.pub_removeNodeFromLayer(0, to_remove);
        EXPECT_FALSE(layerContainsNode(g, 0, a));
        EXPECT_FALSE(layerContainsNode(g, 0, b));
        EXPECT_TRUE(layerContainsNode(g, 0, c));
    }

    TEST_F(HypergraphInternalsTest, RemoveNodeFromLayer_Set_EmptySetNoOp) {
        auto n = g.createNode("n", 0, nullptr);
        int count_before = static_cast<int>(g.getNodesAt(0).size());
        g.pub_removeNodeFromLayer(0, std::unordered_set<Node*>{});
        EXPECT_EQ(static_cast<int>(g.getNodesAt(0).size()), count_before);
    }

    // =============================================================================
    // 3. Hyperedge management — addHyperedgeToLayer / removeHyperedgeFromLayer
    // =============================================================================

    TEST_F(HypergraphInternalsTest, AddHyperedgeToLayer_CreatesLayerOnDemand) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = g.createHyperedge({ p }, { c }, -1);
        g.pub_addHyperedgeToLayer(7, edge);
        EXPECT_EQ(edge->getLayer(), 7);
        EXPECT_EQ(countEdgesInLayer(g, 7), 1);
    }

    TEST_F(HypergraphInternalsTest, AddHyperedgeToLayer_DuplicateIgnored) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        int count_before = countEdgesInLayer(g, 0);
        auto edge = g.getLayerData(0).outgoing_edges.front();
        g.pub_addHyperedgeToLayer(0, edge);  // already in layer 0
        EXPECT_EQ(countEdgesInLayer(g, 0), count_before);
    }

    TEST_F(HypergraphInternalsTest, RemoveHyperedgeFromLayer_Ptr_SetsLayerToMinusOne) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = g.getLayerData(0).outgoing_edges.front();
        g.pub_removeHyperedgeFromLayer(0, edge);
        EXPECT_EQ(countEdgesInLayer(g, 0), 0);
        EXPECT_EQ(edge->getLayer(), -1);
    }

    TEST_F(HypergraphInternalsTest, RemoveHyperedgeFromLayer_Set_RemovesAll) {
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

    TEST_F(HypergraphInternalsTest, RemoveHyperedgeFromLayer_NonexistentLayerNoThrow) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = g.getLayerData(0).outgoing_edges.front();
        EXPECT_NO_THROW(g.pub_removeHyperedgeFromLayer(99, edge));
    }

    // =============================================================================
    // 4. createHyperedge / getAllHyperedges / edgeIsShort
    // =============================================================================

    TEST_F(HypergraphInternalsTest, CreateHyperedge_NonSegment_RegisteredAsOriginal) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, nullptr);
        auto edge = g.createHyperedge({ p }, { c }, -1);
        EXPECT_FALSE(edge->isSegment());
        bool found = false;
        for (const auto& e : g.getAllHyperedges()) if (e == edge) { found = true; break; }
        EXPECT_TRUE(found);
    }

    TEST_F(HypergraphInternalsTest, CreateHyperedge_WithLayer_RegisteredInLayerData) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, nullptr);
        auto edge = g.createHyperedge({ p }, { c }, 0);
        EXPECT_EQ(edge->getLayer(), 0);
        auto& edges = g.getLayerData(0).outgoing_edges;
        EXPECT_NE(std::find(edges.begin(), edges.end(), edge), edges.end());
    }

    TEST_F(HypergraphInternalsTest, CreateHyperedge_WiresParentChildLinks) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = std::make_shared<Node>("c");
        g.rawNodes().push_back(c);
        g.pub_addNodeToLayer(1, -1, c);
        g.createHyperedge({ p }, { c }, 0);
        auto p_children = p->getChildren();
        auto c_parents = c->getParents();
        EXPECT_NE(std::find(p_children.begin(), p_children.end(), c), p_children.end());
        EXPECT_NE(std::find(c_parents.begin(), c_parents.end(), p), c_parents.end());
    }

    TEST_F(HypergraphInternalsTest, GetAllHyperedges_IncludesSegments) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2);  // creates segments
        auto all = g.getAllHyperedges();
        bool has_segment = false;
        bool has_original = false;
        for (const auto& e : all) {
            if (e->isSegment()) has_segment = true;
            else has_original = true;
        }
        EXPECT_TRUE(has_segment);
        EXPECT_TRUE(has_original);
    }

    TEST_F(HypergraphInternalsTest, EdgeIsShort_AdjacentLayers_ReturnsSourceLayer) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = g.getLayerData(0).outgoing_edges.front();
        EXPECT_EQ(g.edgeIsShort(edge), 0);
    }

    TEST_F(HypergraphInternalsTest, EdgeIsShort_LongEdge_ReturnsMinusOne) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = std::make_shared<Node>("c");
        g.rawNodes().push_back(c);
        g.pub_addNodeToLayer(2, -1, c);
        auto edge = g.createHyperedge({ p }, { c }, -1);
        EXPECT_EQ(g.edgeIsShort(edge), -1);
    }

    TEST_F(HypergraphInternalsTest, EdgeIsShort_NullEdge_ReturnsMinusOne) {
        EXPECT_EQ(g.edgeIsShort(nullptr), -1);
    }

    // =============================================================================
    // 5. Layer queries
    // =============================================================================

    TEST_F(HypergraphInternalsTest, GetLayerCount_CorrectAfterNodeCreation) {
        EXPECT_EQ(g.getLayerCount(), 0);
        auto p = g.createNode("p", 0, nullptr);
        EXPECT_EQ(g.getLayerCount(), 1);
        auto c = g.createNode("c", 0, p);
        EXPECT_EQ(g.getLayerCount(), 2);
    }

    TEST_F(HypergraphInternalsTest, GetNodesAt_ReturnsCorrectNodes) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        auto c = g.createNode("c", 0, a);
        auto layer0 = g.getNodesAt(0);
        EXPECT_NE(std::find(layer0.begin(), layer0.end(), a), layer0.end());
        EXPECT_NE(std::find(layer0.begin(), layer0.end(), b), layer0.end());
        auto layer1 = g.getNodesAt(1);
        EXPECT_NE(std::find(layer1.begin(), layer1.end(), c), layer1.end());
    }

    TEST_F(HypergraphInternalsTest, GetNodesAt_NonexistentLayer_ReturnsEmpty) {
        EXPECT_TRUE(g.getNodesAt(99).empty());
    }

    TEST_F(HypergraphInternalsTest, GetAllNodes_ReturnsEveryNode) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto all = g.getAllNodes();
        EXPECT_EQ(all.size(), 2u);
        EXPECT_NE(std::find(all.begin(), all.end(), a), all.end());
        EXPECT_NE(std::find(all.begin(), all.end(), b), all.end());
    }

    TEST_F(HypergraphInternalsTest, GetLayerData_NonexistentLayer_ReturnsEmptyStatic) {
        const auto& data = g.getLayerData(999);
        EXPECT_TRUE(data.nodes.empty());
        EXPECT_TRUE(data.outgoing_edges.empty());
    }

    TEST_F(HypergraphInternalsTest, GetLayers_ReturnsAllLayers) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        const auto& layers = g.getLayers();
        EXPECT_EQ(layers.size(), 2u);
        EXPECT_NE(layers.find(0), layers.end());
        EXPECT_NE(layers.find(1), layers.end());
    }

    // =============================================================================
    // 6. splitLongEdge
    // =============================================================================

    TEST_F(HypergraphInternalsTest, SplitLongEdge_TwoLayerGap_CreatesDummies) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        auto edge = g.createHyperedge({ r2 }, { n2 }, -1);
        r2->addChild(n2); n2->addParent(r2);
        g.pub_splitLongEdge(edge);
        EXPECT_GE(countDummyNodesInLayer(g, 1), 1);
        EXPECT_GE(countSegmentEdges(g), 2);
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    TEST_F(HypergraphInternalsTest, SplitLongEdge_AlreadySplit_ReSplitsCleanly) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2);  // first split
        int seg_before = countSegmentEdges(g);
        auto edge = findOriginalEdgeWithSource(g, r2);
        ASSERT_NE(edge, nullptr);
        g.pub_splitLongEdge(edge);  // re-split
        // Segment count should be the same (dissolved and rebuilt)
        EXPECT_EQ(countSegmentEdges(g), seg_before);
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    TEST_F(HypergraphInternalsTest, SplitLongEdge_SegmentsLinkedToOriginal) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        auto edge = g.createHyperedge({ r2 }, { n2 }, -1);
        r2->addChild(n2); n2->addParent(r2);
        g.pub_splitLongEdge(edge);
        bool found = false;
        for (const auto& e : g.getAllHyperedges())
            if (e->isSegment() && e->getOrigin().lock() == edge) { found = true; break; }
        EXPECT_TRUE(found);
    }

    TEST_F(HypergraphInternalsTest, SplitLongEdge_ShortEdge_NoChange) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = g.getLayerData(0).outgoing_edges.front();
        int segs_before = countSegmentEdges(g);
        g.pub_splitLongEdge(edge);  // should do nothing
        EXPECT_EQ(countSegmentEdges(g), segs_before);
    }

    // =============================================================================
    // 7. dissolveSegments
    // =============================================================================

    TEST_F(HypergraphInternalsTest, DissolveSegments_CleansUpDummiesAndSegments) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2);
        ASSERT_GT(countSegmentEdges(g), 0);
        ASSERT_GT(countDummyNodesInLayer(g, 1), 0);
        std::unordered_set<Hyperedge*> long_edges;
        for (const auto& e : g.getAllHyperedges())
            if (!e->isSegment() && edgeHasSource(e, r2)) { long_edges.insert(e.get()); break; }
        g.pub_dissolveSegments(long_edges);
        EXPECT_EQ(countSegmentEdges(g), 0);
        EXPECT_EQ(countDummyNodesInLayer(g, 1), 0);
    }

    TEST_F(HypergraphInternalsTest, DissolveSegments_EmptySetNoOp) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2);
        int seg_before = countSegmentEdges(g);
        g.pub_dissolveSegments({});
        EXPECT_EQ(countSegmentEdges(g), seg_before);
    }

    TEST_F(HypergraphInternalsTest, DissolveSegments_OriginalEdgeSurvives) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n2);
        auto orig = findOriginalEdgeWithSource(g, r2);
        ASSERT_NE(orig, nullptr);
        g.pub_dissolveSegments({ orig.get() });
        bool found = false;
        for (const auto& e : g.getAllHyperedges()) if (e == orig) { found = true; break; }
        EXPECT_TRUE(found) << "dissolveSegments must not erase the original edge";
    }

    // =============================================================================
    // 8. applyRelocationAndPropagate
    // =============================================================================

    TEST_F(HypergraphInternalsTest, RelocationPropagates_DescendantsFollowNode) {
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

    TEST_F(HypergraphInternalsTest, RelocationSplitsEdgeIfNecessary) {
        auto root = g.createNode("root", 0, nullptr);
        auto n1 = g.createNode("n1", 0, root);
        g.pub_applyRelocationAndPropagate(n1, 3);
        EXPECT_GE(countDummyNodesInLayer(g, 1), 1);
        EXPECT_GE(countDummyNodesInLayer(g, 2), 1);
        EXPECT_GE(countSegmentEdges(g), 2);
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    TEST_F(HypergraphInternalsTest, RelocationCleansUpEmptyLayers) {
        auto root = g.createNode("root", 0, nullptr);
        auto n1 = g.createNode("n1", 0, root);
        g.pub_applyRelocationAndPropagate(n1, 2);
        // Layer 1 should only contain dummies (from re-split) or be absent
        for (const auto& n : g.getNodesAt(1))
            EXPECT_TRUE(n->isDummy()) << "Layer 1 should only contain dummies after relocation";
    }

    TEST_F(HypergraphInternalsTest, BatchRelocation_MultipleNodesAtOnce) {
        auto a = g.createNode("a", 0, nullptr);
        auto ac = g.createNode("ac", 0, a);
        auto b = g.createNode("b", 0, nullptr);
        auto bc = g.createNode("bc", 0, b);
        g.pub_applyRelocationAndPropagate({ {ac, 3}, {bc, 3} });
        EXPECT_EQ(ac->getLayer(), 3);
        EXPECT_EQ(bc->getLayer(), 3);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    // =============================================================================
    // 9. relocateNodes
    // =============================================================================

    TEST_F(HypergraphInternalsTest, RelocateNodes_EmptyVector_ReturnsFalse) {
        EXPECT_FALSE(g.pub_relocateNodes({}));
    }

    TEST_F(HypergraphInternalsTest, RelocateNodes_CorrectLayer_ReturnsFalse) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        // c is already at correct layer
        EXPECT_FALSE(g.pub_relocateNodes({ c }));
    }

    TEST_F(HypergraphInternalsTest, RelocateNodes_WrongLayer_ReturnsTrue_AndMoves) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        // manually push c to wrong layer
        g.pub_removeNodeFromLayer(1, c);
        g.pub_addNodeToLayer(3, -1, c);
        EXPECT_TRUE(g.pub_relocateNodes({ c }));
        EXPECT_EQ(c->getLayer(), 1);  // correct layer is 1
    }

    // =============================================================================
    // 10. parentIsInAncestors
    // =============================================================================

    TEST_F(HypergraphInternalsTest, ParentIsInAncestors_DirectParent) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_TRUE(g.pub_parentIsInAncestors(c, p));
    }

    TEST_F(HypergraphInternalsTest, ParentIsInAncestors_IndirectAncestor) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        EXPECT_TRUE(g.pub_parentIsInAncestors(c, a));
    }

    TEST_F(HypergraphInternalsTest, ParentIsInAncestors_UnrelatedNode) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        EXPECT_FALSE(g.pub_parentIsInAncestors(b, a));
    }

    TEST_F(HypergraphInternalsTest, ParentIsInAncestors_NullInputs) {
        auto a = g.createNode("a", 0, nullptr);
        EXPECT_FALSE(g.pub_parentIsInAncestors(nullptr, a));
        EXPECT_FALSE(g.pub_parentIsInAncestors(a, nullptr));
    }

    TEST_F(HypergraphInternalsTest, ParentIsInAncestors_DeepChain) {
        NodePtr prev = g.createNode("n0", 0, nullptr);
        NodePtr root = prev;
        for (int i = 1; i < 10; ++i)
            prev = g.createNode("n" + std::to_string(i), 0, prev);
        EXPECT_TRUE(g.pub_parentIsInAncestors(prev, root));
    }

    TEST_F(HypergraphInternalsTest, ParentIsInAncestors_VectorShortCircuits) {
        auto p = g.createNode("p", 0, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto c2 = g.createNode("c2", 0, nullptr);
        EXPECT_TRUE(g.pub_parentIsInAncestors({ c1, c2 }, p));
        auto x = g.createNode("x", 0, nullptr);
        auto y = g.createNode("y", 0, nullptr);
        EXPECT_FALSE(g.pub_parentIsInAncestors({ x, y }, p));
    }

    // =============================================================================
    // 11. childIsInDescendants
    // =============================================================================

    TEST_F(HypergraphInternalsTest, ChildIsInDescendants_DirectChild) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_TRUE(g.pub_childIsInDescendants({ p }, c));
    }

    TEST_F(HypergraphInternalsTest, ChildIsInDescendants_IndirectDescendant) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        EXPECT_TRUE(g.pub_childIsInDescendants({ a }, c));
    }

    TEST_F(HypergraphInternalsTest, ChildIsInDescendants_UnrelatedNode) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        EXPECT_FALSE(g.pub_childIsInDescendants({ a }, b));
    }

    TEST_F(HypergraphInternalsTest, ChildIsInDescendants_NullInputs) {
        auto a = g.createNode("a", 0, nullptr);
        EXPECT_FALSE(g.pub_childIsInDescendants({ a }, nullptr));
        EXPECT_FALSE(g.pub_childIsInDescendants({}, a));
    }

    TEST_F(HypergraphInternalsTest, ChildIsInDescendants_Symmetry_WithParentIsInAncestors) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        EXPECT_EQ(g.pub_parentIsInAncestors(c, a), g.pub_childIsInDescendants({ a }, c));
        EXPECT_EQ(g.pub_parentIsInAncestors(a, c), g.pub_childIsInDescendants({ c }, a));
    }

    // =============================================================================
    // 12. checkCycles
    // =============================================================================

    TEST_F(HypergraphInternalsTest, CheckCycles_NoCycle_SimpleDAG) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        EXPECT_FALSE(g.pub_checkCycles(a));
        EXPECT_FALSE(g.pub_checkCycles(b));
    }

    TEST_F(HypergraphInternalsTest, CheckCycles_DirectCycleDetected) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        b->addChild(a);
        a->addParent(b);
        EXPECT_TRUE(g.pub_checkCycles(a));
    }

    TEST_F(HypergraphInternalsTest, CheckCycles_LongCycleDetected) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);
        d->addChild(a);
        a->addParent(d);
        EXPECT_TRUE(g.pub_checkCycles(a));
    }

    TEST_F(HypergraphInternalsTest, CheckCycles_IsolatedNode_NoCycle) {
        auto n = g.createNode("n", 0, nullptr);
        EXPECT_FALSE(g.pub_checkCycles(n));
    }

    TEST_F(HypergraphInternalsTest, CheckCycles_DiamondDAG_NoCycle) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);
        auto d = g.createNode("d", 0, b);
        g.addConnection(c, d);
        EXPECT_FALSE(g.pub_checkCycles(a));
    }

    // =============================================================================
    // 13. getAllAncestors / getAllDescendants
    // =============================================================================

    TEST_F(HypergraphInternalsTest, GetAllAncestors_SingleNode_DirectParent) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto ancestors = g.pub_getAllAncestors({ c });
        EXPECT_TRUE(ancestors.count(p.get()));
        EXPECT_FALSE(ancestors.count(c.get())) << "Node should not be its own ancestor";
    }

    TEST_F(HypergraphInternalsTest, GetAllAncestors_DeepChain) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto ancestors = g.pub_getAllAncestors({ c });
        EXPECT_TRUE(ancestors.count(a.get()));
        EXPECT_TRUE(ancestors.count(b.get()));
        EXPECT_FALSE(ancestors.count(c.get()));
    }

    TEST_F(HypergraphInternalsTest, GetAllAncestors_EmptyVector) {
        EXPECT_TRUE(g.pub_getAllAncestors({}).empty());
    }

    TEST_F(HypergraphInternalsTest, GetAllAncestors_MultipleStartNodes_Union) {
        auto r = g.createNode("r", 0, nullptr);
        auto a = g.createNode("a", 0, r);
        auto b = g.createNode("b", 0, r);
        // Both a and b share ancestor r
        auto ancestors = g.pub_getAllAncestors({ a, b });
        EXPECT_TRUE(ancestors.count(r.get()));
        EXPECT_FALSE(ancestors.count(a.get()));
        EXPECT_FALSE(ancestors.count(b.get()));
    }

    TEST_F(HypergraphInternalsTest, GetAllDescendants_SingleNode_DirectChild) {
        auto p = g.createNode("p", 0, nullptr);
        auto c = g.createNode("c", 0, p);
        auto desc = g.pub_getAllDescendants({ p });
        EXPECT_TRUE(desc.count(c.get()));
        EXPECT_FALSE(desc.count(p.get())) << "Node should not be its own descendant";
    }

    TEST_F(HypergraphInternalsTest, GetAllDescendants_DeepChain) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto desc = g.pub_getAllDescendants({ a });
        EXPECT_TRUE(desc.count(b.get()));
        EXPECT_TRUE(desc.count(c.get()));
        EXPECT_FALSE(desc.count(a.get()));
    }

    TEST_F(HypergraphInternalsTest, GetAllDescendants_EmptyVector) {
        EXPECT_TRUE(g.pub_getAllDescendants({}).empty());
    }

    // =============================================================================
    // 14. removeTransitiveConnections
    // =============================================================================

    TEST_F(HypergraphInternalsTest, RemoveTransitiveConnections_RemovesRedundantEdge) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        auto c = g.createNode("c", 0, a);  // a->c
        g.addConnection(a, b);             // a->b (b moves to layer 1)
        g.addConnection(b, c);             // b->c; now a->c is transitive
        // The transitive removal should have happened inside addConnection,
        // but we test it explicitly here with the new state
        bool ac_exists = false;
        for (const auto& e : g.getAllHyperedges()) {
            if (!e->isSegment() && edgeHasSource(e, a) && edgeHasTarget(e, c))
                ac_exists = true;
        }
        EXPECT_FALSE(ac_exists) << "a->c should have been removed as transitive";
    }

    TEST_F(HypergraphInternalsTest, RemoveTransitiveConnections_PreservesNonRedundantEdge) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);
        g.addConnection(b, c);
        bool ab_exists = false;
        for (const auto& e : g.getAllHyperedges())
            if (!e->isSegment() && edgeHasSource(e, a) && edgeHasTarget(e, b))
                ab_exists = true;
        EXPECT_TRUE(ab_exists) << "Non-redundant a->b must survive";
    }

    // =============================================================================
    // 15. INTENSIVE — complex topology and extreme cases for protected methods
    // =============================================================================

    static bool eachNodeInExactlyOneLayer(const TestableHypergraph& g) {
        std::unordered_map<Node*, int> count;
        for (const auto& [l, data] : g.getLayers())
            for (const auto& n : data.nodes) count[n.get()]++;
        for (const auto& [n, c] : count) if (c != 1) return false;
        return true;
    }

    static bool layerOrderIsConsistent(const TestableHypergraph& g) {
        for (const auto& n : g.getAllNodes())
            for (const auto& p : n->getParents())
                if (!p->isDummy() && !n->isDummy())
                    if (n->getLayer() <= p->getLayer()) return false;
        return true;
    }

    // ---- splitLongEdge extreme cases ----------------------------------------------

    TEST_F(HypergraphInternalsTest, Stress_SplitLongEdge_FiveLayerGap) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto n4 = g.createNode("n4", 0, n3);
        auto n5 = g.createNode("n5", 0, n4);
        auto r2 = g.createNode("r2", 0, nullptr);

        // Create a long edge spanning 5 layers
        auto edge = g.createHyperedge({ r2 }, { n5 }, -1);
        r2->addChild(n5); n5->addParent(r2);
        g.pub_splitLongEdge(edge);

        EXPECT_EQ(countSegmentEdges(g), 5);
        for (int L = 1; L <= 4; L++)
            EXPECT_GE(countDummyNodesInLayer(g, L), 1) << "Missing dummy at layer " << L;
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    TEST_F(HypergraphInternalsTest, Stress_SplitLongEdge_MultipleSourcesMultipleTargets) {
        // Two sources at layer 0, two targets at layer 3
        auto s1 = g.createNode("s1", 0, nullptr);
        auto s2 = g.createNode("s2", 0, nullptr);
        auto r = g.createNode("r", 0, nullptr);
        auto m1 = g.createNode("m1", 0, r);
        auto m2 = g.createNode("m2", 0, m1);
        auto t1 = g.createNode("t1", 0, m2);
        auto t2 = g.createNode("t2", 0, m2);

        auto edge = g.createHyperedge({ s1, s2 }, { t1, t2 }, -1);
        s1->addChild(t1); s1->addChild(t2);
        s2->addChild(t1); s2->addChild(t2);
        t1->addParent(s1); t1->addParent(s2);
        t2->addParent(s1); t2->addParent(s2);

        g.pub_splitLongEdge(edge);

        EXPECT_GE(countSegmentEdges(g), 3);
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        // Real nodes must not have dummy parents/children
        for (const auto& p : t1->getParents())
            EXPECT_FALSE(p->isDummy()) << "t1 has dummy parent";
        for (const auto& p : t2->getParents())
            EXPECT_FALSE(p->isDummy()) << "t2 has dummy parent";
    }

    TEST_F(HypergraphInternalsTest, Stress_SplitAndReSplit_DummiesCleaned) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n3);

        int dummies_before = 0;
        for (const auto& [l, data] : g.getLayers())
            for (const auto& n : data.nodes) if (n->isDummy()) dummies_before++;

        // Re-split — dummy count should stay the same
        auto edge = findOriginalEdgeWithSource(g, r2);
        ASSERT_NE(edge, nullptr);
        g.pub_splitLongEdge(edge);

        int dummies_after = 0;
        for (const auto& [l, data] : g.getLayers())
            for (const auto& n : data.nodes) if (n->isDummy()) dummies_after++;

        EXPECT_EQ(dummies_after, dummies_before) << "Re-split must not accumulate dummies";
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
    }

    // ---- dissolveSegments extreme cases -------------------------------------------

    TEST_F(HypergraphInternalsTest, Stress_DissolveSegments_MultipleLongEdgesAtOnce) {
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr);
        auto r3 = g.createNode("r3", 0, nullptr);
        g.addConnection(r2, n2);
        g.addConnection(r3, n2);

        std::unordered_set<Hyperedge*> long_edges;
        for (const auto& e : g.getAllHyperedges()) {
            if (e->isSegment()) continue;
            if (edgeHasSource(e, r2) || edgeHasSource(e, r3))
                long_edges.insert(e.get());
        }
        g.pub_dissolveSegments(long_edges);

        EXPECT_EQ(countSegmentEdges(g), 0);
        // Both long edges' dummies at layer 1 should be gone (n1 is a real node, stays)
        EXPECT_EQ(countDummyNodesInLayer(g, 1), 0);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
    }

    // ---- applyRelocationAndPropagate extreme cases --------------------------------

    TEST_F(HypergraphInternalsTest, Stress_BatchRelocation_DiamondDAG) {
        // a->b, a->c, b->d, c->d; batch-relocate b and c simultaneously
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);
        auto d = g.createNode("d", 0, b);
        g.addConnection(c, d);

        // Force b and c to relocate to layer 3
        g.pub_applyRelocationAndPropagate({ {b, 3}, {c, 3} });

        EXPECT_EQ(b->getLayer(), 3);
        EXPECT_EQ(c->getLayer(), 3);
        EXPECT_GE(d->getLayer(), 4);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
    }

    TEST_F(HypergraphInternalsTest, Stress_RelocationPropagates_WideBranchingTree) {
        // root -> a, b, c, d each with two children; relocate root's direct children
        auto root = g.createNode("root", 0, nullptr);
        std::vector<NodePtr> mids, leaves;
        for (int i = 0; i < 4; i++) {
            auto m = g.createNode("m" + std::to_string(i), 0, root);
            mids.push_back(m);
            leaves.push_back(g.createNode("l" + std::to_string(i * 2), 0, m));
            leaves.push_back(g.createNode("l" + std::to_string(i * 2 + 1), 0, m));
        }

        // Relocate all mids to layer 3 (normally at layer 1)
        std::vector<std::pair<NodePtr, int>> relocations;
        for (const auto& m : mids) relocations.push_back({ m, 3 });
        g.pub_applyRelocationAndPropagate(relocations);

        for (const auto& m : mids)
            EXPECT_EQ(m->getLayer(), 3);
        for (const auto& l : leaves)
            EXPECT_EQ(l->getLayer(), 4);

        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
    }

    TEST_F(HypergraphInternalsTest, Stress_RelocationAfterLongEdge_AllInvariants) {
        // Build r->n1->n2->n3; add long edge r2->n3; then relocate n1
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr);
        g.addConnection(r2, n3);

        // Now relocate n1 deeper (to layer 3)
        g.pub_applyRelocationAndPropagate(n1, 3);

        EXPECT_EQ(n1->getLayer(), 3);
        EXPECT_GE(n2->getLayer(), 4);
        EXPECT_GE(n3->getLayer(), 5);
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
    }

    // ---- parentIsInAncestors / childIsInDescendants: extreme cases ----------------

    TEST_F(HypergraphInternalsTest, Stress_ParentIsInAncestors_VeryDeepChain_RootFound) {
        NodePtr prev = g.createNode("n0", 0, nullptr);
        NodePtr root = prev;
        for (int i = 1; i < 50; i++)
            prev = g.createNode("n" + std::to_string(i), 0, prev);
        EXPECT_TRUE(g.pub_parentIsInAncestors(prev, root));
        EXPECT_FALSE(g.pub_parentIsInAncestors(root, prev));
    }

    TEST_F(HypergraphInternalsTest, Stress_ParentIsInAncestors_WideDiamond) {
        // one root -> 10 mid nodes -> one sink; root should be ancestor of sink
        auto root = g.createNode("root", 0, nullptr);
        std::vector<NodePtr> mids;
        for (int i = 0; i < 10; i++)
            mids.push_back(g.createNode("m" + std::to_string(i), 0, root));
        auto sink = g.createNode("sink", 0, mids[0]);
        for (int i = 1; i < 10; i++) g.addConnection(mids[i], sink);

        EXPECT_TRUE(g.pub_parentIsInAncestors(sink, root));
        EXPECT_FALSE(g.pub_parentIsInAncestors(root, sink));
    }

    TEST_F(HypergraphInternalsTest, Stress_ChildIsInDescendants_BranchingTree_AllLeaves) {
        auto root = g.createNode("root", 0, nullptr);
        std::vector<NodePtr> leaves;
        std::vector<NodePtr> current{ root };
        for (int depth = 0; depth < 3; depth++) {
            std::vector<NodePtr> next;
            for (const auto& p : current) {
                for (int i = 0; i < 3; i++) {
                    auto c = g.createNode("n", 0, p);
                    next.push_back(c);
                    if (depth == 2) leaves.push_back(c);
                }
            }
            current = next;
        }
        for (const auto& leaf : leaves)
            EXPECT_TRUE(g.pub_childIsInDescendants({ root }, leaf))
            << "root should have every leaf as descendant";
    }

    // ---- getAllAncestors / getAllDescendants extreme cases -------------------------

    TEST_F(HypergraphInternalsTest, Stress_GetAllAncestors_DiamondDAG_NoDuplicates) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);
        auto d = g.createNode("d", 0, b);
        g.addConnection(c, d);  // d has two parents

        auto ancestors = g.pub_getAllAncestors({ d });
        // a should appear exactly once even though it is ancestor via both b and c
        EXPECT_EQ(ancestors.count(a.get()), 1u);
        EXPECT_EQ(ancestors.count(b.get()), 1u);
        EXPECT_EQ(ancestors.count(c.get()), 1u);
        EXPECT_EQ(ancestors.count(d.get()), 0u);  // d is not its own ancestor
    }

    TEST_F(HypergraphInternalsTest, Stress_GetAllDescendants_DiamondDAG_NoDuplicates) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);
        auto d = g.createNode("d", 0, b);
        g.addConnection(c, d);

        auto desc = g.pub_getAllDescendants({ a });
        EXPECT_EQ(desc.count(b.get()), 1u);
        EXPECT_EQ(desc.count(c.get()), 1u);
        EXPECT_EQ(desc.count(d.get()), 1u);
        EXPECT_EQ(desc.count(a.get()), 0u);  // a is not its own descendant
    }

    TEST_F(HypergraphInternalsTest, Stress_GetAllAncestors_MultipleStartNodes_SharedAncestorOnce) {
        auto root = g.createNode("root", 0, nullptr);
        auto mid = g.createNode("mid", 0, root);
        auto a = g.createNode("a", 0, mid);
        auto b = g.createNode("b", 0, mid);

        auto ancestors = g.pub_getAllAncestors({ a, b });
        // root and mid should each appear exactly once
        EXPECT_EQ(ancestors.count(root.get()), 1u);
        EXPECT_EQ(ancestors.count(mid.get()), 1u);
        EXPECT_EQ(ancestors.count(a.get()), 0u);
        EXPECT_EQ(ancestors.count(b.get()), 0u);
    }

    TEST_F(HypergraphInternalsTest, Stress_GetAllDescendants_MultipleStartNodes_SharedDescendantOnce) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, nullptr);
        auto mid = g.createNode("mid", 0, a);
        g.addConnection(b, mid);
        auto leaf = g.createNode("leaf", 0, mid);

        auto desc = g.pub_getAllDescendants({ a, b });
        EXPECT_EQ(desc.count(mid.get()), 1u);
        EXPECT_EQ(desc.count(leaf.get()), 1u);
        EXPECT_EQ(desc.count(a.get()), 0u);
        EXPECT_EQ(desc.count(b.get()), 0u);
    }

    // ---- checkCycles: complex DAG cases -------------------------------------------

    TEST_F(HypergraphInternalsTest, Stress_CheckCycles_ComplexDAG_NoCycle) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, a);
        auto d = g.createNode("d", 0, b);
        g.addConnection(c, d);
        auto e = g.createNode("e", 0, d);

        EXPECT_FALSE(g.pub_checkCycles(a));
        EXPECT_FALSE(g.pub_checkCycles(b));
        EXPECT_FALSE(g.pub_checkCycles(c));
    }

    TEST_F(HypergraphInternalsTest, Stress_CheckCycles_ManualCycleInComplexGraph) {
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);
        // Manually wire d -> b (creating a cycle b->c->d->b)
        d->addChild(b);
        b->addParent(d);

        EXPECT_TRUE(g.pub_checkCycles(b));
    }

    // ---- removeTransitiveConnections extreme cases --------------------------------

    TEST_F(HypergraphInternalsTest, Stress_RemoveTransitiveConnections_LongChain) {
        // a->b->c->d->e; add direct a->e (transitive); then call removeTransitiveConnections
        auto a = g.createNode("a", 0, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);
        auto e = g.createNode("e", 0, d);

        // Create direct a->e (bypassing validation for test purposes)
        auto direct_ae = g.createHyperedge({ a }, { e }, -1);

        g.pub_removeTransitiveConnections({ a }, { e });

        // The direct a->e edge should now be removed
        bool ae_exists = false;
        for (const auto& edge : g.getAllHyperedges()) {
            if (!edge->isSegment() && edgeHasSource(edge, a) && edgeHasTarget(edge, e))
                ae_exists = true;
        }
        EXPECT_FALSE(ae_exists) << "Direct a->e should be removed as transitive";

        // Intermediate edges should survive
        bool ab_exists = false, bc_exists = false;
        for (const auto& edge : g.getAllHyperedges()) {
            if (!edge->isSegment()) {
                if (edgeHasSource(edge, a) && edgeHasTarget(edge, b)) ab_exists = true;
                if (edgeHasSource(edge, b) && edgeHasTarget(edge, c)) bc_exists = true;
            }
        }
        EXPECT_TRUE(ab_exists);
        EXPECT_TRUE(bc_exists);
    }

    TEST_F(HypergraphInternalsTest, Stress_RemoveTransitiveConnections_MultipleParentsMultipleChildren) {
        // Two parents p1, p2; two children c1, c2; direct edges p1->c1, p2->c2 are transitive
        // after adding p1->m->c1 and p2->m->c2 (shared middle m)
        auto p1 = g.createNode("p1", 0, nullptr);
        auto p2 = g.createNode("p2", 0, nullptr);
        auto m = g.createNode("m", 0, p1);
        g.addConnection(p2, m);
        auto c1 = g.createNode("c1", 0, m);
        auto c2 = g.createNode("c2", 0, m);

        // Create direct (now transitive) edges p1->c1 and p2->c2
        g.createHyperedge({ p1 }, { c1 }, -1);
        g.createHyperedge({ p2 }, { c2 }, -1);

        g.pub_removeTransitiveConnections({ p1, p2 }, { c1, c2 });

        for (const auto& edge : g.getAllHyperedges()) {
            if (edge->isSegment()) continue;
            EXPECT_FALSE(edgeHasSource(edge, p1) && edgeHasTarget(edge, c1))
                << "p1->c1 should be removed as transitive";
            EXPECT_FALSE(edgeHasSource(edge, p2) && edgeHasTarget(edge, c2))
                << "p2->c2 should be removed as transitive";
        }
    }

    // ---- global invariant checks after a sequence of protected calls --------------

    TEST_F(HypergraphInternalsTest, Stress_SequenceOfProtectedCalls_AllInvariants) {
        // Build a graph, split a long edge, relocate some nodes, then verify everything
        auto r = g.createNode("r", 0, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr);

        // Create a long edge and split it
        auto long_edge = g.createHyperedge({ r2 }, { n3 }, -1);
        r2->addChild(n3); n3->addParent(r2);
        g.pub_splitLongEdge(long_edge);

        // Relocate n1 deeper
        g.pub_applyRelocationAndPropagate(n1, 3);

        // Dissolve segments of the long edge and re-split
        g.pub_dissolveSegments({ long_edge.get() });
        g.pub_splitLongEdge(long_edge);

        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
        EXPECT_TRUE(layerOrderIsConsistent(g));
    }

} // namespace hypergraph_logic::hypergraph_tests::internals