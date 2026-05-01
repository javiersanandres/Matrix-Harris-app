#include "GlobalSifting.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <unordered_set>
#include <vector>

namespace hypergraph_logic {
    namespace graphicalhypergraph_tests {
        namespace minimizeCrossings {

            using namespace sifting_internal;

            // ============================================================================
            // TestGlobalSifter
            //
            // Subclass of GlobalSifter that uses the TESTING-only no-op constructor
            // so that S_ and B_ can be set directly. layers_ is bound to a static dummy
            // that is never read by any sifting method.
            //
            // Free functions below match the call signatures used in the tests exactly,
            // routing each call through a temporary TestGlobalSifter.
            // ============================================================================

            struct TestGlobalSifter : GlobalSifter {
                TestGlobalSifter(SiftState S, BlockList B) : GlobalSifter() {
                    S_ = std::move(S);
                    B_ = std::move(B);
                }

                void callSortAdjacencies() { sortAdjacencies(); }
                int  callSiftingSwap(int a, int b) { return siftingSwap(a, b); }
                int  callSiftingStep(int a) { return siftingStep(a); }
                void callOrderLayers() { orderLayersByBlockOrder(); }
                int  callCountCrossings() { return countCrossings(); }

                static int staticUswap(const SiftState& S,
                    const std::vector<int>& Na, const std::vector<int>& Nb,
                    const std::vector<int>& pi)
                {
                    return uswap(S, Na, Nb, pi);
                }
                static int staticGetNodeAtLevel(const SiftState& S, Block& blk, int layer)
                {
                    return getNodeAtLevel(S, blk, layer);
                }
                static int staticCountBilayer(const std::vector<int>& l1,
                    const std::vector<int>& l2, const std::vector<std::vector<int>>& out)
                {
                    return countBilayerCrossings(l1, l2, out);
                }
            };

            // ── Free-function shims ───────────────────────────────────────────────────

            static void sortAdjacencies(SiftState& S, BlockList& B) {
                TestGlobalSifter g(S, B);
                g.callSortAdjacencies();
                S = g.S_; B = g.B_;
            }

            static int siftingSwap(SiftState& S, int a, int b) {
                TestGlobalSifter g(S, {});
                int r = g.callSiftingSwap(a, b);
                S = g.S_;
                return r;
            }

            static int siftingStep(SiftState& S, BlockList& B, int a) {
                TestGlobalSifter g(S, B);
                int r = g.callSiftingStep(a);
                S = g.S_; B = g.B_;
                return r;
            }

            static void orderLayersByBlockOrder(SiftState& S, BlockList& B) {
                TestGlobalSifter g(S, B);
                g.callOrderLayers();
                S = g.S_; B = g.B_;
            }

            static int countTotalCrossings(SiftState& S, BlockList& B) {
                TestGlobalSifter g(S, B);
                int r = g.callCountCrossings();
                S = g.S_; B = g.B_;
                return r;
            }

            static int uswap(const SiftState& S,
                const std::vector<int>& Na, const std::vector<int>& Nb,
                const std::vector<int>& pi)
            {
                return TestGlobalSifter::staticUswap(S, Na, Nb, pi);
            }

            static int getNodeAtLevel(const SiftState& S, Block& blk, int layer)
            {
                return TestGlobalSifter::staticGetNodeAtLevel(S, blk, layer);
            }

            static int countBilayerCrossings(const std::vector<int>& l1,
                const std::vector<int>& l2, const std::vector<std::vector<int>>& out)
            {
                return TestGlobalSifter::staticCountBilayer(l1, l2, out);
            }


            // ============================================================================
            // State-building helpers
            // ============================================================================

            // Build a flat SiftState where every node is on g1_layer 0 and is a singleton
            // block with no edges. Useful for testing pi / adjacency invariants.
            static SiftState makeSingletonState(int n) {
                SiftState S;
                S.g1_in.resize(n);
                S.g1_out.resize(n);
                for (int i = 0; i < n; ++i) {
                    S.g1_nodes.emplace_back(nullptr, 0);
                    S.g1_nodes[i].block_id = i;
                    S.g1_layers[0].push_back(i);
                    S.blocks.emplace_back(std::vector<int>{i});
                }
                S.pi.resize(n, 0);
                S.fixed_position_count = 0;
                return S;
            }

            // Build a two-real-layer SiftState and populate B in natural order.
            //
            //   g1_layer 0: nodes  0 .. p-1           (upper real nodes)
            //   g1_layer 1: nodes  p+q .. p+q+|E|-1   (hub nodes, one per edge)
            //   g1_layer 2: nodes  p .. p+q-1         (lower real nodes)
            //
            // edge_list[i] = {src, tgt}  (0-based within their respective layers)
            static SiftState makeTwoLayerState(
                int p, int q,
                const std::vector<std::pair<int, int>>& edge_list,
                BlockList& B_out)
            {
                SiftState S;
                S.fixed_position_count = 0;

                // Upper nodes (g1_layer 0)
                for (int i = 0; i < p; ++i) {
                    int idx = static_cast<int>(S.g1_nodes.size());
                    S.g1_nodes.emplace_back(nullptr, 0);
                    S.g1_layers[0].push_back(idx);
                }
                // Lower nodes (g1_layer 2)
                for (int i = 0; i < q; ++i) {
                    int idx = static_cast<int>(S.g1_nodes.size());
                    S.g1_nodes.emplace_back(nullptr, 2);
                    S.g1_layers[2].push_back(idx);
                }

                // Reserve adjacency for upper + lower nodes before adding hubs
                int base = p + q;
                S.g1_in.resize(base + static_cast<int>(edge_list.size()));
                S.g1_out.resize(base + static_cast<int>(edge_list.size()));
                S.g1_nodes.reserve(base + static_cast<int>(edge_list.size()));

                // Hub nodes (g1_layer 1)
                for (const auto& [src, tgt] : edge_list) {
                    int hub = static_cast<int>(S.g1_nodes.size());
                    S.g1_nodes.emplace_back(nullptr, 1);
                    S.g1_layers[1].push_back(hub);
                    S.g1_in.push_back({});
                    S.g1_out.push_back({});

                    int src_g1 = src;       // upper node index
                    int tgt_g1 = p + tgt;   // lower node index

                    S.g1_out[src_g1].push_back(hub);
                    S.g1_in[hub].push_back(src_g1);
                    S.g1_out[hub].push_back(tgt_g1);
                    S.g1_in[tgt_g1].push_back(hub);
                }

                S.g1_in.resize(S.g1_nodes.size());
                S.g1_out.resize(S.g1_nodes.size());

                // One singleton block per G1 node
                for (int i = 0; i < static_cast<int>(S.g1_nodes.size()); ++i) {
                    S.g1_nodes[i].block_id = i;
                    S.blocks.emplace_back(std::vector<int>{i});
                }
                S.pi.resize(S.blocks.size(), 0);

                B_out.clear();
                for (int i = 0; i < static_cast<int>(S.blocks.size()); ++i)
                    B_out.push_back(i);

                return S;
            }

