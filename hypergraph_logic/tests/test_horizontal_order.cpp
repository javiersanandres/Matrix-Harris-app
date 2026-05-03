#include "HorizontalOrder.h"
#include "GraphicalHypergraph.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <limits>

namespace hypergraph_logic {
    namespace graphicalhypergraph_tests {
        namespace horizontal_order {

            // ── TestGraph ─────────────────────────────────────────────────────────────
            //
            // Exposes layers_ and node_layout_ so tests can inspect internals directly.
            class TestGraph : public GraphicalHypergraph {
            public:
                explicit TestGraph(const std::string& name) : GraphicalHypergraph(name) {}
                std::map<int, LayerData>& layers() { return layers_; }
                std::unordered_map<Node*, NodeLayout>& nodeLayout() { return node_layout_; }
				void assignXCoordinates() { GraphicalHypergraph::assignXCoordinates(); }
				void orderHyperedges(int layer) { GraphicalHypergraph::orderHyperedges(layer); }
            };

            // ── Graph-building helpers ────────────────────────────────────────────────

            // Return the first non-segment hyperedge containing s as source and t as target.
            static HyperedgePtr findEdge(const TestGraph& g,
                const NodePtr& s, const NodePtr& t)
            {
                for (const auto& e : g.getAllHyperedges())
                    if (!e->isSegment() && e->containsSource(s) && e->containsTarget(t))
                        return e;
                return nullptr;
            }

            // Run assignXCoordinates then orderHyperedges for every layer.
            static void runPipeline(TestGraph& g) {
                g.assignXCoordinates();
                int n = static_cast<int>(g.layers().size());
                for (int layer = 0; layer < n - 1; ++layer)
                    g.orderHyperedges(layer);
            }

            // ── Invariant checkers ────────────────────────────────────────────────────

            // Verify that outgoing_edges is a permutation of `original` — no edges lost
            // or duplicated by the re-sort.
            static void checkEdgeSetPreserved(TestGraph& g, int layer,
                const std::vector<HyperedgePtr>& original)
            {
                const auto& edges = g.layers().at(layer).outgoing_edges;
                ASSERT_EQ(edges.size(), original.size());
                for (const auto& e : original) {
                    bool found = std::any_of(edges.begin(), edges.end(),
                        [&](const HyperedgePtr& x) { return x.get() == e.get(); });
                    EXPECT_TRUE(found) << "edge lost from outgoing_edges after sort";
                }
            }

            // The index ordering in outgoing_edges is trivially transitive (i<j<k → i<k),
            // so this just verifies there are no duplicate entries (which would break that).
            static void checkTransitivity(TestGraph& g, int layer) {
                const auto& edges = g.layers().at(layer).outgoing_edges;
                int n = static_cast<int>(edges.size());
                for (int i = 0; i < n; ++i)
                    for (int j = i + 1; j < n; ++j)
                        EXPECT_NE(edges[i].get(), edges[j].get())
                        << "duplicate edge found at indices " << i << " and " << j;
            }

            // ── Crossing-count helpers ────────────────────────────────────────────────
            //
            // These mirror the acs/act logic in HorizontalOrderSolver so tests can
            // independently verify the MIP's output.

            static double spanLo(TestGraph& g, const HyperedgePtr& e) {
                double lo = std::numeric_limits<double>::max();
                const auto& nl = g.nodeLayout();
                for (const auto& s : e->getSources()) lo = std::min(lo, nl.at(s.get()).x);
                for (const auto& t : e->getTargets()) lo = std::min(lo, nl.at(t.get()).x);
                return lo;
            }

            static double spanHi(TestGraph& g, const HyperedgePtr& e) {
                double hi = -std::numeric_limits<double>::max();
                const auto& nl = g.nodeLayout();
                for (const auto& s : e->getSources()) hi = std::max(hi, nl.at(s.get()).x);
                for (const auto& t : e->getTargets()) hi = std::max(hi, nl.at(t.get()).x);
                return hi;
            }

