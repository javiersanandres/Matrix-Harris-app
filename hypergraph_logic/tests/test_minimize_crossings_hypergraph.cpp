#include "GlobalSifting.h"
#include "GraphicalHypergraph.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <vector>

namespace hypergraph_logic {
    namespace graphicalhypergraph_tests {
        namespace minimizeCrossings {

            using namespace sifting_internal;

            // ============================================================================
            // Fixture: exposes the protected layers_ map so tests can construct a
            // GlobalSifter directly without going through minimizeCrossings().
            // ============================================================================

            class TestGraph : public GraphicalHypergraph {
            public:
                explicit TestGraph(const std::string& name) : GraphicalHypergraph(name) {}
                std::map<int, LayerData>& layers() { return layers_; }
                int minimizeCrossingsForNodes(const std::vector<Node*>& nodes, int start_layer, int end_layer) {
					return GraphicalHypergraph::minimizeCrossingsForNodes(nodes, start_layer, end_layer);
                }
                int sift(int rounds, int start_layer, int end_layer = -1, CrossingSeed seed = CrossingSeed::KeepOrder) {
                    return Hypergraph::minimizeCrossings(rounds, start_layer, end_layer, seed);
                }
                void placeUnpositioned(int first, int last) { placeUnpositionedNodes(first, last); }
                std::optional<double> guess(const Node* n) const { return guessX(n, {}); }
                bool hasValidLayout(const Node* n) const { return validLayout(n) != nullptr; }
                void forgetLayout(Node* n) { node_layout_.erase(n); }
                void setLayoutOwner(Node* key, const NodePtr& owner) { node_layout_[key].node = owner; }
            };

            // ============================================================================
            // Helpers
            // ============================================================================

            // Count how many G1 nodes have original == nullptr (i.e. hub nodes).
            static int countHubs(const GlobalSifter& sifter) {
                int n = 0;
                for (const auto& gn : sifter.S_.g1_nodes)
                    if (gn.original == nullptr) ++n;
                return n;
            }

            // Count how many G1 nodes have a non-null original (real/dummy nodes).
            static int countRealNodes(const GlobalSifter& sifter) {
                int n = 0;
                for (const auto& gn : sifter.S_.g1_nodes)
                    if (gn.original != nullptr) ++n;
                return n;
            }

            static HyperedgePtr findEdgeWithSourceAndTarget(const TestGraph& g,
                const NodePtr& s, const NodePtr& t) {
                for (const auto& e : g.getAllHyperedges()) {
                    if (!e->isSegment() && e->containsSource(s) && e->containsTarget(t)) return e;
                }
                return nullptr;
            }

            // Return the last hypergraph layer index in the graph.
            static int lastLayer(TestGraph& G) {
                return static_cast<int>(G.layers().rbegin()->first);
            }

            // Return the position of node in its layer (0-based) after writeBack.
            static int positionInLayer(TestGraph& G, Node* node) {
                for (const auto& [layer, data] : G.layers()) {
                    for (int i = 0; i < static_cast<int>(data.nodes.size()); ++i)
                        if (data.nodes[i].get() == node) return i;
                }
                return -1;
            }

            // ============================================================================
            // buildG1 — two-layer graph, single binary edge
            // ============================================================================

            class BuildG1_TwoLayers : public ::testing::Test {
            protected:
                void SetUp() override {
                    A = G.createNode("A", 0, 0, nullptr);
                    B = G.createNode("B", A->getLayer() + 1, 0, A);
                }
                TestGraph G{ "TwoLayers" };
                NodePtr A, B;
            };

            TEST_F(BuildG1_TwoLayers, NodeCountIncludesHub) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                // 2 real nodes + 1 hub = 3 G1 nodes
                EXPECT_EQ(sifter.S_.g1_nodes.size(), 3u);
            }

            TEST_F(BuildG1_TwoLayers, ExactlyOneHub) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                EXPECT_EQ(countHubs(sifter), 1);
                EXPECT_EQ(countRealNodes(sifter), 2);
            }