            // Build corresponding Siftstate to Figure 1 of the paper.
            static SiftState buildPaperState() {
                SiftState S;
                S.fixed_position_count = 0;

                for (int i = 0; i < 3; i++) {
                    S.g1_nodes.emplace_back(nullptr, 0);
                    S.g1_layers[0].push_back(i);
                    S.blocks.emplace_back(std::vector<int>{i});
                    S.g1_nodes[i].block_id = i;
                }
                for (int i = 3; i < 7; i++) {
                    S.g1_nodes.emplace_back(nullptr, i - 2);
                    S.g1_layers[i - 2].push_back(i);
                    S.blocks.emplace_back(std::vector<int>{i});
                    S.g1_nodes[i].block_id = i;
                }
                for (int i = 7; i < 9; i++) {
                    S.g1_nodes.emplace_back(nullptr, 1);
                    S.g1_layers[1].push_back(i);
                }
                for (int i = 9; i < 11; i++) {
                    S.g1_nodes.emplace_back(nullptr, 2);
                    S.g1_layers[2].push_back(i);
                }
                for (int i = 11; i < 13; i++) {
                    S.g1_nodes.emplace_back(nullptr, 3);
                    S.g1_layers[3].push_back(i);
                }
                S.blocks.emplace_back(std::vector<int>{7, 9});
                S.g1_nodes[7].block_id = 7;
                S.g1_nodes[9].block_id = 7;
                S.blocks.emplace_back(std::vector<int>{8, 10, 12});
                S.g1_nodes[8].block_id = 8;
                S.g1_nodes[10].block_id = 8;
                S.g1_nodes[12].block_id = 8;
                S.blocks.emplace_back(std::vector<int>{11});
                S.g1_nodes[11].block_id = 9;

                std::vector<std::pair<int, int>> edges = {
                    {0, 7}, {1, 3}, {1, 8}, { 2, 3 },
                    {7, 9}, {3, 4}, {8, 10},
                    {9, 5}, {4, 5}, {4, 11}, { 10, 12 },
                    {5, 6}, {11, 6}, {12, 6},
                };

                S.g1_in.assign(S.g1_nodes.size(), {});
                S.g1_out.assign(S.g1_nodes.size(), {});

                for (const auto& [src, tgt] : edges) {
                    S.g1_out[src].push_back(tgt);
                    S.g1_in[tgt].push_back(src);
                }

                S.pi.resize(S.blocks.size(), 0);
                for (int i = 0; i < static_cast<int>(S.blocks.size()); i++) {
                    S.pi[i] = i;
                }
                return S;
            }


            // Build a variant of Figure 1 in the paper
            static SiftState buildPaperStateV2() {
                SiftState S = buildPaperState();
                S.g1_nodes.emplace_back(nullptr, 4);
                S.g1_layers[4].push_back(13);
                S.blocks.emplace_back(std::vector<int>{13});
                S.g1_nodes[13].block_id = 10;

                std::vector<std::pair<int, int>> edges = {
                    {12, 13}
                };

                S.g1_in.resize(S.g1_nodes.size());
                S.g1_out.resize(S.g1_nodes.size());

                for (const auto& [src, tgt] : edges) {
                    S.g1_out[src].push_back(tgt);
                    S.g1_in[tgt].push_back(src);
                }

                S.pi.push_back(static_cast<int>(S.blocks.size()) - 1);
                return S;
            }


            // Brute-force O(E^2) crossing counter — used to cross-check the BJM tree result.
            static int bruteForceCountBilayer(
                const std::vector<int>& layer1,
                const std::vector<int>& layer2,
                const std::vector<std::vector<int>>& out)
            {
                std::unordered_map<int, int> pos2;
                for (int j = 0; j < static_cast<int>(layer2.size()); ++j)
                    pos2[layer2[j]] = j;

                std::vector<std::pair<int, int>> edges;
                for (int i = 0; i < static_cast<int>(layer1.size()); ++i)
                    for (int v : out[layer1[i]])
                        edges.push_back({ i, pos2.at(v) });

                int crossings = 0;
                for (int a = 0; a < static_cast<int>(edges.size()); ++a)
                    for (int b = a + 1; b < static_cast<int>(edges.size()); ++b)
                        if ((edges[a].first < edges[b].first && edges[a].second > edges[b].second) ||
                            (edges[a].first > edges[b].first && edges[a].second < edges[b].second))
                            ++crossings;
                return crossings;
            }

            // ============================================================================
            // countBilayerCrossings
            // ============================================================================

            TEST(CountBilayerCrossings, EmptyLayers) {
                std::vector<std::vector<int>> out;
                EXPECT_EQ(countBilayerCrossings({}, {}, out), 0);
            }

            TEST(CountBilayerCrossings, EmptySouthLayer) {
                std::vector<std::vector<int>> out(2);
                EXPECT_EQ(countBilayerCrossings({ 0, 1 }, {}, out), 0);
            }

            TEST(CountBilayerCrossings, EmptyNorthLayer) {
                std::vector<std::vector<int>> out(2);
                EXPECT_EQ(countBilayerCrossings({}, { 0, 1 }, out), 0);
            }

            TEST(CountBilayerCrossings, NoEdges) {
                std::vector<std::vector<int>> out(4);
                EXPECT_EQ(countBilayerCrossings({ 0, 1 }, { 2, 3 }, out), 0);
            }

            TEST(CountBilayerCrossings, ParallelEdges) {
                // 0->2, 1->3 — no crossings
                std::vector<std::vector<int>> out(4);
                out[0] = { 2 };
                out[1] = { 3 };
                int r = countBilayerCrossings({ 0, 1 }, { 2, 3 }, out);
                EXPECT_EQ(r, 0);
                EXPECT_EQ(r, bruteForceCountBilayer({ 0, 1 }, { 2, 3 }, out));
            }

            TEST(CountBilayerCrossings, OneCrossing) {
                // 0->3, 1->2 — one crossing
                std::vector<std::vector<int>> out(4);
                out[0] = { 3 };
                out[1] = { 2 };
                int r = countBilayerCrossings({ 0, 1 }, { 2, 3 }, out);
                EXPECT_EQ(r, 1);
                EXPECT_EQ(r, bruteForceCountBilayer({ 0, 1 }, { 2, 3 }, out));
            }

            TEST(CountBilayerCrossings, FullyReversedThreeNodes) {
                // 0->5, 1->4, 2->3 — 3 crossings (all pairs cross)
                std::vector<std::vector<int>> out(6);
                out[0] = { 5 };
                out[1] = { 4 };
                out[2] = { 3 };
                int r = countBilayerCrossings({ 0, 1, 2 }, { 3, 4, 5 }, out);
                EXPECT_EQ(r, 3);
                EXPECT_EQ(r, bruteForceCountBilayer({ 0, 1, 2 }, { 3, 4, 5 }, out));
            }