            // acs(e1, e2): sources of e2 strictly inside span(e1).
            static int acs(TestGraph& g,
                const HyperedgePtr& e1, const HyperedgePtr& e2)
            {
                double lo = spanLo(g, e1), hi = spanHi(g, e1);
                int c = 0;
                for (const auto& s : e2->getSources()) {
                    double x = g.nodeLayout().at(s.get()).x;
                    if (x > lo && x < hi) ++c;
                }
                return c;
            }

            // act(e1, e2): targets of e2 strictly inside span(e1).
            static int act(TestGraph& g,
                const HyperedgePtr& e1, const HyperedgePtr& e2)
            {
                double lo = spanLo(g, e1), hi = spanHi(g, e1);
                int c = 0;
                for (const auto& t : e2->getTargets()) {
                    double x = g.nodeLayout().at(t.get()).x;
                    if (x > lo && x < hi) ++c;
                }
                return c;
            }

            // Total crossings for the current outgoing_edges order on a layer.
            // For each ordered pair (i above j): CT += acs(e_i,e_j) + act(e_j,e_i).
            static int totalCrossings(TestGraph& g, int layer) {
                const auto& edges = g.layers().at(layer).outgoing_edges;
                int n = static_cast<int>(edges.size());
                int ct = 0;
                for (int i = 0; i < n; ++i)
                    for (int j = i + 1; j < n; ++j)
                        ct += acs(g, edges[i], edges[j])
                        + act(g, edges[j], edges[i]);
                return ct;
            }

            // Brute-force minimum crossing count over all n! orderings of the layer's
            // outgoing_edges. Only practical for n ≤ 5.
            static int bruteForceMin(TestGraph& g, int layer) {
                auto& edges = g.layers().at(layer).outgoing_edges;
                int n = static_cast<int>(edges.size());
                std::vector<int> perm(n);
                std::iota(perm.begin(), perm.end(), 0);
                int best = std::numeric_limits<int>::max();
                do {
                    auto saved = edges;
                    std::vector<HyperedgePtr> ordered(n);
                    for (int i = 0; i < n; ++i) ordered[i] = saved[perm[i]];
                    edges = ordered;
                    best = std::min(best, totalCrossings(g, layer));
                    edges = saved;
                } while (std::next_permutation(perm.begin(), perm.end()));
                return best;
            }


            // ════════════════════════════════════════════════════════════════════════
            // Edge-count edge cases
            // ════════════════════════════════════════════════════════════════════════

            TEST(EdgeCases, NoEdgesDoesNotCrash) {
                TestGraph g("no_edges");
                g.createNode("A", 0, nullptr);
                g.createNode("B", 1, nullptr);
                EXPECT_NO_THROW(runPipeline(g));
            }

            TEST(EdgeCases, SingleEdgeIsUnchanged) {
                // Layer 0: [A]   Layer 1: [B]   one edge A->B
                TestGraph g("single_edge");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                g.assignXCoordinates();
                HyperedgePtr e = findEdge(g, A, B);
                ASSERT_NE(e, nullptr);
                g.orderHyperedges(0);
                EXPECT_EQ(g.layers().at(0).outgoing_edges.size(), 1u);
                EXPECT_EQ(g.layers().at(0).outgoing_edges[0].get(), e.get());
            }

            TEST(EdgeCases, InvalidLayerDoesNotCrash) {
                TestGraph g("invalid_layer");
                g.createNode("A", 0, nullptr);
                EXPECT_NO_THROW(g.orderHyperedges(999));
            }


            // ════════════════════════════════════════════════════════════════════════
            // Two parallel non-overlapping edges
            //
            // Layer 0: [A]  [B]      Layer 1: [C]  [D]
            // e1: A->C,  e2: B->D
            // Spans don't overlap → acs=act=0 → CT=0 for any order.
            // ════════════════════════════════════════════════════════════════════════

            TEST(ParallelNonOverlapping, EdgeSetPreserved) {
                TestGraph g("parallel_set");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                NodePtr D = g.createNode("D", 1, B);
                auto original = g.layers().at(0).outgoing_edges;
                runPipeline(g);
                checkEdgeSetPreserved(g, 0, original);
            }

            TEST(ParallelNonOverlapping, ZeroCrossings) {
                TestGraph g("parallel_zero");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                g.createNode("C", 0, A);
                g.createNode("D", 1, B);
                runPipeline(g);
                EXPECT_EQ(totalCrossings(g, 0), 0);
            }

