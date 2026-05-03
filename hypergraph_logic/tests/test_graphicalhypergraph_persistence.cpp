#include <gtest/gtest.h>
#include "GraphicalHypergraph.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace hypergraph_logic {
    namespace graphicalhypergraph_tests {
        namespace persistence {

            // =============================================================================
            // TestableGraphicalHypergraph
            // Exposes protected members needed for deep-inspection assertions.
            // =============================================================================
            class TestableGraphicalHypergraph : public GraphicalHypergraph {
            public:
                using GraphicalHypergraph::GraphicalHypergraph;

                // Allow constructing from a GraphicalHypergraph value (clone() or fromJSON()).
                explicit TestableGraphicalHypergraph(GraphicalHypergraph&& other)
                    : GraphicalHypergraph(std::move(other)) {
                }

                // Raw access for assertions
                const std::vector<NodePtr>& rawNodes() const { return all_nodes_; }
                const std::unordered_map<HyperedgePtr, std::vector<HyperedgePtr>, HyperedgePtrHash>&
                    rawEdges() const { return all_hyperedges_; }
                const std::map<int, LayerData>& rawLayers() const { return layers_; }
                const std::unordered_map<Node*, NodeLayout>& rawNodeLayout() const { return node_layout_; }
                const std::unordered_map<Hyperedge*, double>& rawEdgeLayout() const { return edge_layout_; }
                const std::unordered_map<int, double>& rawLayerLayout() const { return layer_layout_; }
            };

            // =============================================================================
            // Helpers
            // =============================================================================

            // Build a name->NodePtr map over all_nodes_ so assertions can find nodes by
            // name in the copy without relying on pointer equality across graphs.
            static std::unordered_map<std::string, NodePtr>
                nodesByName(const TestableGraphicalHypergraph& g) {
                std::unordered_map<std::string, NodePtr> m;
                for (const auto& n : g.rawNodes())
                    if (!n->isDummy())
                        m[n->getName()] = n;
                return m;
            }

            // Count all dummy nodes across all layers.
            static int countDummies(const TestableGraphicalHypergraph& g) {
                int c = 0;
                for (const auto& n : g.rawNodes()) if (n->isDummy()) ++c;
                return c;
            }

            // Count segment edges.
            static int countSegments(const TestableGraphicalHypergraph& g) {
                int c = 0;
                for (const auto& [orig, segs] : g.rawEdges()) c += static_cast<int>(segs.size());
                return c;
            }

            // Count original (non-segment) edges.
            static int countOriginals(const TestableGraphicalHypergraph& g) {
                return static_cast<int>(g.rawEdges().size());
            }

            // True if every segment edge spans exactly one layer.
            static bool allSegmentsAreShort(const TestableGraphicalHypergraph& g) {
                for (const auto& [orig, segs] : g.rawEdges()) {
                    for (const auto& seg : segs) {
                        for (const auto& s : seg->getSources())
                            for (const auto& t : seg->getTargets())
                                if (std::abs(s->getLayer() - t->getLayer()) != 1) return false;
                    }
                }
                return true;
            }

            // True if every node in every LayerData entry is also in all_nodes_.
            static bool layersConsistentWithAllNodes(const TestableGraphicalHypergraph& g) {
                std::unordered_set<Node*> in_all;
                for (const auto& n : g.rawNodes()) in_all.insert(n.get());
                for (const auto& [idx, data] : g.rawLayers())
                    for (const auto& n : data.nodes)
                        if (!in_all.count(n.get())) return false;
                return true;
            }

            // True if every node appears in exactly one layer.
            static bool eachNodeInExactlyOneLayer(const TestableGraphicalHypergraph& g) {
                std::unordered_map<Node*, int> seen;
                for (const auto& [idx, data] : g.rawLayers())
                    for (const auto& n : data.nodes) {
                        if (seen.count(n.get())) return false;
                        seen[n.get()] = idx;
                    }
                return true;
            }

            // True if node->getLayer() matches the layer it actually lives in.
            static bool layerFieldsMatchLayerMap(const TestableGraphicalHypergraph& g) {
                for (const auto& [idx, data] : g.rawLayers())
                    for (const auto& n : data.nodes)
                        if (n->getLayer() != idx) return false;
                return true;
            }

            // True if every edge in a layer's outgoing_edges is also in all_hyperedges_.
            static bool edgesConsistentWithAllEdges(const TestableGraphicalHypergraph& g) {
                std::unordered_set<Hyperedge*> known;
                for (const auto& [orig, segs] : g.rawEdges()) {
                    known.insert(orig.get());
                    for (const auto& s : segs) known.insert(s.get());
                }
                for (const auto& [idx, data] : g.rawLayers())
                    for (const auto& e : data.outgoing_edges)
                        if (!known.count(e.get())) return false;
                return true;
            }

            // True if the parent/child links of real nodes match what the original edges say.
            // For each original edge, every real source must have every real target as a child,
            // and every real target must have every real source as a parent.
            static bool realNodeLinksMatchOriginalEdges(const TestableGraphicalHypergraph& g) {
                for (const auto& [orig, segs] : g.rawEdges()) {
                    auto srcs = orig->getSources();
                    auto tgts = orig->getTargets();
                    for (const auto& src : srcs) {
                        if (src->isDummy()) continue;
                        auto children = src->getChildren();
                        for (const auto& tgt : tgts) {
                            if (tgt->isDummy()) continue;
                            if (std::find(children.begin(), children.end(), tgt) == children.end())
                                return false;
                        }
                    }
                    for (const auto& tgt : tgts) {
                        if (tgt->isDummy()) continue;
                        auto parents = tgt->getParents();
                        for (const auto& src : srcs) {
                            if (src->isDummy()) continue;
                            if (std::find(parents.begin(), parents.end(), src) == parents.end())
                                return false;
                        }
                    }
                }
                return true;
            }

            // True if dummy nodes' parent/child links are consistent with their segments.
            // For each segment, the asymmetric rule is checked:
            //   dummy->dummy : both sides must know each other.
            //   dummy->real  : only dummy must have real as child.
            //   real->dummy  : only dummy must have real as parent.
            static bool dummyLinksMatchSegments(const TestableGraphicalHypergraph& g) {
                for (const auto& [orig, segs] : g.rawEdges()) {
                    for (const auto& seg : segs) {
                        for (const auto& src : seg->getSources()) {
                            for (const auto& tgt : seg->getTargets()) {
                                bool sd = src->isDummy();
                                bool td = tgt->isDummy();
                                if (!sd && !td) continue; // real->real handled by original edge

                                auto src_children = src->getChildren();
                                auto tgt_parents = tgt->getParents();

                                if (sd && td) {
                                    if (std::find(src_children.begin(), src_children.end(), tgt) == src_children.end())
                                        return false;
                                    if (std::find(tgt_parents.begin(), tgt_parents.end(), src) == tgt_parents.end())
                                        return false;
                                }
                                else if (sd && !td) {
                                    // Only dummy (src) knows the real target.
                                    if (std::find(src_children.begin(), src_children.end(), tgt) == src_children.end())
                                        return false;
                                    // Real target must NOT have dummy as parent.
                                    if (std::find(tgt_parents.begin(), tgt_parents.end(), src) != tgt_parents.end())
                                        return false;
                                }
                                else { // !sd && td
                                    // Only dummy (tgt) knows the real source.
                                    if (std::find(tgt_parents.begin(), tgt_parents.end(), src) == tgt_parents.end())
                                        return false;
                                    // Real source must NOT have dummy as child.
                                    if (std::find(src_children.begin(), src_children.end(), tgt) != src_children.end())
                                        return false;
                                }
                            }
                        }
                    }
                }
                return true;
            }

            // True if every segment's origin weak_ptr resolves to a known original edge.
            static bool allSegmentOriginsValid(const TestableGraphicalHypergraph& g) {
                for (const auto& [orig, segs] : g.rawEdges()) {
                    for (const auto& seg : segs) {
                        auto locked = seg->getOrigin().lock();
                        if (!locked) return false;
                        if (locked != orig) return false;
                    }
                }
                return true;
            }

            // True if node_layout_ has an entry for every real and dummy node in all_nodes_.
            static bool nodeLayoutComplete(const TestableGraphicalHypergraph& g) {
                for (const auto& n : g.rawNodes())
                    if (g.rawNodeLayout().find(n.get()) == g.rawNodeLayout().end())
                        return false;
                return true;
            }

            // True if edge_layout_ has an entry for every original edge
            // (segments do not get their own entry).
            static bool edgeLayoutComplete(const TestableGraphicalHypergraph& g) {
                for (const auto& [orig, segs] : g.rawEdges()) {
                    if (segs.empty()) {
                        if (g.rawEdgeLayout().find(orig.get()) == g.rawEdgeLayout().end())
                            return false;
                    }
                    else {
                        if (g.rawEdgeLayout().find(orig.get()) != g.rawEdgeLayout().end())
                            return false;

						for (const auto& seg : segs) {
                            if (g.rawEdgeLayout().find(seg.get()) == g.rawEdgeLayout().end())
                                return false;
                        }
                    }
                }
                return true;
            }

            // True if every port's edge pointer in node_layout_ points to a known edge.
            static bool portEdgePointersValid(const TestableGraphicalHypergraph& g) {
                std::unordered_set<Hyperedge*> known;
                for (const auto& [orig, segs] : g.rawEdges()) {
                    known.insert(orig.get());
                    for (const auto& s : segs) known.insert(s.get());
                }
                for (const auto& [raw, layout] : g.rawNodeLayout()) {
                    for (const auto& port : layout.source_ports)
                        if (!known.count(port.edge)) return false;
                    for (const auto& port : layout.target_ports)
                        if (!known.count(port.edge)) return false;
                }
                return true;
            }

            // Compare two graphs for structural equality:
            // same layer count, same node names per layer (in order), same original edge
            // count, same segment count, same layer y-coordinates, same node x-coordinates
            // (matched by name), same edge y-coordinates (matched by source/target names).
            //
            // We do NOT compare pointer values — they are always different between the
            // original graph and any copy. Everything is compared by logical content.
            static void assertStructurallyEqual(
                const TestableGraphicalHypergraph& orig,
                const TestableGraphicalHypergraph& copy)
            {
                // Layer count
                ASSERT_EQ(orig.getLayerCount(), copy.getLayerCount());

                // Layer contents (node names in order, per layer)
                for (const auto& [idx, data] : orig.rawLayers()) {
                    auto copy_nodes = copy.getNodesAt(idx);
                    auto orig_nodes = orig.getNodesAt(idx);
                    ASSERT_EQ(orig_nodes.size(), copy_nodes.size())
                        << "Layer " << idx << " has different node count";
                    for (size_t i = 0; i < orig_nodes.size(); ++i) {
                        EXPECT_EQ(orig_nodes[i]->getName(), copy_nodes[i]->getName())
                            << "Node name mismatch at layer " << idx << " position " << i;
                        EXPECT_EQ(orig_nodes[i]->isDummy(), copy_nodes[i]->isDummy())
                            << "Dummy flag mismatch at layer " << idx << " position " << i;
                    }
                }

                // Edge counts
                EXPECT_EQ(countOriginals(orig), countOriginals(copy));
                EXPECT_EQ(countSegments(orig), countSegments(copy));
                EXPECT_EQ(countDummies(orig), countDummies(copy));

                // Outgoing edge order per layer (by source name sets, in order)
                for (const auto& [idx, data] : orig.rawLayers()) {
                    const auto& orig_edges = data.outgoing_edges;
                    const auto& copy_edges = copy.rawLayers().at(idx).outgoing_edges;
                    ASSERT_EQ(orig_edges.size(), copy_edges.size())
                        << "Outgoing edge count mismatch at layer " << idx;
                    for (size_t i = 0; i < orig_edges.size(); ++i) {
                        // Compare source name sets
                        std::vector<std::string> orig_src_names, copy_src_names;
                        for (const auto& s : orig_edges[i]->getSources())
                            orig_src_names.push_back(s->getName());
                        for (const auto& s : copy_edges[i]->getSources())
                            copy_src_names.push_back(s->getName());
                        std::sort(orig_src_names.begin(), orig_src_names.end());
                        std::sort(copy_src_names.begin(), copy_src_names.end());
                        EXPECT_EQ(orig_src_names, copy_src_names)
                            << "Edge source mismatch at layer " << idx << " position " << i;
                    }
                }

                // Node x-coordinates (matched by name)
                auto orig_by_name = nodesByName(orig);
                auto copy_by_name = nodesByName(copy);
                for (const auto& [name, orig_node] : orig_by_name) {
                    auto it = copy_by_name.find(name);
                    ASSERT_NE(it, copy_by_name.end()) << "Node '" << name << "' missing from copy";
                    double orig_x = orig.rawNodeLayout().at(orig_node.get()).x;
                    double copy_x = copy.rawNodeLayout().at(it->second.get()).x;
                    EXPECT_DOUBLE_EQ(orig_x, copy_x) << "x mismatch for node '" << name << "'";
                }

                // Layer y-coordinates
                ASSERT_EQ(orig.rawLayerLayout().size(), copy.rawLayerLayout().size());
                for (const auto& [idx, y] : orig.rawLayerLayout()) {
                    auto it = copy.rawLayerLayout().find(idx);
                    ASSERT_NE(it, copy.rawLayerLayout().end()) << "Layer " << idx << " missing from copy layout";
                    EXPECT_DOUBLE_EQ(y, it->second) << "y mismatch for layer " << idx;
                }
            }

            // Assert that a graph satisfies all internal invariants after a copy operation.
            static void assertAllInvariants(const TestableGraphicalHypergraph& g) {
                EXPECT_TRUE(layersConsistentWithAllNodes(g)) << "Layer/allNodes inconsistency";
                EXPECT_TRUE(eachNodeInExactlyOneLayer(g)) << "Node appears in multiple layers";
                EXPECT_TRUE(layerFieldsMatchLayerMap(g)) << "Node::getLayer() disagrees with layer map";
                EXPECT_TRUE(edgesConsistentWithAllEdges(g)) << "Edge in layer not in all_hyperedges_";
                EXPECT_TRUE(allSegmentsAreShort(g)) << "Segment edge is not short";
                EXPECT_TRUE(allSegmentOriginsValid(g)) << "Segment origin is expired or wrong";
                EXPECT_TRUE(realNodeLinksMatchOriginalEdges(g)) << "Real node parent/child links wrong";
                EXPECT_TRUE(dummyLinksMatchSegments(g)) << "Dummy node parent/child links wrong";
                EXPECT_TRUE(nodeLayoutComplete(g)) << "node_layout_ missing entries";
                EXPECT_TRUE(edgeLayoutComplete(g)) << "edge_layout_ missing entries";
                EXPECT_TRUE(portEdgePointersValid(g)) << "Port edge pointer is dangling";
            }

            // =============================================================================
            // RAII helper: writes a file in the constructor path, deletes it on destruction.
            // =============================================================================
            struct TempFile {
                std::string path;
                explicit TempFile(const std::string& name)
                    : path((std::filesystem::temp_directory_path() / name).string()) {
                }
                ~TempFile() { std::filesystem::remove(path); }
            };

            // =============================================================================
            // Fixture
            // Builds a fresh TestableGraphicalHypergraph and calls computeLayout() so that
            // all layout maps are populated before the persistence functions are tested.
            // =============================================================================
            class PersistenceTest : public ::testing::Test {
            protected:
                TestableGraphicalHypergraph g{ "test_graph" };

                // Helper: build a simple two-layer chain A->B and call computeLayout().
                void buildSimpleGraph() {
                    g.createNode("A", -1, nullptr);
                    g.createNode("B", -1, g.getNodesAt(0).front());
                    g.computeLayout();
                }

                // Helper: build a three-layer chain A->B->C with an additional root R->C
                // (long edge, triggers splitting) and call computeLayout().
                void buildGraphWithLongEdge() {
                    auto A = g.createNode("A", -1, nullptr);
                    auto B = g.createNode("B", -1, A);
                    auto C = g.createNode("C", -1, B);
                    auto R = g.createNode("R", -1, nullptr);
                    g.addConnection(R, C);  // long edge R->C, skips layer 1
                    g.computeLayout();
                }

                // Helper: build a hyperedge with multiple sources and targets.
                void buildHyperedgeGraph() {
                    auto S1 = g.createNode("S1", -1, nullptr);
                    auto S2 = g.createNode("S2", -1, nullptr);
                    auto T1 = g.createNode("T1", -1, S1);
                    g.addSourceToEdge(g.getLayerData(0).outgoing_edges.front(), S2);
                    auto T2 = g.createNode("T2", -1, nullptr);
                    g.addTargetToEdge(g.getLayerData(0).outgoing_edges.front(), T2);
                    g.computeLayout();
                }

                // Helper: build a diamond DAG: root->L, root->R, L->leaf, R->leaf.
                void buildDiamondGraph() {
                    auto root = g.createNode("root", -1, nullptr);
                    auto L = g.createNode("L", -1, root);
                    auto R = g.createNode("R", -1, root);
                    auto leaf = g.createNode("leaf", -1, L);
                    g.addConnection(R, leaf);
                    g.computeLayout();
                }
            };

            // =============================================================================
            // 1. clone() — structural invariants
            // =============================================================================

            TEST_F(PersistenceTest, Clone_EmptyGraph_ProducesEmptyGraph) {
                auto copy = TestableGraphicalHypergraph(g.clone());
                EXPECT_EQ(copy.getLayerCount(), 0);
                EXPECT_TRUE(copy.rawNodes().empty());
                EXPECT_TRUE(copy.rawEdges().empty());
            }

            TEST_F(PersistenceTest, Clone_SimpleGraph_AllInvariantsSatisfied) {
                buildSimpleGraph();
                auto copy = TestableGraphicalHypergraph(g.clone());
                assertAllInvariants(copy);
            }

            TEST_F(PersistenceTest, Clone_LongEdgeGraph_AllInvariantsSatisfied) {
                buildGraphWithLongEdge();
                auto copy = TestableGraphicalHypergraph(g.clone());
                assertAllInvariants(copy);
            }

            TEST_F(PersistenceTest, Clone_HyperedgeGraph_AllInvariantsSatisfied) {
                buildHyperedgeGraph();
                auto copy = TestableGraphicalHypergraph(g.clone());
                assertAllInvariants(copy);
            }

            TEST_F(PersistenceTest, Clone_DiamondGraph_AllInvariantsSatisfied) {
                buildDiamondGraph();
                auto copy = TestableGraphicalHypergraph(g.clone());
                assertAllInvariants(copy);
            }

            // =============================================================================
            // 2. clone() — independence (mutating the copy does not affect the original)
            // =============================================================================

            TEST_F(PersistenceTest, Clone_IsDeepCopy_NodePointersDistinct) {
                buildSimpleGraph();
                auto copy = TestableGraphicalHypergraph(g.clone());

                for (const auto& orig_node : g.rawNodes())
                    for (const auto& copy_node : copy.rawNodes())
                        EXPECT_NE(orig_node.get(), copy_node.get())
                        << "clone() shared a Node pointer with the original";
            }

            TEST_F(PersistenceTest, Clone_IsDeepCopy_EdgePointersDistinct) {
                buildSimpleGraph();
                auto copy = TestableGraphicalHypergraph(g.clone());

                std::unordered_set<Hyperedge*> orig_edges;
                for (const auto& [e, segs] : g.rawEdges()) {
                    orig_edges.insert(e.get());
                    for (const auto& s : segs) orig_edges.insert(s.get());
                }
                for (const auto& [e, segs] : copy.rawEdges()) {
                    EXPECT_EQ(orig_edges.count(e.get()), 0u)
                        << "clone() shared an original edge pointer";
                    for (const auto& s : segs)
                        EXPECT_EQ(orig_edges.count(s.get()), 0u)
                        << "clone() shared a segment edge pointer";
                }
            }

            TEST_F(PersistenceTest, Clone_IsDeepCopy_MutatingCopyDoesNotAffectOriginal) {
                buildSimpleGraph();
                auto copy = TestableGraphicalHypergraph(g.clone());

                int orig_node_count = static_cast<int>(g.rawNodes().size());
                copy.createNode("extra", -1, nullptr);
                EXPECT_EQ(static_cast<int>(g.rawNodes().size()), orig_node_count)
                    << "Adding a node to the clone affected the original";
            }

            TEST_F(PersistenceTest, Clone_IsDeepCopy_PortEdgePointersPointIntoCopy) {
                buildGraphWithLongEdge();
                auto copy = TestableGraphicalHypergraph(g.clone());

                // Collect all edge raw pointers in the copy.
                std::unordered_set<Hyperedge*> copy_edges;
                for (const auto& [e, segs] : copy.rawEdges()) {
                    copy_edges.insert(e.get());
                    for (const auto& s : segs) copy_edges.insert(s.get());
                }
                // Every port in the copy must point into the copy, not the original.
                for (const auto& [raw, layout] : copy.rawNodeLayout()) {
                    for (const auto& port : layout.source_ports)
                        EXPECT_TRUE(copy_edges.count(port.edge))
                        << "source_port in clone points outside the clone";
                    for (const auto& port : layout.target_ports)
                        EXPECT_TRUE(copy_edges.count(port.edge))
                        << "target_port in clone points outside the clone";
                }
            }

            // =============================================================================
            // 3. clone() — structural equality with original
            // =============================================================================

            TEST_F(PersistenceTest, Clone_SimpleGraph_StructurallyEqualToOriginal) {
                buildSimpleGraph();
                auto copy = TestableGraphicalHypergraph(g.clone());
                assertStructurallyEqual(g, copy);
            }

            TEST_F(PersistenceTest, Clone_LongEdgeGraph_StructurallyEqualToOriginal) {
                buildGraphWithLongEdge();
                auto copy = TestableGraphicalHypergraph(g.clone());
                assertStructurallyEqual(g, copy);
            }

            TEST_F(PersistenceTest, Clone_DiamondGraph_StructurallyEqualToOriginal) {
                buildDiamondGraph();
                auto copy = TestableGraphicalHypergraph(g.clone());
                assertStructurallyEqual(g, copy);
            }

            TEST_F(PersistenceTest, Clone_SegmentOriginsPointIntoClone) {
                buildGraphWithLongEdge();
                auto copy = TestableGraphicalHypergraph(g.clone());

                // Collect original-edge raw pointers from the copy.
                std::unordered_set<Hyperedge*> copy_originals;
                for (const auto& [e, segs] : copy.rawEdges())
                    copy_originals.insert(e.get());

                // Every segment's origin must resolve to one of those, not the original graph's.
                for (const auto& [e, segs] : copy.rawEdges()) {
                    for (const auto& seg : segs) {
                        auto locked = seg->getOrigin().lock();
                        ASSERT_TRUE(locked) << "Segment origin is expired in clone";
                        EXPECT_TRUE(copy_originals.count(locked.get()))
                            << "Segment origin in clone resolves to an edge outside the clone";
                    }
                }
            }

            // =============================================================================
            // 4. clone() — dummy node parent/child links
            // =============================================================================

            TEST_F(PersistenceTest, Clone_DummyLinks_DummyToDummyBothSidesKnowEachOther) {
                buildGraphWithLongEdge();  // guarantees at least one dummy chain
                auto copy = TestableGraphicalHypergraph(g.clone());
                EXPECT_TRUE(dummyLinksMatchSegments(copy));
            }

            TEST_F(PersistenceTest, Clone_DummyLinks_RealNodesUnaware) {
                buildGraphWithLongEdge();
                auto copy = TestableGraphicalHypergraph(g.clone());

                // No real node in the copy should have a dummy as a parent or child.
                for (const auto& n : copy.rawNodes()) {
                    if (n->isDummy()) continue;
                    for (const auto& p : n->getParents())
                        EXPECT_FALSE(p->isDummy())
                        << "Real node '" << n->getName() << "' has a dummy parent";
                    for (const auto& c : n->getChildren())
                        EXPECT_FALSE(c->isDummy())
                        << "Real node '" << n->getName() << "' has a dummy child";
                }
            }

            // =============================================================================
            // 5. toJSON() + fromJSON() — round-trip invariants
            // =============================================================================

            TEST_F(PersistenceTest, RoundTrip_EmptyGraph_ProducesEmptyGraph) {
                TempFile tmp("empty_graph.json");
                g.toJSON(tmp.path);
                auto loaded = TestableGraphicalHypergraph(
                    GraphicalHypergraph::fromJSON(tmp.path));
                EXPECT_EQ(loaded.getLayerCount(), 0);
                EXPECT_TRUE(loaded.rawNodes().empty());
                EXPECT_TRUE(loaded.rawEdges().empty());
            }

            TEST_F(PersistenceTest, RoundTrip_SimpleGraph_AllInvariantsSatisfied) {
                buildSimpleGraph();
                TempFile tmp("simple_graph.json");
                g.toJSON(tmp.path);
                auto loaded = TestableGraphicalHypergraph(
                    GraphicalHypergraph::fromJSON(tmp.path));
                assertAllInvariants(loaded);
            }

            TEST_F(PersistenceTest, RoundTrip_LongEdgeGraph_AllInvariantsSatisfied) {
                buildGraphWithLongEdge();
                TempFile tmp("long_edge_graph.json");
                g.toJSON(tmp.path);
                auto loaded = TestableGraphicalHypergraph(
                    GraphicalHypergraph::fromJSON(tmp.path));
                assertAllInvariants(loaded);
            }

            TEST_F(PersistenceTest, RoundTrip_HyperedgeGraph_AllInvariantsSatisfied) {
                buildHyperedgeGraph();
                TempFile tmp("hyperedge_graph.json");
                g.toJSON(tmp.path);
                auto loaded = TestableGraphicalHypergraph(
                    GraphicalHypergraph::fromJSON(tmp.path));
                assertAllInvariants(loaded);
            }

            TEST_F(PersistenceTest, RoundTrip_DiamondGraph_AllInvariantsSatisfied) {
                buildDiamondGraph();
                TempFile tmp("diamond_graph.json");
                g.toJSON(tmp.path);
                auto loaded = TestableGraphicalHypergraph(
                    GraphicalHypergraph::fromJSON(tmp.path));
                assertAllInvariants(loaded);
            }

            // =============================================================================
            // 6. toJSON() + fromJSON() — structural equality
            // =============================================================================

            TEST_F(PersistenceTest, RoundTrip_SimpleGraph_StructurallyEqualToOriginal) {
                buildSimpleGraph();
                TempFile tmp("simple_eq.json");
                g.toJSON(tmp.path);
                auto loaded = TestableGraphicalHypergraph(
                    GraphicalHypergraph::fromJSON(tmp.path));
                assertStructurallyEqual(g, loaded);
            }

            TEST_F(PersistenceTest, RoundTrip_LongEdgeGraph_StructurallyEqualToOriginal) {
                buildGraphWithLongEdge();
                TempFile tmp("long_edge_eq.json");
                g.toJSON(tmp.path);
                auto loaded = TestableGraphicalHypergraph(
                    GraphicalHypergraph::fromJSON(tmp.path));
                assertStructurallyEqual(g, loaded);
            }

            TEST_F(PersistenceTest, RoundTrip_DiamondGraph_StructurallyEqualToOriginal) {
                buildDiamondGraph();
                TempFile tmp("diamond_eq.json");
                g.toJSON(tmp.path);
                auto loaded = TestableGraphicalHypergraph(
                    GraphicalHypergraph::fromJSON(tmp.path));
                assertStructurallyEqual(g, loaded);
            }

            TEST_F(PersistenceTest, RoundTrip_GraphName_Preserved) {
                buildSimpleGraph();
                TempFile tmp("name_preserved.json");
                g.toJSON(tmp.path);
                struct NameReader : public GraphicalHypergraph {
                    explicit NameReader(GraphicalHypergraph&& other)
                        : GraphicalHypergraph(std::move(other)) {
                    }
                    const std::string& pubName() const { return name_; }
                };
                auto loaded = NameReader(GraphicalHypergraph::fromJSON(tmp.path));
                EXPECT_EQ(loaded.pubName(), "test_graph");
            }

            // =============================================================================
            // 7. toJSON() + fromJSON() — dummy and segment specifics
            // =============================================================================

            TEST_F(PersistenceTest, RoundTrip_DummyCount_Preserved) {
                buildGraphWithLongEdge();
                TempFile tmp("dummy_count.json");
                g.toJSON(tmp.path);
                auto loaded = TestableGraphicalHypergraph(
                    GraphicalHypergraph::fromJSON(tmp.path));
                EXPECT_EQ(countDummies(g), countDummies(loaded));
            }

            TEST_F(PersistenceTest, RoundTrip_SegmentCount_Preserved) {
                buildGraphWithLongEdge();
                TempFile tmp("seg_count.json");
                g.toJSON(tmp.path);
                auto loaded = TestableGraphicalHypergraph(
                    GraphicalHypergraph::fromJSON(tmp.path));
                EXPECT_EQ(countSegments(g), countSegments(loaded));
            }

            TEST_F(PersistenceTest, RoundTrip_DummyLinks_DummyToDummyBothSidesKnowEachOther) {
                buildGraphWithLongEdge();
                TempFile tmp("dummy_links.json");
                g.toJSON(tmp.path);
                auto loaded = TestableGraphicalHypergraph(
                    GraphicalHypergraph::fromJSON(tmp.path));
                EXPECT_TRUE(dummyLinksMatchSegments(loaded));
            }

            TEST_F(PersistenceTest, RoundTrip_DummyLinks_RealNodesUnaware) {
                buildGraphWithLongEdge();
                TempFile tmp("real_unaware.json");
                g.toJSON(tmp.path);
                auto loaded = TestableGraphicalHypergraph(
                    GraphicalHypergraph::fromJSON(tmp.path));

                for (const auto& n : loaded.rawNodes()) {
                    if (n->isDummy()) continue;
                    for (const auto& p : n->getParents())
                        EXPECT_FALSE(p->isDummy())
                        << "Real node '" << n->getName() << "' has a dummy parent after fromJSON";
                    for (const auto& c : n->getChildren())
                        EXPECT_FALSE(c->isDummy())
                        << "Real node '" << n->getName() << "' has a dummy child after fromJSON";
                }
            }

            TEST_F(PersistenceTest, RoundTrip_SegmentOriginsValid) {
                buildGraphWithLongEdge();
                TempFile tmp("seg_origins.json");
                g.toJSON(tmp.path);
                auto loaded = TestableGraphicalHypergraph(
                    GraphicalHypergraph::fromJSON(tmp.path));
                EXPECT_TRUE(allSegmentOriginsValid(loaded));
            }

            // =============================================================================
            // 8. toJSON() error handling
            // =============================================================================

            TEST_F(PersistenceTest, ToJSON_BadPath_ThrowsRuntimeError) {
                buildSimpleGraph();
                EXPECT_THROW(g.toJSON("/nonexistent_dir/file.json"), std::runtime_error);
            }

            // =============================================================================
            // 9. fromJSON() error handling
            // =============================================================================

            TEST_F(PersistenceTest, FromJSON_NonexistentFile_ThrowsRuntimeError) {
                EXPECT_THROW(
                    GraphicalHypergraph::fromJSON("/nonexistent_dir/no_such_file.json"),
                    std::runtime_error);
            }

            TEST_F(PersistenceTest, FromJSON_MalformedJSON_ThrowsRuntimeError) {
                TempFile tmp("malformed.json");
                { std::ofstream f(tmp.path); f << "{ this is not valid json :::"; }
                EXPECT_THROW(GraphicalHypergraph::fromJSON(tmp.path), std::runtime_error);
            }

            TEST_F(PersistenceTest, FromJSON_EmptyFile_ThrowsRuntimeError) {
                TempFile tmp("empty.json");
                { std::ofstream f(tmp.path); }  // write nothing
                EXPECT_THROW(GraphicalHypergraph::fromJSON(tmp.path), std::runtime_error);
            }

            // =============================================================================
            // 10. Double round-trip: toJSON -> fromJSON -> toJSON -> fromJSON
            // The second loaded graph must equal the first.
            // =============================================================================

            TEST_F(PersistenceTest, DoubleRoundTrip_SimpleGraph_StableUnderRepeatedSerialization) {
                buildSimpleGraph();
                TempFile tmp1("double_rt_1.json");
                TempFile tmp2("double_rt_2.json");

                g.toJSON(tmp1.path);
                auto loaded1 = TestableGraphicalHypergraph(GraphicalHypergraph::fromJSON(tmp1.path));
                loaded1.toJSON(tmp2.path);
                auto loaded2 = TestableGraphicalHypergraph(GraphicalHypergraph::fromJSON(tmp2.path));

                assertAllInvariants(loaded2);
                assertStructurallyEqual(loaded1, loaded2);
            }

            TEST_F(PersistenceTest, DoubleRoundTrip_LongEdgeGraph_StableUnderRepeatedSerialization) {
                buildGraphWithLongEdge();
                TempFile tmp1("double_rt_long_1.json");
                TempFile tmp2("double_rt_long_2.json");

                g.toJSON(tmp1.path);
                auto loaded1 = TestableGraphicalHypergraph(GraphicalHypergraph::fromJSON(tmp1.path));
                loaded1.toJSON(tmp2.path);
                auto loaded2 = TestableGraphicalHypergraph(GraphicalHypergraph::fromJSON(tmp2.path));

                assertAllInvariants(loaded2);
                assertStructurallyEqual(loaded1, loaded2);
            }

            // =============================================================================
            // 11. clone() vs toJSON+fromJSON agreement
            // Both operations must produce graphs that are structurally equal to each other.
            // =============================================================================

            TEST_F(PersistenceTest, CloneAndRoundTrip_ProduceStructurallyEqualGraphs) {
                buildGraphWithLongEdge();
                TempFile tmp("clone_vs_json.json");

                auto cloned = TestableGraphicalHypergraph(g.clone());
                g.toJSON(tmp.path);
                auto loaded = TestableGraphicalHypergraph(
                    GraphicalHypergraph::fromJSON(tmp.path));

                assertStructurallyEqual(cloned, loaded);
            }

        } // namespace persistence
    } // namespace graphicalhypergraph_tests
} // namespace hypergraph_logic