            TEST(CountBilayerCrossings, K22Complete) {
                // K_{2,2}: 0->{2,3}, 1->{2,3} — exactly 1 crossing
                std::vector<std::vector<int>> out(4);
                out[0] = { 2, 3 };
                out[1] = { 2, 3 };
                int r = countBilayerCrossings({ 0, 1 }, { 2, 3 }, out);
                EXPECT_EQ(r, 1);
                EXPECT_EQ(r, bruteForceCountBilayer({ 0, 1 }, { 2, 3 }, out));
            }

            TEST(CountBilayerCrossings, K33Complete) {
                // K_{3,3} — brute-force reference
                std::vector<std::vector<int>> out(6);
                for (int i = 0; i < 3; ++i) out[i] = { 3, 4, 5 };
                int r = countBilayerCrossings({ 0, 1, 2 }, { 3, 4, 5 }, out);
                int bf = bruteForceCountBilayer({ 0, 1, 2 }, { 3, 4, 5 }, out);
                EXPECT_EQ(r, bf);
            }

            TEST(CountBilayerCrossings, MultiEdgeFromOneNode) {
                // 0->{2,3}, 1->2 — one crossing (0->3 crosses 1->2)
                std::vector<std::vector<int>> out(4);
                out[0] = { 2, 3 };
                out[1] = { 2 };
                int r = countBilayerCrossings({ 0, 1 }, { 2, 3 }, out);
                EXPECT_EQ(r, 1);
                EXPECT_EQ(r, bruteForceCountBilayer({ 0, 1 }, { 2, 3 }, out));
            }

            TEST(CountBilayerCrossings, SingleNodeEachSide) {
                std::vector<std::vector<int>> out(2);
                out[0] = { 1 };
                EXPECT_EQ(countBilayerCrossings({ 0 }, { 1 }, out), 0);
            }

            TEST(CountBilayerCrossings, AgreeWithBruteForce_FourByFour) {
                // 4 north, 4 south, hand-crafted crossing pattern
                std::vector<std::vector<int>> out(8);
                out[0] = { 5, 7 };
                out[1] = { 4, 6 };
                out[2] = { 7 };
                out[3] = { 4, 5 };
                int r = countBilayerCrossings({ 0,1,2,3 }, { 4,5,6,7 }, out);
                int bf = bruteForceCountBilayer({ 0,1,2,3 }, { 4,5,6,7 }, out);
                EXPECT_EQ(r, bf);
            }

            TEST(CountBilayerCrossings, SymmetryReversedLayerOrderSameCount) {
                // Reversing north/south layers should give the same crossing count
                std::vector<std::vector<int>> out(4);
                out[0] = { 3 };
                out[1] = { 2 };
                int forward = countBilayerCrossings({ 0, 1 }, { 2, 3 }, out);

                // Build reverse: edges go from south to north in the transposed graph
                std::vector<std::vector<int>> out_rev(4);
                out_rev[3] = { 0 };
                out_rev[2] = { 1 };
                int backward = countBilayerCrossings({ 2, 3 }, { 0, 1 }, out_rev);
                EXPECT_EQ(forward, backward);
            }

            // ============================================================================
            // orderLayersByBlockOrder
            // ============================================================================

            TEST(OrderLayersByBlockOrder, PiMatchesPositionInB) {
                SiftState S = makeSingletonState(4);
                BlockList B = { 3, 1, 0, 2 };
                orderLayersByBlockOrder(S, B);
                EXPECT_EQ(S.pi[3], 0);
                EXPECT_EQ(S.pi[1], 1);
                EXPECT_EQ(S.pi[0], 2);
                EXPECT_EQ(S.pi[2], 3);
            }

            TEST(OrderLayersByBlockOrder, LayerSortedByPiAscending) {
                SiftState S = makeSingletonState(4);
                BlockList B = { 3, 2, 1, 0 };
                orderLayersByBlockOrder(S, B);
                const auto& layer = S.g1_layers.at(0);
                for (int i = 1; i < static_cast<int>(layer.size()); ++i)
                    EXPECT_LE(S.pi[S.g1_nodes[layer[i - 1]].block_id],
                        S.pi[S.g1_nodes[layer[i]].block_id]);
            }

            TEST(OrderLayersByBlockOrder, IdentityBLeavesLayerUnchanged) {
                SiftState S = makeSingletonState(3);
                BlockList B = { 0, 1, 2 };
                orderLayersByBlockOrder(S, B);
                const auto& layer = S.g1_layers.at(0);
                EXPECT_EQ(layer[0], 0);
                EXPECT_EQ(layer[1], 1);
                EXPECT_EQ(layer[2], 2);
            }