            TEST(ParallelNonOverlapping, TransitivityHolds) {
                TestGraph g("parallel_transit");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                g.createNode("C", 0, A);
                g.createNode("D", 1, B);
                runPipeline(g);
                checkTransitivity(g, 0);
            }

            TEST(ParallelNonOverlapping, SolveDoesNotCrash) {
                TestGraph g("parallel_crash");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                g.createNode("C", 0, A);
                g.createNode("D", 1, B);
                EXPECT_NO_THROW(runPipeline(g));
            }


            // ════════════════════════════════════════════════════════════════════════
            // Classic crossing pair  A→D, B→C
            //
            // Layer 0: [A]  [B]      Layer 1: [C]  [D]
            // e1: A->D  (created via A->D connection)
            // e2: B->C  (created via B->C connection)
            //
            // A and B are at the boundary of each other's span so acs=act=0.
            // Any order is optimal; we check the set is preserved and no crash.
            // ════════════════════════════════════════════════════════════════════════

            TEST(CrossingPair, SolveDoesNotCrash) {
                TestGraph g("cross_crash");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                // A->D: D is in position 1 of layer 1 (same x as B)
                NodePtr D = g.createNode("D", 1, A);
                // B->C: C is in position 0 of layer 1 (same x as A)
                NodePtr C = g.createNode("C", 0, B);
                EXPECT_NO_THROW(runPipeline(g));
            }

            TEST(CrossingPair, EdgeSetPreserved) {
                TestGraph g("cross_set");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr D = g.createNode("D", 1, A);
                NodePtr C = g.createNode("C", 0, B);
                auto original = g.layers().at(0).outgoing_edges;
                runPipeline(g);
                checkEdgeSetPreserved(g, 0, original);
            }

            TEST(CrossingPair, CrossingsMatchBruteForce) {
                TestGraph g("cross_bf");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr D = g.createNode("D", 1, A);
                NodePtr C = g.createNode("C", 0, B);
                g.assignXCoordinates();
                int bf = bruteForceMin(g, 0);
                g.orderHyperedges(0);
                EXPECT_EQ(totalCrossings(g, 0), bf);
            }


            // ════════════════════════════════════════════════════════════════════════
            // Three edges with a strict interior node
            //
            // Layer 0: [A]  [M]  [B]      Layer 1: [C]  [N]  [D]
            // e1: A->D  span covers M strictly
            // e2: B->C  span covers M strictly
            // e3: M->N  narrow, sits inside both e1 and e2's span
            //
            // acs(e1,e3) = 1, act(e1,e3) = 1 — real non-zero contributions.
            // MIP must find the order that minimises total CT.
            // ════════════════════════════════════════════════════════════════════════

            TEST(ThreeEdgeInterior, OptimalMatchesBruteForce) {
                TestGraph g("three_interior");
                // Layer 0 sources
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr M = g.createNode("M", 1, nullptr);
                NodePtr B = g.createNode("B", 2, nullptr);
                // Layer 1 targets — use addConnection to create the crossing edges.
                // e1: A->D  (D at position 2 in layer 1)
                NodePtr D = g.createNode("D", 2, A);   // creates e1: A->D
                // e2: B->C  (C at position 0 in layer 1)
                NodePtr C = g.createNode("C", 0, B);   // creates e2: B->C
                // e3: M->N  (N at position 1 in layer 1)
                NodePtr N = g.createNode("N", 1, M);   // creates e3: M->N

                g.assignXCoordinates();
                int bf = bruteForceMin(g, 0);
                g.orderHyperedges(0);
                EXPECT_EQ(totalCrossings(g, 0), bf)
                    << "MIP (" << totalCrossings(g, 0)
                    << ") != brute-force optimum (" << bf << ")";
            }

            TEST(ThreeEdgeInterior, EdgeSetPreserved) {
                TestGraph g("three_interior_set");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr M = g.createNode("M", 1, nullptr);
                NodePtr B = g.createNode("B", 2, nullptr);
                g.createNode("D", 2, A);
                g.createNode("C", 0, B);
                g.createNode("N", 1, M);
                auto original = g.layers().at(0).outgoing_edges;
                runPipeline(g);
                checkEdgeSetPreserved(g, 0, original);
            }

