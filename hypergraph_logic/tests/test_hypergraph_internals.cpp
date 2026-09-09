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
//       splitLongEdge, dissolveSegments, resyncSegmentEndpoints
//       splitLongEdge's segment/dummy reuse on resplit (identity preservation,
//       partial resync, and stale cleanup when the split shrinks or shifts)
//       applyRelocationAndPropagate (single + batch)
//       removeTransitiveConnections
//       parentIsInAncestors, childIsInDescendants
//       checkCycles
//       getAllAncestors, getAllDescendants
//       relocateNodes
//       resolveTargetLayer
//       renumberLayersFrom
//       compactLayerNumbers
//       choosePositionForRelocatedNode
//       cleanUp
//   - out_min_new_layer / out_altered_layers reporting across the engine layer:
//       addNodeToLayer, createHyperedge, addHyperedgeToLayer, resyncSegmentEndpoints,
//       splitLongEdge, settleEdgePlacement, collapseToShortLayer, resettleEdge,
//       relocateNodes, applyRelocationAndPropagate, removeTransitiveConnections (now void),
//       resolveOwnRedundantTargets (now int* instead of int&)
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
        void pub_addNodeToLayer(int layer, int pos, const NodePtr& n, int* out_min_new_layer = nullptr) { addNodeToLayer(layer, pos, n, out_min_new_layer); }
        void pub_removeNodeFromLayer(int layer, const NodePtr& n) { removeNodeFromLayer(layer, n); }
        void pub_removeNodeFromLayer(int layer, const std::unordered_set<Node*>& ns) { removeNodeFromLayer(layer, ns); }
        void pub_addHyperedgeToLayer(int layer, const HyperedgePtr& e, std::set<int>* out_altered_layers = nullptr) { addHyperedgeToLayer(layer, e, out_altered_layers); }
        void pub_removeHyperedgeFromLayer(int layer, const HyperedgePtr& e) { removeHyperedgeFromLayer(layer, e); }
        void pub_removeHyperedgeFromLayer(int layer, const std::unordered_set<Hyperedge*>& es) { removeHyperedgeFromLayer(layer, es); }

        // Edge management
        void pub_splitLongEdge(const HyperedgePtr& e, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr) { splitLongEdge(e, out_min_new_layer, out_altered_layers); }
        void pub_dissolveSegments(const std::unordered_set<Hyperedge*>& es) { dissolveSegments(es); }
        void pub_resyncSegmentEndpoints(const HyperedgePtr& seg, const std::vector<NodePtr>& new_sources, const std::vector<NodePtr>& new_targets, std::set<int>* out_altered_layers = nullptr) {
            resyncSegmentEndpoints(seg, new_sources, new_targets, out_altered_layers);
        }
        HyperedgePtr pub_createHyperedge(const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets, int layer, std::set<int>* out_altered_layers = nullptr) {
            return createHyperedge(sources, targets, layer, out_altered_layers);
        }
        HyperedgePtr pub_createHyperedge(const WeakHyperedgePtr& origin, const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets, int layer, std::set<int>* out_altered_layers = nullptr) {
            return createHyperedge(origin, sources, targets, layer, out_altered_layers);
        }
        int pub_edgeIsShort(const HyperedgePtr& e) { return edgeIsShort(e); }
        int pub_settleEdgePlacement(const HyperedgePtr& e, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr) {
            return settleEdgePlacement(e, out_min_new_layer, out_altered_layers);
        }
        void pub_collectSegmentDummies(const HyperedgePtr& e, std::vector<Node*>& out_nodes, int& min_layer, int& max_layer, bool include_real_sources = true) {
            collectSegmentDummies(e, out_nodes, min_layer, max_layer, include_real_sources);
        }
        int pub_settleEdgePlacementAndCollectDummies(const HyperedgePtr& e, std::vector<Node*>& seed_nodes, int& min_layer, int& max_layer, bool include_real_sources = true, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr) {
            return settleEdgePlacementAndCollectDummies(e, seed_nodes, min_layer, max_layer, include_real_sources, out_min_new_layer, out_altered_layers);
        }
        void pub_settleAndMinimizeIfSplit(const HyperedgePtr& e, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr) {
            settleAndMinimizeIfSplit(e, out_min_new_layer, out_altered_layers);
        }
        void pub_collapseToShortLayer(const HyperedgePtr& e, int k, std::set<int>* out_altered_layers = nullptr) {
            collapseToShortLayer(e, k, out_altered_layers);
        }
        void pub_resettleEdge(const HyperedgePtr& e, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr) {
            resettleEdge(e, out_min_new_layer, out_altered_layers);
        }

        // Relocation
        void pub_applyRelocationAndPropagate(const NodePtr& n, int layer, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr) {
            applyRelocationAndPropagate({ {n, layer} }, out_min_new_layer, out_altered_layers);
        }
        void pub_applyRelocationAndPropagate(const std::vector<std::pair<NodePtr, int>>& r, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr) {
            applyRelocationAndPropagate(r, out_min_new_layer, out_altered_layers);
        }
        bool pub_relocateNodes(const std::vector<NodePtr>& nodes, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr) {
            return relocateNodes(nodes, out_min_new_layer, out_altered_layers);
        }
        int pub_resolveTargetLayer(const NodePtr& n) { return resolveTargetLayer(n); }
        void pub_renumberLayersFrom(int from_layer) { renumberLayersFrom(from_layer); }
        void pub_compactLayerNumbers() { compactLayerNumbers(); }
        int pub_choosePositionForRelocatedNode(int new_layer, const NodePtr& n) const { return choosePositionForRelocatedNode(new_layer, n); }
        void pub_cleanUp() { cleanUp(); }

        // Transitive connections
        void pub_removeTransitiveConnections(const std::vector<NodePtr>& parents, const std::vector<NodePtr>& children, const HyperedgePtr& edge_to_skip = nullptr, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr) {
            removeTransitiveConnections(parents, children, edge_to_skip, out_min_new_layer, out_altered_layers);
        }
        HyperedgePtr pub_resolveOwnRedundantTargets(const HyperedgePtr& edge, const NodePtr& target, int* out_min_new_layer = nullptr, std::set<int>* out_altered_layers = nullptr) {
            return resolveOwnRedundantTargets(edge, target, out_min_new_layer, out_altered_layers);
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
        g.createNode("a", 0, nullptr, nullptr);
        auto m = std::make_shared<Node>("m");
        g.pub_addNodeToLayer(0, 9999, m);
        EXPECT_EQ(g.getNodesAt(0).back(), m);
    }

    TEST_F(HypergraphInternalsTest, AddNodeToLayer_NegativePositionAppendsToEnd) {
        g.createNode("a", 0, nullptr, nullptr);
        auto m = std::make_shared<Node>("m");
        g.pub_addNodeToLayer(0, -1, m);
        EXPECT_EQ(g.getNodesAt(0).back(), m);
    }

    TEST_F(HypergraphInternalsTest, AddNodeToLayer_DuplicateIgnored) {
        auto n = g.createNode("n", 0, nullptr, nullptr);
        int count_before = static_cast<int>(g.getNodesAt(0).size());
        g.pub_addNodeToLayer(0, 0, n);  // already there
        EXPECT_EQ(static_cast<int>(g.getNodesAt(0).size()), count_before);
    }

    TEST_F(HypergraphInternalsTest, RemoveNodeFromLayer_Ptr_RemovesCorrectNode) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        g.pub_removeNodeFromLayer(0, a);
        EXPECT_FALSE(layerContainsNode(g, 0, a));
        EXPECT_TRUE(layerContainsNode(g, 0, b));
    }

    TEST_F(HypergraphInternalsTest, RemoveNodeFromLayer_Ptr_NonexistentLayerNoThrow) {
        auto n = g.createNode("n", 0, nullptr, nullptr);
        EXPECT_NO_THROW(g.pub_removeNodeFromLayer(99, n));
    }

    TEST_F(HypergraphInternalsTest, RemoveNodeFromLayer_Set_RemovesAll) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, nullptr, nullptr);
        std::unordered_set<Node*> to_remove{ a.get(), b.get() };
        g.pub_removeNodeFromLayer(0, to_remove);
        EXPECT_FALSE(layerContainsNode(g, 0, a));
        EXPECT_FALSE(layerContainsNode(g, 0, b));
        EXPECT_TRUE(layerContainsNode(g, 0, c));
    }

    TEST_F(HypergraphInternalsTest, RemoveNodeFromLayer_Set_EmptySetNoOp) {
        auto n = g.createNode("n", 0, nullptr, nullptr);
        int count_before = static_cast<int>(g.getNodesAt(0).size());
        g.pub_removeNodeFromLayer(0, std::unordered_set<Node*>{});
        EXPECT_EQ(static_cast<int>(g.getNodesAt(0).size()), count_before);
    }

    // =============================================================================
    // 3. Hyperedge management — addHyperedgeToLayer / removeHyperedgeFromLayer
    // =============================================================================

    TEST_F(HypergraphInternalsTest, AddHyperedgeToLayer_CreatesLayerOnDemand) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = g.pub_createHyperedge({ p }, { c }, -1);
        g.pub_addHyperedgeToLayer(7, edge);
        EXPECT_EQ(edge->getLayer(), 7);
        EXPECT_EQ(countEdgesInLayer(g, 7), 1);
    }

    TEST_F(HypergraphInternalsTest, AddHyperedgeToLayer_DuplicateIgnored) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        int count_before = countEdgesInLayer(g, 0);
        auto edge = g.getLayerData(0).outgoing_edges.front();
        g.pub_addHyperedgeToLayer(0, edge);  // already in layer 0
        EXPECT_EQ(countEdgesInLayer(g, 0), count_before);
    }

    TEST_F(HypergraphInternalsTest, RemoveHyperedgeFromLayer_Ptr_SetsLayerToMinusOne) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = g.getLayerData(0).outgoing_edges.front();
        g.pub_removeHyperedgeFromLayer(0, edge);
        EXPECT_EQ(countEdgesInLayer(g, 0), 0);
        EXPECT_EQ(edge->getLayer(), -1);
    }

    TEST_F(HypergraphInternalsTest, RemoveHyperedgeFromLayer_Set_RemovesAll) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
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
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = g.getLayerData(0).outgoing_edges.front();
        EXPECT_NO_THROW(g.pub_removeHyperedgeFromLayer(99, edge));
    }

    // =============================================================================
    // 4. createHyperedge / getAllHyperedges / edgeIsShort
    // =============================================================================

    TEST_F(HypergraphInternalsTest, CreateHyperedge_NonSegment_RegisteredAsOriginal) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, nullptr, nullptr);
        auto edge = g.pub_createHyperedge({ p }, { c }, -1);
        EXPECT_FALSE(edge->isSegment());
        bool found = false;
        for (const auto& e : g.getAllHyperedges()) if (e == edge) { found = true; break; }
        EXPECT_TRUE(found);
    }

    TEST_F(HypergraphInternalsTest, CreateHyperedge_WithLayer_RegisteredInLayerData) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, nullptr, nullptr);
        auto edge = g.pub_createHyperedge({ p }, { c }, 0);
        EXPECT_EQ(edge->getLayer(), 0);
        auto& edges = g.getLayerData(0).outgoing_edges;
        EXPECT_NE(std::find(edges.begin(), edges.end(), edge), edges.end());
    }

    TEST_F(HypergraphInternalsTest, CreateHyperedge_WiresParentChildLinks) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = std::make_shared<Node>("c");
        g.rawNodes().push_back(c);
        g.pub_addNodeToLayer(1, -1, c);
        g.pub_createHyperedge({ p }, { c }, 0);
        auto p_children = p->getChildren();
        auto c_parents = c->getParents();
        EXPECT_NE(std::find(p_children.begin(), p_children.end(), c), p_children.end());
        EXPECT_NE(std::find(c_parents.begin(), c_parents.end(), p), c_parents.end());
    }

    TEST_F(HypergraphInternalsTest, GetAllHyperedges_IncludesSegments) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
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
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = g.getLayerData(0).outgoing_edges.front();
        EXPECT_EQ(g.pub_edgeIsShort(edge), 0);
    }

    TEST_F(HypergraphInternalsTest, EdgeIsShort_LongEdge_ReturnsMinusOne) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = std::make_shared<Node>("c");
        g.rawNodes().push_back(c);
        g.pub_addNodeToLayer(2, -1, c);
        auto edge = g.pub_createHyperedge({ p }, { c }, -1);
        EXPECT_EQ(g.pub_edgeIsShort(edge), -1);
    }

    TEST_F(HypergraphInternalsTest, EdgeIsShort_NullEdge_ReturnsMinusOne) {
        EXPECT_EQ(g.pub_edgeIsShort(nullptr), -1);
    }

    // =============================================================================
    // 5. Layer queries
    // =============================================================================

    TEST_F(HypergraphInternalsTest, GetLayerCount_CorrectAfterNodeCreation) {
        EXPECT_EQ(g.getLayerCount(), 0);
        auto p = g.createNode("p", 0, nullptr, nullptr);
        EXPECT_EQ(g.getLayerCount(), 1);
        auto c = g.createNode("c", 0, p);
        EXPECT_EQ(g.getLayerCount(), 2);
    }

    TEST_F(HypergraphInternalsTest, GetNodesAt_ReturnsCorrectNodes) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
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
        auto a = g.createNode("a", 0, nullptr, nullptr);
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
        auto p = g.createNode("p", 0, nullptr, nullptr);
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
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        auto edge = g.pub_createHyperedge({ r2 }, { n2 }, -1);
        r2->addChild(n2); n2->addParent(r2);
        g.pub_splitLongEdge(edge);
        EXPECT_GE(countDummyNodesInLayer(g, 1), 1);
        EXPECT_GE(countSegmentEdges(g), 2);
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    TEST_F(HypergraphInternalsTest, SplitLongEdge_AlreadySplit_ReSplitsCleanly) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
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
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        auto edge = g.pub_createHyperedge({ r2 }, { n2 }, -1);
        r2->addChild(n2); n2->addParent(r2);
        g.pub_splitLongEdge(edge);
        bool found = false;
        for (const auto& e : g.getAllHyperedges())
            if (e->isSegment() && e->getOrigin().lock() == edge) { found = true; break; }
        EXPECT_TRUE(found);
    }

    TEST_F(HypergraphInternalsTest, SplitLongEdge_ShortEdge_NoChange) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = g.getLayerData(0).outgoing_edges.front();
        int segs_before = countSegmentEdges(g);
        g.pub_splitLongEdge(edge);  // should do nothing
        EXPECT_EQ(countSegmentEdges(g), segs_before);
    }

    // =============================================================================
    // 6a. splitLongEdge — reuse of the previous split on resplit
    // =============================================================================
    //
    // These verify the behaviour change: resplitting an already-split edge must
    // reuse the segments/dummies for transitions that are unaffected, rather than
    // dissolving and rebuilding the whole chain from scratch every time.

    TEST_F(HypergraphInternalsTest, SplitLongEdge_ReSplitWithNoStructuralChange_PreservesAllIdentities) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        g.addConnection(r2, n3); // splits into segments at layers 0,1,2 with dummies at 1,2

        auto edge = findOriginalEdgeWithSource(g, r2);
        ASSERT_NE(edge, nullptr);

        // Snapshot every segment and dummy belonging to this edge's split.
        std::vector<HyperedgePtr> segs_before = g.rawEdges()[edge];
        std::vector<NodePtr> dummies_before;
        for (int l = 1; l <= 2; ++l)
            for (const auto& n : g.getNodesAt(l)) if (n->isDummy()) dummies_before.push_back(n);
        ASSERT_EQ(segs_before.size(), 3u);
        ASSERT_EQ(dummies_before.size(), 2u);

        // Nothing about the graph changed, so resplitting must be a pure no-op:
        // the exact same segment and dummy objects should still be in place.
        g.pub_splitLongEdge(edge);

        EXPECT_EQ(g.rawEdges()[edge], segs_before) << "Resplitting with no change must not recreate any segment";

        std::vector<NodePtr> dummies_after;
        for (int l = 1; l <= 2; ++l)
            for (const auto& n : g.getNodesAt(l)) if (n->isDummy()) dummies_after.push_back(n);
        EXPECT_EQ(dummies_after, dummies_before) << "Resplitting with no change must not recreate any dummy";
    }

    TEST_F(HypergraphInternalsTest, SplitLongEdge_ResplitAfterRelocationDeepens_ReusesUnaffectedShallowChain) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        g.addConnection(r2, n3); // r2 -> n3, segments at layers 0,1,2; dummies at 1,2

        auto edge = findOriginalEdgeWithSource(g, r2);
        ASSERT_NE(edge, nullptr);

        // r2 stays put, so the shallowest transition (layer 0, feeding the layer-1
        // dummy) is untouched by anything that happens further down the chain.
        HyperedgePtr seg0_before, seg1_before;
        for (const auto& seg : g.rawEdges()[edge]) {
            if (seg->getLayer() == 0) seg0_before = seg;
            if (seg->getLayer() == 1) seg1_before = seg;
        }
        ASSERT_NE(seg0_before, nullptr);
        ASSERT_NE(seg1_before, nullptr);

        NodePtr dummy1_before, dummy2_before;
        for (const auto& n : g.getNodesAt(1)) if (n->isDummy()) dummy1_before = n;
        for (const auto& n : g.getNodesAt(2)) if (n->isDummy()) dummy2_before = n;
        ASSERT_NE(dummy1_before, nullptr);
        ASSERT_NE(dummy2_before, nullptr);

        // Relocate n1 much deeper: n2 and n3 (and therefore the long edge's real
        // target) get pushed down with it, forcing r2 -> n3 to be resplit with a
        // longer chain than before.
        g.pub_applyRelocationAndPropagate(n1, 3);
        ASSERT_GE(n3->getLayer(), 5);

        // Layers 0 and 1 of the chain are unaffected by the deepening: the segment
        // at layer 0 and the dummy at layer 1 should be the exact same objects.
        bool seg0_reused = false, seg1_reused = false;
        for (const auto& seg : g.rawEdges()[edge]) {
            if (seg == seg0_before) seg0_reused = true;
            if (seg == seg1_before) seg1_reused = true;
        }
        EXPECT_TRUE(seg0_reused) << "Segment at layer 0 should be reused, not recreated";
        EXPECT_TRUE(seg1_reused) << "Segment at layer 1 should be reused, not recreated";
        EXPECT_TRUE(layerContainsNode(g, 1, dummy1_before)) << "Dummy at layer 1 should be reused, not recreated";
        EXPECT_TRUE(layerContainsNode(g, 2, dummy2_before)) << "Dummy at layer 2 should be reused, not recreated";

        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
    }

    TEST_F(HypergraphInternalsTest, SplitLongEdge_ResplitAfterShrink_RemovesOnlyStaleSegmentsAndDummies) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto n4 = g.createNode("n4", 0, n3);
        auto n5 = g.createNode("n5", 0, n4);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);

        auto edge = g.pub_createHyperedge({ r2 }, { n5 }, -1);
        r2->addChild(n5); n5->addParent(r2);
        g.pub_splitLongEdge(edge); // 5-layer gap: segments at 0..4, dummies at 1..4
        ASSERT_EQ(countSegmentEdges(g), 5);

        HyperedgePtr seg0_before;
        for (const auto& seg : g.rawEdges()[edge]) if (seg->getLayer() == 0) seg0_before = seg;
        ASSERT_NE(seg0_before, nullptr);
        NodePtr dummy1_before;
        for (const auto& n : g.getNodesAt(1)) if (n->isDummy()) dummy1_before = n;
        ASSERT_NE(dummy1_before, nullptr);

        // Simulate n5 relocating much shallower (as if some other structural
        // change had already moved it), collapsing the gap from 5 layers to 2.
        g.pub_removeNodeFromLayer(5, n5);
        g.pub_addNodeToLayer(2, -1, n5);

        g.pub_splitLongEdge(edge);

        // Only one transition remains beyond the source layer now.
        EXPECT_EQ(countSegmentEdges(g), 2);
        EXPECT_EQ(g.rawEdges()[edge].size(), 2u);
        EXPECT_EQ(countDummyNodesInLayer(g, 3), 0) << "Stale dummy at layer 3 must be cleaned up";
        EXPECT_EQ(countDummyNodesInLayer(g, 4), 0) << "Stale dummy at layer 4 must be cleaned up";

        // The transition nearest the (unmoved) source is still the same object.
        bool seg0_reused = false;
        for (const auto& seg : g.rawEdges()[edge]) if (seg == seg0_before) seg0_reused = true;
        EXPECT_TRUE(seg0_reused) << "Segment at layer 0 should be reused, not recreated";
        EXPECT_TRUE(layerContainsNode(g, 1, dummy1_before)) << "Dummy at layer 1 should be reused, not recreated";

        EXPECT_TRUE(allSegmentEdgesAreShort(g));
        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        EXPECT_TRUE(eachNodeInExactlyOneLayer(g));
    }

    // =============================================================================
    // 6b. resyncSegmentEndpoints
    // =============================================================================
    //
    // Direct tests of the helper splitLongEdge uses to reuse a segment in place:
    // it should touch only endpoints that actually changed, and keep the
    // dummy/real parent-child wiring in sync with the same rules
    // createHyperedge(origin, ...) uses when building a segment from scratch.

    TEST_F(HypergraphInternalsTest, ResyncSegmentEndpoints_NoChange_IsNoOp) {
        auto d = std::make_shared<Node>();     // dummy source
        auto t = std::make_shared<Node>("t");  // real target
        auto seg = g.pub_createHyperedge(WeakHyperedgePtr{}, { d }, { t }, 0);
        d->addChild(t); // mirrors the dummy->real wiring createHyperedge(origin, ...) performs

        g.pub_resyncSegmentEndpoints(seg, { d }, { t });

        ASSERT_EQ(seg->getSources().size(), 1u);
        ASSERT_EQ(seg->getTargets().size(), 1u);
        EXPECT_TRUE(edgeHasSource(seg, d));
        EXPECT_TRUE(edgeHasTarget(seg, t));
        EXPECT_EQ(d->getChildren().size(), 1u) << "Link must not be duplicated";
    }

    TEST_F(HypergraphInternalsTest, ResyncSegmentEndpoints_AddedRealTarget_LinksDummySourceOnly) {
        auto d = std::make_shared<Node>();
        auto t1 = std::make_shared<Node>("t1");
        auto t2 = std::make_shared<Node>("t2");
        auto seg = g.pub_createHyperedge(WeakHyperedgePtr{}, { d }, { t1 }, 0);
        d->addChild(t1);

        g.pub_resyncSegmentEndpoints(seg, { d }, { t1, t2 });

        EXPECT_TRUE(edgeHasTarget(seg, t2));
        bool d_has_t2_child = false;
        for (const auto& c : d->getChildren()) if (c == t2) d_has_t2_child = true;
        EXPECT_TRUE(d_has_t2_child) << "dummy -> real must set the dummy's child link";
        for (const auto& p : t2->getParents()) EXPECT_NE(p, d) << "real target must not gain the dummy as a visible parent";
    }

    TEST_F(HypergraphInternalsTest, ResyncSegmentEndpoints_RemovedDummyTarget_UnlinksParentOnly) {
        auto s = std::make_shared<Node>("s");  // real source
        auto d1 = std::make_shared<Node>();    // dummy target to be dropped
        auto d2 = std::make_shared<Node>();    // dummy target that stays
        auto seg = g.pub_createHyperedge(WeakHyperedgePtr{}, { s }, { d1, d2 }, 0);
        d1->addParent(s); d2->addParent(s); // real -> dummy: only the dummy's parent link is set

        g.pub_resyncSegmentEndpoints(seg, { s }, { d2 });

        EXPECT_FALSE(edgeHasTarget(seg, d1));
        EXPECT_TRUE(edgeHasTarget(seg, d2));
        for (const auto& p : d1->getParents()) EXPECT_NE(p, s) << "dropped dummy must be unlinked from its real parent";
        bool d2_still_has_parent = false;
        for (const auto& p : d2->getParents()) if (p == s) d2_still_has_parent = true;
        EXPECT_TRUE(d2_still_has_parent) << "kept dummy's link must survive untouched";
    }

    TEST_F(HypergraphInternalsTest, ResyncSegmentEndpoints_DummyToDummy_LinksBothWays) {
        auto d1 = std::make_shared<Node>();
        auto d2 = std::make_shared<Node>();
        auto seg = g.pub_createHyperedge(WeakHyperedgePtr{}, { d1 }, {}, 0);

        g.pub_resyncSegmentEndpoints(seg, { d1 }, { d2 });

        EXPECT_TRUE(edgeHasTarget(seg, d2));
        bool d1_has_child = false;
        for (const auto& c : d1->getChildren()) if (c == d2) d1_has_child = true;
        bool d2_has_parent = false;
        for (const auto& p : d2->getParents()) if (p == d1) d2_has_parent = true;
        EXPECT_TRUE(d1_has_child) << "dummy -> dummy must set the child link";
        EXPECT_TRUE(d2_has_parent) << "dummy -> dummy must set the parent link";
    }

    // =============================================================================
    // 7. dissolveSegments
    // =============================================================================

    TEST_F(HypergraphInternalsTest, DissolveSegments_CleansUpDummiesAndSegments) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
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
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        g.addConnection(r2, n2);
        int seg_before = countSegmentEdges(g);
        g.pub_dissolveSegments({});
        EXPECT_EQ(countSegmentEdges(g), seg_before);
    }

    TEST_F(HypergraphInternalsTest, DissolveSegments_OriginalEdgeSurvives) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
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
        auto root = g.createNode("root", 0, nullptr, nullptr);
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
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, root);
        g.pub_applyRelocationAndPropagate(n1, 3);
        EXPECT_GE(countDummyNodesInLayer(g, 1), 1);
        EXPECT_GE(countDummyNodesInLayer(g, 2), 1);
        EXPECT_GE(countSegmentEdges(g), 2);
        EXPECT_TRUE(allSegmentEdgesAreShort(g));
    }

    TEST_F(HypergraphInternalsTest, RelocationCleansUpEmptyLayers) {
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, root);
        g.pub_applyRelocationAndPropagate(n1, 2);
        // Layer 1 should only contain dummies (from re-split) or be absent
        for (const auto& n : g.getNodesAt(1))
            EXPECT_TRUE(n->isDummy()) << "Layer 1 should only contain dummies after relocation";
    }

    TEST_F(HypergraphInternalsTest, BatchRelocation_MultipleNodesAtOnce) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto ac = g.createNode("ac", 0, a);
        auto b = g.createNode("b", 0, nullptr, nullptr);
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
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        // c is already at correct layer
        EXPECT_FALSE(g.pub_relocateNodes({ c }));
    }

    TEST_F(HypergraphInternalsTest, RelocateNodes_WrongLayer_ReturnsTrue_AndMoves) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
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
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_TRUE(g.pub_parentIsInAncestors(c, p));
    }

    TEST_F(HypergraphInternalsTest, ParentIsInAncestors_IndirectAncestor) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        EXPECT_TRUE(g.pub_parentIsInAncestors(c, a));
    }

    TEST_F(HypergraphInternalsTest, ParentIsInAncestors_UnrelatedNode) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        EXPECT_FALSE(g.pub_parentIsInAncestors(b, a));
    }

    TEST_F(HypergraphInternalsTest, ParentIsInAncestors_NullInputs) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        EXPECT_FALSE(g.pub_parentIsInAncestors(nullptr, a));
        EXPECT_FALSE(g.pub_parentIsInAncestors(a, nullptr));
    }

    TEST_F(HypergraphInternalsTest, ParentIsInAncestors_DeepChain) {
        NodePtr prev = g.createNode("n0", 0, nullptr, nullptr);
        NodePtr root = prev;
        for (int i = 1; i < 10; ++i)
            prev = g.createNode("n" + std::to_string(i), 0, prev);
        EXPECT_TRUE(g.pub_parentIsInAncestors(prev, root));
    }

    TEST_F(HypergraphInternalsTest, ParentIsInAncestors_VectorShortCircuits) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c1 = g.createNode("c1", 0, p);
        auto c2 = g.createNode("c2", 0, nullptr, nullptr);
        EXPECT_TRUE(g.pub_parentIsInAncestors({ c1, c2 }, p));
        auto x = g.createNode("x", 0, nullptr, nullptr);
        auto y = g.createNode("y", 0, nullptr, nullptr);
        EXPECT_FALSE(g.pub_parentIsInAncestors({ x, y }, p));
    }

    // =============================================================================
    // 11. childIsInDescendants
    // =============================================================================

    TEST_F(HypergraphInternalsTest, ChildIsInDescendants_DirectChild) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_TRUE(g.pub_childIsInDescendants({ p }, c));
    }

    TEST_F(HypergraphInternalsTest, ChildIsInDescendants_IndirectDescendant) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        EXPECT_TRUE(g.pub_childIsInDescendants({ a }, c));
    }

    TEST_F(HypergraphInternalsTest, ChildIsInDescendants_UnrelatedNode) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        EXPECT_FALSE(g.pub_childIsInDescendants({ a }, b));
    }

    TEST_F(HypergraphInternalsTest, ChildIsInDescendants_NullInputs) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        EXPECT_FALSE(g.pub_childIsInDescendants({ a }, nullptr));
        EXPECT_FALSE(g.pub_childIsInDescendants({}, a));
    }

    TEST_F(HypergraphInternalsTest, ChildIsInDescendants_Symmetry_WithParentIsInAncestors) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        EXPECT_EQ(g.pub_parentIsInAncestors(c, a), g.pub_childIsInDescendants({ a }, c));
        EXPECT_EQ(g.pub_parentIsInAncestors(a, c), g.pub_childIsInDescendants({ c }, a));
    }

    // =============================================================================
    // 12. checkCycles
    // =============================================================================

    TEST_F(HypergraphInternalsTest, CheckCycles_NoCycle_SimpleDAG) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        EXPECT_FALSE(g.pub_checkCycles(a));
        EXPECT_FALSE(g.pub_checkCycles(b));
    }

    TEST_F(HypergraphInternalsTest, CheckCycles_DirectCycleDetected) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        b->addChild(a);
        a->addParent(b);
        EXPECT_TRUE(g.pub_checkCycles(a));
    }

    TEST_F(HypergraphInternalsTest, CheckCycles_LongCycleDetected) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);
        d->addChild(a);
        a->addParent(d);
        EXPECT_TRUE(g.pub_checkCycles(a));
    }

    TEST_F(HypergraphInternalsTest, CheckCycles_IsolatedNode_NoCycle) {
        auto n = g.createNode("n", 0, nullptr, nullptr);
        EXPECT_FALSE(g.pub_checkCycles(n));
    }

    TEST_F(HypergraphInternalsTest, CheckCycles_DiamondDAG_NoCycle) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
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
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto ancestors = g.pub_getAllAncestors({ c });
        EXPECT_TRUE(ancestors.count(p.get()));
        EXPECT_FALSE(ancestors.count(c.get())) << "Node should not be its own ancestor";
    }

    TEST_F(HypergraphInternalsTest, GetAllAncestors_DeepChain) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
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
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto a = g.createNode("a", 0, r);
        auto b = g.createNode("b", 0, r);
        // Both a and b share ancestor r
        auto ancestors = g.pub_getAllAncestors({ a, b });
        EXPECT_TRUE(ancestors.count(r.get()));
        EXPECT_FALSE(ancestors.count(a.get()));
        EXPECT_FALSE(ancestors.count(b.get()));
    }

    TEST_F(HypergraphInternalsTest, GetAllDescendants_SingleNode_DirectChild) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto desc = g.pub_getAllDescendants({ p });
        EXPECT_TRUE(desc.count(c.get()));
        EXPECT_FALSE(desc.count(p.get())) << "Node should not be its own descendant";
    }

    TEST_F(HypergraphInternalsTest, GetAllDescendants_DeepChain) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
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
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
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
        auto a = g.createNode("a", 0, nullptr, nullptr);
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
    // 15. resolveTargetLayer
    // =============================================================================

    TEST_F(HypergraphInternalsTest, ResolveTargetLayer_NoParentsNoOverride_ReturnsZero) {
        auto n = g.createNode("n", 0, nullptr, nullptr);
        EXPECT_EQ(g.pub_resolveTargetLayer(n), 0);
        EXPECT_EQ(n->getDesiredLayer(), -1);
    }

    TEST_F(HypergraphInternalsTest, ResolveTargetLayer_WithParent_ReturnsParentLayerPlusOne) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        EXPECT_EQ(g.pub_resolveTargetLayer(c), 1);
    }

    TEST_F(HypergraphInternalsTest, ResolveTargetLayer_MultipleParents_UsesDeepest) {
        auto p1 = g.createNode("p1", 0, nullptr, nullptr);
        auto p2 = g.createNode("p2", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p1);
        g.addConnection(p2, c);
        // Push p2 deeper so it becomes the binding parent for c's depth rule.
        g.relocateNodeToLayer(p2, 4);
        EXPECT_EQ(g.pub_resolveTargetLayer(c), 3);
    }

    TEST_F(HypergraphInternalsTest, ResolveTargetLayer_ActiveOverrideDeeperThanDepthRule_Honoured) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        g.relocateNodeToLayer(c, 3);
        ASSERT_EQ(c->getDesiredLayer(), 2);
        EXPECT_EQ(g.pub_resolveTargetLayer(c), 2);
        EXPECT_EQ(c->getDesiredLayer(), 2); // still valid, unchanged
    }

    TEST_F(HypergraphInternalsTest, ResolveTargetLayer_OverrideCaughtUpByDeepenedParent_ClearsOverride) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p); // depth rule 1
        g.relocateNodeToLayer(c, 3);      // override = 3
        // Manually deepen p to exactly the layer that makes the natural depth rule equal 3.
        g.pub_removeNodeFromLayer(p->getLayer(), p);
        g.pub_addNodeToLayer(2, -1, p);
        EXPECT_EQ(g.pub_resolveTargetLayer(c), 3);
        EXPECT_EQ(c->getDesiredLayer(), -1) << "Override should be cleared once the depth rule catches up to it";
    }

    TEST_F(HypergraphInternalsTest, ResolveTargetLayer_OverrideInvalidatedByDeeperParent_ClearsAndReturnsNewDepthRule) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p); // depth rule 1
        g.relocateNodeToLayer(c, 3);      // override = 3
        // Manually push p past the override, so the depth rule now exceeds it.
        g.pub_removeNodeFromLayer(p->getLayer(), p);
        g.pub_addNodeToLayer(5, -1, p);
        EXPECT_EQ(g.pub_resolveTargetLayer(c), 6);
        EXPECT_EQ(c->getDesiredLayer(), -1) << "Override should be discarded once it violates the depth rule";
    }

    // =============================================================================
    // 16. renumberLayersFrom
    // =============================================================================

    TEST_F(HypergraphInternalsTest, RenumberLayersFrom_EmptyGraph_NoOp) {
        EXPECT_NO_THROW(g.pub_renumberLayersFrom(0));
        EXPECT_EQ(g.getLayerCount(), 0);
    }

    TEST_F(HypergraphInternalsTest, RenumberLayersFrom_ShiftsLayersAtOrAfterUpByOne) {
        auto a = g.createNode("a", 0, nullptr, nullptr); // layer 0
        auto b = g.createNode("b", 0, a);       // layer 1
        g.pub_renumberLayersFrom(0);
        EXPECT_EQ(a->getLayer(), 1);
        EXPECT_EQ(b->getLayer(), 2);
        EXPECT_TRUE(layerContainsNode(g, 1, a));
        EXPECT_TRUE(layerContainsNode(g, 2, b));
        EXPECT_TRUE(g.getNodesAt(0).empty());
    }

    TEST_F(HypergraphInternalsTest, RenumberLayersFrom_LeavesLayersBelowFromLayerUntouched) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        g.pub_renumberLayersFrom(1); // shift everything >= 1
        EXPECT_EQ(a->getLayer(), 0); // untouched
        EXPECT_EQ(b->getLayer(), 2);
        EXPECT_EQ(c->getLayer(), 3);
    }

    TEST_F(HypergraphInternalsTest, RenumberLayersFrom_DoesNotAutoCreateFromLayerEntry) {
        // Documents the current contract: renumberLayersFrom shifts existing content out of
        // the way but does not, by itself, guarantee from_layer exists afterward if nothing
        // was already at or below it to leave behind.
        auto a = g.createNode("a", 0, nullptr, nullptr);
        g.pub_renumberLayersFrom(0);
        EXPECT_EQ(g.getLayerCount(), 1); // only the shifted layer (1) exists
        EXPECT_TRUE(g.getNodesAt(0).empty());
    }

    TEST_F(HypergraphInternalsTest, RenumberLayersFrom_UpdatesOutgoingEdgeLayer) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        auto edge = findOriginalEdgeWithSource(g, a);
        ASSERT_NE(edge, nullptr);
        ASSERT_EQ(edge->getLayer(), 0);
        g.pub_renumberLayersFrom(0);
        EXPECT_EQ(edge->getLayer(), 1);
    }

    TEST_F(HypergraphInternalsTest, RenumberLayersFrom_PreservesActiveOverrideValue) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        g.relocateNodeToLayer(c, 3);
        g.relocateNodeToLayer(c, 3);
        ASSERT_EQ(c->getDesiredLayer(), 3);
        g.pub_renumberLayersFrom(2); // c (at 3) is >= 2, so it shifts to 4
        EXPECT_EQ(c->getLayer(), 4);
        EXPECT_EQ(c->getDesiredLayer(), 4) << "An active override must track the node's new layer";
    }

    // =============================================================================
    // 17. compactLayerNumbers
    // =============================================================================

    TEST_F(HypergraphInternalsTest, CompactLayerNumbers_EmptyGraph_NoOp) {
        EXPECT_NO_THROW(g.pub_compactLayerNumbers());
        EXPECT_EQ(g.getLayerCount(), 0);
    }

    TEST_F(HypergraphInternalsTest, CompactLayerNumbers_AlreadyDense_NoOp) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        g.pub_compactLayerNumbers();
        EXPECT_EQ(a->getLayer(), 0);
        EXPECT_EQ(b->getLayer(), 1);
    }

    TEST_F(HypergraphInternalsTest, CompactLayerNumbers_ClosesGap_RelabelsToZeroBasedRank) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        g.pub_removeNodeFromLayer(0, a);
        g.pub_addNodeToLayer(5, -1, a); // a now sits alone at layer 5, a gap from 0
        ASSERT_EQ(g.getLayerCount(), 2);
        g.pub_compactLayerNumbers();
        EXPECT_EQ(a->getLayer(), 1);
        ASSERT_EQ(g.getLayerCount(), 2);
        EXPECT_TRUE(layerContainsNode(g, 1, a));
    }

    TEST_F(HypergraphInternalsTest, CompactLayerNumbers_MultipleGaps_PreservesRelativeOrder) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        g.pub_removeNodeFromLayer(0, a);
        g.pub_addNodeToLayer(3, -1, a);
        auto b = std::make_shared<Node>("b");
        g.rawNodes().push_back(b);
        g.pub_addNodeToLayer(7, -1, b);
        g.pub_compactLayerNumbers();
        EXPECT_EQ(a->getLayer(), 1);
        EXPECT_EQ(b->getLayer(), 2);
    }

    TEST_F(HypergraphInternalsTest, CompactLayerNumbers_LeavesAlreadyDensePrefixUntouched) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        g.relocateNodeToLayer(c, 3); // dense range {0,1,2,3} after the dummy-chain split
        g.relocateNodeToLayer(c, 3); // dense range {0,1,2,3} after the dummy-chain split
        ASSERT_EQ(c->getLayer(), 3);
        ASSERT_EQ(c->getDesiredLayer(), 3);

        // Open a gap strictly beyond the already-dense prefix.
        auto iso = std::make_shared<Node>("iso");
        g.rawNodes().push_back(iso);
        g.pub_addNodeToLayer(10, -1, iso);

        g.pub_compactLayerNumbers();

        EXPECT_EQ(c->getLayer(), 3) << "Already-dense prefix must be left alone";
        EXPECT_EQ(c->getDesiredLayer(), 3);
        EXPECT_EQ(iso->getLayer(), 4) << "Only the out-of-sequence node should be re-ranked";
    }

    // =============================================================================
    // 18. choosePositionForRelocatedNode
    // =============================================================================

    TEST_F(HypergraphInternalsTest, ChoosePositionForRelocatedNode_BaseClassDefault_AlwaysAppends) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
        EXPECT_EQ(g.pub_choosePositionForRelocatedNode(0, a), -1);
        EXPECT_EQ(g.pub_choosePositionForRelocatedNode(7, b), -1);
    }

    // =============================================================================
    // 19. cleanUp
    // =============================================================================

    TEST_F(HypergraphInternalsTest, CleanUp_EmptyGraph_NoOp) {
        EXPECT_NO_THROW(g.pub_cleanUp());
    }

    TEST_F(HypergraphInternalsTest, CleanUp_ErasesEmptyLayerAndRecompacts) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        g.pub_removeNodeFromLayer(0, a);
        g.pub_addNodeToLayer(5, -1, a); // layer 0 now empty of nodes and edges; gap to 5
        g.pub_cleanUp();
        EXPECT_EQ(g.getLayerCount(), 1);
        EXPECT_EQ(a->getLayer(), 0);
    }

    TEST_F(HypergraphInternalsTest, CleanUp_DoesNotEraseNonEmptyLayer) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        g.pub_cleanUp();
        EXPECT_EQ(g.getLayerCount(), 2);
        EXPECT_TRUE(layerContainsNode(g, 0, a));
        EXPECT_TRUE(layerContainsNode(g, 1, b));
    }

    // =============================================================================
    // 20. addNodeToLayer — out_min_new_layer reporting
    // =============================================================================

    TEST_F(HypergraphInternalsTest, AddNodeToLayer_ReportsPlacedLayer) {
        auto n = std::make_shared<Node>("n");
        g.rawNodes().push_back(n);
        int min_new_layer = INT_MAX;
        g.pub_addNodeToLayer(3, -1, n, &min_new_layer);
        EXPECT_EQ(min_new_layer, 3);
    }

    TEST_F(HypergraphInternalsTest, AddNodeToLayer_OnlyLowersNeverRaises) {
        auto n1 = std::make_shared<Node>("n1");
        auto n2 = std::make_shared<Node>("n2");
        g.rawNodes().push_back(n1);
        g.rawNodes().push_back(n2);
        int min_new_layer = 2; // pre-seeded, simulating an earlier step in a larger operation
        g.pub_addNodeToLayer(5, -1, n1, &min_new_layer);
        EXPECT_EQ(min_new_layer, 2) << "5 is not shallower than the pre-seeded 2, so it must stay";
        g.pub_addNodeToLayer(0, -1, n2, &min_new_layer);
        EXPECT_EQ(min_new_layer, 0) << "0 is shallower, so it must lower the accumulator";
    }

    TEST_F(HypergraphInternalsTest, AddNodeToLayer_DuplicateInSameLayer_DoesNotReport) {
        auto n = g.createNode("n", 0, nullptr, nullptr);
        int min_new_layer = INT_MAX;
        g.pub_addNodeToLayer(0, -1, n, &min_new_layer); // already there; early-return dedup
        EXPECT_EQ(min_new_layer, INT_MAX);
    }

    TEST_F(HypergraphInternalsTest, AddNodeToLayer_NullptrOutParam_NoCrash) {
        auto n = std::make_shared<Node>("n");
        g.rawNodes().push_back(n);
        EXPECT_NO_THROW(g.pub_addNodeToLayer(2, -1, n, nullptr));
    }

    // =============================================================================
    // 21. createHyperedge / addHyperedgeToLayer — out_altered_layers reporting
    // =============================================================================

    TEST_F(HypergraphInternalsTest, CreateHyperedge_PlacedAtRealLayer_Reports) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = std::make_shared<Node>("c");
        g.rawNodes().push_back(c);
        g.pub_addNodeToLayer(1, -1, c);
        std::set<int> altered;
        g.pub_createHyperedge({ p }, { c }, 0, &altered);
        EXPECT_EQ(altered, std::set<int>({ 0 }));
    }

    TEST_F(HypergraphInternalsTest, CreateHyperedge_UnplacedLayerNegative_DoesNotReport) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = std::make_shared<Node>("c");
        g.rawNodes().push_back(c);
        std::set<int> altered;
        g.pub_createHyperedge({ p }, { c }, -1, &altered);
        EXPECT_TRUE(altered.empty());
    }

    TEST_F(HypergraphInternalsTest, CreateHyperedge_WithOrigin_ReportsRealLayer) {
        auto d1 = std::make_shared<Node>();
        auto d2 = std::make_shared<Node>();
        g.rawNodes().push_back(d1);
        g.rawNodes().push_back(d2);
        g.pub_addNodeToLayer(0, -1, d1);
        g.pub_addNodeToLayer(1, -1, d2);
        std::set<int> altered;
        g.pub_createHyperedge(WeakHyperedgePtr{}, { d1 }, { d2 }, 0, &altered);
        EXPECT_EQ(altered, std::set<int>({ 0 }));
    }

    TEST_F(HypergraphInternalsTest, AddHyperedgeToLayer_NewPlacement_Reports) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = g.pub_createHyperedge({ p }, { c }, -1); // not placed yet
        std::set<int> altered;
        g.pub_addHyperedgeToLayer(0, edge, &altered);
        EXPECT_EQ(altered, std::set<int>({ 0 }));
    }

    TEST_F(HypergraphInternalsTest, AddHyperedgeToLayer_AlreadyThere_DedupDoesNotReport) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        ASSERT_EQ(edge->getLayer(), 0);
        std::set<int> altered;
        g.pub_addHyperedgeToLayer(0, edge, &altered); // already registered at layer 0
        EXPECT_TRUE(altered.empty());
    }

    // =============================================================================
    // 22. resyncSegmentEndpoints — out_altered_layers reporting
    // =============================================================================

    TEST_F(HypergraphInternalsTest, ResyncSegmentEndpoints_NoChange_DoesNotReport) {
        auto d = std::make_shared<Node>();
        auto t = std::make_shared<Node>();
        g.rawNodes().push_back(d);
        g.rawNodes().push_back(t);
        g.pub_addNodeToLayer(0, -1, d);
        g.pub_addNodeToLayer(1, -1, t);
        auto seg = g.pub_createHyperedge(WeakHyperedgePtr{}, { d }, { t }, 0);
        std::set<int> altered;
        g.pub_resyncSegmentEndpoints(seg, { d }, { t }, &altered);
        EXPECT_TRUE(altered.empty());
    }

    TEST_F(HypergraphInternalsTest, ResyncSegmentEndpoints_AddedTarget_ReportsSegmentLayer) {
        auto d = std::make_shared<Node>();
        auto t1 = std::make_shared<Node>();
        auto t2 = std::make_shared<Node>();
        g.rawNodes().push_back(d); g.rawNodes().push_back(t1); g.rawNodes().push_back(t2);
        g.pub_addNodeToLayer(0, -1, d);
        g.pub_addNodeToLayer(1, -1, t1);
        g.pub_addNodeToLayer(1, -1, t2);
        auto seg = g.pub_createHyperedge(WeakHyperedgePtr{}, { d }, { t1 }, 0);
        std::set<int> altered;
        g.pub_resyncSegmentEndpoints(seg, { d }, { t1, t2 }, &altered);
        EXPECT_EQ(altered, std::set<int>({ 0 })) << "Segment's own layer (0), not the target's layer";
    }

    TEST_F(HypergraphInternalsTest, ResyncSegmentEndpoints_RemovedTarget_StillReports) {
        // Per the corrected rule: removal from a surviving segment counts too, not just addition.
        auto s = std::make_shared<Node>();
        auto d1 = std::make_shared<Node>();
        auto d2 = std::make_shared<Node>();
        g.rawNodes().push_back(s); g.rawNodes().push_back(d1); g.rawNodes().push_back(d2);
        g.pub_addNodeToLayer(0, -1, s);
        g.pub_addNodeToLayer(1, -1, d1);
        g.pub_addNodeToLayer(1, -1, d2);
        auto seg = g.pub_createHyperedge(WeakHyperedgePtr{}, { s }, { d1, d2 }, 0);
        std::set<int> altered;
        g.pub_resyncSegmentEndpoints(seg, { s }, { d2 }, &altered);
        EXPECT_EQ(altered, std::set<int>({ 0 }));
    }

    TEST_F(HypergraphInternalsTest, ResyncSegmentEndpoints_ReportsSegmentLayerEvenDeepInTheGraph) {
        auto d = std::make_shared<Node>();
        auto t1 = std::make_shared<Node>();
        auto t2 = std::make_shared<Node>();
        g.rawNodes().push_back(d); g.rawNodes().push_back(t1); g.rawNodes().push_back(t2);
        g.pub_addNodeToLayer(4, -1, d);
        g.pub_addNodeToLayer(5, -1, t1);
        g.pub_addNodeToLayer(5, -1, t2);
        auto seg = g.pub_createHyperedge(WeakHyperedgePtr{}, { d }, { t1 }, 4);
        std::set<int> altered;
        g.pub_resyncSegmentEndpoints(seg, { d }, { t1, t2 }, &altered);
        EXPECT_EQ(altered, std::set<int>({ 4 }));
    }

    // =============================================================================
    // 23. splitLongEdge — combined out_min_new_layer / out_altered_layers reporting
    // =============================================================================

    TEST_F(HypergraphInternalsTest, SplitLongEdge_FreshSplit_ReportsAllNewDummyAndSegmentLayers) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n = std::make_shared<Node>("n");
        g.rawNodes().push_back(n);
        g.pub_addNodeToLayer(4, -1, n); // r at 0, n at 4: needs dummies at 1,2,3
        auto edge = g.pub_createHyperedge({ r }, { n }, -1);
        int min_new_layer = INT_MAX;
        std::set<int> altered;
        g.pub_splitLongEdge(edge, &min_new_layer, &altered);
        EXPECT_EQ(min_new_layer, 1) << "Shallowest new dummy is at layer 1";
        EXPECT_EQ(altered, std::set<int>({ 0, 1, 2, 3 })) << "Every segment's own layer (0..3) is reported";
    }

    TEST_F(HypergraphInternalsTest, SplitLongEdge_IdenticalResplit_ReusesEverythingAndReportsNothing) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n = std::make_shared<Node>("n");
        g.rawNodes().push_back(n);
        g.pub_addNodeToLayer(3, -1, n);
        auto edge = g.pub_createHyperedge({ r }, { n }, -1);
        g.pub_splitLongEdge(edge); // first split, no reporting needed here

        // Re-split with nothing actually changed: every dummy/segment is reused, not recreated.
        int min_new_layer = INT_MAX;
        std::set<int> altered;
        g.pub_splitLongEdge(edge, &min_new_layer, &altered);
        EXPECT_EQ(min_new_layer, INT_MAX) << "No new node was placed; every dummy was reused";
        EXPECT_TRUE(altered.empty()) << "No segment's source/target set actually changed";
    }

    TEST_F(HypergraphInternalsTest, SplitLongEdge_ShrinkingIntoShort_DoesNotReportStaleTeardown) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n = std::make_shared<Node>("n");
        g.rawNodes().push_back(n);
        g.pub_addNodeToLayer(4, -1, n);
        auto edge = g.pub_createHyperedge({ r }, { n }, -1);
        g.pub_splitLongEdge(edge); // dummies at 1,2,3

        // Move n much closer, shrinking the split down to a single hop 0->1.
        g.pub_removeNodeFromLayer(4, n);
        g.pub_addNodeToLayer(1, -1, n);
        int min_new_layer = INT_MAX;
        std::set<int> altered;
        g.pub_splitLongEdge(edge, &min_new_layer, &altered);
        EXPECT_EQ(min_new_layer, INT_MAX) << "Nothing NEW was placed; the edge only shrank";
        EXPECT_TRUE(altered.empty()) << "Tearing down stale segments/dummies is pure removal, exempt";
    }

    // =============================================================================
    // 24. settleEdgePlacement family / collapseToShortLayer / resettleEdge — reporting
    // =============================================================================

    TEST_F(HypergraphInternalsTest, SettleEdgePlacement_ShortEdge_ReportsItsLayerOnly) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = g.pub_createHyperedge({ p }, { c }, -1);
        int min_new_layer = INT_MAX;
        std::set<int> altered;
        int k = g.pub_settleEdgePlacement(edge, &min_new_layer, &altered);
        EXPECT_EQ(k, 0);
        EXPECT_EQ(min_new_layer, INT_MAX) << "No new node placed for a short edge";
        EXPECT_EQ(altered, std::set<int>({ 0 }));
    }

    TEST_F(HypergraphInternalsTest, SettleEdgePlacement_LongEdge_ReportsBothOutParams) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto n = std::make_shared<Node>("n");
        g.rawNodes().push_back(n);
        g.pub_addNodeToLayer(3, -1, n);
        auto edge = g.pub_createHyperedge({ p }, { n }, -1);
        int min_new_layer = INT_MAX;
        std::set<int> altered;
        int k = g.pub_settleEdgePlacement(edge, &min_new_layer, &altered);
        EXPECT_LT(k, 0);
        EXPECT_EQ(min_new_layer, 1);
        EXPECT_EQ(altered, std::set<int>({ 0, 1, 2 }));
    }

    TEST_F(HypergraphInternalsTest, CollapseToShortLayer_Reports) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto n = std::make_shared<Node>("n");
        g.rawNodes().push_back(n);
        g.pub_addNodeToLayer(3, -1, n);
        auto edge = g.pub_createHyperedge({ p }, { n }, -1);
        g.pub_splitLongEdge(edge);
        g.pub_removeNodeFromLayer(3, n);
        g.pub_addNodeToLayer(1, -1, n);
        std::set<int> altered;
        g.pub_collapseToShortLayer(edge, 0, &altered);
        EXPECT_EQ(altered, std::set<int>({ 0 }));
    }

    TEST_F(HypergraphInternalsTest, ResettleEdge_NothingChanged_DoesNotReport) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        ASSERT_EQ(edge->getLayer(), 0);
        std::set<int> altered;
        g.pub_resettleEdge(edge, nullptr, &altered); // already short at the correct layer
        EXPECT_TRUE(altered.empty());
    }

    TEST_F(HypergraphInternalsTest, ResettleEdge_BecomesLong_ReportsBothOutParams) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto c = g.createNode("c", 0, p);
        auto edge = findOriginalEdgeWithSource(g, p);
        ASSERT_NE(edge, nullptr);
        // Manually push c deeper, as if some relocation elsewhere already moved it.
        g.pub_removeNodeFromLayer(1, c);
        g.pub_addNodeToLayer(3, -1, c);
        int min_new_layer = INT_MAX;
        std::set<int> altered;
        g.pub_resettleEdge(edge, &min_new_layer, &altered);
        EXPECT_EQ(min_new_layer, 1);
        EXPECT_FALSE(altered.empty());
    }

    // =============================================================================
    // 25. relocateNodes / applyRelocationAndPropagate / removeTransitiveConnections /
    //     resolveOwnRedundantTargets — reporting and new signatures
    // =============================================================================

    TEST_F(HypergraphInternalsTest, RelocateNodes_ReportsShallowestTouchedLayer) {
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, root);
        g.pub_removeNodeFromLayer(1, n1);
        g.pub_addNodeToLayer(4, -1, n1); // wrong layer w.r.t. the depth rule; forces relocation
        int min_new_layer = INT_MAX;
        std::set<int> altered;
        bool moved = g.pub_relocateNodes({ n1 }, &min_new_layer, &altered);
        EXPECT_TRUE(moved);
        EXPECT_EQ(min_new_layer, 1);
    }

    TEST_F(HypergraphInternalsTest, RelocateNodes_NoMoveNeeded_DoesNotReport) {
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, root);
        int min_new_layer = INT_MAX;
        std::set<int> altered;
        bool moved = g.pub_relocateNodes({ n1 }, &min_new_layer, &altered);
        EXPECT_FALSE(moved);
        EXPECT_EQ(min_new_layer, INT_MAX);
        EXPECT_TRUE(altered.empty());
    }

    TEST_F(HypergraphInternalsTest, ApplyRelocationAndPropagate_CascadeReportsThroughDescendants) {
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, root);
        auto n2 = g.createNode("n2", 0, n1);
        int min_new_layer = INT_MAX;
        std::set<int> altered;
        g.pub_applyRelocationAndPropagate(n1, 4, &min_new_layer, &altered); // forces n2 to cascade to 5
        EXPECT_EQ(n2->getLayer(), 5);
        EXPECT_EQ(min_new_layer, 1);
        EXPECT_TRUE(altered.count(4) > 0);
    }

    TEST_F(HypergraphInternalsTest, RemoveTransitiveConnections_IsVoidAndReportsSplitOfTrimmedEdge) {
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);   // a->b, b at layer 1
        auto c = g.createNode("c", 0, b);   // b->c, c at layer 2; a->c would be transitively redundant
        auto d = std::make_shared<Node>("d");
        g.rawNodes().push_back(d);
        g.pub_addNodeToLayer(5, -1, d);      // unrelated, deep, non-redundant target

        auto edge = g.pub_createHyperedge({ a }, { c, d }, -1); // a->{c,d}: c redundant, d survives

        int min_new_layer = INT_MAX;
        std::set<int> altered;
        // Return type is void: this line must compile with no captured return value.
        g.pub_removeTransitiveConnections({ a }, { c }, nullptr, &min_new_layer, &altered);

        EXPECT_EQ(min_new_layer, 1) << "Trimmed edge a->{d} is long and splits; first dummy at layer 1";
        EXPECT_EQ(altered, std::set<int>({ 0, 1, 2, })) << "Every segment of the trimmed replacement";
    }

    TEST_F(HypergraphInternalsTest, ResolveOwnRedundantTargets_PointerSignature_ReportsOnDissolveAndReplace) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto target = std::make_shared<Node>("target");
        g.rawNodes().push_back(target);
        g.pub_addNodeToLayer(5, -1, target);
        auto b = g.createNode("b", 0, target); // b is target's child, placed at layer 6

        auto edge = g.pub_createHyperedge({ p }, { b }, -1); // p->b; b entirely redundant once target reaches it

        auto n0 = g.createNode("n0", -1, nullptr);
        auto n1 = g.createNode("n1", -1, n0);
        auto n2 = g.createNode("n1", -1, n1);
        auto n3 = g.createNode("n1", -1, n2);
        auto n4 = g.createNode("n1", -1, n3);
        g.addConnection(n4, target);

        int min_new_layer = INT_MAX;
        std::set<int> altered;
        HyperedgePtr replacement = g.pub_resolveOwnRedundantTargets(edge, target, &min_new_layer, &altered);

        ASSERT_NE(replacement, nullptr) << "edge's only target was fully redundant; must be replaced";
        EXPECT_TRUE(edgeHasSource(replacement, p));
        EXPECT_TRUE(edgeHasTarget(replacement, target));
        EXPECT_EQ(min_new_layer, 1);
        EXPECT_EQ(altered, std::set<int>({ 0, 1, 2, 3, 4 }));
    }

    TEST_F(HypergraphInternalsTest, ResolveOwnRedundantTargets_PartialSurvival_ReturnsNullptr) {
        auto p = g.createNode("p", 0, nullptr, nullptr);
        auto target = std::make_shared<Node>("target");
        g.rawNodes().push_back(target);
        g.pub_addNodeToLayer(5, -1, target);
        auto b = g.createNode("b", 0, target);   // redundant given target
        auto e = g.createNode("e", 0, p);        // unrelated to target, survives

        auto edge = g.pub_createHyperedge({ p }, { b, e }, -1);

        HyperedgePtr replacement = g.pub_resolveOwnRedundantTargets(edge, target, nullptr, nullptr);
        EXPECT_EQ(replacement, nullptr) << "e is not redundant, so edge survives rather than being replaced";
    }

    // =============================================================================
    // 26. INTENSIVE — complex topology and extreme cases for protected methods
    // =============================================================================

    // ---- splitLongEdge extreme cases ----------------------------------------------

    TEST_F(HypergraphInternalsTest, Stress_SplitLongEdge_FiveLayerGap) {
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto n4 = g.createNode("n4", 0, n3);
        auto n5 = g.createNode("n5", 0, n4);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);

        // Create a long edge spanning 5 layers
        auto edge = g.pub_createHyperedge({ r2 }, { n5 }, -1);
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
        auto s1 = g.createNode("s1", 0, nullptr, nullptr);
        auto s2 = g.createNode("s2", 0, nullptr, nullptr);
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto m1 = g.createNode("m1", 0, r);
        auto m2 = g.createNode("m2", 0, m1);
        auto t1 = g.createNode("t1", 0, m2);
        auto t2 = g.createNode("t2", 0, m2);

        auto edge = g.pub_createHyperedge({ s1, s2 }, { t1, t2 }, -1);
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
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
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
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
        auto r3 = g.createNode("r3", 0, nullptr, nullptr);
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
        auto a = g.createNode("a", 0, nullptr, nullptr);
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
        auto root = g.createNode("root", 0, nullptr, nullptr);
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
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);
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
        NodePtr prev = g.createNode("n0", 0, nullptr, nullptr);
        NodePtr root = prev;
        for (int i = 1; i < 50; i++)
            prev = g.createNode("n" + std::to_string(i), 0, prev);
        EXPECT_TRUE(g.pub_parentIsInAncestors(prev, root));
        EXPECT_FALSE(g.pub_parentIsInAncestors(root, prev));
    }

    TEST_F(HypergraphInternalsTest, Stress_ParentIsInAncestors_WideDiamond) {
        // one root -> 10 mid nodes -> one sink; root should be ancestor of sink
        auto root = g.createNode("root", 0, nullptr, nullptr);
        std::vector<NodePtr> mids;
        for (int i = 0; i < 10; i++)
            mids.push_back(g.createNode("m" + std::to_string(i), 0, root));
        auto sink = g.createNode("sink", 0, mids[0]);
        for (int i = 1; i < 10; i++) g.addConnection(mids[i], sink);

        EXPECT_TRUE(g.pub_parentIsInAncestors(sink, root));
        EXPECT_FALSE(g.pub_parentIsInAncestors(root, sink));
    }

    TEST_F(HypergraphInternalsTest, Stress_ChildIsInDescendants_BranchingTree_AllLeaves) {
        auto root = g.createNode("root", 0, nullptr, nullptr);
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
        auto a = g.createNode("a", 0, nullptr, nullptr);
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
        auto a = g.createNode("a", 0, nullptr, nullptr);
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
        auto root = g.createNode("root", 0, nullptr, nullptr);
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
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, nullptr, nullptr);
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
        auto a = g.createNode("a", 0, nullptr, nullptr);
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
        auto a = g.createNode("a", 0, nullptr, nullptr);
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
        auto a = g.createNode("a", 0, nullptr, nullptr);
        auto b = g.createNode("b", 0, a);
        auto c = g.createNode("c", 0, b);
        auto d = g.createNode("d", 0, c);
        auto e = g.createNode("e", 0, d);

        // Create direct a->e (bypassing validation for test purposes)
        auto direct_ae = g.pub_createHyperedge({ a }, { e }, -1);

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
        auto p1 = g.createNode("p1", 0, nullptr, nullptr);
        auto p2 = g.createNode("p2", 0, nullptr, nullptr);
        auto m = g.createNode("m", 0, p1);
        g.addConnection(p2, m);
        auto c1 = g.createNode("c1", 0, m);
        auto c2 = g.createNode("c2", 0, m);

        // Create direct (now transitive) edges p1->c1 and p2->c2
        g.pub_createHyperedge({ p1 }, { c1 }, -1);
        g.pub_createHyperedge({ p2 }, { c2 }, -1);

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
        auto r = g.createNode("r", 0, nullptr, nullptr);
        auto n1 = g.createNode("n1", 0, r);
        auto n2 = g.createNode("n2", 0, n1);
        auto n3 = g.createNode("n3", 0, n2);
        auto r2 = g.createNode("r2", 0, nullptr, nullptr);

        // Create a long edge and split it
        auto long_edge = g.pub_createHyperedge({ r2 }, { n3 }, -1);
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

    TEST_F(HypergraphInternalsTest, Stress_DesiredLayerOverride_SurvivesOrphaningWithoutLeavingGaps) {
        auto root = g.createNode("root", 0, nullptr, nullptr);
        auto child = g.createNode("child", 0, root);
        g.relocateNodeToLayer(child, 4); // valid override; creates a dummy chain through 1-3
        g.relocateNodeToLayer(child, 4); // valid override; creates a dummy chain through 1-3
        g.relocateNodeToLayer(child, 4); // valid override; creates a dummy chain through 1-3
        ASSERT_EQ(child->getLayer(), 4);
        ASSERT_EQ(child->getDesiredLayer(), 4);

        // Orphaning child (removing its only parent connection) leaves it parentless; its
        // override (4 > depth rule 0) should still be honoured, and no layer should be
        // left empty or non-dense as a result of the edge/dummy-chain teardown.
        g.removeConnection(root, child);

        EXPECT_TRUE(layersAreConsistentWithAllNodes(g));
        for (const auto& [layer, data] : g.getLayers()) {
            EXPECT_FALSE(data.nodes.empty() && data.outgoing_edges.empty())
                << "Layer " << layer << " should have been erased by cleanUp if left empty";
        }
        int expected = 0;
        for (const auto& [layer, data] : g.getLayers()) {
            EXPECT_EQ(layer, expected++) << "Layer numbering must remain dense from 0";
        }
    }

} // namespace hypergraph_logic::hypergraph_tests::internals