            TEST(OrderLayersByBlockOrder, MultipleLayersEachSorted) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{1,1} }, B);
                std::reverse(B.begin(), B.end());
                orderLayersByBlockOrder(S, B);
                for (auto& [layer_key, nodes] : S.g1_layers)
                    for (int i = 1; i < static_cast<int>(nodes.size()); ++i)
                        EXPECT_LE(S.pi[S.g1_nodes[nodes[i - 1]].block_id],
                            S.pi[S.g1_nodes[nodes[i]].block_id]);
            }

            TEST(OrderLayersByBlockOrder, RepeatedCallIdempotent) {
                SiftState S = makeSingletonState(4);
                BlockList B = { 2, 3, 0, 1 };
                orderLayersByBlockOrder(S, B);
                auto layer_after_first = S.g1_layers.at(0);
                orderLayersByBlockOrder(S, B);
                EXPECT_EQ(S.g1_layers.at(0), layer_after_first);
            }

            // ============================================================================
            // sortAdjacencies
            // ============================================================================

            TEST(SortAdjacencies, UpperNodesHaveEmptyNminus) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{1,1} }, B);
                sortAdjacencies(S, B);
                EXPECT_TRUE(S.blocks[0].N_minus.empty());
                EXPECT_TRUE(S.blocks[1].N_minus.empty());
            }

            TEST(SortAdjacencies, LowerNodesHaveEmptyNplus) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{1,1} }, B);
                sortAdjacencies(S, B);
                EXPECT_TRUE(S.blocks[2].N_plus.empty());
                EXPECT_TRUE(S.blocks[3].N_plus.empty());
            }

            TEST(SortAdjacencies, NplusSortedByPi) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{0,1},{1,0} }, B);
                sortAdjacencies(S, B);
                for (auto& blk : S.blocks)
                    for (int i = 1; i < static_cast<int>(blk.N_plus.size()); ++i)
                        EXPECT_LE(S.pi[S.g1_nodes[blk.N_plus[i - 1]].block_id],
                            S.pi[S.g1_nodes[blk.N_plus[i]].block_id]);
            }

            TEST(SortAdjacencies, NminusSortedByPi) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{1,0},{1,1} }, B);
                sortAdjacencies(S, B);
                for (auto& blk : S.blocks)
                    for (int i = 1; i < static_cast<int>(blk.N_minus.size()); ++i)
                        EXPECT_LE(S.pi[S.g1_nodes[blk.N_minus[i - 1]].block_id],
                            S.pi[S.g1_nodes[blk.N_minus[i]].block_id]);
            }

            TEST(SortAdjacencies, CrossRefsConsistentSingleEdge) {
                BlockList B;
                SiftState S = makeTwoLayerState(1, 1, { {0,0} }, B);
                sortAdjacencies(S, B);
                // node 0 (upper, block 0), node 1 (lower, block 1), node 2 (hub, block 2)
                Block& upper = S.blocks[0];
                Block& hub = S.blocks[2];

                ASSERT_FALSE(upper.N_plus.empty());
                ASSERT_FALSE(hub.N_minus.empty());

                int ip = upper.I_plus[0];
                ASSERT_GE(ip, 0);
                // I_plus[0] must index into hub.N_minus and point back to upper's lower node
                EXPECT_EQ(hub.N_minus[ip], upper.lower());

                int im = hub.I_minus[ip];
                ASSERT_GE(im, 0);
                EXPECT_EQ(upper.N_plus[im], hub.upper());
            }

            TEST(SortAdjacencies, CrossRefsConsistentParallelEdges) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{1,1} }, B);
                sortAdjacencies(S, B);
                // For each upper block verify I_plus back-references
                for (int bid = 0; bid < 2; ++bid) {
                    Block& upper = S.blocks[bid];
                    for (int i = 0; i < static_cast<int>(upper.N_plus.size()); ++i) {
                        int hub_bid = S.g1_nodes[upper.N_plus[i]].block_id;
                        Block& hub = S.blocks[hub_bid];
                        int ip = upper.I_plus[i];
                        ASSERT_GE(ip, 0);
                        EXPECT_EQ(hub.N_minus[ip], upper.lower());
                    }
                }
            }

            TEST(SortAdjacencies, PiReflectsB) {
                SiftState S = makeSingletonState(3);
                BlockList B = { 2, 0, 1 };
                sortAdjacencies(S, B);
                EXPECT_EQ(S.pi[2], 0);
                EXPECT_EQ(S.pi[0], 1);
                EXPECT_EQ(S.pi[1], 2);
            }

            TEST(SortAdjacencies, IMinusIPlus_SizesMatchNSizes) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{0,1} }, B);
                sortAdjacencies(S, B);
                for (auto& blk : S.blocks) {
                    EXPECT_EQ(blk.I_minus.size(), blk.N_minus.size());
                    EXPECT_EQ(blk.I_plus.size(), blk.N_plus.size());
                }
            }

            TEST(SortAdjacencies, PaperExample) {
                SiftState S = buildPaperState();
                BlockList B;

                for (int i = 0; i < static_cast<int>(S.blocks.size()); ++i)
                    B.push_back(i);

                sortAdjacencies(S, B);

                Block& blk = S.blocks[0];
                EXPECT_EQ(blk.N_minus.size(), 0);
                EXPECT_EQ(blk.I_minus.size(), 0);
                EXPECT_EQ(blk.N_plus.size(), 1);
                EXPECT_EQ(blk.N_plus[0], 7);
                EXPECT_EQ(blk.I_plus.size(), 1);
                EXPECT_EQ(blk.I_plus[0], 0);
                blk = S.blocks[1];
                EXPECT_EQ(blk.N_minus.size(), 0);
                EXPECT_EQ(blk.I_minus.size(), 0);
                EXPECT_EQ(blk.N_plus.size(), 2);
                EXPECT_EQ(blk.N_plus[0], 3);
                EXPECT_EQ(blk.N_plus[1], 8);
                EXPECT_EQ(blk.I_plus.size(), 2);
                EXPECT_EQ(blk.I_plus[0], 0);
                EXPECT_EQ(blk.I_plus[1], 0);
                blk = S.blocks[2];
                EXPECT_EQ(blk.N_minus.size(), 0);
                EXPECT_EQ(blk.I_minus.size(), 0);
                EXPECT_EQ(blk.N_plus.size(), 1);
                EXPECT_EQ(blk.N_plus[0], 3);
                EXPECT_EQ(blk.I_plus.size(), 1);
                EXPECT_EQ(blk.I_plus[0], 1);
                blk = S.blocks[3];
                EXPECT_EQ(blk.N_minus.size(), 2);
                EXPECT_EQ(blk.N_minus[0], 1);
                EXPECT_EQ(blk.N_minus[1], 2);
                EXPECT_EQ(blk.I_minus.size(), 2);
                EXPECT_EQ(blk.I_minus[0], 0);
                EXPECT_EQ(blk.I_minus[1], 0);
                EXPECT_EQ(blk.N_plus.size(), 1);
                EXPECT_EQ(blk.N_plus[0], 4);
                EXPECT_EQ(blk.I_plus.size(), 1);
                EXPECT_EQ(blk.I_plus[0], 0);
                blk = S.blocks[4];
                EXPECT_EQ(blk.N_minus.size(), 1);
                EXPECT_EQ(blk.N_minus[0], 3);
                EXPECT_EQ(blk.I_minus.size(), 1);
                EXPECT_EQ(blk.I_minus[0], 0);
                EXPECT_EQ(blk.N_plus.size(), 2);
                EXPECT_EQ(blk.N_plus[0], 5);
                EXPECT_EQ(blk.N_plus[1], 11);
                EXPECT_EQ(blk.I_plus.size(), 2);
                EXPECT_EQ(blk.I_plus[0], 0);
                EXPECT_EQ(blk.I_plus[1], 0);
                blk = S.blocks[6];
                EXPECT_EQ(blk.N_minus.size(), 3);
                EXPECT_EQ(blk.N_minus[0], 5);
                EXPECT_EQ(blk.N_minus[1], 12);
                EXPECT_EQ(blk.N_minus[2], 11);
                EXPECT_EQ(blk.I_minus.size(), 3);
                EXPECT_EQ(blk.I_minus[0], 0);
                EXPECT_EQ(blk.I_minus[1], 0);
                EXPECT_EQ(blk.I_minus[2], 0);
                EXPECT_EQ(blk.N_plus.size(), 0);
                EXPECT_EQ(blk.I_plus.size(), 0);
                blk = S.blocks[7];
                EXPECT_EQ(blk.N_minus.size(), 1);
                EXPECT_EQ(blk.N_minus[0], 0);
                EXPECT_EQ(blk.I_minus.size(), 1);
                EXPECT_EQ(blk.I_minus[0], 0);
                EXPECT_EQ(blk.N_plus.size(), 1);
                EXPECT_EQ(blk.N_plus[0], 5);
                EXPECT_EQ(blk.I_plus.size(), 1);
                EXPECT_EQ(blk.I_plus[0], 1);
                blk = S.blocks[8];
                EXPECT_EQ(blk.N_minus.size(), 1);
                EXPECT_EQ(blk.N_minus[0], 1);
                EXPECT_EQ(blk.I_minus.size(), 1);
                EXPECT_EQ(blk.I_minus[0], 1);
                EXPECT_EQ(blk.N_plus.size(), 1);
                EXPECT_EQ(blk.N_plus[0], 6);
                EXPECT_EQ(blk.I_plus.size(), 1);
                EXPECT_EQ(blk.I_plus[0], 1);
                blk = S.blocks[9];
                EXPECT_EQ(blk.N_minus.size(), 1);
                EXPECT_EQ(blk.N_minus[0], 4);
                EXPECT_EQ(blk.I_minus.size(), 1);
                EXPECT_EQ(blk.I_minus[0], 1);
                EXPECT_EQ(blk.N_plus.size(), 1);
                EXPECT_EQ(blk.N_plus[0], 6);
                EXPECT_EQ(blk.I_plus.size(), 1);
                EXPECT_EQ(blk.I_plus[0], 2);
            }


            // ============================================================================
            // uswap
            // ============================================================================

            TEST(Uswap, BothEmpty) {
                SiftState S = makeSingletonState(2);
                S.pi = { 0, 1 };
                EXPECT_EQ(uswap(S, {}, {}, S.pi), 0);
            }

            TEST(Uswap, NaEmpty) {
                SiftState S = makeSingletonState(3);
                S.pi = { 0, 1, 2 };
                EXPECT_EQ(uswap(S, {}, { 0 }, S.pi), 0);
            }

            TEST(Uswap, NbEmpty) {
                SiftState S = makeSingletonState(3);
                S.pi = { 0, 1, 2 };
                EXPECT_EQ(uswap(S, { 0 }, {}, S.pi), 0);
            }

            TEST(Uswap, ANeighbourLeftOfBNeighbour_PositiveDelta) {
                // Na at pi=0, Nb at pi=1 -> A moves right over B, crossing created -> +1
                SiftState S = makeSingletonState(4);
                S.pi = { 0, 1, 2, 3 };
                EXPECT_EQ(uswap(S, { 0 }, { 1 }, S.pi), 1);
            }

            TEST(Uswap, ANeighbourRightOfBNeighbour_NegativeDelta) {
                // Na at pi=1, Nb at pi=0 -> crossing resolved -> -1
                SiftState S = makeSingletonState(4);
                S.pi = { 0, 1, 2, 3 };
                EXPECT_EQ(uswap(S, { 1 }, { 0 }, S.pi), -1);
            }

            TEST(Uswap, CommonNeighbour_EqualSizes_Zero) {
                // Both point to same block -> (s-j)-(r-i) = (1-0)-(1-0) = 0
                SiftState S = makeSingletonState(3);
                S.pi = { 0, 1, 2 };
                EXPECT_EQ(uswap(S, { 0 }, { 0 }, S.pi), 0);
            }

            TEST(Uswap, TwoNaOneNb_PositiveDelta) {
                // Na: {0(pi=0), 2(pi=2)}, Nb: {1(pi=1)}
                // pa=0 < pb=1: c += (1-0)=1, i++
                // pa=2 > pb=1: c -= (2-1)=1, j++ -> done. Result = 0
                SiftState S = makeSingletonState(5);
                S.pi = { 0, 1, 2, 3, 4 };
                EXPECT_EQ(uswap(S, { 0, 2 }, { 1 }, S.pi), 0);
            }

            TEST(Uswap, OneNaTwoNb_NegativeDelta) {
                // Na: {2(pi=2)}, Nb: {0(pi=0), 3(pi=3)}
                // pa=2 > pb=0: c -= (1-0)=1, j++
                // pa=2 < pb=3: c += (2-1)=1, i++ -> done. Result = 0
                SiftState S = makeSingletonState(5);
                S.pi = { 0, 1, 2, 3, 4 };
                EXPECT_EQ(uswap(S, { 2 }, { 0, 3 }, S.pi), 0);
            }

            TEST(Uswap, AllNaLeftOfAllNb) {
                // Na: {0(pi=0), 1(pi=1)}, Nb: {2(pi=2), 3(pi=3)} -> +4
                SiftState S = makeSingletonState(4);
                S.pi = { 0, 1, 2, 3 };
                EXPECT_EQ(uswap(S, { 0, 1 }, { 2, 3 }, S.pi), 4);
            }

            TEST(Uswap, AllNaRightOfAllNb) {
                SiftState S = makeSingletonState(4);
                S.pi = { 0, 1, 2, 3 };
                EXPECT_EQ(uswap(S, { 2, 3 }, { 0, 1 }, S.pi), -4);
            }

            // ============================================================================
            // siftingSwap — delta verified against countTotalCrossings difference
            // ============================================================================

            // Apply siftingSwap(a_id, b_id), manually update B, then compare the reported
            // delta with the actual difference in countTotalCrossings.
            static void verifySiftingSwapDelta(
                SiftState S, BlockList B,   // passed by value so each call starts fresh
                int a_id, int b_id)
            {
                sortAdjacencies(S, B);
                int before = countTotalCrossings(S, B);

                int delta = siftingSwap(S, a_id, b_id);

                // Reflect the swap in B (a_id and b_id swapped positions)
                int pos_a = -1, pos_b = -1;
                for (int i = 0; i < static_cast<int>(B.size()); ++i) {
                    if (B[i] == a_id) pos_a = i;
                    if (B[i] == b_id) pos_b = i;
                }
                if (pos_a >= 0 && pos_b >= 0) std::swap(B[pos_a], B[pos_b]);

                int after = countTotalCrossings(S, B);

                EXPECT_EQ(delta, after - before)
                    << "siftingSwap reported delta=" << delta
                    << " but countTotalCrossings diff=" << (after - before)
                    << " (before=" << before << ", after=" << after << ")";
            }

            TEST(SiftingSwap, ParallelEdges_DeltaMatchesCounting) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{1,1} }, B);
                // Hubs are blocks 4 and 5; swap them
                verifySiftingSwapDelta(S, B, 4, 5);
            }

            TEST(SiftingSwap, CrossedEdges_DeltaMatchesCounting) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,1},{1,0} }, B);
                verifySiftingSwapDelta(S, B, 4, 5);
            }

            TEST(SiftingSwap, SwapUpperNodes_DeltaMatchesCounting) {
                BlockList B;
                SiftState S = makeTwoLayerState(3, 2, { {0,1},{1,0},{2,1} }, B);
                verifySiftingSwapDelta(S, B, 0, 1);
            }

            TEST(SiftingSwap, PiIncrementedForADecrementedForB) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{1,1} }, B);
                sortAdjacencies(S, B);
                int pi_a = S.pi[0], pi_b = S.pi[1];
                siftingSwap(S, 0, 1);
                EXPECT_EQ(S.pi[0], pi_a + 1);
                EXPECT_EQ(S.pi[1], pi_b - 1);
            }

            TEST(SiftingSwap, SwappingTwiceRestoresCrossings) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,1},{1,0} }, B);
                sortAdjacencies(S, B);
                int before = countTotalCrossings(S, B);

                siftingSwap(S, 4, 5);
                std::swap(B[4], B[5]);
                sortAdjacencies(S, B);
                siftingSwap(S, 5, 4);
                std::swap(B[4], B[5]);

                int after = countTotalCrossings(S, B);
                EXPECT_EQ(before, after);
            }

            TEST(SiftingSwap, DeltaZeroForDisjointNeighbourhoods) {
                // Two upper nodes, each connected to a distinct lower node, no shared
                // neighbour -> swapping their hubs changes no crossings at the hub layer
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{1,1} }, B);
                verifySiftingSwapDelta(S, B, 4, 5);
            }

            TEST(SiftingSwap, PaperExample) {
                SiftState S = buildPaperState();
                BlockList B;
                for (int i = 0; i < static_cast<int>(S.blocks.size()); i++)
                    B.push_back(i);
                sortAdjacencies(S, B);
                int delta = siftingSwap(S, 7, 8);
                EXPECT_EQ(delta, 2);
                Block& blk = S.blocks[0];
                EXPECT_EQ(blk.N_minus.size(), 0);
                EXPECT_EQ(blk.I_minus.size(), 0);
                EXPECT_EQ(blk.N_plus.size(), 1);
                EXPECT_EQ(blk.N_plus[0], 7);
                EXPECT_EQ(blk.I_plus.size(), 1);
                EXPECT_EQ(blk.I_plus[0], 0);
                blk = S.blocks[1];
                EXPECT_EQ(blk.N_minus.size(), 0);
                EXPECT_EQ(blk.I_minus.size(), 0);
                EXPECT_EQ(blk.N_plus.size(), 2);
                EXPECT_EQ(blk.N_plus[0], 3);
                EXPECT_EQ(blk.N_plus[1], 8);
                EXPECT_EQ(blk.I_plus.size(), 2);
                EXPECT_EQ(blk.I_plus[0], 0);
                EXPECT_EQ(blk.I_plus[1], 0);
                blk = S.blocks[2];
                EXPECT_EQ(blk.N_minus.size(), 0);
                EXPECT_EQ(blk.I_minus.size(), 0);
                EXPECT_EQ(blk.N_plus.size(), 1);
                EXPECT_EQ(blk.N_plus[0], 3);
                EXPECT_EQ(blk.I_plus.size(), 1);
                EXPECT_EQ(blk.I_plus[0], 1);
                blk = S.blocks[3];
                EXPECT_EQ(blk.N_minus.size(), 2);
                EXPECT_EQ(blk.N_minus[0], 1);
                EXPECT_EQ(blk.N_minus[1], 2);
                EXPECT_EQ(blk.I_minus.size(), 2);
                EXPECT_EQ(blk.I_minus[0], 0);
                EXPECT_EQ(blk.I_minus[1], 0);
                EXPECT_EQ(blk.N_plus.size(), 1);
                EXPECT_EQ(blk.N_plus[0], 4);
                EXPECT_EQ(blk.I_plus.size(), 1);
                EXPECT_EQ(blk.I_plus[0], 0);
                blk = S.blocks[4];
                EXPECT_EQ(blk.N_minus.size(), 1);
                EXPECT_EQ(blk.N_minus[0], 3);
                EXPECT_EQ(blk.I_minus.size(), 1);
                EXPECT_EQ(blk.I_minus[0], 0);
                EXPECT_EQ(blk.N_plus.size(), 2);
                EXPECT_EQ(blk.N_plus[0], 5);
                EXPECT_EQ(blk.N_plus[1], 11);
                EXPECT_EQ(blk.I_plus.size(), 2);
                EXPECT_EQ(blk.I_plus[0], 0);
                EXPECT_EQ(blk.I_plus[1], 0);
                blk = S.blocks[6];
                EXPECT_EQ(blk.N_minus.size(), 3);
                EXPECT_EQ(blk.N_minus[0], 5);
                EXPECT_EQ(blk.N_minus[1], 12);
                EXPECT_EQ(blk.N_minus[2], 11);
                EXPECT_EQ(blk.I_minus.size(), 3);
                EXPECT_EQ(blk.I_minus[0], 0);
                EXPECT_EQ(blk.I_minus[1], 0);
                EXPECT_EQ(blk.I_minus[2], 0);
                EXPECT_EQ(blk.N_plus.size(), 0);
                EXPECT_EQ(blk.I_plus.size(), 0);
                blk = S.blocks[7];
                EXPECT_EQ(blk.N_minus.size(), 1);
                EXPECT_EQ(blk.N_minus[0], 0);
                EXPECT_EQ(blk.I_minus.size(), 1);
                EXPECT_EQ(blk.I_minus[0], 0);
                EXPECT_EQ(blk.N_plus.size(), 1);
                EXPECT_EQ(blk.N_plus[0], 5);
                EXPECT_EQ(blk.I_plus.size(), 1);
                EXPECT_EQ(blk.I_plus[0], 1);
                blk = S.blocks[8];
                EXPECT_EQ(blk.N_minus.size(), 1);
                EXPECT_EQ(blk.N_minus[0], 1);
                EXPECT_EQ(blk.I_minus.size(), 1);
                EXPECT_EQ(blk.I_minus[0], 1);
                EXPECT_EQ(blk.N_plus.size(), 1);
                EXPECT_EQ(blk.N_plus[0], 6);
                EXPECT_EQ(blk.I_plus.size(), 1);
                EXPECT_EQ(blk.I_plus[0], 1);
                blk = S.blocks[9];
                EXPECT_EQ(blk.N_minus.size(), 1);
                EXPECT_EQ(blk.N_minus[0], 4);
                EXPECT_EQ(blk.I_minus.size(), 1);
                EXPECT_EQ(blk.I_minus[0], 1);
                EXPECT_EQ(blk.N_plus.size(), 1);
                EXPECT_EQ(blk.N_plus[0], 6);
                EXPECT_EQ(blk.I_plus.size(), 1);
                EXPECT_EQ(blk.I_plus[0], 2);
            }

            TEST(SiftingSwap, PaperExample2) {
                SiftState S = buildPaperStateV2();
                BlockList B;
                for (int i = 0; i < static_cast<int>(S.blocks.size()); i++)
                    B.push_back(i);
                sortAdjacencies(S, B);
                int delta = siftingSwap(S, 7, 8);
                EXPECT_EQ(delta, 2);
            }
            TEST(SiftingSwap, PaperExample3) {
                SiftState S = buildPaperStateV2();
                BlockList B;
                for (int i = 0; i < 7; i++)
                    B.push_back(i);
                B.push_back(10);
                for (int i = 7; i < 10; i++)
                    B.push_back(i);

                sortAdjacencies(S, B);
                int delta = siftingSwap(S, 7, 8);
                EXPECT_EQ(delta, 2);
            }
            TEST(SiftingSwap, PaperExample4) {
                SiftState S = buildPaperState();
                BlockList B;
                for (int i = 0; i < 6; i++)
                    B.push_back(i);
                B.push_back(8);
                for (int i = 6; i < 10; i++)
                    if (i != 8) B.push_back(i);

                sortAdjacencies(S, B);
                int delta = siftingSwap(S, 5, 8);
                EXPECT_EQ(delta, 0);

                std::swap(B[5], B[6]);
                sortAdjacencies(S, B);
                delta = siftingSwap(S, 4, 5);
                EXPECT_EQ(delta, 0);
            }

            // ============================================================================
            // siftingStep — chi_star verified against countTotalCrossings difference
            // ============================================================================

            static void verifySiftingStepChi(SiftState& S, BlockList& B, int a_id) {
                sortAdjacencies(S, B);
                int before = countTotalCrossings(S, B);
                int chi_star = siftingStep(S, B, a_id);
                int after = countTotalCrossings(S, B);

                EXPECT_LE(after, before) << "siftingStep must not increase crossings";
            }

            TEST(SiftingStep, AlreadyOptimal_ChiStarZero) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{1,1} }, B);
                sortAdjacencies(S, B);
                EXPECT_EQ(siftingStep(S, B, B[0]), 0);
            }

            TEST(SiftingStep, CrossedEdges_ChiStarMatchesCounting) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,1},{1,0} }, B);
                verifySiftingStepChi(S, B, B[S.fixed_position_count]);
            }

            TEST(SiftingStep, CrossedEdges_CrossingsNonIncreasing) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,1},{1,0} }, B);
                sortAdjacencies(S, B);
                int before = countTotalCrossings(S, B);
                BlockList snap = B;
                for (int a_id : snap)
                    siftingStep(S, B, a_id);
                EXPECT_LE(countTotalCrossings(S, B), before);
            }

            TEST(SiftingStep, ParallelEdges_NoCrossingsCreated) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{1,1} }, B);
                BlockList snap = B;
                for (int a_id : snap)
                    verifySiftingStepChi(S, B, a_id);
            }

            TEST(SiftingStep, BlockListContainsAllBlocksExactlyOnce) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{1,1} }, B);
                sortAdjacencies(S, B);
                siftingStep(S, B, B[0]);

                std::vector<int> sorted_B = B;
                std::sort(sorted_B.begin(), sorted_B.end());
                for (int i = 0; i < static_cast<int>(sorted_B.size()); ++i)
                    EXPECT_EQ(sorted_B[i], i);
            }

            TEST(SiftingStep, ThreeUpperThreeLower_FullRound_NonIncreasing) {
                BlockList B;
                SiftState S = makeTwoLayerState(3, 3, { {0,2},{1,1},{2,0},{0,1} }, B);
                sortAdjacencies(S, B);
                int before = countTotalCrossings(S, B);
                BlockList snap = B;
                for (int a_id : snap)
                    siftingStep(S, B, a_id);
                EXPECT_LE(countTotalCrossings(S, B), before);
            }

            TEST(SiftingStep, ChiStarMatchesCountingOnComplexGraph) {
                BlockList B;
                SiftState S = makeTwoLayerState(3, 3, { {0,2},{1,0},{2,1} }, B);
                verifySiftingStepChi(S, B, B[S.fixed_position_count]);
            }

            TEST(SiftingStep, AnchorBlocksUnmoved) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,1},{1,0} }, B);
                S.fixed_position_count = 1; // treat block B[0] as anchor
                sortAdjacencies(S, B);
                int anchor = B[0];
                siftingStep(S, B, B[1]); // sift the second block
                EXPECT_EQ(B[0], anchor) << "anchor block must stay at position 0";
            }

            // ============================================================================
            // countTotalCrossings
            // ============================================================================

            TEST(CountTotalCrossings, NoEdges) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, {}, B);
                EXPECT_EQ(countTotalCrossings(S, B), 0);
            }

            TEST(CountTotalCrossings, ParallelEdges_Zero) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{1,1} }, B);
                EXPECT_EQ(countTotalCrossings(S, B), 0);
            }

            TEST(CountTotalCrossings, CrossedEdges_NonZero) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,1},{1,0} }, B);
                EXPECT_GT(countTotalCrossings(S, B), 0);
            }

            TEST(CountTotalCrossings, SingleEdge_Zero) {
                BlockList B;
                SiftState S = makeTwoLayerState(1, 1, { {0,0} }, B);
                EXPECT_EQ(countTotalCrossings(S, B), 0);
            }

            TEST(CountTotalCrossings, ReorderingReducesCrossings) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,1},{1,0} }, B);
                int crossed = countTotalCrossings(S, B);

                // Swap the two lower-node blocks (2 and 3) in B to uncross
                int pos2 = -1, pos3 = -1;
                for (int i = 0; i < static_cast<int>(B.size()); ++i) {
                    if (B[i] == 2) pos2 = i;
                    if (B[i] == 3) pos3 = i;
                }
                ASSERT_GE(pos2, 0);
                ASSERT_GE(pos3, 0);
                std::swap(B[pos2], B[pos3]);

                EXPECT_LE(countTotalCrossings(S, B), crossed);
            }

            TEST(CountTotalCrossings, AgreeWithManualBilayerSum) {
                BlockList B;
                SiftState S = makeTwoLayerState(3, 3, { {0,2},{1,0},{2,1} }, B);
                orderLayersByBlockOrder(S, B);

                int manual = 0;
                auto it = S.g1_layers.begin();
                auto prev = it++;
                for (; it != S.g1_layers.end(); ++prev, ++it)
                    manual += countBilayerCrossings(prev->second, it->second, S.g1_out);

                EXPECT_EQ(countTotalCrossings(S, B), manual);
            }

            TEST(CountTotalCrossings, RepeatedCallSameResult) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,1},{1,0} }, B);
                int first = countTotalCrossings(S, B);
                int second = countTotalCrossings(S, B);
                EXPECT_EQ(first, second);
            }

            // ============================================================================
            // buildBlocks
            // ============================================================================

            TEST(BuildBlocks, AllSingletons_BlockCountEqualsNodeCount) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{1,1} }, B);
                EXPECT_EQ(S.blocks.size(), S.g1_nodes.size());
            }

            TEST(BuildBlocks, NoEmptyBlocks) {
                BlockList B;
                SiftState S = makeTwoLayerState(3, 3, { {0,0},{1,1},{2,2} }, B);
                for (const auto& blk : S.blocks)
                    EXPECT_FALSE(blk.g1_nodes.empty());
            }

            TEST(BuildBlocks, BlockIdConsistency) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{0,1},{1,0} }, B);
                for (int bid = 0; bid < static_cast<int>(S.blocks.size()); ++bid)
                    for (int g1_idx : S.blocks[bid].g1_nodes)
                        EXPECT_EQ(S.g1_nodes[g1_idx].block_id, bid);
            }

            TEST(BuildBlocks, PiResizedToBlockCount) {
                BlockList B;
                SiftState S = makeTwoLayerState(2, 2, { {0,0},{1,1} }, B);
                EXPECT_EQ(S.pi.size(), S.blocks.size());
            }

            TEST(BuildBlocks, EveryNodeBelongsToExactlyOneBlock) {
                BlockList B;
                SiftState S = makeTwoLayerState(3, 2, { {0,0},{1,1},{2,0} }, B);
                std::unordered_set<int> seen;
                for (int bid = 0; bid < static_cast<int>(S.blocks.size()); ++bid)
                    for (int g1_idx : S.blocks[bid].g1_nodes)
                        EXPECT_TRUE(seen.insert(g1_idx).second)
                        << "g1_node " << g1_idx << " appears in more than one block";
                EXPECT_EQ(seen.size(), S.g1_nodes.size());
            }

            // ============================================================================
            // levels / getNodeAtLevel
            // ============================================================================
            TEST(GetNodeAtLevel, ReturnsCorrectNodeForUpperLayer) {
                SiftState S = makeSingletonState(2);
                S.g1_nodes[0].g1_layer = 2;
                S.g1_nodes[1].g1_layer = 4;
                EXPECT_EQ(getNodeAtLevel(S, S.blocks[0], 2), 0);
                EXPECT_EQ(getNodeAtLevel(S, S.blocks[1], 4), 1);
            }

            TEST(GetNodeAtLevel, ReturnsMinusOneWhenMissing) {
                SiftState S = makeSingletonState(1);
                S.g1_nodes[0].g1_layer = 2;
                EXPECT_EQ(getNodeAtLevel(S, S.blocks[0], 99), -1);
            }

            TEST(GetNodeAtLevel, HubFoundAtLayerOne) {
                BlockList B;
                SiftState S = makeTwoLayerState(1, 1, { {0,0} }, B);
                // Hub node is block 2, at g1_layer 1
                EXPECT_EQ(getNodeAtLevel(S, S.blocks[2], 1), 2);
            }

            TEST(GetNodeAtLevel, UpperNodeNotFoundAtHubLayer) {
                BlockList B;
                SiftState S = makeTwoLayerState(1, 1, { {0,0} }, B);
                // Upper node (block 0) is at g1_layer 0, not 1
                EXPECT_EQ(getNodeAtLevel(S, S.blocks[0], 1), -1);
            }


            // ============================================================================
            // Check Minimization with the Paper Example
            // ============================================================================
            TEST(CrossingMinimization, PaperExample) {
                SiftState S = buildPaperState();
                BlockList B;
                for (int i = 0; i < static_cast<int>(S.blocks.size()); ++i)
                    B.push_back(i);

                int before = countTotalCrossings(S, B);
                sortAdjacencies(S, B);

                int numblocks = static_cast<int>(B.size());
                for (int round = 0; round < 10; round++) {
                    int chi = 0;
                    BlockList snapshot = B;
                    for (int i = S.fixed_position_count; i < numblocks; i++)
                        chi += siftingStep(S, B, snapshot[i]);
                }

                EXPECT_LE(countTotalCrossings(S, B), before) << "Should be able to reduce crossings";
            }
            TEST(CrossingMinimization, PaperExample2) {
                SiftState S = buildPaperState();
                S.fixed_position_count = 3; // treat blocks 0,1,2 as anchors
                BlockList B;
                for (int i = 0; i < static_cast<int>(S.blocks.size()); ++i)
                    B.push_back(i);
                int before = countTotalCrossings(S, B);
                sortAdjacencies(S, B);

                int numblocks = static_cast<int>(B.size());
                for (int round = 0; round < 10; round++) {
                    int chi = 0;
                    BlockList snapshot = B;
                    for (int i = S.fixed_position_count; i < numblocks; i++)
                        chi += siftingStep(S, B, snapshot[i]);
                }

                EXPECT_LE(countTotalCrossings(S, B), before) << "Should be able to reduce crossings";
            }

            // ── orderBlocks ─────────────────────────────────────────────-────────────────
            //
            // This is the equivalent of the buildBlockOrder in minimizeCrossings.cpp, but 
            // adapted to work with the SiftState and its g1_layers structure.

            static BlockList orderBlocksByLayerPropagation(SiftState& S) {
                // Step 1: assign initial pi from the first layer's current node order,
                // giving every block a well-defined starting position.
                S.pi.assign(S.blocks.size(), INT_MAX);
                {
                    const auto& first_layer = S.g1_layers.begin()->second;
                    for (int pos = 0; pos < static_cast<int>(first_layer.size()); ++pos)
                        S.pi[S.g1_nodes[first_layer[pos]].block_id] = pos;
                }

                // Step 2: propagate downward, layer by layer.
                bool first = true;
                for (auto& [layer_key, nodes] : S.g1_layers) {
                    if (first) { first = false; continue; } // first layer is fixed

                    // Compute left(A) for each G1 node in this layer.
                    std::unordered_map<int, int> left_of;
                    for (int node_idx : nodes)
                        left_of[node_idx] = INT_MAX;

                    for (int node_idx : nodes)
                        for (int parent : S.g1_in[node_idx])
                            left_of[node_idx] = std::min(
                                left_of[node_idx],
                                S.pi[S.g1_nodes[parent].block_id]);

                    // Stable sort this layer by left(A), preserving relative order for
                    // nodes with no parents (left_of == INT_MAX).
                    std::stable_sort(nodes.begin(), nodes.end(),
                        [&](int a, int b) {
                            return left_of.at(a) < left_of.at(b);
                        });

                    // Update pi with local positions so the next layer sees consistent
                    // parent positions. Global pi is rebuilt in the final step.
                    for (int pos = 0; pos < static_cast<int>(nodes.size()); ++pos)
                        S.pi[S.g1_nodes[nodes[pos]].block_id] = pos;
                }

                // Step 3: build B in layer-traversal order, skipping duplicate block ids
                // (blocks that span multiple layers via dummy chains appear only once).
                BlockList B;
                B.reserve(S.blocks.size());
                std::unordered_set<int> visited;
                for (auto& [layer_key, nodes] : S.g1_layers)
                    for (int node_idx : nodes) {
                        int bid = S.g1_nodes[node_idx].block_id;
                        if (visited.insert(bid).second)
                            B.push_back(bid);
                    }

                // Step 4: assign final globally consistent pi from B.
                for (int pos = 0; pos < static_cast<int>(B.size()); ++pos)
                    S.pi[B[pos]] = pos;

                return B;
            }


            TEST(CrossingMinimization, PaperExampleWithGoodInitialOrder) {
                SiftState S = buildPaperState();
                BlockList B = orderBlocksByLayerPropagation(S);

                int before = countTotalCrossings(S, B);
                sortAdjacencies(S, B);

                int numblocks = static_cast<int>(B.size());
                for (int round = 0; round < 10; round++) {
                    int chi = 0;
                    BlockList snapshot = B;
                    for (int i = S.fixed_position_count; i < numblocks; i++)
                        chi += siftingStep(S, B, snapshot[i]);
                }
                EXPECT_EQ(countTotalCrossings(S, B), 0) << "Should already be optimal with good initial order";

                EXPECT_LE(countTotalCrossings(S, B), before) << "Should be able to reduce crossings";
            }

        } // namespace minimizeCrossings
    } // namespace graphicalhypergraph_tests
} // namespace hypergraph_logic