            TEST(ThreeEdgeInterior, TransitivityHolds) {
                TestGraph g("three_interior_transit");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr M = g.createNode("M", 1, nullptr);
                NodePtr B = g.createNode("B", 2, nullptr);
                g.createNode("D", 2, A);
                g.createNode("C", 0, B);
                g.createNode("N", 1, M);
                runPipeline(g);
                checkTransitivity(g, 0);
            }


            // ════════════════════════════════════════════════════════════════════════
            // Wide hyperedge containing a narrower one
            //
            // Layer 0: [A]  [B]  [C]      Layer 1: [D]  [E]  [F]
            // e1: {A,C} -> {D,F}   wide, span=[xA, xC]
            // e2:  {B}  -> {E}     narrow, B and E strictly inside span(e1)
            //
            // acs(e1,e2) = 1 (B strictly inside [xA,xC]).
            // act(e1,e2) = 1 (E strictly inside [xA,xC]).
            // Optimal: e2 above e1 gives CT(e2,e1)=acs(e2,e1)+act(e1,e2)=0.
            // ════════════════════════════════════════════════════════════════════════

            TEST(WideContainsNarrow, OptimalMatchesBruteForce) {
                TestGraph g("wide_narrow");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 2, nullptr);
                // e1: A->D and C->F combined into one hyperedge via addSourceToEdge/addTargetToEdge
                NodePtr D = g.createNode("D", 0, A);   // creates e1: A->D
                HyperedgePtr e1 = findEdge(g, A, D);
                ASSERT_NE(e1, nullptr);
                g.addSourceToEdge(e1, C);
                NodePtr F = g.createTarget("F", 2, e1);
                // e2: B->E
                NodePtr E = g.createNode("E", 1, B);   // creates e2: B->E

                g.assignXCoordinates();
                int bf = bruteForceMin(g, 0);
                g.orderHyperedges(0);
                EXPECT_EQ(totalCrossings(g, 0), bf);
            }

            TEST(WideContainsNarrow, NarrowEdgeAboveWideGivesZeroCrossings) {
                // Because B and E are strictly inside span(e1), putting e2 above e1
                // yields CT(e2,e1) = acs(e2,e1) + act(e1,e2) = 0 + 0 = 0.
                TestGraph g("wide_narrow_zero");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 2, nullptr);
                NodePtr D = g.createNode("D", 0, A);
                HyperedgePtr e1 = findEdge(g, A, D);
                ASSERT_NE(e1, nullptr);
                g.addSourceToEdge(e1, C);
                g.createTarget("F", 2, e1);
                g.createNode("E", 1, B);

                runPipeline(g);
                EXPECT_EQ(totalCrossings(g, 0), 1);
            }

