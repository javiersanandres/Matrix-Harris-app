#include "BrandesKopf.h"
#include "GraphicalHypergraph.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace hypergraph_logic {
    namespace graphicalhypergraph_tests {
        namespace horizontal_coordinates {

            using namespace bk_internal;

            // ============================================================================
            // Key layout constants (defined in BrandesKopf.h):
            //   NODE_WIDTH       = 80.0   real node box width
            //   DUMMY_NODE_WIDTH =  0.0   bend-point dummy width
            //   MIN_BLOCK_SEP    = 16.0   minimum gap between adjacent bounding boxes
            //
            // Separation formula between two adjacent blocks a and b:
            //   sep(a, b) = (blockWidth(a) + blockWidth(b)) / 2 + MIN_BLOCK_SEP
            //
            //   real  + real  : (80 + 80) / 2 + 16 = 96
            //   real  + dummy : (80 +  0) / 2 + 16 = 56
            //   dummy + dummy : ( 0 +  0) / 2 + 16 = 16
            // ============================================================================

            // ============================================================================
            // Graph-building helpers
            // ============================================================================

            // Thin subclass that exposes the protected layers_ map so tests can call
            // buildG2() and other bk_internal helpers directly.
            class TestGraph : public GraphicalHypergraph {
            public:
                explicit TestGraph(const std::string& name) : GraphicalHypergraph(name) {}
                std::map<int, LayerData>& layers() { return layers_; }
            };

            // Return the first non-segment hyperedge whose source set contains s
            // and whose target set contains t.
            static HyperedgePtr findEdgeWithSourceAndTarget(const TestGraph& g,
                const NodePtr& s, const NodePtr& t)
            {
                for (const auto& e : g.getAllHyperedges())
                    if (!e->isSegment() && e->containsSource(s) && e->containsTarget(t))
                        return e;
                return nullptr;
            }

            // Return the G2 integer id of a node given its NodePtr.
            static int findG2Id(const G2& g2, const NodePtr& np) {
                for (int i = 0; i < static_cast<int>(g2.nodes.size()); ++i)
                    if (g2.nodes[i] == np.get()) return i;
                return -1;
            }

            // Assert that every adjacent pair in each layer satisfies the separation
            // formula. Calls assignCoordinates() internally.
            static void checkNoOverlap(TestGraph& g) {
                g.assignCoordinates();
                for (const auto& [layer, data] : g.getLayers()) {
                    const auto& nodes = data.nodes;
                    for (std::size_t i = 0; i + 1 < nodes.size(); ++i) {
                        double xi = g.getX(nodes[i]);
                        double xi1 = g.getX(nodes[i + 1]);
                        double wi = nodes[i]->isDummy() ? DUMMY_NODE_WIDTH : NODE_WIDTH;
                        double wi1 = nodes[i + 1]->isDummy() ? DUMMY_NODE_WIDTH : NODE_WIDTH;
                        double min_sep = (wi + wi1) * 0.5 + MIN_BLOCK_SEP;
                        EXPECT_GE(xi1 - xi, min_sep - 1e-9)
                            << "Overlap in layer " << layer
                            << " between positions " << i << " and " << i + 1
                            << ": x[i]=" << xi << " x[i+1]=" << xi1
                            << " required sep=" << min_sep;
                    }
                }
            }

            // Two real layers, shared hyperedge {A,B} -> {C,D}.
            // Matches the buildTwoLayerFan() example from the task description.
            static TestGraph buildTwoLayerFan() {
                TestGraph g("fan");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                NodePtr D = g.createNode("D", 1, B);
                HyperedgePtr edge = findEdgeWithSourceAndTarget(g, A, C);
                g.addSourceToEdge(edge, B);
                g.addTargetToEdge(edge, D);
                return g;
            }

            // Four-layer straight chain  A -> d1 -> d2 -> B,
            // where d1 and d2 are dummy bend-points.
            static TestGraph buildLongChain() {
                TestGraph g("long_chain");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr d1 = g.createNode("d1", 0, A);
                NodePtr d2 = g.createNode("d2", 0, d1);
                g.createNode("B", 0, d2);
                return g;
            }

            // Build a minimal BlockList where every node is its own singleton block.
            static BlockList makeSingletonBlockList(int n) {
                BlockList B;
                B.root.resize(n); B.align.resize(n); B.block_widths.resize(n);
                for (int i = 0; i < n; ++i) { B.root[i] = i; B.align[i] = i; }
                return B;
            }


            static G2 buildPaperExampleG2() {
				G2 g2;
                g2.num_layers = 5;
                g2.nodes.assign(26, nullptr);
                g2.layers.resize(5);
                g2.layers[0] = { 0, 1 };
                g2.layers[1] = { 2, 3, 4, 5, 6, 7, 8, 9 };
				g2.layers[2] = { 10, 11, 12, 13, 14, 15 };
				g2.layers[3] = { 16, 17, 18, 19, 20, 21, 22 };
				g2.layers[4] = { 23, 24, 25 };

                std::vector<std::pair<int, int>> edges = {
                    {0, 2 }, { 0, 7 }, { 0, 9 }, {1, 4}, { 1, 6},
                    { 3,11 }, { 4,11 }, { 5,11 }, { 6,12 }, { 7,13 }, { 8,11 },{8, 15},{9,11},{9,14},
                    {10,16 }, {10,17 }, {10,21 }, {12,19 }, {13,20 }, {14,21 }, {15, 18 }, {15, 22},
					{16,23 }, {16,24 }, {17,24 }, {18, 23}, {19, 25}, {20, 25 }, {21, 25 }, {22, 25 }
				};

				g2.upper.assign(26, {});
				g2.lower.assign(26, {});
                for (const auto& [u, v] : edges) {
                    g2.upper[v].push_back(u);
                    g2.lower[u].push_back(v);
                }
                g2.pos = {
                    0, 1,
                    0, 1, 2, 3, 4, 5, 6, 7,
                    0, 1, 2, 3, 4, 5,
                    0, 1, 2, 3, 4, 5, 6,
                    0, 1, 2
				};

                g2.marked.insert({ 8, 11 });
                g2.marked.insert({ 9, 11 });
                g2.marked.insert({ 10, 21 });
                g2.marked.insert({ 15, 18 });

                return g2;
            }

            // ============================================================================
            // buildG2
            // ============================================================================

            TEST(BuildG2, LayerCountMatchesHypergraph) {
                TestGraph g("g2_layers");
                g.createNode("A", 0, nullptr);
                g.createNode("B", 1, nullptr);
                g.createNode("C", 0, g.getAllNodes()[0]);

                G2 g2 = buildG2(g.layers());
                EXPECT_EQ(g2.num_layers, static_cast<int>(g.layers().size()));
                EXPECT_EQ(static_cast<int>(g2.layers.size()), g2.num_layers);
            }

            TEST(BuildG2, PositionIndicesAreConsistent) {
                TestGraph g("g2_pos");
                g.createNode("A", 0, nullptr);
                g.createNode("B", 1, nullptr);
                g.createNode("C", 2, nullptr);

                G2 g2 = buildG2(g.layers());
                for (int id = 0; id < static_cast<int>(g2.nodes.size()); ++id) {
                    int layer = g2.nodes[id]->getLayer();
                    int pos = g2.pos[id];
                    ASSERT_LT(pos, static_cast<int>(g2.layers[layer].size()));
                    EXPECT_EQ(g2.layers[layer][pos], id);
                }
            }

            TEST(BuildG2, UpperAndLowerNeighboursAreSymmetric) {
                TestGraph g("g2_sym");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);

                G2 g2 = buildG2(g.layers());
                int idA = findG2Id(g2, A);
                int idB = findG2Id(g2, B);
                ASSERT_NE(idA, -1); ASSERT_NE(idB, -1);

                EXPECT_TRUE(std::find(g2.lower[idA].begin(), g2.lower[idA].end(), idB)
                    != g2.lower[idA].end());
                EXPECT_TRUE(std::find(g2.upper[idB].begin(), g2.upper[idB].end(), idA)
                    != g2.upper[idB].end());
            }

            TEST(BuildG2, NeighboursAreSortedByPosition) {
                TestGraph g("g2_sorted");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                g.addConnection(B, C);

                G2 g2 = buildG2(g.layers());
                int idC = findG2Id(g2, C);
                ASSERT_NE(idC, -1);

                const auto& upper = g2.upper[idC];
                for (std::size_t i = 0; i + 1 < upper.size(); ++i)
                    EXPECT_LE(g2.pos[upper[i]], g2.pos[upper[i + 1]])
                    << "upper neighbours of C must be sorted by position";
            }

            // ============================================================================
            // isInnerSegment
            // ============================================================================

            TEST(IsInnerSegment, DummyDummyExclusiveIsInner) {
                TestGraph g("inner_seg");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr d1 = g.createNode("d1", 0, A);
                NodePtr d2 = g.createNode("d2", 0, d1);
				NodePtr d3 = g.createNode("d3", 0, d2);
                NodePtr B = g.createNode("B", 0, nullptr);
                g.addConnection(B, d3);

                G2 g2 = buildG2(g.layers());

                EXPECT_TRUE(isInnerSegment(g2, 3, 5))
                    << "(d1, d2) should be an inner segment";
            }

            TEST(IsInnerSegment, RealDummyEdgeIsNotInner) {
                TestGraph g("inner_real_dummy");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr d1 = g.createNode("d1", 0, A);
                g.createNode("B", 0, d1);

                G2 g2 = buildG2(g.layers());
                int idA = findG2Id(g2, A);
                int idd1 = findG2Id(g2, d1);
                ASSERT_NE(idA, -1); ASSERT_NE(idd1, -1);

                EXPECT_FALSE(isInnerSegment(g2, idA, idd1))
                    << "(A, d1): A is real, so not an inner segment";
            }

            TEST(IsInnerSegment, DummyRealEdgeIsNotInner) {
                TestGraph g("inner_dummy_real");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr d1 = g.createNode("d1", 0, A);
                NodePtr B = g.createNode("B", 0, d1);

                G2 g2 = buildG2(g.layers());
                int idd1 = findG2Id(g2, d1);
                int idB = findG2Id(g2, B);
                ASSERT_NE(idd1, -1); ASSERT_NE(idB, -1);

                EXPECT_FALSE(isInnerSegment(g2, idd1, idB))
                    << "(d1, B): B is real, so not an inner segment";
            }

            TEST(IsInnerSegment, DummyWithMultipleParentsIsNotInner) {
                TestGraph g("inner_multi_parent");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr d1 = g.createNode("d1", 0, A);
                g.addConnection(B, d1);
                g.createNode("C", 0, d1);

                G2 g2 = buildG2(g.layers());
                int idA = findG2Id(g2, A);
                int idd1 = findG2Id(g2, d1);
                ASSERT_NE(idA, -1); ASSERT_NE(idd1, -1);

                EXPECT_FALSE(isInnerSegment(g2, idA, idd1))
                    << "d1 has 2 parents, so (A, d1) is not an inner segment";
            }

            TEST(IsInnerSegment, DummyWithMultipleChildrenIsNotInner) {
                TestGraph g("inner_multi_child");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr d1 = g.createNode("d1", 0, A);
                NodePtr d2 = g.createNode("d2", 0, d1);
                NodePtr d3 = g.createNode("d3", 1, d1);
                g.createNode("C", 0, d2);
                g.createNode("D", 0, d3);

                G2 g2 = buildG2(g.layers());
                int idd1 = findG2Id(g2, d1);
                int idd2 = findG2Id(g2, d2);
                ASSERT_NE(idd1, -1); ASSERT_NE(idd2, -1);

                EXPECT_FALSE(isInnerSegment(g2, idd1, idd2))
                    << "d1 has 2 children, so (d1, d2) is not an inner segment";
            }

            // ============================================================================
            // markType1Conflicts
            // ============================================================================

            TEST(MarkType1Conflicts, StraightChainProducesNoMarks) {
                TestGraph g("no_conflicts");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                g.createNode("C", 0, B);

                G2 g2 = buildG2(g.layers());
                markType1Conflicts(g2);
                EXPECT_TRUE(g2.marked.empty())
                    << "A straight chain should produce zero type-1 conflict marks";
            }

            TEST(MarkType1Conflicts, ParallelChainsProduceNoMarks) {
                TestGraph g("parallel_no_conflicts");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                g.createNode("C", 0, A);
                g.createNode("D", 1, B);

                G2 g2 = buildG2(g.layers());
                markType1Conflicts(g2);
                EXPECT_TRUE(g2.marked.empty())
                    << "Non-crossing parallel chains should have no type-1 conflicts";
            }

            // ============================================================================
            // Alignment
            // ============================================================================

            TEST(Alignment, SimpleChainNodesShareXCoordinate) {
                // Layer 0: [A]
                // Layer 1: [B]   child of A -> single vertical edge -> one block
                TestGraph g("chain");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                g.assignCoordinates();
                EXPECT_NEAR(g.getX(A), g.getX(B), 1e-9);
            }

            TEST(Alignment, TwoParallelChainsEachPairAligned) {
                // Layer 0: [A] [B]
                // Layer 1: [C] [D]   A->C, B->D
                TestGraph g("parallel");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                NodePtr D = g.createNode("D", 1, B);
                g.assignCoordinates();
                EXPECT_NEAR(g.getX(A), g.getX(C), 1e-9);
                EXPECT_NEAR(g.getX(B), g.getX(D), 1e-9);
                EXPECT_GE(std::abs(g.getX(B) - g.getX(A)), 96.0 - 1e-9);
            }

            TEST(Alignment, LongInnerSegmentChainIsCollinear) {
                // A -> d1 -> d2 -> B must all share the same x.
                TestGraph g = buildLongChain();
                auto nodes = g.getAllNodes();
                g.assignCoordinates();
                EXPECT_NEAR(g.getX(nodes[0]), g.getX(nodes[1]), 1e-9);
                EXPECT_NEAR(g.getX(nodes[1]), g.getX(nodes[2]), 1e-9);
                EXPECT_NEAR(g.getX(nodes[2]), g.getX(nodes[3]), 1e-9);
            }

            TEST(Alignment, MedianParentAlignmentThreeToOne) {
                // Layer 0: [A] [B] [C]
                // Layer 1:     [D]       parents: A, B, C  ->  median = B
                TestGraph g("median");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 2, nullptr);
                NodePtr D = g.createNode("D", 1, B);
                g.addConnection(A, D);
                g.addConnection(C, D);
                g.assignCoordinates();
                EXPECT_NEAR(g.getX(D), g.getX(B), 1.0)
                    << "D should align with its median parent B";
            }

            TEST(Alignment, SymmetricGridColumnsAlignedAndEquallySpaced) {
                // Layer 0: [A] [B] [C]
                // Layer 1: [D] [E] [F]   A->D, B->E, C->F
                TestGraph g("symmetric");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 2, nullptr);
                NodePtr D = g.createNode("D", 0, A);
                NodePtr E = g.createNode("E", 1, B);
                NodePtr F = g.createNode("F", 2, C);
                g.assignCoordinates();

                EXPECT_NEAR(g.getX(A), g.getX(D), 1e-9);
                EXPECT_NEAR(g.getX(B), g.getX(E), 1e-9);
                EXPECT_NEAR(g.getX(C), g.getX(F), 1e-9);
                EXPECT_NEAR(g.getX(B) - g.getX(A), 96.0, 1e-9);
                EXPECT_NEAR(g.getX(C) - g.getX(B), 96.0, 1e-9);
            }


            TEST(Alignment, PaperExampleBlockAlignmentLeftDown) {
				G2 g2 = buildPaperExampleG2();
				BlockList B = verticalAlignment(g2, 1, 1);
				ASSERT_EQ(B.root.size(), g2.nodes.size());
				for (int i : {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 15, 17, 18 })
					ASSERT_EQ(B.root[i], i);

                ASSERT_EQ(B.root[2], 0);
                ASSERT_EQ(B.root[4], 1);
                ASSERT_EQ(B.root[11], 5);
                ASSERT_EQ(B.root[12], 6);
                ASSERT_EQ(B.root[19], 6);
                ASSERT_EQ(B.root[13], 7);
                ASSERT_EQ(B.root[20], 7);
                ASSERT_EQ(B.root[14], 9);
                ASSERT_EQ(B.root[21], 9);
                ASSERT_EQ(B.root[22], 15);
                ASSERT_EQ(B.root[16], 10);
                ASSERT_EQ(B.root[23], 10);
                ASSERT_EQ(B.root[24], 17);
                ASSERT_EQ(B.root[25], 7);
            }

            TEST(Alignment, PaperExampleBlockAlignmentRightDown) {
                G2 g2 = buildPaperExampleG2();
                BlockList B = verticalAlignment(g2, 1, -1);
                ASSERT_EQ(B.root.size(), g2.nodes.size());
                for (int i : {0, 1, 2, 3, 4, 6, 7, 8, 14, 10, 18, 16})
                    ASSERT_EQ(B.root[i], i);

                ASSERT_EQ(B.root[9], 0);
                ASSERT_EQ(B.root[15], 8);
                ASSERT_EQ(B.root[13], 7);
                ASSERT_EQ(B.root[12], 6);
                ASSERT_EQ(B.root[11], 5);
                ASSERT_EQ(B.root[22], 8);
                ASSERT_EQ(B.root[21], 14);
                ASSERT_EQ(B.root[20], 7);
                ASSERT_EQ(B.root[19], 6);
                ASSERT_EQ(B.root[17], 10);
                ASSERT_EQ(B.root[25], 14);
                ASSERT_EQ(B.root[24], 10);
                ASSERT_EQ(B.root[23], 16);

            }
            TEST(Alignment, PaperExampleBlockAlignmentLeftUp) {
                G2 g2 = buildPaperExampleG2();
                BlockList B = verticalAlignment(g2, -1, 1);
                ASSERT_EQ(B.root.size(), g2.nodes.size());
                for (int i : {23, 24, 25, 18, 20,21,22,11, 2, 4, 5, 9, 1})
                    ASSERT_EQ(B.root[i], i);

                ASSERT_EQ(B.root[16], 23);
                ASSERT_EQ(B.root[17], 24);
                ASSERT_EQ(B.root[19], 25);
                ASSERT_EQ(B.root[10], 24);
                ASSERT_EQ(B.root[12], 25);
                ASSERT_EQ(B.root[13], 20);
                ASSERT_EQ(B.root[14], 21);
                ASSERT_EQ(B.root[15], 22);
                ASSERT_EQ(B.root[3], 11);
                ASSERT_EQ(B.root[6], 25);
                ASSERT_EQ(B.root[7], 20);
                ASSERT_EQ(B.root[8], 22);
				ASSERT_EQ(B.root[0], 20);
            }

			TEST(Alignment, PaperExampleBlockAlignmentRightUp) {
                G2 g2 = buildPaperExampleG2();
                BlockList B = verticalAlignment(g2, -1, -1);
                ASSERT_EQ(B.root.size(), g2.nodes.size());
                for (int i : {23, 24, 25, 19, 20, 21, 16, 17, 11, 8, 4, 3, 2, 0})
                    ASSERT_EQ(B.root[i], i);

                ASSERT_EQ(B.root[22], 25);
                ASSERT_EQ(B.root[18], 23);
                ASSERT_EQ(B.root[15], 25);
                ASSERT_EQ(B.root[14], 21);
                ASSERT_EQ(B.root[13], 20);
                ASSERT_EQ(B.root[12], 19);
                ASSERT_EQ(B.root[10], 17);
                ASSERT_EQ(B.root[9], 21);
                ASSERT_EQ(B.root[7], 20);
                ASSERT_EQ(B.root[6], 19);
                ASSERT_EQ(B.root[5], 11);
                ASSERT_EQ(B.root[1], 19);
            }

            // ============================================================================
            // horizontalCompaction
            // ============================================================================
            TEST(horizontalCompaction, PaperExampleBlockAlignmentLeftDown) {
				double sep = NODE_WIDTH + MIN_BLOCK_SEP;
                G2 g2 = buildPaperExampleG2();
                BlockList B = verticalAlignment(g2, 1, 1);
				auto x = horizontalCompaction(g2, B, 1, 1);
				ASSERT_EQ(x.size(), g2.nodes.size());
				ASSERT_EQ(x[0], x[2]);
				ASSERT_EQ(x[1], x[4]);
				ASSERT_EQ(x[5], x[11]);
				ASSERT_EQ(x[6], x[12]);
				ASSERT_EQ(x[6], x[19]);
				ASSERT_EQ(x[7], x[13]);
				ASSERT_EQ(x[7], x[20]);
				ASSERT_EQ(x[7], x[25]);
				ASSERT_EQ(x[9], x[14]);
				ASSERT_EQ(x[9], x[21]);
				ASSERT_EQ(x[10], x[16]);
                ASSERT_EQ(x[10], x[23]);
                ASSERT_EQ(x[15], x[22]);
                ASSERT_EQ(x[17], x[24]);

				ASSERT_EQ(x[0], 0.0);
                ASSERT_EQ(x[1], 2*sep);
				ASSERT_EQ(x[3], sep);
				ASSERT_EQ(x[5], 3 * sep);
				ASSERT_EQ(x[6], 4 * sep);
                ASSERT_EQ(x[7], 5 * sep);
				ASSERT_EQ(x[8], 6 * sep);
				ASSERT_EQ(x[9], 7 * sep);
				ASSERT_EQ(x[10], sep);
				ASSERT_EQ(x[15], 8 * sep);
				ASSERT_EQ(x[17], 2*sep);
				ASSERT_EQ(x[18], 3 * sep);
            }

            TEST(horizontalCompaction, PaperExampleBlockAlignmentRightDown) {
                double sep = NODE_WIDTH + MIN_BLOCK_SEP;
                G2 g2 = buildPaperExampleG2();
                BlockList B = verticalAlignment(g2, 1, -1);
                auto x = horizontalCompaction(g2, B, 1, -1);
                ASSERT_EQ(x.size(), g2.nodes.size());
                ASSERT_EQ(x[0], x[9]);
                ASSERT_EQ(x[8], x[15]);
                ASSERT_EQ(x[8], x[22]);
                ASSERT_EQ(x[14], x[21]);
                ASSERT_EQ(x[14], x[25]);
                ASSERT_EQ(x[7], x[13]);
                ASSERT_EQ(x[7], x[20]);
                ASSERT_EQ(x[6], x[12]);
                ASSERT_EQ(x[6], x[19]);
                ASSERT_EQ(x[5], x[11]);
                ASSERT_EQ(x[10], x[17]);
                ASSERT_EQ(x[10], x[24]);
                ASSERT_EQ(x[16], x[23]);

                ASSERT_EQ(x[0], -sep);
                ASSERT_EQ(x[1], 0.0);
                ASSERT_EQ(x[8], -2*sep);
                ASSERT_EQ(x[14], -3 * sep);
                ASSERT_EQ(x[7], -4 * sep);
                ASSERT_EQ(x[6], -5 * sep);
                ASSERT_EQ(x[5], -6 * sep);
                ASSERT_EQ(x[18], -6 * sep);
                ASSERT_EQ(x[4], -7*sep);
                ASSERT_EQ(x[10], -7 * sep);
                ASSERT_EQ(x[3], -8 * sep);
                ASSERT_EQ(x[16], -8 * sep);
                ASSERT_EQ(x[2], -9 * sep);
            }
            TEST(horizontalCompaction, PaperExampleBlockAlignmentLeftUp) {
                double sep = NODE_WIDTH + MIN_BLOCK_SEP;
                G2 g2 = buildPaperExampleG2();
                BlockList B = verticalAlignment(g2, -1, 1);
                auto x = horizontalCompaction(g2, B, -1, 1);
                ASSERT_EQ(x.size(), g2.nodes.size());
                ASSERT_EQ(x[23], x[16]);
                ASSERT_EQ(x[24], x[17]);
                ASSERT_EQ(x[24], x[10]);
                ASSERT_EQ(x[25], x[19]);
                ASSERT_EQ(x[25], x[12]);
                ASSERT_EQ(x[25], x[6]);
                ASSERT_EQ(x[20], x[13]);
                ASSERT_EQ(x[20], x[7]);
                ASSERT_EQ(x[20], x[0]);
                ASSERT_EQ(x[21], x[14]);
                ASSERT_EQ(x[22], x[15]);
                ASSERT_EQ(x[22], x[8]);
                ASSERT_EQ(x[11], x[3]);

                ASSERT_EQ(x[16], 0.0);
                ASSERT_EQ(x[10], sep);
                ASSERT_EQ(x[2], sep);
                ASSERT_EQ(x[3], 2 * sep);
                ASSERT_EQ(x[18], 2 * sep);
                ASSERT_EQ(x[4], 3 * sep);
                ASSERT_EQ(x[5], 4 * sep);
                ASSERT_EQ(x[6], 5 * sep);
                ASSERT_EQ(x[0], 6*sep);
                ASSERT_EQ(x[1], 7 * sep);
                ASSERT_EQ(x[14], 7 * sep);
                ASSERT_EQ(x[8], 8 * sep);
                ASSERT_EQ(x[9], 9 * sep);
            }

            TEST(horizontalCompaction, PaperExampleBlockAlignmentRightUp) {
                double sep = NODE_WIDTH + MIN_BLOCK_SEP;
                G2 g2 = buildPaperExampleG2();
                BlockList B = verticalAlignment(g2, -1, -1);
                auto x = horizontalCompaction(g2, B, -1, -1);
                ASSERT_EQ(x.size(), g2.nodes.size());
                ASSERT_EQ(x[25], x[22]);
                ASSERT_EQ(x[25], x[15]);
                ASSERT_EQ(x[23], x[18]);
                ASSERT_EQ(x[21], x[14]);
                ASSERT_EQ(x[21], x[9]);
                ASSERT_EQ(x[20], x[13]);
                ASSERT_EQ(x[20], x[7]);
                ASSERT_EQ(x[19], x[12]);
                ASSERT_EQ(x[19], x[6]);
                ASSERT_EQ(x[19], x[1]);
                ASSERT_EQ(x[17], x[10]);
                ASSERT_EQ(x[11], x[5]);

                ASSERT_EQ(x[25], 0.0);
                ASSERT_EQ(x[24], -sep);
                ASSERT_EQ(x[9], -sep);
                ASSERT_EQ(x[8], -2 * sep);
                ASSERT_EQ(x[7], -3 * sep);
                ASSERT_EQ(x[1], -4 * sep);
                ASSERT_EQ(x[0], -5 * sep);
                ASSERT_EQ(x[5], -5 * sep);
                ASSERT_EQ(x[18], -5 * sep);
                ASSERT_EQ(x[4], -6 * sep);
                ASSERT_EQ(x[10], -6 * sep);
                ASSERT_EQ(x[3], -7 * sep);
                ASSERT_EQ(x[16], -7 * sep);
                ASSERT_EQ(x[2], -8 * sep);
            }

            // ============================================================================
            // assignCoordinates – single-node and empty graphs
            // ============================================================================

            TEST(AssignCoordinates, EmptyGraphDoesNotCrash) {
                TestGraph g("empty");
                EXPECT_NO_THROW(g.assignCoordinates());
            }

            TEST(AssignCoordinates, SingleNodeGivesFiniteCoordinate) {
                TestGraph g("single");
                NodePtr A = g.createNode("A", 0, nullptr);
                g.assignCoordinates();
                EXPECT_TRUE(std::isfinite(g.getX(A)));
            }

            TEST(AssignCoordinates, IsIdempotent) {
                TestGraph g("idempotent");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);

                g.assignCoordinates();
                double xA1 = g.getX(A), xB1 = g.getX(B), xC1 = g.getX(C);

                g.assignCoordinates();
                EXPECT_DOUBLE_EQ(g.getX(A), xA1);
                EXPECT_DOUBLE_EQ(g.getX(B), xB1);
                EXPECT_DOUBLE_EQ(g.getX(C), xC1);
            }

            TEST(AssignCoordinates, AllCoordinatesAreFinite) {
                TestGraph g = buildTwoLayerFan();
                g.assignCoordinates();
                for (const auto& n : g.getAllNodes())
                    EXPECT_TRUE(std::isfinite(g.getX(n)))
                    << "Node " << n->getName() << " has non-finite coordinate";
            }

            TEST(AssignCoordinates, GetXThrowsForNodeNotInGraph) {
                TestGraph g("unknown");
                g.createNode("A", 0, nullptr);
                g.assignCoordinates();

                TestGraph other("other");
                NodePtr orphan = other.createNode("orphan", 0, nullptr);
                EXPECT_THROW(g.getX(orphan), std::out_of_range);
            }

            // ============================================================================
            // Separation constraints
            // ============================================================================

            TEST(Separation, TwoIsolatedRealNodesAreAtLeast96Apart) {
                TestGraph g("sep_real_real");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                g.assignCoordinates();
                EXPECT_GE(g.getX(B) - g.getX(A), 96.0 - 1e-9);
            }

            TEST(Separation, DummyAndRealNodeAreAtLeast56Apart) {
                // Layer 0: [A]  (real)
                // Layer 1: [d1] (pos 0)  [B] (real pos 1)
                // Layer 2: [C]  (real)
                TestGraph g("sep_dummy_real");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr d1 = g.createNode("d1", 0, A);
                NodePtr B = g.createNode("B", 1, nullptr);
                g.createNode("C", 0, d1);

                g.assignCoordinates();
                double xd1 = g.getX(d1), xB = g.getX(B);
                EXPECT_GT(xB, xd1);
                EXPECT_GE(xB - xd1, 56.0 - 1e-9)
                    << "sep(dummy, real) must be >= 56; got " << (xB - xd1);
            }

            TEST(Separation, ThreeRealNodesEachPairAtLeast96Apart) {
                TestGraph g("sep_three");
                g.createNode("A", 0, nullptr);
                g.createNode("B", 1, nullptr);
                g.createNode("C", 2, nullptr);
                g.assignCoordinates();

                auto nodes = g.getAllNodes();
                EXPECT_GE(g.getX(nodes[1]) - g.getX(nodes[0]), 96.0 - 1e-9);
                EXPECT_GE(g.getX(nodes[2]) - g.getX(nodes[1]), 96.0 - 1e-9);
            }



            // ============================================================================
            // Fan topologies
            // ============================================================================

            TEST(Fan, FanOutParentSitsBetweenChildren) {
                // Layer 0: [A]
                // Layer 1: [C] [D]   A->C, A->D
                TestGraph g("fan_out");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                NodePtr D = g.createNode("D", 1, A);
                g.assignCoordinates();

                double xC = g.getX(C), xD = g.getX(D), xA = g.getX(A);
                EXPECT_GE(xD - xC, 96.0 - 1e-9);
                EXPECT_GE(xA, std::min(xC, xD) - 1e-9);
                EXPECT_LE(xA, std::max(xC, xD) + 1e-9);
            }

            TEST(Fan, FanInChildSitsBetweenParents) {
                // Layer 0: [A] [B]
                // Layer 1: [C]        parents: A, B
                TestGraph g("fan_in");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                g.addConnection(B, C);
                g.assignCoordinates();

                double xA = g.getX(A), xB = g.getX(B), xC = g.getX(C);
                EXPECT_GE(xB - xA, 96.0 - 1e-9);
                EXPECT_GE(xC, xA - 1e-9);
                EXPECT_LE(xC, xB + 1e-9);
            }

            TEST(Fan, TwoLayerHyperedgeFanNoOverlap) {
                TestGraph g = buildTwoLayerFan();
                checkNoOverlap(g);
            }

            TEST(Fan, TwoLayerHyperedgeSrcTgtRoughlyAligned) {
                // {A,B} -> {C,D}: after BK balancing A ~ C and B ~ D.
                TestGraph g = buildTwoLayerFan();
                auto nodes = g.getAllNodes();
                g.assignCoordinates();
                EXPECT_NEAR(g.getX(nodes[0]), g.getX(nodes[2]), 1.0);
                EXPECT_NEAR(g.getX(nodes[1]), g.getX(nodes[3]), 1.0);
            }

            // ============================================================================
            // Diamond graph
            // ============================================================================

            TEST(Diamond, SinkNodeSitsBetweenItsParents) {
                // Layer 0:    [A]
                // Layer 1: [B]  [C]    A->B, A->C
                // Layer 2:    [D]      B->D, C->D
                TestGraph g("diamond");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                NodePtr C = g.createNode("C", 1, A);
                NodePtr D = g.createNode("D", 0, B);
                g.addConnection(C, D);
                g.assignCoordinates();

                double xB = g.getX(B), xC = g.getX(C), xD = g.getX(D);
                EXPECT_GE(xC - xB, 96.0 - 1e-9);
                EXPECT_GE(xD, xB - 1e-9);
                EXPECT_LE(xD, xC + 1e-9);
            }

            TEST(Diamond, SourceNodeSitsBetweenItsChildren) {
                TestGraph g("diamond_src");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                NodePtr C = g.createNode("C", 1, A);
                NodePtr D = g.createNode("D", 0, B);
                g.addConnection(C, D);
                g.assignCoordinates();

                double xA = g.getX(A), xB = g.getX(B), xC = g.getX(C);
                EXPECT_GE(xA, xB - 1e-9);
                EXPECT_LE(xA, xC + 1e-9);
            }

            // ============================================================================
            // Crossing edges
            // ============================================================================

            TEST(CrossingEdges, CoordinatesAreFiniteAndOrdered) {
                // Layer 0: [A] [B]
                // Layer 1: [C] [D]   A->D (crossing), B->C (crossing)
                TestGraph g("crossing");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, B);
                NodePtr D = g.createNode("D", 1, A);
                g.assignCoordinates();

                EXPECT_GE(g.getX(B) - g.getX(A), 96.0 - 1e-9);
                EXPECT_GE(g.getX(D) - g.getX(C), 96.0 - 1e-9);
            }

            // ============================================================================
            // Layer order preservation
            // ============================================================================

            TEST(LayerOrder, XCoordinatesMonotonicallyIncreaseWithPosition) {
                TestGraph g("order");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 2, nullptr);
                g.createNode("D", 0, A);
                g.createNode("E", 1, B);
                g.createNode("F", 2, C);
                g.assignCoordinates();

                for (const auto& [layer, data] : g.getLayers())
                    for (std::size_t i = 0; i + 1 < data.nodes.size(); ++i)
                        EXPECT_LT(g.getX(data.nodes[i]), g.getX(data.nodes[i + 1]))
                        << "Order violated in layer " << layer
                        << " between positions " << i << " and " << i + 1;
            }

            // ============================================================================
            // 4x4 ladder – large structured graph
            // ============================================================================

            TEST(LargeGraph, FourByFourLadderColumnsAlignedAndSpaced96) {
                TestGraph g("ladder_4x4");
                std::vector<std::vector<NodePtr>> grid(4, std::vector<NodePtr>(4));

                for (int col = 0; col < 4; ++col)
                    grid[0][col] = g.createNode("R0C" + std::to_string(col), col, nullptr);

                for (int row = 1; row < 4; ++row)
                    for (int col = 0; col < 4; ++col)
                        grid[row][col] = g.createNode(
                            "R" + std::to_string(row) + "C" + std::to_string(col),
                            col, grid[row - 1][col]);

                g.assignCoordinates();

                for (int col = 0; col < 4; ++col) {
                    double x0 = g.getX(grid[0][col]);
                    for (int row = 1; row < 4; ++row)
                        EXPECT_NEAR(g.getX(grid[row][col]), x0, 1e-9)
                        << "Column " << col << " row " << row << " misaligned";
                }

                for (int col = 0; col + 1 < 4; ++col)
                    EXPECT_NEAR(
                        g.getX(grid[0][col + 1]) - g.getX(grid[0][col]), 96.0, 1e-9)
                    << "Column spacing should be exactly 96";
            }

            // ============================================================================
            // Hyperedge API
            // ============================================================================

            TEST(HyperedgeAPI, AddSourceAndTargetToEdge) {
                TestGraph g("he_api");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                NodePtr D = g.createNode("D", 1, B);

                HyperedgePtr e = findEdgeWithSourceAndTarget(g, A, C);
                ASSERT_NE(e, nullptr);

                g.addSourceToEdge(e, B);
                g.addTargetToEdge(e, D);

                EXPECT_TRUE(e->containsSource(A));
                EXPECT_TRUE(e->containsSource(B));
                EXPECT_TRUE(e->containsTarget(C));
                EXPECT_TRUE(e->containsTarget(D));
                EXPECT_NO_THROW(g.assignCoordinates());
            }

            TEST(HyperedgeAPI, CreateSourceAndTargetHelpers) {
                TestGraph g("src_tgt");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);

                HyperedgePtr e = findEdgeWithSourceAndTarget(g, A, B);
                ASSERT_NE(e, nullptr);

                NodePtr S = g.createSource("S", 1, e);
                NodePtr T = g.createTarget("T", 1, e);

                EXPECT_TRUE(e->containsSource(S));
                EXPECT_TRUE(e->containsTarget(T));
                EXPECT_NO_THROW(g.assignCoordinates());
            }

            TEST(HyperedgeAPI, ThreeLayerGraphHasNoOverlap) {
                TestGraph g("three_layer");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                NodePtr D = g.createNode("D", 1, B);
                g.createNode("E", 0, C);
                g.createNode("F", 1, D);
                checkNoOverlap(g);
            }

        } // namespace horizontal_coordinates
    } // namespace graphicalhypergraph_tests
} // namespace hypergraph_logic