            TEST_F(BuildG1_TwoLayers, NodesAssignedToCorrectG1Layers) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                int a_idx = sifter.S_.node_to_g1.at(A.get());
                EXPECT_EQ(sifter.S_.g1_nodes[a_idx].g1_layer, 0);
                int b_idx = sifter.S_.node_to_g1.at(B.get());
                EXPECT_EQ(sifter.S_.g1_nodes[b_idx].g1_layer, 2);
                EXPECT_TRUE(sifter.S_.g1_layers.count(1));
                EXPECT_EQ(sifter.S_.g1_layers.at(1).size(), 1u);
                EXPECT_EQ(sifter.S_.g1_nodes[sifter.S_.g1_layers.at(1)[0]].g1_layer, 1);
            }

            TEST_F(BuildG1_TwoLayers, AdjacencyCorrect) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                int a_idx = sifter.S_.node_to_g1.at(A.get());
                int b_idx = sifter.S_.node_to_g1.at(B.get());
                int hub_idx = sifter.S_.g1_layers.at(1)[0];

                ASSERT_EQ(sifter.S_.g1_out[a_idx].size(), 1u);
                EXPECT_EQ(sifter.S_.g1_out[a_idx][0], hub_idx);
                ASSERT_EQ(sifter.S_.g1_out[hub_idx].size(), 1u);
                EXPECT_EQ(sifter.S_.g1_out[hub_idx][0], b_idx);
                ASSERT_EQ(sifter.S_.g1_in[hub_idx].size(), 1u);
                EXPECT_EQ(sifter.S_.g1_in[hub_idx][0], a_idx);
                ASSERT_EQ(sifter.S_.g1_in[b_idx].size(), 1u);
                EXPECT_EQ(sifter.S_.g1_in[b_idx][0], hub_idx);
            }

            TEST_F(BuildG1_TwoLayers, NodeToG1MapsAllRealNodes) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                EXPECT_TRUE(sifter.S_.node_to_g1.count(A.get()));
                EXPECT_TRUE(sifter.S_.node_to_g1.count(B.get()));
            }

            // ============================================================================
            // buildG1 — three-layer graph, two edges, one hub per edge
            // ============================================================================

            class BuildG1_ThreeLayers : public ::testing::Test {
            protected:
                void SetUp() override {
                    A = G.createNode("A", 0, 0, nullptr);
                    B = G.createNode("B", 0, 1, nullptr);
                    C = G.createNode("C", A->getLayer() + 1, 0, A);
                    G.addConnection(B, C);
                    D = G.createNode("D", C->getLayer() + 1, 0, C);
                }
                TestGraph G{ "ThreeLayers" };
                NodePtr A, B, C, D;
            };

            TEST_F(BuildG1_ThreeLayers, G1LayerKeysCorrect) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                EXPECT_TRUE(sifter.S_.g1_layers.count(0));
                EXPECT_TRUE(sifter.S_.g1_layers.count(1));
                EXPECT_TRUE(sifter.S_.g1_layers.count(2));
                EXPECT_TRUE(sifter.S_.g1_layers.count(3));
                EXPECT_TRUE(sifter.S_.g1_layers.count(4));
            }

            TEST_F(BuildG1_ThreeLayers, HubCountMatchesEdgeCount) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                EXPECT_EQ(countHubs(sifter), 3);
            }

            TEST_F(BuildG1_ThreeLayers, RealNodeCountMatchesHypergraphNodes) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                EXPECT_EQ(countRealNodes(sifter), 4); // A, B, C, D
            }

            TEST_F(BuildG1_ThreeLayers, CNodeHasTwoIncomingHubs) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                int c_idx = sifter.S_.node_to_g1.at(C.get());
                EXPECT_EQ(sifter.S_.g1_in[c_idx].size(), 2u);
            }

            // ============================================================================
            // buildG1 — start_layer anchor behaviour
            // ============================================================================

            class BuildG1_StartLayer : public ::testing::Test {
            protected:
                void SetUp() override {
                    A = G.createNode("A", 0, 0, nullptr);
                    B = G.createNode("B", A->getLayer() + 1, 0, A);
                    C = G.createNode("C", B->getLayer() + 1, 0, B);
                }
                TestGraph G{ "StartLayer" };
                NodePtr A, B, C;
            };

            TEST_F(BuildG1_StartLayer, StartLayerZero_AllNodesRegistered) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                EXPECT_TRUE(sifter.S_.node_to_g1.count(A.get()));
                EXPECT_TRUE(sifter.S_.node_to_g1.count(B.get()));
                EXPECT_TRUE(sifter.S_.node_to_g1.count(C.get()));
            }

            TEST_F(BuildG1_StartLayer, StartLayerOne_AnchorIncluded) {
                GlobalSifter sifter(1, lastLayer(G), G.layers());
                EXPECT_TRUE(sifter.S_.node_to_g1.count(A.get()));
                EXPECT_TRUE(sifter.S_.node_to_g1.count(B.get()));
                EXPECT_TRUE(sifter.S_.node_to_g1.count(C.get()));
            }

            TEST_F(BuildG1_StartLayer, StartLayerOne_OnlyAnchorBlocksAreFixed) {
                GlobalSifter sifter(1, lastLayer(G), G.layers());
                for (const auto& blk : sifter.S_.blocks) {
                    const bool in_anchor = sifter.S_.g1_nodes[blk.upper()].g1_layer == 0;
                    EXPECT_EQ(blk.movable, !in_anchor);
                }
            }

            TEST_F(BuildG1_StartLayer, StartLayerZero_EveryBlockIsMovable) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                for (const auto& blk : sifter.S_.blocks)
                    EXPECT_TRUE(blk.movable);
            }

            // ============================================================================
            // buildG1 — end_layer restricts the window
            // ============================================================================

            class BuildG1_EndLayer : public ::testing::Test {
            protected:
                void SetUp() override {
                    // Layer 0: A, Layer 1: B, Layer 2: C, Layer 3: D
                    A = G.createNode("A", 0, 0, nullptr);
                    B = G.createNode("B", A->getLayer() + 1, 0, A);
                    C = G.createNode("C", B->getLayer() + 1, 0, B);
                    D = G.createNode("D", C->getLayer() + 1, 0, C);
                }
                TestGraph G{ "EndLayer" };
                NodePtr A, B, C, D;
            };

            TEST_F(BuildG1_EndLayer, NodesAboveEndLayerExcluded) {
                // end_layer=1: only layers 0 and 1 (A, B) are siftable;
                // layer 2 (C) is included as a read-ahead for crossing info but
                // D (layer 3) must not appear in node_to_g1.
                GlobalSifter sifter(0, 1, G.layers());
                EXPECT_TRUE(sifter.S_.node_to_g1.count(A.get()));
                EXPECT_TRUE(sifter.S_.node_to_g1.count(B.get()));
                EXPECT_TRUE(sifter.S_.node_to_g1.count(C.get())); // read-ahead layer
                EXPECT_FALSE(sifter.S_.node_to_g1.count(D.get()));
            }

            TEST_F(BuildG1_EndLayer, EndLayerEqualsLastLayer_AllNodesRegistered) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                EXPECT_TRUE(sifter.S_.node_to_g1.count(A.get()));
                EXPECT_TRUE(sifter.S_.node_to_g1.count(B.get()));
                EXPECT_TRUE(sifter.S_.node_to_g1.count(C.get()));
                EXPECT_TRUE(sifter.S_.node_to_g1.count(D.get()));
            }

            TEST_F(BuildG1_EndLayer, WriteBackOnlyTouchesLayersInRange) {
                // Deliberately put nodes in a bad order on layer 1 and verify that
                // writeBack does not touch layer 2 or 3.
                GlobalSifter sifter(0, 1, G.layers());
                sifter.runSifting(3);

                // Record layer 2 and 3 order before writeBack
                auto layer2_before = G.layers().at(2).nodes;
                auto layer3_before = G.layers().at(3).nodes;

                sifter.writeBack();

                EXPECT_EQ(G.layers().at(2).nodes, layer2_before);
                EXPECT_EQ(G.layers().at(3).nodes, layer3_before);
            }

            // ============================================================================
            // buildG1 — hyperedge with two sources (fan-in)
            // ============================================================================

            class BuildG1_FanIn : public ::testing::Test {
            protected:
                void SetUp() override {
                    A = G.createNode("A", 0, 0, nullptr);
                    B = G.createNode("B", 0, 1, nullptr);
                    C = G.createNode("C", A->getLayer() + 1, 0, A);
                    G.addConnection(B, C);
                }
                TestGraph G{ "FanIn" };
                NodePtr A, B, C;
            };

            TEST_F(BuildG1_FanIn, HubHasTwoParents) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                int c_idx = sifter.S_.node_to_g1.at(C.get());
                EXPECT_EQ(sifter.S_.g1_in[c_idx].size(), 2u);
            }

            // ============================================================================
            // buildBlocks — singleton blocks (no dummy chains)
            // ============================================================================

            class BuildBlocks_NoDummies : public ::testing::Test {
            protected:
                void SetUp() override {
                    A = G.createNode("A", 0, 0, nullptr);
                    B = G.createNode("B", 0, 1, nullptr);
                    C = G.createNode("C", A->getLayer() + 1, 0, A);
                    D = G.createNode("D", B->getLayer() + 1, 0, B);
                }
                TestGraph G{ "NoDummies" };
                NodePtr A, B, C, D;
            };

            TEST_F(BuildBlocks_NoDummies, BlockCountEqualsG1NodeCount) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                EXPECT_EQ(sifter.S_.blocks.size(), sifter.S_.g1_nodes.size());
            }

            TEST_F(BuildBlocks_NoDummies, NoEmptyBlocks) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                for (const auto& blk : sifter.S_.blocks)
                    EXPECT_FALSE(blk.g1_nodes.empty());
            }

            TEST_F(BuildBlocks_NoDummies, BlockIdConsistency) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                for (int bid = 0; bid < static_cast<int>(sifter.S_.blocks.size()); ++bid)
                    for (int g1_idx : sifter.S_.blocks[bid].g1_nodes)
                        EXPECT_EQ(sifter.S_.g1_nodes[g1_idx].block_id, bid);
            }

            TEST_F(BuildBlocks_NoDummies, EveryG1NodeBelongsToExactlyOneBlock) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                std::unordered_set<int> seen;
                for (int bid = 0; bid < static_cast<int>(sifter.S_.blocks.size()); ++bid)
                    for (int g1_idx : sifter.S_.blocks[bid].g1_nodes)
                        EXPECT_TRUE(seen.insert(g1_idx).second)
                        << "g1_node " << g1_idx << " appears in multiple blocks";
                EXPECT_EQ(seen.size(), sifter.S_.g1_nodes.size());
            }

            TEST_F(BuildBlocks_NoDummies, PiResizedToBlockCount) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                EXPECT_EQ(sifter.S_.pi.size(), sifter.S_.blocks.size());
            }

            // ============================================================================
            // buildBlocks — dummy chain detection
            // ============================================================================

            TEST(BuildBlocks_DummyChainStandalone, ChainNodesCollapsedIntoOneBlock) {
                TestGraph G("ChainGraph");
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", 0, 1, nullptr);
                NodePtr bridge = G.createNode("bridge", A->getLayer() + 1, 0, A);
                NodePtr C = G.createNode("C", bridge->getLayer() + 1, 0, bridge);
                NodePtr D = G.createNode("D", C->getLayer() + 1, 0, C);
                G.addConnection(B, D);

                GlobalSifter sifter(0, lastLayer(G), G.layers());

                bool found_chain = false;
                for (const auto& blk : sifter.S_.blocks)
                    if (blk.g1_nodes.size() > 1) { found_chain = true; break; }
                EXPECT_TRUE(found_chain) << "expected at least one multi-node (chain) block";
            }

            TEST(BuildBlocks_DummyChainStandalone, BlockIdConsistencyWithChains) {
                TestGraph G("ChainGraph2");
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", 0, 1, nullptr);
                NodePtr bridge = G.createNode("bridge", A->getLayer() + 1, 0, A);
                NodePtr C = G.createNode("C", bridge->getLayer() + 1, 0, bridge);
                G.addConnection(B, C);

                GlobalSifter sifter(0, lastLayer(G), G.layers());

                for (int bid = 0; bid < static_cast<int>(sifter.S_.blocks.size()); ++bid)
                    for (int g1_idx : sifter.S_.blocks[bid].g1_nodes)
                        EXPECT_EQ(sifter.S_.g1_nodes[g1_idx].block_id, bid);
            }

            TEST(BuildBlocks_DummyChainStandalone, ComplicatedTopology) {
                TestGraph G("ChainGraph3");
                NodePtr a0 = G.createNode("a0", 0, 0, nullptr);
                NodePtr b0 = G.createNode("b0", 0, 1, nullptr);
                NodePtr b1 = G.createNode("b1", b0->getLayer() + 1, 0, b0);
                NodePtr c3;
                for (int i = 0; i <= 3; i++)
                    c3 = G.createNode("c" + std::to_string(i), c3 ? c3->getLayer() + 1 : 0, 0, c3);
                NodePtr D = G.createNode("D", a0->getLayer() + 1, 0, a0);
                HyperedgePtr edge = findEdgeWithSourceAndTarget(G, a0, D);
                ASSERT_NE(edge, nullptr);
                G.addSourceToEdge(edge, b1);
                G.addSourceToEdge(edge, c3);

                GlobalSifter sifter(0, lastLayer(G), G.layers());

                std::unordered_set<int> seen;
                for (int bid = 0; bid < static_cast<int>(sifter.S_.blocks.size()); ++bid)
                    for (int g1_idx : sifter.S_.blocks[bid].g1_nodes)
                        EXPECT_TRUE(seen.insert(g1_idx).second)
                        << "g1_node " << g1_idx << " appears in more than one block";
                EXPECT_EQ(seen.size(), sifter.S_.g1_nodes.size());
            }

            TEST(BuildBlocks_DummyChainStandalone, ComplicatedTopology2) {
                TestGraph G("ChainGraph3");
                NodePtr a0 = G.createNode("a0", 0, 0, nullptr);
                NodePtr b0 = G.createNode("b0", 0, 1, nullptr);
                NodePtr b1 = G.createNode("b1", b0->getLayer() + 1, 0, b0);
                NodePtr c3;
                for (int i = 0; i <= 3; i++)
                    c3 = G.createNode("c" + std::to_string(i), c3 ? c3->getLayer() + 1 : 0, 0, c3);
                NodePtr d5;
                for (int i = 0; i <= 5; i++)
                    d5 = G.createNode("d" + std::to_string(i), d5 ? d5->getLayer() + 1 : 0, 0, d5);

                NodePtr D = G.createNode("D", a0->getLayer() + 1, 0, a0);
                HyperedgePtr edge = findEdgeWithSourceAndTarget(G, a0, D);
                ASSERT_NE(edge, nullptr);
                G.addSourceToEdge(edge, b1);
                G.addSourceToEdge(edge, c3);
                G.addConnection(d5, D);

                GlobalSifter sifter(0, lastLayer(G), G.layers());

                std::unordered_set<int> seen;
                for (int bid = 0; bid < static_cast<int>(sifter.S_.blocks.size()); ++bid)
                    for (int g1_idx : sifter.S_.blocks[bid].g1_nodes)
                        EXPECT_TRUE(seen.insert(g1_idx).second)
                        << "g1_node " << g1_idx << " appears in more than one block";
                EXPECT_EQ(seen.size(), sifter.S_.g1_nodes.size());
            }

            // ============================================================================
            // buildBlockOrder — block list properties
            // ============================================================================

            class BuildBlockOrder_Base : public ::testing::Test {
            protected:
                void SetUp() override {
                    A = G.createNode("A", 0, 0, nullptr);
                    B = G.createNode("B", 0, 1, nullptr);
                    C = G.createNode("C", A->getLayer() + 1, 0, A);
                    D = G.createNode("D", B->getLayer() + 1, 0, B);
                }
                TestGraph G{ "BlockOrder" };
                NodePtr A, B, C, D;
            };

            TEST_F(BuildBlockOrder_Base, BContainsEveryBlockExactlyOnce) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                std::unordered_set<int> seen;
                for (int bid : sifter.B_)
                    EXPECT_TRUE(seen.insert(bid).second)
                    << "block " << bid << " appears more than once in B";
                EXPECT_EQ(seen.size(), sifter.S_.blocks.size());
            }

            TEST_F(BuildBlockOrder_Base, BSizeEqualsBlockCount) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                EXPECT_EQ(sifter.B_.size(), sifter.S_.blocks.size());
            }

            TEST_F(BuildBlockOrder_Base, AllBlockIdsInRange) {
                GlobalSifter sifter(0, lastLayer(G), G.layers());
                int num_blocks = static_cast<int>(sifter.S_.blocks.size());
                for (int bid : sifter.B_) {
                    EXPECT_GE(bid, 0) << "negative block id";
                    EXPECT_LT(bid, num_blocks) << "block id out of range";
                }
            }

            TEST_F(BuildBlockOrder_Base, AnchorBlocksAreFixedAndKeepTheirOrder) {
                // start_layer=1 -> anchor layer=0, which has nodes A and B.
                GlobalSifter sifter(1, lastLayer(G), G.layers());
                const int a = sifter.S_.g1_nodes[sifter.S_.node_to_g1.at(A.get())].block_id;
                const int b = sifter.S_.g1_nodes[sifter.S_.node_to_g1.at(B.get())].block_id;
                EXPECT_FALSE(sifter.S_.blocks[a].movable);
                EXPECT_FALSE(sifter.S_.blocks[b].movable);
                sifter.runSifting(5);
                EXPECT_LT(sifter.S_.pi[a], sifter.S_.pi[b]);
            }

            // ============================================================================
            // buildBlockOrder — topological order: sources before targets
            // ============================================================================

            TEST(BuildBlockOrder_TopoOrder, UpperLayerBlocksBeforeLowerLayerBlocks) {
                TestGraph G("TopoOrder");
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", A->getLayer() + 1, 0, A);

                GlobalSifter sifter(0, lastLayer(G), G.layers());

                int first_bid = sifter.B_[0];
                const Block& first_block = sifter.S_.blocks[first_bid];
                int first_g1_layer = sifter.S_.g1_nodes[first_block.upper()].g1_layer;
                EXPECT_EQ(first_g1_layer, 0) << "first block should be from the uppermost g1_layer";
            }

            TEST(BuildBlockOrder_TopoOrder, ThreeLayerGraph_LayerOrderRespected) {
                TestGraph G("ThreeLayers");
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", A->getLayer() + 1, 0, A);
                NodePtr C = G.createNode("C", B->getLayer() + 1, 0, B);

                GlobalSifter sifter(0, lastLayer(G), G.layers());

                for (int i = 1; i < static_cast<int>(sifter.B_.size()); ++i) {
                    int layer_prev = sifter.S_.g1_nodes[sifter.S_.blocks[sifter.B_[i - 1]].upper()].g1_layer;
                    int layer_curr = sifter.S_.g1_nodes[sifter.S_.blocks[sifter.B_[i]].upper()].g1_layer;
                    EXPECT_LE(layer_prev, layer_curr)
                        << "block at position " << i - 1 << " (g1_layer " << layer_prev
                        << ") comes before block at position " << i
                        << " (g1_layer " << layer_curr << ")";
                }
            }

            // ============================================================================
            // Full pipeline: constructor + sortAdjacencies validity
            // ============================================================================

            TEST(FullPipeline, SortAdjacencies_NminusNplusSizesValid) {
                TestGraph G("Pipeline");
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", 0, 1, nullptr);
                NodePtr C = G.createNode("C", A->getLayer() + 1, 0, A);
                NodePtr D = G.createNode("D", B->getLayer() + 1, 0, B);

                GlobalSifter sifter(0, lastLayer(G), G.layers());

                for (const auto& blk : sifter.S_.blocks) {
                    EXPECT_EQ(blk.I_minus.size(), blk.N_minus.size());
                    EXPECT_EQ(blk.I_plus.size(), blk.N_plus.size());
                }
            }

            TEST(FullPipeline, CountTotalCrossings_NonNegative) {
                TestGraph G("Pipeline2");
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", 0, 1, nullptr);
                NodePtr C = G.createNode("C", A->getLayer() + 1, 0, A);
                NodePtr D = G.createNode("D", B->getLayer() + 1, 0, B);

                GlobalSifter sifter(0, lastLayer(G), G.layers());
                EXPECT_GE(sifter.countCrossings(), 0);
            }

            TEST(FullPipeline, SiftingRoundDoesNotIncreaseCrossings) {
                TestGraph G("Crossed");
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", 0, 1, nullptr);
                NodePtr C = G.createNode("C", A->getLayer() + 1, 0, A);
                NodePtr D = G.createNode("D", B->getLayer() + 1, 0, B);
                G.addConnection(A, D);
                G.addConnection(B, C);

                GlobalSifter sifter(0, lastLayer(G), G.layers());
                int before = sifter.countCrossings();
                sifter.runSifting(1);
                int after = sifter.countCrossings();
                EXPECT_LE(after, before);
            }

            // ============================================================================
            // minimizeCrossingsForNodes — focused sifting for dummy node placement
            //
            // The two intended call sites are:
            //   1. Locating dummy nodes that were just inserted for a long edge.
            //   2. Repositioning a single childless node after a new connection is added.
            // ============================================================================

            // ── Use case 1: dummy placement after long-edge insertion ─────────────────
            //
            // Graph layout before sifting:
            //   Layer 0: A  B            (A left, B right)
            //   Layer 1: dummy_A  C      (dummy_A is the relay for A->C, appended at end)
            //   Layer 2: C's target D
            //
            // The dummy was appended at the right of layer 1 even though its parent A is
            // on the left. minimizeCrossingsForNodes should move it before C.

            TEST(MinimizeCrossingsForNodes, DummyPlacement_LongEdge_MovedToReduceCrossings) {
                TestGraph G("LongEdgeDummy");

                // Build a 3-layer graph: A and B at layer 0, C at layer 1, D at layer 2.
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", 0, 1, nullptr);     // B at layer 0, right of A
                NodePtr C = G.createNode("C", B->getLayer() + 1, 0, B);           // C at layer 1, child of B
                NodePtr D = G.createNode("D", C->getLayer() + 1, 0, C);           // D at layer 2

                // Add a long edge A -> D, which inserts a dummy at layer 1 (appended last).
                G.addConnection(A, D);

                // The dummy for A->D was appended after C in layer 1.  It should be moved
                // left (before C) because A is to the left of B.
                int before_crossings = G.minimizeCrossings();

                // Find the dummy node: it is the one with isDummy() == true at layer 1.
                Node* dummy_node = nullptr;
                for (const auto& n : G.layers().at(1).nodes)
                    if (n->isDummy()) { dummy_node = n.get(); break; }
                ASSERT_NE(dummy_node, nullptr) << "expected a dummy node at layer 1";

                // Reset to worst-case order: put dummy at the end of layer 1 again.
                auto& layer1_nodes = G.layers().at(1).nodes;
                auto dummy_it = std::find_if(layer1_nodes.begin(), layer1_nodes.end(),
                    [](const NodePtr& n) { return n->isDummy(); });
                ASSERT_NE(dummy_it, layer1_nodes.end());
                // Rotate dummy to the back to simulate a freshly inserted worst-case position.
                std::rotate(dummy_it, dummy_it + 1, layer1_nodes.end());

                int crossings_before_focused = G.minimizeCrossingsForNodes(
                    { dummy_node }, 0, lastLayer(G));
                // Focused sifting must not produce more crossings than the full sifting did.
                EXPECT_LE(crossings_before_focused, before_crossings + 1)
                    << "focused sifting should recover a low-crossing position for the dummy";
            }

            TEST(MinimizeCrossingsForNodes, DummyPlacement_WriteBackChangesLayerOrder) {
                TestGraph G("LongEdgeDummy2");

                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", 0, 1, nullptr);
                NodePtr C = G.createNode("C", B->getLayer() + 1, 0, B);
                NodePtr D = G.createNode("D", C->getLayer() + 1, 0, C);
                G.addConnection(A, D); // inserts dummy at layer 1 and layer 2

                // Push all dummies to the back of their respective layers (worst-case).
                for (auto& [layer, data] : G.layers()) {
                    auto& nodes = data.nodes;
                    std::stable_partition(nodes.begin(), nodes.end(),
                        [](const NodePtr& n) { return !n->isDummy(); });
                }

                // Collect dummy nodes across layers 1 and 2.
                std::vector<Node*> dummies;
                for (auto& [layer, data] : G.layers())
                    for (const auto& n : data.nodes)
                        if (n->isDummy()) dummies.push_back(n.get());
                ASSERT_FALSE(dummies.empty());

                auto order_before = G.layers().at(1).nodes;
                G.minimizeCrossingsForNodes(dummies, 0, lastLayer(G));
                auto order_after = G.layers().at(1).nodes;

                // The layer order must have changed (dummy moved from the back).
                EXPECT_NE(order_before, order_after)
                    << "writeBack should have updated the layer order";
            }

            TEST(MinimizeCrossingsForNodes, DummyPlacement_OnlyTargetedLayersChanged) {
                // A 4-layer chain. We insert a long edge skipping layers 1 and 2, producing
                // dummies there. We call siftNodes scoped to [0, 2]. Layer 3 must be
                // untouched by writeBack.
                TestGraph G("LongEdge4Layer");

                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", 0, 1, nullptr);
                NodePtr mid1 = G.createNode("mid1", B->getLayer() + 1, 0, B);  // layer 1
                NodePtr mid2 = G.createNode("mid2", mid1->getLayer() + 1, 0, mid1); // layer 2
                NodePtr E = G.createNode("E", mid2->getLayer() + 1, 0, mid2); // layer 3

                // Long edge A -> E inserts dummies at layers 1, 2.
                G.addConnection(A, E);

                auto layer3_before = G.layers().at(3).nodes;

                std::vector<Node*> dummies;
                for (int l = 1; l <= 2; l++)
                    for (const auto& n : G.layers().at(l).nodes)
                        if (n->isDummy()) dummies.push_back(n.get());

                G.minimizeCrossingsForNodes(dummies, 0, 2);

                EXPECT_EQ(G.layers().at(3).nodes, layer3_before)
                    << "layer 3 must not be touched when end_layer=2";
            }

            // ── Use case 2: repositioning a childless node after a new connection ─────

            TEST(MinimizeCrossingsForNodes, ChildlessNode_RepositionedAfterNewConnection) {
                // Layout:
                //   Layer 0: P0  P1  P2    (three sources, well-ordered)
                //   Layer 1: C0  C1  C2    (three targets, initially in order)
                //
                // We add a new edge P2 -> C0. C0 has no children. The connection makes
                // the current ordering (P2 far left of C0) sub-optimal; focused sifting
                // on C0 should move it to the right to reduce crossings.

                TestGraph G("ChildlessReposition");

                NodePtr P0 = G.createNode("P0", 0, 0, nullptr);
                NodePtr P1 = G.createNode("P1", 0, 1, nullptr);
                NodePtr P2 = G.createNode("P2", 0, 2, nullptr);

                // Create children; initial order C0 C1 C2.
                NodePtr C0 = G.createNode("C0", P0->getLayer() + 1, 0, P0);  // P0 -> C0
                NodePtr C1 = G.createNode("C1", P1->getLayer() + 1, 0, P1);  // P1 -> C1
                NodePtr C2 = G.createNode("C2", P2->getLayer() + 1, 0, P2);  // P2 -> C2

                // New connection that creates a crossing: P2 -> C0.
                G.addConnection(P2, C0);

                // C0 currently sits at position 0. After focused sifting it should move
                // right to minimise the crossing introduced by P2 -> C0.
                int pos_before = positionInLayer(G, C0.get());

                G.minimizeCrossingsForNodes({ C0.get() }, 0, lastLayer(G));

                int pos_after = positionInLayer(G, C0.get());

                EXPECT_LT(pos_after, pos_before)
                    << "C0 should have moved right to reduce crossings with the P2->C0 edge";
            }

            TEST(MinimizeCrossingsForNodes, ChildlessNode_NoCrossings_PositionUnchanged) {
                // If the graph already has zero crossings, sifting a childless node should
                // leave its position unchanged.
                TestGraph G("AlreadyOptimal");

                NodePtr P0 = G.createNode("P0", 0, 0, nullptr);
                NodePtr P1 = G.createNode("P1", 0, 1, nullptr);
                NodePtr C0 = G.createNode("C0", P0->getLayer() + 1, 0, P0);
                NodePtr C1 = G.createNode("C1", P1->getLayer() + 1, 1, P1);

                // P0->C0 and P1->C1 are already parallel; zero crossings.
                int pos_before = positionInLayer(G, C0.get());
                G.minimizeCrossingsForNodes({ C0.get() }, 0, lastLayer(G));
                int pos_after = positionInLayer(G, C0.get());

                EXPECT_EQ(pos_before, pos_after)
                    << "position should be unchanged when there are no crossings to fix";
            }

            TEST(MinimizeCrossingsForNodes, EmptyNodeList_NoOp) {
                TestGraph G("EmptyList");
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", A->getLayer() + 1, 0, A);

                auto order_before = G.layers().at(1).nodes;
                G.minimizeCrossingsForNodes({}, 0, lastLayer(G));
                EXPECT_EQ(G.layers().at(1).nodes, order_before)
                    << "empty node list must be a no-op";
            }

            TEST(MinimizeCrossingsForNodes, NodeOutsideWindow_Ignored) {
                // Nodes outside [start_layer, end_layer] are silently skipped.
                TestGraph G("OutsideWindow");
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", A->getLayer() + 1, 0, A);
                NodePtr C = G.createNode("C", B->getLayer() + 1, 0, B);

                auto order_before = G.layers().at(1).nodes;
                // Pass C (layer 2) but restrict window to [0, 1]; C should be ignored.
                G.minimizeCrossingsForNodes({ C.get() }, 0, 1);
                // B (layer 1) is the only siftable node in the window; its layer must
                // still be valid (one element, unchanged since no crossings exist).
                EXPECT_EQ(G.layers().at(1).nodes, order_before);
            }

            TEST(MinimizeCrossingsForNodes, RightBiasWhenShifting) {
                // Nodes outside [start_layer, end_layer] are silently skipped.
                TestGraph G("OutsideWindow");
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", 0, 1, nullptr);
				NodePtr C = G.createNode("C", 0, 2, nullptr);
                NodePtr D = G.createNode("D", A->getLayer() + 1, 0, A);
				NodePtr E = G.createNode("E", C->getLayer() + 1, 1, C);

                G.addConnection(A, B);
                G.minimizeCrossingsForNodes({ B.get() }, 0, 1);
				EXPECT_EQ(positionInLayer(G, B.get()), 1)
                    << "B should have moved right to reduce crossings with A->B edge";
            }

            // ============================================================================
            // Seed order: the block list reproduces every layer, ranges, placement
            // ============================================================================

            // Crossings of the layers exactly as they are (no barycenter, orders kept).
            static int layerCrossings(TestGraph& G) {
                GlobalSifter sifter(0, lastLayer(G), G.layers(), false);
                return sifter.countCrossings();
            }

            static std::vector<Node*> layerNodes(TestGraph& G, int layer) {
                std::vector<Node*> out;
                for (const auto& n : G.layers().at(layer).nodes) out.push_back(n.get());
                return out;
            }

            static Node* dummyAt(TestGraph& G, int layer) {
                for (const auto& n : G.layers().at(layer).nodes)
                    if (n->isDummy()) return n.get();
                return nullptr;
            }

            // A -> a1 -> a2 -> a3 -> Z, plus a long edge B -> Z beside it: no crossings.
            struct ChainBesidePath {
                TestGraph G{ "chain" };
                NodePtr A, B, a1, a2, a3, Z;
                ChainBesidePath() {
                    A = G.createNode("A", 0, 0, nullptr);
                    B = G.createNode("B", 0, 1, nullptr);
                    a1 = G.createNode("a1", 1, 0, A);
                    a2 = G.createNode("a2", 2, 0, a1);
                    a3 = G.createNode("a3", 3, 0, a2);
                    Z = G.createNode("Z", 4, 0, a3);
                    G.addConnection(B, Z);
                }
            };

            TEST(SeedOrder, BlockListReproducesLayersWithDummyChains) {
                ChainBesidePath g;
                ASSERT_EQ(layerCrossings(g.G), 0);
                // A chain must not be forced to the left of blocks starting on deeper layers.
                for (int layer = 2; layer <= 3; ++layer)
                    EXPECT_FALSE(g.G.layers().at(layer).nodes.front()->isDummy()) << "layer " << layer;
            }

            TEST(SeedOrder, ChainThroughAnchorIsNotForcedLeft) {
                ChainBesidePath g;
                const auto before2 = layerNodes(g.G, 2), before3 = layerNodes(g.G, 3);
                g.G.sift(10, 2);
                EXPECT_EQ(layerCrossings(g.G), 0);
                EXPECT_EQ(layerNodes(g.G, 2), before2);
                EXPECT_EQ(layerNodes(g.G, 3), before3);
            }

            TEST(SeedOrder, RangeLeavesTheLayerBelowUntouched) {
                // L0: A B, L1: b1 a1 (crossed), L2: b2 a2.
                TestGraph G("range");
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", 0, 1, nullptr);
                NodePtr a1 = G.createNode("a1", 1, 0, A);
                NodePtr b1 = G.createNode("b1", 1, 0, B);
                NodePtr a2 = G.createNode("a2", 2, 0, a1);
                NodePtr b2 = G.createNode("b2", 2, 0, b1);
                G.layers().at(0).nodes = { A, B };
                G.layers().at(1).nodes = { b1, a1 };
                G.layers().at(2).nodes = { b2, a2 };
                const int before = layerCrossings(G);
                ASSERT_GT(before, 0);

                G.sift(10, 0, 0); // only layer 0 may move; layer 1 is read, never changed
                EXPECT_EQ(layerNodes(G, 1), (std::vector<Node*>{ b1.get(), a1.get() }));
                EXPECT_EQ(layerNodes(G, 2), (std::vector<Node*>{ b2.get(), a2.get() }));
                EXPECT_LE(layerCrossings(G), before);
            }

            TEST(SeedOrder, ChainReachingTheLayerBelowTheRangeIsFixed) {
                ChainBesidePath g;
                GlobalSifter sifter(0, 1, g.G.layers(), false);
                Node* d1 = dummyAt(g.G, 1);
                ASSERT_NE(d1, nullptr);
                const Block& chain = sifter.S_.blocks[sifter.S_.g1_nodes[sifter.S_.node_to_g1.at(d1)].block_id];
                EXPECT_GT(chain.g1_nodes.size(), 1u) << "d1 and d2 should form one chain";
                EXPECT_FALSE(chain.movable);
                const Block& a1 = sifter.S_.blocks[sifter.S_.g1_nodes[sifter.S_.node_to_g1.at(g.a1.get())].block_id];
                EXPECT_TRUE(a1.movable);
            }

            TEST(SeedOrder, FollowParentsKeepsRootsAfterTheirLeftNeighbour) {
                TestGraph G("roots");
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", 0, 1, nullptr);
                NodePtr a1 = G.createNode("a1", 1, 0, A);
                NodePtr b1 = G.createNode("b1", 1, 1, B);
                NodePtr R = G.createNode("R", 1, 0, nullptr); // a root at layer 1
                ASSERT_EQ(R->getLayer(), 1);
                G.layers().at(0).nodes = { A, B };

                G.layers().at(1).nodes = { R, b1, a1 };
                GlobalSifter first(1, lastLayer(G), G.layers(), false, CrossingSeed::FollowParents);
                EXPECT_EQ(layerNodes(G, 1), (std::vector<Node*>{ R.get(), a1.get(), b1.get() }));

                G.layers().at(1).nodes = { b1, R, a1 };
                GlobalSifter second(1, lastLayer(G), G.layers(), false, CrossingSeed::FollowParents);
                EXPECT_EQ(layerNodes(G, 1), (std::vector<Node*>{ a1.get(), b1.get(), R.get() }));
            }

            TEST(SeedOrder, BarycenterNeverWorsensTheOrderItStartsFrom) {
                // A crossed start with a chain passing through: A B / b1 a1 d1 / b2 a2 d2 / Z.
                ChainBesidePath g;
                auto& L1 = g.G.layers().at(1).nodes;
                auto& L2 = g.G.layers().at(2).nodes;
                std::reverse(L1.begin(), L1.end());
                std::reverse(L2.begin(), L2.end());
                GlobalSifter kept(0, lastLayer(g.G), g.G.layers(), false);
                const int start = kept.countCrossings();
                GlobalSifter seeded(0, lastLayer(g.G), g.G.layers(), true);
                EXPECT_LE(seeded.countCrossings(), start);
            }

            TEST(SeedOrder, HubsAreSortedByTheBarycenterOfAllTheirEndpoints) {
                // upper: u0 u1 u2, lower: v1 v2.
                // H1: {u0, u2} -> {v2} (pulled right by u2 and v2), H2: {u1} -> {v1}.
                // Leftmost parent alone would put H1 first (u0 < u1) and cost 2 crossings;
                // the barycenter puts H2 first, with a single crossing (u0->H1 vs u1->H2).
                TestGraph G("hubs");
                NodePtr u0 = G.createNode("u0", 0, 0, nullptr);
                NodePtr u1 = G.createNode("u1", 0, 1, nullptr);
                NodePtr u2 = G.createNode("u2", 0, 2, nullptr);
                NodePtr v2 = G.createNode("v2", 1, 0, u0);
                NodePtr v1 = G.createNode("v1", 1, 0, u1);
                HyperedgePtr h1 = findEdgeWithSourceAndTarget(G, u0, v2);
                ASSERT_NE(h1, nullptr);
                G.addSourceToEdge(h1, u2);
                G.layers().at(0).nodes = { u0, u1, u2 };
                G.layers().at(1).nodes = { v1, v2 };

                GlobalSifter sifter(0, lastLayer(G), G.layers(), false);
                const auto& hubs = sifter.S_.g1_layers.at(1);
                ASSERT_EQ(hubs.size(), 2u);
                const int u1_g1 = sifter.S_.node_to_g1.at(u1.get());
                EXPECT_EQ(sifter.S_.g1_in[hubs[0]], (std::vector<int>{ u1_g1 })) << "H2 should come first";
                EXPECT_EQ(sifter.countCrossings(), 1);
            }

            TEST(Placement, UnpositionedNodeGoesUnderItsParent) {
                TestGraph G("place");
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr B = G.createNode("B", 0, 1, nullptr);
                NodePtr C = G.createNode("C", 0, 2, nullptr);
                NodePtr a = G.createNode("a", 1, 0, A);
                NodePtr b = G.createNode("b", 1, 1, B);
                NodePtr c = G.createNode("c", 1, 2, C);
                G.layers().at(0).nodes = { A, B, C };
                G.layers().at(1).nodes = { a, b, c };
                G.computeLayout();

                // b loses its position and sits at the end, as a freshly appended node would.
                G.layers().at(1).nodes = { a, c, b };
                G.forgetLayout(b.get());
                G.placeUnpositioned(0, lastLayer(G));
                EXPECT_EQ(layerNodes(G, 1), (std::vector<Node*>{ a.get(), b.get(), c.get() }));
            }

            TEST(Placement, NewDummiesFollowTheStraightLine) {
                ChainBesidePath g;
                g.G.computeLayout();
                Node* d1 = dummyAt(g.G, 1);
                Node* d2 = dummyAt(g.G, 2);
                Node* d3 = dummyAt(g.G, 3);
                ASSERT_TRUE(d1 && d2 && d3);
                const double xb = g.G.getX(g.B), x3 = g.G.getNodeLayout().at(d3).x;
                g.G.forgetLayout(d1);
                g.G.forgetLayout(d2);

                // B (layer 0) and d3 (layer 3) are the nearest known ends of the chain:
                // d1 and d2 sit 1/3 and 2/3 of the way along the straight line.
                auto x1 = g.G.guess(d1);
                ASSERT_TRUE(x1.has_value());
                EXPECT_NEAR(*x1, xb + (x3 - xb) / 3.0, 1e-9);
                auto x2 = g.G.guess(d2);
                ASSERT_TRUE(x2.has_value());
                EXPECT_NEAR(*x2, xb + 2.0 * (x3 - xb) / 3.0, 1e-9);
            }

            TEST(Placement, LayoutOfAnotherNodeAtTheSameAddressIsIgnored) {
                TestGraph G("stale");
                NodePtr A = G.createNode("A", 0, 0, nullptr);
                NodePtr a = G.createNode("a", 1, 0, A);
                G.computeLayout();
                EXPECT_TRUE(G.hasValidLayout(a.get()));
                G.setLayoutOwner(a.get(), A); // as if the entry had been written for another node
                EXPECT_FALSE(G.hasValidLayout(a.get()));
                EXPECT_FALSE(G.hasValidLayout(nullptr));
            }

            // ============================================================================
            // Crossing minimization ranges after operations (LayerSpan)
            // ============================================================================

            TEST(OperationRange, NewNodeWithDummiesLeavesDeeperLayersAlone) {
                // L0: P B, B -> b1 -> b2, and a crossing further down: u -> z, v -> w.
                TestGraph G("cap");
                NodePtr P = G.createNode("P", 0, 0, nullptr);
                NodePtr B = G.createNode("B", 0, 1, nullptr);
                NodePtr b1 = G.createNode("b1", 1, 0, B);
                NodePtr b2 = G.createNode("b2", 2, 0, b1);
                NodePtr u = G.createNode("u", 3, 0, b2);
                NodePtr v = G.createNode("v", 3, 1, b2);
                NodePtr w = G.createNode("w", 4, 0, v);
                NodePtr z = G.createNode("z", 4, 1, u);
                G.layers().at(0).nodes = { P, B };
                G.layers().at(3).nodes = { u, v };
                G.layers().at(4).nodes = { w, z };

                // A child of P two layers down, no position given: its dummy (layer 1) and the
                // node itself (layer 2) are placed; layers 3 and 4 are none of its business.
                G.createNode("n", 2, -1, P);
                EXPECT_EQ(layerNodes(G, 3), (std::vector<Node*>{ u.get(), v.get() }));
                EXPECT_EQ(layerNodes(G, 4), (std::vector<Node*>{ w.get(), z.get() }));
            }

            TEST(OperationRange, RootMovedBelowTheLastLayerIsMinimized) {
                // L0: Q A R, A -> a1 -> a2, and c is a child of both Q and R.
                TestGraph G("below");
                NodePtr Q = G.createNode("Q", 0, 0, nullptr);
                NodePtr A = G.createNode("A", 0, 1, nullptr);
                NodePtr R = G.createNode("R", 0, 2, nullptr);
                NodePtr a1 = G.createNode("a1", 1, 0, A);
                NodePtr a2 = G.createNode("a2", 2, 0, a1);
                NodePtr c = G.createNode("c", 1, 1, Q);
                G.addConnection(R, c);
                G.layers().at(0).nodes = { Q, A, R };
                ASSERT_EQ(lastLayer(G), 2);

                // R goes to a new layer 3, so c goes to 4 and Q -> c gets dummies at 1..3. Left
                // at the end of their layers, they would cross A -> a1 -> a2.
                G.Hypergraph::relocateNodeToLayer(R, 3);
                ASSERT_EQ(R->getLayer(), 3);
                ASSERT_EQ(c->getLayer(), 4);
                EXPECT_EQ(layerCrossings(G), 0);
            }
        } // namespace minimizeCrossings
    } // namespace graphicalhypergraph_tests
} // namespace hypergraph_logic