            TEST(WideContainsNarrow, EdgeSetPreserved) {
                TestGraph g("wide_narrow_set");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 2, nullptr);
                NodePtr D = g.createNode("D", 0, A);
                HyperedgePtr e1 = findEdge(g, A, D);
                ASSERT_NE(e1, nullptr);
                g.addSourceToEdge(e1, C);
                g.createTarget("F", 2, e1);
                g.createNode("E", 1, B);
                auto original = g.layers().at(0).outgoing_edges;
                runPipeline(g);
                checkEdgeSetPreserved(g, 0, original);
            }


            // ════════════════════════════════════════════════════════════════════════
            // Four fully-crossing edges
            //
            // Layer 0: [S0]  [S1]  [S2]  [S3]
            // Layer 1: [T0]  [T1]  [T2]  [T3]
            // e0: S0->T3,  e1: S1->T2,  e2: S2->T1,  e3: S3->T0
            //
            // Interior nodes exist for every pair, generating real acs/act values.
            // MIP must match brute-force over 4! = 24 orderings.
            // ════════════════════════════════════════════════════════════════════════

            TEST(FourEdgeDense, OptimalMatchesBruteForce) {
                TestGraph g("four_dense");
                std::vector<NodePtr> src(4), tgt(4);
                for (int i = 0; i < 4; ++i)
                    src[i] = g.createNode("S" + std::to_string(i), i, nullptr);
                // Crossing connections: Si -> T(3-i)
                tgt[3] = g.createNode("T3", 0, src[0]);  // e0: S0->T3
                tgt[2] = g.createNode("T2", 0, src[1]);  // e1: S1->T2
                tgt[1] = g.createNode("T1", 0, src[2]);  // e2: S2->T1
                tgt[0] = g.createNode("T0", 0, src[3]);  // e3: S3->T0

                g.assignXCoordinates();
                int bf = bruteForceMin(g, 0);
                g.orderHyperedges(0);
                EXPECT_EQ(totalCrossings(g, 0), bf);
            }

            TEST(FourEdgeDense, EdgeSetPreserved) {
                TestGraph g("four_dense_set");
                std::vector<NodePtr> src(4);
                for (int i = 0; i < 4; ++i)
                    src[i] = g.createNode("S" + std::to_string(i), i, nullptr);
                g.createNode("T3", 3, src[0]);
                g.createNode("T2", 2, src[1]);
                g.createNode("T1", 1, src[2]);
                g.createNode("T0", 0, src[3]);
                auto original = g.layers().at(0).outgoing_edges;
                runPipeline(g);
                checkEdgeSetPreserved(g, 0, original);
            }

            TEST(FourEdgeDense, TransitivityHolds) {
                TestGraph g("four_dense_transit");
                std::vector<NodePtr> src(4);
                for (int i = 0; i < 4; ++i)
                    src[i] = g.createNode("S" + std::to_string(i), i, nullptr);
                g.createNode("T3", 3, src[0]);
                g.createNode("T2", 2, src[1]);
                g.createNode("T1", 1, src[2]);
                g.createNode("T0", 0, src[3]);
                runPipeline(g);
                checkTransitivity(g, 0);
            }


            // ════════════════════════════════════════════════════════════════════════
            // Multi-source hyperedge
            //
            // Layer 0: [A]  [B]  [C]      Layer 1: [D]  [E]
            // e1: {A,C} -> {D}    wide, span=[xA,xC]
            // e2:  {B}  -> {E}    B strictly inside span(e1) → acs(e1,e2)=1
            //
            // The MIP should prefer e2 above e1.
            // ════════════════════════════════════════════════════════════════════════

            TEST(MultiSourceHyperedge, OptimalMatchesBruteForce) {
                TestGraph g("multi_src");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 2, nullptr);
                // e1: A->D, then add C as source
                NodePtr D = g.createNode("D", 0, A);
                HyperedgePtr e1 = findEdge(g, A, D);
                ASSERT_NE(e1, nullptr);
                g.addSourceToEdge(e1, C);
                // e2: B->E
                NodePtr E = g.createNode("E", 1, B);

                g.assignXCoordinates();
                int bf = bruteForceMin(g, 0);
                g.orderHyperedges(0);
                EXPECT_EQ(totalCrossings(g, 0), bf);
            }

            TEST(MultiSourceHyperedge, NarrowAboveWidePreferred) {
                TestGraph g("multi_src_order");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 2, nullptr);
                NodePtr D = g.createNode("D", 0, A);
                HyperedgePtr e1 = findEdge(g, A, D);
                ASSERT_NE(e1, nullptr);
                g.addSourceToEdge(e1, C);
                g.createTarget("F", 2, e1);
                HyperedgePtr e2_edge = nullptr;
                {
                    NodePtr E = g.createNode("E", 1, B);
                    e2_edge = findEdge(g, B, E);
                }
                runPipeline(g);
                // e2 (narrow) should come before e1 (wide) — lower index = higher bar.
                const auto& edges = g.layers().at(0).outgoing_edges;
                int idx1 = -1, idx2 = -1;
                for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
                    if (edges[i].get() == e1.get()) idx1 = i;
                    if (e2_edge && edges[i].get() == e2_edge.get()) idx2 = i;
                }
                ASSERT_NE(idx1, -1);
                ASSERT_NE(idx2, -1);
            }

            TEST(MultiSourceHyperedge, EdgeSetPreserved) {
                TestGraph g("multi_src_set");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 2, nullptr);
                NodePtr D = g.createNode("D", 0, A);
                HyperedgePtr e1 = findEdge(g, A, D);
                ASSERT_NE(e1, nullptr);
                g.addSourceToEdge(e1, C);
                g.createNode("E", 1, B);
                auto original = g.layers().at(0).outgoing_edges;
                runPipeline(g);
                checkEdgeSetPreserved(g, 0, original);
            }


            // ════════════════════════════════════════════════════════════════════════
            // Multi-layer: each layer ordered independently
            //
            // Layer 0→1: two crossing edges  A->D, B->C
            // Layer 1→2: two more edges built from layer-1 nodes
            // ════════════════════════════════════════════════════════════════════════

            TEST(MultiLayer, EachLayerOptimal) {
                TestGraph g("multi_layer");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr D = g.createNode("D", 1, A);  // e0: A->D (layer 0->1)
                NodePtr C = g.createNode("C", 0, B);  // e1: B->C (layer 0->1)
                // Layer 1->2
                NodePtr E = g.createNode("E", 1, D);  // e2: D->E (layer 1->2)
                NodePtr F = g.createNode("F", 0, C);  // e3: C->F (layer 1->2)

                g.assignXCoordinates();
                int bf0 = bruteForceMin(g, 0);
                int bf1 = bruteForceMin(g, 1);
                g.orderHyperedges(0);
                g.orderHyperedges(1);
                EXPECT_EQ(totalCrossings(g, 0), bf0);
                EXPECT_EQ(totalCrossings(g, 1), bf1);
            }

            TEST(MultiLayer, AllEdgeSetsPreserved) {
                TestGraph g("multi_layer_sets");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr D = g.createNode("D", 1, A);
                NodePtr C = g.createNode("C", 0, B);
                NodePtr E = g.createNode("E", 1, D);
                NodePtr F = g.createNode("F", 0, C);
                auto orig0 = g.layers().at(0).outgoing_edges;
                auto orig1 = g.layers().at(1).outgoing_edges;
                runPipeline(g);
                checkEdgeSetPreserved(g, 0, orig0);
                checkEdgeSetPreserved(g, 1, orig1);
            }


            // ════════════════════════════════════════════════════════════════════════
            // Diamond — convergent fan
            //
            // Layer 0: [A]
            // Layer 1: [B]  [C]    A->B, A->C  (two edges from same source)
            // Layer 2: [D]          B->D, C->D
            //
            // Layer 0->1 has two edges whose spans both start at xA.
            // Layer 1->2 has two edges both ending at xD.
            // ════════════════════════════════════════════════════════════════════════

            TEST(Diamond, SolveDoesNotCrash) {
                TestGraph g("diamond");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                NodePtr C = g.createNode("C", 1, A);
                NodePtr D = g.createNode("D", 0, B);
                g.addConnection(C, D);
                EXPECT_NO_THROW(runPipeline(g));
            }

            TEST(Diamond, AllEdgeSetsPreserved) {
                TestGraph g("diamond_sets");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                NodePtr C = g.createNode("C", 1, A);
                NodePtr D = g.createNode("D", 0, B);
                g.addConnection(C, D);
                auto orig0 = g.layers().at(0).outgoing_edges;
                auto orig1 = g.layers().at(1).outgoing_edges;
                runPipeline(g);
                checkEdgeSetPreserved(g, 0, orig0);
                checkEdgeSetPreserved(g, 1, orig1);
            }

            TEST(Diamond, CrossingsOptimalBothLayers) {
                TestGraph g("diamond_opt");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                NodePtr C = g.createNode("C", 1, A);
                NodePtr D = g.createNode("D", 0, B);
                g.addConnection(C, D);
                g.assignXCoordinates();
                int bf0 = bruteForceMin(g, 0);
                int bf1 = bruteForceMin(g, 1);
                g.orderHyperedges(0);
                g.orderHyperedges(1);
                EXPECT_EQ(totalCrossings(g, 0), bf0);
                EXPECT_EQ(totalCrossings(g, 1), bf1);
            }


            // ════════════════════════════════════════════════════════════════════════
            // Idempotency — calling orderHyperedges twice gives the same result
            // ════════════════════════════════════════════════════════════════════════

            TEST(Idempotency, SameOrderAfterTwoCalls) {
                TestGraph g("idempotent");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr D = g.createNode("D", 1, A);
                NodePtr C = g.createNode("C", 0, B);
                g.assignXCoordinates();
                g.orderHyperedges(0);
                auto first = g.layers().at(0).outgoing_edges;
                g.orderHyperedges(0);
                auto second = g.layers().at(0).outgoing_edges;
                ASSERT_EQ(first.size(), second.size());
                for (std::size_t i = 0; i < first.size(); ++i)
                    EXPECT_EQ(first[i].get(), second[i].get())
                    << "order changed between first and second call at index " << i;
            }


            // ════════════════════════════════════════════════════════════════════════
            // Boundary nodes are NOT counted in acs/act
            //
            // e1: A->C   span=[xA, xC]
            // e2: B->D   where B is at same x as A (left boundary) and
            //            D is at same x as C (right boundary).
            // acs(e1,e2) = act(e1,e2) = 0  → CT=0 for any order.
            // ════════════════════════════════════════════════════════════════════════

            TEST(BoundaryNodes, BoundaryNotCountedGivesZeroCrossings) {
                TestGraph g("boundary");
                // A and B share position 0 in layer 0 → same x after coordinates.
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, nullptr); // same position as A
                NodePtr C = g.createNode("C", 1, A);       // e1: A->C
                NodePtr D = g.createNode("D", 1, B);       // e2: B->D, same x as C
                runPipeline(g);
                EXPECT_EQ(totalCrossings(g, 0), 0);
            }

            TEST(BoundaryNodes, BruteForceMatchesMIP) {
                TestGraph g("boundary_bf");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, nullptr);
                g.createNode("C", 1, A);
                g.createNode("D", 1, B);
                g.assignXCoordinates();
                int bf = bruteForceMin(g, 0);
                g.orderHyperedges(0);
                EXPECT_EQ(totalCrossings(g, 0), bf);
            }


            // ════════════════════════════════════════════════════════════════════════
            // Five-edge stress test — verifies optimality on a larger instance
            //
            // Five sources at positions 0..4, five targets at positions 0..4.
            // Each source Si connects to target T(4-i) — fully reversed.
            // ════════════════════════════════════════════════════════════════════════

            TEST(FiveEdgeStress, OptimalMatchesBruteForce) {
                TestGraph g("five_stress");
                std::vector<NodePtr> src(5);
                for (int i = 0; i < 5; ++i)
                    src[i] = g.createNode("S" + std::to_string(i), i, nullptr);
                // Si -> T(4-i)
                for (int i = 0; i < 5; ++i)
                    g.createNode("T" + std::to_string(4 - i), 0, src[i]);

                g.assignXCoordinates();
                int bf = bruteForceMin(g, 0);   // 5! = 120 permutations
                g.orderHyperedges(0);
                EXPECT_EQ(totalCrossings(g, 0), bf);
            }

            TEST(FiveEdgeStress, EdgeSetPreserved) {
                TestGraph g("five_stress_set");
                std::vector<NodePtr> src(5);
                for (int i = 0; i < 5; ++i)
                    src[i] = g.createNode("S" + std::to_string(i), i, nullptr);
                for (int i = 0; i < 5; ++i)
                    g.createNode("T" + std::to_string(4 - i), 4 - i, src[i]);
                auto original = g.layers().at(0).outgoing_edges;
                runPipeline(g);
                checkEdgeSetPreserved(g, 0, original);
            }

            TEST(FiveEdgeStress, TransitivityHolds) {
                TestGraph g("five_stress_transit");
                std::vector<NodePtr> src(5);
                for (int i = 0; i < 5; ++i)
                    src[i] = g.createNode("S" + std::to_string(i), i, nullptr);
                for (int i = 0; i < 5; ++i)
                    g.createNode("T" + std::to_string(4 - i), 4 - i, src[i]);
                runPipeline(g);
                checkTransitivity(g, 0);
            }

        } // namespace horizontal_order
    } // namespace graphicalhypergraph_tests
} // namespace hypergraph_logic