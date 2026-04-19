// ============================================================================
// test_minimizeCrossings_hypergraph.cpp
//
// GoogleTest tests for the hypergraph-dependent sifting_internal functions:
//   buildG1, buildBlocks, buildBlockOrder
//
// These tests use a real GraphicalHypergraph (or its Hypergraph base) to
// build proper LayerData / Node / Hyperedge structures, then exercise the
// G1-construction and block-building pipeline against those structures.
//
// Drop this file alongside test_minimizeCrossings.cpp — it lives in the same
// namespace so the two files can be compiled together into one test binary.
// ============================================================================

#include "GlobalSifting.h"
#include "GraphicalHypergraph.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace hypergraph_logic {
    namespace graphicalhypergraph_tests {
        namespace minimizeCrossings {

            using namespace sifting_internal;

            // ============================================================================
            // Fixture: a thin wrapper around GraphicalHypergraph that exposes the
            // protected layers_ map so the tests can pass it directly to buildG1 /
            // buildBlockOrder without going through minimizeCrossings().
            // ============================================================================

            class TestGraph : public GraphicalHypergraph {
            public:
                explicit TestGraph(const std::string& name) : GraphicalHypergraph(name) {}
                std::map<int, LayerData>& layers() { return layers_; }
            };

            // ============================================================================
            // Helpers
            // ============================================================================

            // Build the full pipeline up to (but not including) sortAdjacencies so that
            // individual phases can be inspected.
            static SiftState buildStateFromGraph(TestGraph& G, int start_layer = 0) {
                SiftState S;
                buildG1(S, G.layers(), start_layer);
                buildBlocks(S);
                return S;
            }

            // Count how many G1 nodes have original == nullptr (i.e. hub nodes).
            static int countHubs(const SiftState& S) {
                int n = 0;
                for (const auto& gn : S.g1_nodes)
                    if (gn.original == nullptr) ++n;
                return n;
            }

            // Count how many G1 nodes have a non-null original (real/dummy nodes).
            static int countRealNodes(const SiftState& S) {
                int n = 0;
                for (const auto& gn : S.g1_nodes)
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

            // ============================================================================
            // buildG1 — two-layer graph, single binary edge
            // ============================================================================

            class BuildG1_TwoLayers : public ::testing::Test {
            protected:
                void SetUp() override {
                    // Layer 0: A
                    // Layer 1: B
                    // Edge: A -> B  (short, layer 0)
                    A = G.createNode("A", 0, nullptr);
                    B = G.createNode("B", 0, A);
                }
                TestGraph G{ "TwoLayers" };
                NodePtr A, B;
            };

            TEST_F(BuildG1_TwoLayers, NodeCountIncludesHub) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                // 2 real nodes + 1 hub = 3 G1 nodes
                EXPECT_EQ(S.g1_nodes.size(), 3u);
            }

            TEST_F(BuildG1_TwoLayers, ExactlyOneHub) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                EXPECT_EQ(countHubs(S), 1);
                EXPECT_EQ(countRealNodes(S), 2);
            }

            TEST_F(BuildG1_TwoLayers, NodesAssignedToCorrectG1Layers) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                // A is at hypergraph layer 0 -> g1_layer 0
                int a_idx = S.node_to_g1.at(A.get());
                EXPECT_EQ(S.g1_nodes[a_idx].g1_layer, 0);
                // B is at hypergraph layer 1 -> g1_layer 2
                int b_idx = S.node_to_g1.at(B.get());
                EXPECT_EQ(S.g1_nodes[b_idx].g1_layer, 2);
                // Hub is between layers 0 and 2 -> g1_layer 1
                EXPECT_TRUE(S.g1_layers.count(1));
                EXPECT_EQ(S.g1_layers.at(1).size(), 1u);
                EXPECT_EQ(S.g1_nodes[S.g1_layers.at(1)[0]].g1_layer, 1);
            }

            TEST_F(BuildG1_TwoLayers, AdjacencyCorrect) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                int a_idx = S.node_to_g1.at(A.get());
                int b_idx = S.node_to_g1.at(B.get());
                int hub_idx = S.g1_layers.at(1)[0];

                // A -> hub
                ASSERT_EQ(S.g1_out[a_idx].size(), 1u);
                EXPECT_EQ(S.g1_out[a_idx][0], hub_idx);
                // hub -> B
                ASSERT_EQ(S.g1_out[hub_idx].size(), 1u);
                EXPECT_EQ(S.g1_out[hub_idx][0], b_idx);
                // hub's parent is A
                ASSERT_EQ(S.g1_in[hub_idx].size(), 1u);
                EXPECT_EQ(S.g1_in[hub_idx][0], a_idx);
                // B's parent is hub
                ASSERT_EQ(S.g1_in[b_idx].size(), 1u);
                EXPECT_EQ(S.g1_in[b_idx][0], hub_idx);
            }

            TEST_F(BuildG1_TwoLayers, NodeToG1MapsAllRealNodes) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                EXPECT_TRUE(S.node_to_g1.count(A.get()));
                EXPECT_TRUE(S.node_to_g1.count(B.get()));
            }

            // ============================================================================
            // buildG1 — three-layer graph, two edges, one hub per edge
            // ============================================================================

            class BuildG1_ThreeLayers : public ::testing::Test {
            protected:
                void SetUp() override {
                    // Layer 0: A, B
                    // Layer 1: C
                    // Layer 2: D
                    // Edges: A->C (layer 0), B->C (layer 0), C->D (layer 1)
                    A = G.createNode("A", 0, nullptr);
                    B = G.createNode("B", 1, nullptr);
                    C = G.createNode("C", 0, A);
                    G.addConnection(B, C);
                    D = G.createNode("D", 0, C);
                }
                TestGraph G{ "ThreeLayers" };
                NodePtr A, B, C, D;
            };

            TEST_F(BuildG1_ThreeLayers, G1LayerKeysCorrect) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                // Expect g1_layers 0 (A,B), 1 (hub A->C and hub B->C), 2 (C), 3 (hub C->D), 4 (D)
                EXPECT_TRUE(S.g1_layers.count(0));
                EXPECT_TRUE(S.g1_layers.count(1));
                EXPECT_TRUE(S.g1_layers.count(2));
                EXPECT_TRUE(S.g1_layers.count(3));
                EXPECT_TRUE(S.g1_layers.count(4));
            }

            TEST_F(BuildG1_ThreeLayers, HubCountMatchesEdgeCount) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                // Three short edges -> three hubs
                EXPECT_EQ(countHubs(S), 3);
            }

            TEST_F(BuildG1_ThreeLayers, RealNodeCountMatchesHypergraphNodes) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                EXPECT_EQ(countRealNodes(S), 4); // A, B, C, D
            }

            TEST_F(BuildG1_ThreeLayers, CNodeHasTwoIncomingHubs) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                int c_idx = S.node_to_g1.at(C.get());
                // C has two parent hubs (one from A, one from B)
                EXPECT_EQ(S.g1_in[c_idx].size(), 2u);
            }

            // ============================================================================
            // buildG1 — start_layer anchor behaviour
            // ============================================================================

            class BuildG1_StartLayer : public ::testing::Test {
            protected:
                void SetUp() override {
                    // Layer 0: A
                    // Layer 1: B
                    // Layer 2: C
                    A = G.createNode("A", 0, nullptr);
                    B = G.createNode("B", 0, A);
                    C = G.createNode("C", 0, B);
                }
                TestGraph G{ "StartLayer" };
                NodePtr A, B, C;
            };

            TEST_F(BuildG1_StartLayer, StartLayerZero_AllNodesRegistered) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                EXPECT_TRUE(S.node_to_g1.count(A.get()));
                EXPECT_TRUE(S.node_to_g1.count(B.get()));
                EXPECT_TRUE(S.node_to_g1.count(C.get()));
            }

            TEST_F(BuildG1_StartLayer, StartLayerOne_AnchorIncluded) {
                // start_layer=1 -> anchor_layer=0, so A should still be registered
                SiftState S;
                buildG1(S, G.layers(), 1);
                EXPECT_TRUE(S.node_to_g1.count(A.get()));
                EXPECT_TRUE(S.node_to_g1.count(B.get()));
                EXPECT_TRUE(S.node_to_g1.count(C.get()));
            }

            TEST_F(BuildG1_StartLayer, StartLayerOne_FixedPositionCountSetToAnchorSize) {
                SiftState S;
                buildG1(S, G.layers(), 1);
                // anchor_layer=0 has 1 node (A)
                EXPECT_EQ(S.fixed_position_count, 1);
            }

            TEST_F(BuildG1_StartLayer, StartLayerZero_FixedPositionCountIsZero) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                EXPECT_EQ(S.fixed_position_count, 0);
            }

            // ============================================================================
            // buildG1 — hyperedge with two sources (fan-in)
            // ============================================================================

            class BuildG1_FanIn : public ::testing::Test {
            protected:
                void SetUp() override {
                    // Layer 0: A, B
                    // Layer 1: C
                    // One hyperedge {A, B} -> {C}
                    A = G.createNode("A", 0, nullptr);
                    B = G.createNode("B", 1, nullptr);
                    C = G.createNode("C", 0, A);
                    G.addConnection(B, C);
                }
                TestGraph G{ "FanIn" };
                NodePtr A, B, C;
            };

            TEST_F(BuildG1_FanIn, HubHasTwoParents) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                // The hub for each edge from A and B has exactly one parent each
                // (two separate hubs since createNode/addConnection creates two separate edges)
                int c_idx = S.node_to_g1.at(C.get());
                // C should have 2 incoming hub connections
                EXPECT_EQ(S.g1_in[c_idx].size(), 2u);
            }

            // ============================================================================
            // buildBlocks — singleton blocks (no dummy chains)
            // ============================================================================

            class BuildBlocks_NoDummies : public ::testing::Test {
            protected:
                void SetUp() override {
                    A = G.createNode("A", 0, nullptr);
                    B = G.createNode("B", 1, nullptr);
                    C = G.createNode("C", 0, A);
                    D = G.createNode("D", 0, B);
                }
                TestGraph G{ "NoDummies" };
                NodePtr A, B, C, D;
            };

            TEST_F(BuildBlocks_NoDummies, BlockCountEqualsG1NodeCount) {
                SiftState S = buildStateFromGraph(G);
                // No dummy chains -> one block per G1 node
                EXPECT_EQ(S.blocks.size(), S.g1_nodes.size());
            }

            TEST_F(BuildBlocks_NoDummies, NoEmptyBlocks) {
                SiftState S = buildStateFromGraph(G);
                for (const auto& blk : S.blocks)
                    EXPECT_FALSE(blk.g1_nodes.empty());
            }

            TEST_F(BuildBlocks_NoDummies, BlockIdConsistency) {
                SiftState S = buildStateFromGraph(G);
                for (int bid = 0; bid < static_cast<int>(S.blocks.size()); ++bid)
                    for (int g1_idx : S.blocks[bid].g1_nodes)
                        EXPECT_EQ(S.g1_nodes[g1_idx].block_id, bid);
            }

            TEST_F(BuildBlocks_NoDummies, EveryG1NodeBelongsToExactlyOneBlock) {
                SiftState S = buildStateFromGraph(G);
                std::unordered_set<int> seen;
                for (int bid = 0; bid < static_cast<int>(S.blocks.size()); ++bid)
                    for (int g1_idx : S.blocks[bid].g1_nodes)
                        EXPECT_TRUE(seen.insert(g1_idx).second)
                        << "g1_node " << g1_idx << " appears in multiple blocks";
                EXPECT_EQ(seen.size(), S.g1_nodes.size());
            }

            TEST_F(BuildBlocks_NoDummies, PiResizedToBlockCount) {
                SiftState S = buildStateFromGraph(G);
                EXPECT_EQ(S.pi.size(), S.blocks.size());
            }

            // ============================================================================
            // buildBlocks — dummy chain detection
            // ============================================================================

            class BuildBlocks_DummyChain : public ::testing::Test {
            protected:
                void SetUp() override {
                    // Layer 0: A
                    // Layer 1: (dummy, auto-inserted by splitLongEdge)
                    // Layer 2: B
                    // Long edge A->B spanning two layers produces one dummy node.
                    A = G.createNode("A", 0, nullptr);
                    B = G.createNode("B", 0, nullptr);
                    // Force A to layer 0, B to layer 2 by creating an intermediate node
                    // and then connecting A -> B directly so splitLongEdge fires.
                    // Simplest approach: create A at layer 0, create an intermediate at layer 1,
                    // then connect to B at layer 2.
                    mid = G.createNode("mid", 0, A); // mid at layer 1
                    B = G.createNode("B", 0, mid);   // B at layer 2
                    // Now the edges A->mid and mid->B are each short, so no dummy chain exists.
                    // To get a real dummy chain we need a long edge: connect A directly to B.
                    // The easiest is to build a 3-layer straight chain of real nodes,
                    // then introduce a long edge by skipping a layer, relying on splitLongEdge.
                    // However GraphicalHypergraph splits long edges automatically.
                    // So: a 3-node linear chain A -> mid -> B gives us short edges with no dummies.
                    // A proper dummy chain test requires a long edge of span >= 2.
                    // We create it by having a 4th layer: A(0) -> mid(1) -> mid2(2) -> B(3)
                    // and separately connecting A directly to B to create a long edge with dummies.
                    // For simplicity we rebuild in SetUp to control the graph precisely.
                }
                TestGraph G{ "DummyChain" };
                NodePtr A, mid, B;
            };

            // A cleaner self-contained test using a fresh graph
            TEST(BuildBlocks_DummyChainStandalone, ChainNodesCollapsedIntoOneBlock) {
                // Build a graph where A(layer 0) connects to C(layer 2) via a long edge.
                // GraphicalHypergraph will insert a dummy at layer 1.
                // The dummy chain: dummy(layer1) is the only child/parent pair -> one block.
                TestGraph G("ChainGraph");
                NodePtr A = G.createNode("A", 0, nullptr);
				NodePtr B = G.createNode("B", 1, nullptr);
                // Place C at layer 0 initially, then force it to layer 2 by connecting through
                // an intermediate so splitLongEdge inserts a dummy.
                NodePtr bridge = G.createNode("bridge", 0, A);  // bridge at layer 1
                NodePtr C = G.createNode("C", 0, bridge);   // C at layer 2
				NodePtr D = G.createNode("D", 0, C);   // D at layer 3
                // Now add a direct long edge A->C which will be split with a dummy at layer 1.
                G.addConnection(B, D);

                SiftState S;
                buildG1(S, G.layers(), 0);
                buildBlocks(S);

                // There must be at least one block with more than one G1 node (the chain block)
                bool found_chain = false;
                for (const auto& blk : S.blocks)
                    if (blk.g1_nodes.size() > 1) { found_chain = true; break; }
                EXPECT_TRUE(found_chain) << "expected at least one multi-node (chain) block";
            }

            TEST(BuildBlocks_DummyChainStandalone, BlockIdConsistencyWithChains) {
                TestGraph G("ChainGraph2");
                NodePtr A = G.createNode("A", 0, nullptr);
				NodePtr B = G.createNode("B", 1, nullptr);
                NodePtr bridge = G.createNode("bridge", 0, A);
                NodePtr C = G.createNode("C", 0, bridge);
                G.addConnection(B, C);

                SiftState S;
                buildG1(S, G.layers(), 0);
                buildBlocks(S);

                for (int bid = 0; bid < static_cast<int>(S.blocks.size()); ++bid)
                    for (int g1_idx : S.blocks[bid].g1_nodes)
                        EXPECT_EQ(S.g1_nodes[g1_idx].block_id, bid);
            }

            TEST(BuildBlocks_DummyChainStandalone, ComplicatedTopology) {
                TestGraph G("ChainGraph3");
                NodePtr a0 = G.createNode("a0", 0, nullptr);
                NodePtr b0 = G.createNode("b0", 1, nullptr);
				NodePtr b1 = G.createNode("b1", 0, b0);
                NodePtr c3;
                for (int i = 0; i <= 3; i++) {
                    c3 = G.createNode("c" + std::to_string(i), 0, c3);
				}
                NodePtr D = G.createNode("D", 0, a0);
				HyperedgePtr edge = findEdgeWithSourceAndTarget(G, a0, D);
                ASSERT_NE(edge, nullptr);
				G.addSourceToEdge(edge, b1); // long edge with two sources: {a0, b1} -> D
				G.addSourceToEdge(edge, c3); // long edge with three sources: {a0, b1, c3} -> D

                SiftState S;
                buildG1(S, G.layers(), 0);
                buildBlocks(S);

                std::unordered_set<int> seen;
                for (int bid = 0; bid < static_cast<int>(S.blocks.size()); ++bid)
                    for (int g1_idx : S.blocks[bid].g1_nodes)
                        EXPECT_TRUE(seen.insert(g1_idx).second)
                        << "g1_node " << g1_idx << " appears in more than one block";
                EXPECT_EQ(seen.size(), S.g1_nodes.size());
            }

            TEST(BuildBlocks_DummyChainStandalone, ComplicatedTopology2) {
                TestGraph G("ChainGraph3");
                NodePtr a0 = G.createNode("a0", 0, nullptr);
                NodePtr b0 = G.createNode("b0", 1, nullptr);
                NodePtr b1 = G.createNode("b1", 0, b0);
                NodePtr c3;
                for (int i = 0; i <= 3; i++) {
                    c3 = G.createNode("c" + std::to_string(i), 0, c3);
                }
                NodePtr d5;
                for (int i = 0; i <= 5; i++) {
                    d5 = G.createNode("d" + std::to_string(i), 0, d5);
                }

                NodePtr D = G.createNode("D", 0, a0);
                HyperedgePtr edge = findEdgeWithSourceAndTarget(G, a0, D);
                ASSERT_NE(edge, nullptr);
                G.addSourceToEdge(edge, b1); // long edge with two sources: {a0, b1} -> D
                G.addSourceToEdge(edge, c3); // long edge with three sources: {a0, b1, c3} -> D
				G.addConnection(d5, D);

                SiftState S;
                buildG1(S, G.layers(), 0);
                buildBlocks(S);

                std::unordered_set<int> seen;
                for (int bid = 0; bid < static_cast<int>(S.blocks.size()); ++bid)
                    for (int g1_idx : S.blocks[bid].g1_nodes)
                        EXPECT_TRUE(seen.insert(g1_idx).second)
                        << "g1_node " << g1_idx << " appears in more than one block";
                EXPECT_EQ(seen.size(), S.g1_nodes.size());
            }

            // ============================================================================
            // buildBlockOrder — block list properties
            // ============================================================================

            class BuildBlockOrder_Base : public ::testing::Test {
            protected:
                void SetUp() override {
                    A = G.createNode("A", 0, nullptr);
                    B = G.createNode("B", 1, nullptr);
                    C = G.createNode("C", 0, A);
                    D = G.createNode("D", 0, B);
                }
                TestGraph G{ "BlockOrder" };
                NodePtr A, B, C, D;
            };

            TEST_F(BuildBlockOrder_Base, BContainsEveryBlockExactlyOnce) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                buildBlocks(S);
                BlockList B_list;
                buildBlockOrder(B_list, S, G.layers(), 0);

                std::unordered_set<int> seen;
                for (int bid : B_list)
                    EXPECT_TRUE(seen.insert(bid).second)
                    << "block " << bid << " appears more than once in B";
                EXPECT_EQ(seen.size(), S.blocks.size());
            }

            TEST_F(BuildBlockOrder_Base, BSizeEqualsBlockCount) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                buildBlocks(S);
                BlockList B_list;
                buildBlockOrder(B_list, S, G.layers(), 0);
                EXPECT_EQ(B_list.size(), S.blocks.size());
            }

            TEST_F(BuildBlockOrder_Base, AllBlockIdsInRange) {
                SiftState S;
                buildG1(S, G.layers(), 0);
                buildBlocks(S);
                BlockList B_list;
                buildBlockOrder(B_list, S, G.layers(), 0);
                int num_blocks = static_cast<int>(S.blocks.size());
                for (int bid : B_list)
                    EXPECT_GE(bid, 0) << "negative block id";
                for (int bid : B_list)
                    EXPECT_LT(bid, num_blocks) << "block id out of range";
            }

            TEST_F(BuildBlockOrder_Base, AnchorBlocksAppearFirst) {
                // start_layer=1 -> anchor layer=0, which has nodes A and B.
                // Their blocks should occupy the first fixed_position_count slots.
                SiftState S;
                buildG1(S, G.layers(), 1);
                buildBlocks(S);
                BlockList B_list;
                buildBlockOrder(B_list, S, G.layers(), 1);

                int fpc = S.fixed_position_count; // == number of anchor-layer nodes
                ASSERT_GE(static_cast<int>(B_list.size()), fpc);

                // Collect the block ids that correspond to anchor-layer G1 nodes
                std::unordered_set<int> anchor_bids;
                for (int g1_idx : S.g1_layers.at(0)) // anchor layer g1_layer key = 2*0 = 0
                    anchor_bids.insert(S.g1_nodes[g1_idx].block_id);

                for (int i = 0; i < fpc; ++i)
                    EXPECT_TRUE(anchor_bids.count(B_list[i]))
                    << "B[" << i << "] = " << B_list[i] << " is not an anchor block";
            }

            // ============================================================================
            // buildBlockOrder — topological order: sources before targets
            // ============================================================================

            TEST(BuildBlockOrder_TopoOrder, UpperLayerBlocksBeforeLowerLayerBlocks) {
                // In a two-layer graph, every upper-layer block must appear before every
                // lower-layer block in B (since hubs sit between them, this is verified by
                // checking that the first block in B belongs to the upper layer).
                TestGraph G("TopoOrder");
                NodePtr A = G.createNode("A", 0, nullptr);
                NodePtr B = G.createNode("B", 0, A);

                SiftState S;
                buildG1(S, G.layers(), 0);
                buildBlocks(S);
                BlockList B_list;
                buildBlockOrder(B_list, S, G.layers(), 0);

                // The first block in B_list should correspond to a G1 node on layer 0
                int first_bid = B_list[0];
                const Block& first_block = S.blocks[first_bid];
                int first_g1_layer = S.g1_nodes[first_block.upper()].g1_layer;
                EXPECT_EQ(first_g1_layer, 0) << "first block should be from the uppermost g1_layer";
            }

            TEST(BuildBlockOrder_TopoOrder, ThreeLayerGraph_LayerOrderRespected) {
                TestGraph G("ThreeLayers");
                NodePtr A = G.createNode("A", 0, nullptr);
                NodePtr B = G.createNode("B", 0, A);
                NodePtr C = G.createNode("C", 0, B);

                SiftState S;
                buildG1(S, G.layers(), 0);
                buildBlocks(S);
                BlockList B_list;
                buildBlockOrder(B_list, S, G.layers(), 0);

                // Verify that for every consecutive pair in B_list, the g1_layer of the
                // upper node of the earlier block is <= that of the later block.
                // (A block spanning multiple layers has its upper at the earliest layer.)
                for (int i = 1; i < static_cast<int>(B_list.size()); ++i) {
                    int layer_prev = S.g1_nodes[S.blocks[B_list[i - 1]].upper()].g1_layer;
                    int layer_curr = S.g1_nodes[S.blocks[B_list[i]].upper()].g1_layer;
                    EXPECT_LE(layer_prev, layer_curr)
                        << "block at position " << i - 1 << " (g1_layer " << layer_prev
                        << ") comes before block at position " << i
                        << " (g1_layer " << layer_curr << ")";
                }
            }

            // ============================================================================
            // Full pipeline: buildG1 + buildBlocks + buildBlockOrder + sortAdjacencies
            // verifies that sortAdjacencies does not crash and produces valid N±/I± sizes
            // ============================================================================

            TEST(FullPipeline, SortAdjacencies_NminusNplusSizesValid) {
                TestGraph G("Pipeline");
                NodePtr A = G.createNode("A", 0, nullptr);
                NodePtr B = G.createNode("B", 1, nullptr);
                NodePtr C = G.createNode("C", 0, A);
                NodePtr D = G.createNode("D", 0, B);

                SiftState S;
                buildG1(S, G.layers(), 0);
                buildBlocks(S);
                BlockList B_list;
                buildBlockOrder(B_list, S, G.layers(), 0);
                sortAdjacencies(S, B_list);

                for (const auto& blk : S.blocks) {
                    EXPECT_EQ(blk.I_minus.size(), blk.N_minus.size());
                    EXPECT_EQ(blk.I_plus.size(), blk.N_plus.size());
                }
            }

            TEST(FullPipeline, CountTotalCrossings_NonNegative) {
                TestGraph G("Pipeline2");
                NodePtr A = G.createNode("A", 0, nullptr);
                NodePtr B = G.createNode("B", 1, nullptr);
                NodePtr C = G.createNode("C", 0, A);
                NodePtr D = G.createNode("D", 0, B);

                SiftState S;
                buildG1(S, G.layers(), 0);
                buildBlocks(S);
                BlockList B_list;
                buildBlockOrder(B_list, S, G.layers(), 0);
                sortAdjacencies(S, B_list);

                EXPECT_GE(countTotalCrossings(S, B_list), 0);
            }

            TEST(FullPipeline, SiftingRoundDoesNotIncreaseCrossings) {
                // A graph with two upper nodes and two lower nodes connected in a crossed
                // pattern: A->D, B->C. After one sifting round crossings should not grow.
                TestGraph G("Crossed");
                NodePtr A = G.createNode("A", 0, nullptr);
                NodePtr B = G.createNode("B", 1, nullptr);
                NodePtr C = G.createNode("C", 0, A); // A->C
                NodePtr D = G.createNode("D", 0, B); // B->D
                // Now add the crossing connections: A->D and B->C
                G.addConnection(A, D);
                G.addConnection(B, C);

                SiftState S;
                buildG1(S, G.layers(), 0);
                buildBlocks(S);
                BlockList B_list;
                buildBlockOrder(B_list, S, G.layers(), 0);
                sortAdjacencies(S, B_list);

                int before = countTotalCrossings(S, B_list);

                BlockList snap = B_list;
                int numblocks = static_cast<int>(B_list.size());
                for (int i = S.fixed_position_count; i < numblocks; ++i)
                    siftingStep(S, B_list, snap[i]);

                int after = countTotalCrossings(S, B_list);
                EXPECT_LE(after, before);
            }

        } // namespace minimizeCrossings
    } // namespace graphicalhypergraph_tests
} // namespace hypergraph_logic