#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace hypergraph_logic {
    namespace graphicalhypergraph_tests {
        namespace layout {

            // ════════════════════════════════════════════════════════════════════════
            // TestGraph
            //
            // Exposes protected members so tests can inspect internal layout state
            // directly, and wires up the full pipeline steps individually.
            // ════════════════════════════════════════════════════════════════════════
            class TestGraph : public GraphicalHypergraph {
            public:
                explicit TestGraph(const std::string& name) : GraphicalHypergraph(name) {}

                std::map<int, LayerData>& layers() { return layers_; }
                std::unordered_map<Node*, NodeLayout>& nodeLayout() { return node_layout_; }
                std::unordered_map<Hyperedge*, double>& edgeLayout() { return edge_layout_; }
                std::unordered_map<int, double>& layerLayout() { return layer_layout_; }

                void runAssignXCoordinates() { GraphicalHypergraph::assignXCoordinates(); }
                void runOrderHyperedges(int layer) { GraphicalHypergraph::orderHyperedges(layer); }
                void runAssignPorts() { GraphicalHypergraph::assignPorts(); }
                void runAssignYCoordinates() { GraphicalHypergraph::assignYCoordinates(); }
            };

            // ── Helpers ───────────────────────────────────────────────────────────────

            // Run the full layout pipeline up to and including assignYCoordinates.
            static void runFullPipeline(TestGraph& g) {
                g.runAssignXCoordinates();
                for (const auto& [layer_idx, _] : g.layers())
                    g.runOrderHyperedges(layer_idx);
                g.runAssignPorts();
                g.runAssignYCoordinates();
            }

            // Return the first non-segment hyperedge connecting src -> tgt.
            static HyperedgePtr findEdge(const TestGraph& g,
                const NodePtr& src, const NodePtr& tgt)
            {
                for (const auto& e : g.getAllHyperedges())
                    if (!e->isSegment() && e->containsSource(src) && e->containsTarget(tgt))
                        return e;
                return nullptr;
            }


            // ════════════════════════════════════════════════════════════════════════
            // assignYCoordinates — layer placement
            // ════════════════════════════════════════════════════════════════════════

            // ── SingleLayer ───────────────────────────────────────────────────────────
            //
            // A single layer with no edges. Layer 0 must sit at y = 0.

            TEST(AssignYCoordinates, SingleLayerPlacedAtZero) {
                TestGraph g("single_layer");
                g.createNode("A", 0, nullptr);
                runFullPipeline(g);
                EXPECT_NEAR(g.layerLayout().at(0), 0.0, 1e-9);
            }

            // ── TwoLayers ─────────────────────────────────────────────────────────────
            //
            // Layer 0: [A]   Layer 1: [B]    A -> B.
            // Layer 0 is at y=0. Layer 1 must be strictly below (more negative).

            TEST(AssignYCoordinates, SecondLayerBelowFirst) {
                TestGraph g("two_layers");
                NodePtr A = g.createNode("A", 0, nullptr);
                g.createNode("B", 0, A);
                runFullPipeline(g);
                EXPECT_LT(g.layerLayout().at(1), g.layerLayout().at(0));
            }

            TEST(AssignYCoordinates, SecondLayerClearsNodeBoxAndGap) {
                // The gap between layer 0's bottom box edge and layer 1's top box edge
                // must be at least LAYER_GAP on each side of the bar band.
                // Minimum y of layer 1 = layer_layout[0] - NODE_HEIGHT/2 - LAYER_GAP
                //                        - (bars * HORIZONTAL_SEP) - LAYER_GAP - NODE_HEIGHT/2
                // With one edge and one bar the minimum distance between node centres is:
                //   NODE_HEIGHT + 2*LAYER_GAP + HORIZONTAL_SEP
                TestGraph g("two_layers_gap");
                NodePtr A = g.createNode("A", 0, nullptr);
                g.createNode("B", 0, A);
                runFullPipeline(g);

                double y0 = g.layerLayout().at(0);
                double y1 = g.layerLayout().at(1);
                double min_centre_gap = NODE_HEIGHT + LAYER_GAP;
                EXPECT_LE(y1, y0 - min_centre_gap + 1e-9);
            }

            TEST(AssignYCoordinates, AllLayersPresent) {
                // A four-layer chain must have an entry for every layer index.
                TestGraph g("four_layers");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                NodePtr C = g.createNode("C", 0, B);
                g.createNode("D", 0, C);
                runFullPipeline(g);
                for (int i = 0; i < 4; ++i)
                    EXPECT_TRUE(g.layerLayout().count(i)) << "missing layer " << i;
            }

            TEST(AssignYCoordinates, LayerYMonotonicallyDecreasing) {
                // Each successive layer must be strictly below the previous one.
                TestGraph g("monotone");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                NodePtr C = g.createNode("C", 0, B);
                g.createNode("D", 0, C);
                runFullPipeline(g);
                for (int i = 1; i < 4; ++i)
                    EXPECT_LT(g.layerLayout().at(i), g.layerLayout().at(i - 1))
                    << "layer " << i << " not below layer " << i - 1;
            }


            // ════════════════════════════════════════════════════════════════════════
            // assignYCoordinates — edge bar placement
            // ════════════════════════════════════════════════════════════════════════

            // ── SingleEdge ────────────────────────────────────────────────────────────
            //
            // One edge A -> B.  Its bar must be placed in the gap between layer 0
            // and layer 1, strictly below the upper node boxes and above the lower ones.

            TEST(AssignYCoordinates, SingleEdgeBarPlacedInGap) {
                TestGraph g("single_edge_bar");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                runFullPipeline(g);

                HyperedgePtr e = findEdge(g, A, B);
                ASSERT_NE(e, nullptr);
                ASSERT_TRUE(g.edgeLayout().count(e.get()));

                double bar_y = g.edgeLayout().at(e.get());
                double upper_y = g.layerLayout().at(0) - NODE_HEIGHT / 2.0;
                double lower_y = g.layerLayout().at(1) + NODE_HEIGHT / 2.0;

                // Bar must be below the upper nodes and above the lower ones.
                EXPECT_LE(bar_y, upper_y + 1e-9) << "bar above upper node boxes";
                EXPECT_GE(bar_y, lower_y - 1e-9) << "bar below lower node boxes";
            }

            // ── TrivialEdge ───────────────────────────────────────────────────────────
            //
            // An edge whose two ports share the same x-coordinate (trivial span)
            // must be placed flush against the bottom of the upper node boxes,
            // not inside the bar band.

            TEST(AssignYCoordinates, TrivialEdgePlacedFlushAgainstUpperBox) {
                // A single-node-per-layer chain produces a trivial span when
                // source and target ports are assigned the same x (both centred).
                TestGraph g("trivial_edge");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                runFullPipeline(g);

                HyperedgePtr e = findEdge(g, A, B);
                ASSERT_NE(e, nullptr);

                double src_x = g.nodeLayout().at(A.get()).source_ports[0].x;
                double tgt_x = g.nodeLayout().at(B.get()).target_ports[0].x;

                if (std::abs(src_x - tgt_x) < 1e-9) {
                    // Confirmed trivial: bar must be at upper_bottom.
                    double expected_y = g.layerLayout().at(0) - NODE_HEIGHT / 2.0;
                    EXPECT_NEAR(g.edgeLayout().at(e.get()), expected_y, 1e-9);
                }
            }

            // ── TwoNonOverlappingEdges ────────────────────────────────────────────────
            //
            // Layer 0: [A] [B]   Layer 1: [C] [D]
            // e1: A->C  (left side),  e2: B->D  (right side).
            // The spans do not overlap so both bars can share the same y level.

            TEST(AssignYCoordinates, NonOverlappingEdgesShareSameY) {
                TestGraph g("non_overlap");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                NodePtr D = g.createNode("D", 1, B);
                runFullPipeline(g);

                HyperedgePtr eAC = findEdge(g, A, C);
                HyperedgePtr eBD = findEdge(g, B, D);
                ASSERT_NE(eAC, nullptr);
                ASSERT_NE(eBD, nullptr);

                double yAC = g.edgeLayout().at(eAC.get());
                double yBD = g.edgeLayout().at(eBD.get());

                // Non-overlapping spans: must share y (packed as high as possible).
                double srcAx = g.nodeLayout().at(A.get()).source_ports[0].x;
                double tgtCx = g.nodeLayout().at(C.get()).target_ports[0].x;
                double srcBx = g.nodeLayout().at(B.get()).source_ports[0].x;
                double tgtDx = g.nodeLayout().at(D.get()).target_ports[0].x;

                double xmin1 = std::min(srcAx, tgtCx);
                double xmax1 = std::max(srcAx, tgtCx);
                double xmin2 = std::min(srcBx, tgtDx);
                double xmax2 = std::max(srcBx, tgtDx);

                bool overlaps = (xmin1 <= xmax2) && (xmin2 <= xmax1);
                if (!overlaps)
                    EXPECT_NEAR(yAC, yBD, 1e-9) << "non-overlapping bars should share y";
            }

            TEST(AssignYCoordinates, OverlappingBarsSeperatedByHorizontalSep) {
                // More precise: the difference must be exactly HORIZONTAL_SEP
                // when the conflict is a direct push.
                TestGraph g("overlap_sep");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, nullptr);
                NodePtr D = g.createNode("D", 1, nullptr);
                HyperedgePtr e1 = g.addConnection({ A }, { D });
                HyperedgePtr e2 = g.addConnection({ B }, { C });
                runFullPipeline(g);

                double y1 = g.edgeLayout().at(e1.get());
                double y2 = g.edgeLayout().at(e2.get());
                EXPECT_NEAR(std::abs(y1 - y2), 0, 1e-9);
            }

            // ── AllEdgesRegistered ────────────────────────────────────────────────────
            //
            // Every non-trivial edge that crosses a gap must appear in edge_layout_.

            TEST(AssignYCoordinates, AllEdgesGetYEntry) {
                TestGraph g("all_edges_y");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                NodePtr D = g.createNode("D", 1, B);
                runFullPipeline(g);

                for (const auto& e : g.getAllHyperedges())
                    if (!e->isSegment())
                        EXPECT_TRUE(g.edgeLayout().count(e.get()))
                        << "edge " << e << " missing from edge_layout_";
            }

            // ── ThreeLayersMonotoneBars ───────────────────────────────────────────────
            //
            // A -> B -> C. Each gap has one bar. Bar in gap 0-1 must be above bar in
            // gap 1-2 (less negative y), reflecting the monotone layer descent.

            TEST(AssignYCoordinates, BarsInUpperGapHigherThanLowerGap) {
                TestGraph g("three_layer_bars");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                NodePtr C = g.createNode("C", 0, B);
                runFullPipeline(g);

                HyperedgePtr eAB = findEdge(g, A, B);
                HyperedgePtr eBC = findEdge(g, B, C);
                ASSERT_NE(eAB, nullptr);
                ASSERT_NE(eBC, nullptr);

                // Bar in gap 0-1 is less negative (higher on screen) than bar in gap 1-2.
                EXPECT_GT(g.edgeLayout().at(eAB.get()), g.edgeLayout().at(eBC.get()));
            }

            // ── BarBelowUpperNodeBox ──────────────────────────────────────────────────
            //
            // The bar y must be at most layer_layout[upper] - NODE_HEIGHT/2 - LAYER_GAP
            // (i.e. it clears the node box and the mandatory gap).

            TEST(AssignYCoordinates, BarRespectsLayerGapBelowUpperNodes) {
                TestGraph g("bar_gap_upper");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                NodePtr D = g.createNode("D", 1, B);
                HyperedgePtr e1 = g.addConnection({ A }, { D });
                HyperedgePtr e2 = g.addConnection({ B }, { C });
                runFullPipeline(g);

                EXPECT_EQ(std::fabs(g.edgeLayout()[e1.get()] - g.edgeLayout()[e2.get()]), HORIZONTAL_SEP);
            }

            // ── BarAboveLowerNodeBox ──────────────────────────────────────────────────

            TEST(AssignYCoordinates, BarRespectsLayerGapAboveLowerNodes) {
                TestGraph g("bar_gap_lower");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                runFullPipeline(g);

                HyperedgePtr e = findEdge(g, A, B);
                ASSERT_NE(e, nullptr);

                double lower_bound = g.layerLayout().at(1) + NODE_HEIGHT / 2.0 + LAYER_GAP;
                EXPECT_GE(g.edgeLayout().at(e.get()), lower_bound - 1e-9)
                    << "bar placed below the LAYER_GAP clearance of the lower node row";
            }


            // ════════════════════════════════════════════════════════════════════════
            // computeLayout — end-to-end pipeline
            // ════════════════════════════════════════════════════════════════════════

            // ── BasicCompleteness ─────────────────────────────────────────────────────

            TEST(ComputeLayout, DoesNotThrow) {
                TestGraph g("compute_no_throw");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                NodePtr D = g.createNode("D", 1, B);
                EXPECT_NO_THROW(g.computeLayout());
            }

            TEST(ComputeLayout, AllLayerYsPopulated) {
                TestGraph g("compute_layers");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                NodePtr C = g.createNode("C", 0, B);
                g.computeLayout();
                for (int i = 0; i < 3; ++i)
                    EXPECT_TRUE(g.layerLayout().count(i)) << "layer " << i << " missing";
            }

            TEST(ComputeLayout, AllEdgeYsPopulated) {
                TestGraph g("compute_edges");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                g.computeLayout();
                for (const auto& e : g.getAllHyperedges())
                    if (!e->isSegment())
                        EXPECT_TRUE(g.edgeLayout().count(e.get()))
                        << "edge missing from edge_layout_";
            }

            TEST(ComputeLayout, AllNodeXsPopulated) {
                TestGraph g("compute_xs");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                NodePtr D = g.createNode("D", 1, B);
                g.computeLayout();
                for (const auto& node : g.getAllNodes())
                    EXPECT_TRUE(g.nodeLayout().count(node.get()))
                    << "node " << node->getName() << " missing from node_layout_";
            }

            TEST(ComputeLayout, AllNodePortsPopulated) {
                TestGraph g("compute_ports");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                g.computeLayout();

                // A is a source: must have at least one source port.
                EXPECT_FALSE(g.nodeLayout().at(A.get()).source_ports.empty());
                // B is a target: must have at least one target port.
                EXPECT_FALSE(g.nodeLayout().at(B.get()).target_ports.empty());
            }

            // ── Idempotency ───────────────────────────────────────────────────────────
            //
            // Calling computeLayout twice must produce the same layout, not accumulate
            // ports or shift coordinates.

            TEST(ComputeLayout, IdempotentOnPortCount) {
                TestGraph g("compute_idem_ports");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                g.computeLayout();
                std::size_t src1 = g.nodeLayout().at(A.get()).source_ports.size();
                std::size_t tgt1 = g.nodeLayout().at(B.get()).target_ports.size();
                g.computeLayout();
                EXPECT_EQ(g.nodeLayout().at(A.get()).source_ports.size(), src1);
                EXPECT_EQ(g.nodeLayout().at(B.get()).target_ports.size(), tgt1);
            }

            TEST(ComputeLayout, IdempotentOnLayerY) {
                TestGraph g("compute_idem_layer_y");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                g.computeLayout();
                double y0_first = g.layerLayout().at(0);
                double y1_first = g.layerLayout().at(1);
                g.computeLayout();
                EXPECT_NEAR(g.layerLayout().at(0), y0_first, 1e-9);
                EXPECT_NEAR(g.layerLayout().at(1), y1_first, 1e-9);
            }

            TEST(ComputeLayout, IdempotentOnEdgeY) {
                TestGraph g("compute_idem_edge_y");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                g.computeLayout();
                HyperedgePtr e = findEdge(g, A, B);
                ASSERT_NE(e, nullptr);
                double y_first = g.edgeLayout().at(e.get());
                g.computeLayout();
                EXPECT_NEAR(g.edgeLayout().at(e.get()), y_first, 1e-9);
            }

            TEST(ComputeLayout, IdempotentOnNodeX) {
                TestGraph g("compute_idem_node_x");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                NodePtr D = g.createNode("D", 1, B);
                g.computeLayout();
                double xA = g.getX(A), xB = g.getX(B), xC = g.getX(C), xD = g.getX(D);
                g.computeLayout();
                EXPECT_NEAR(g.getX(A), xA, 1e-9);
                EXPECT_NEAR(g.getX(B), xB, 1e-9);
                EXPECT_NEAR(g.getX(C), xC, 1e-9);
                EXPECT_NEAR(g.getX(D), xD, 1e-9);
            }

            // ── DiamondLayout ─────────────────────────────────────────────────────────
            //
            // Layer 0: [A]
            // Layer 1: [B] [C]   A->B, A->C
            // Layer 2: [D]       B->D, C->D
            //
            // Full pipeline must assign all coordinates and bars without crashing.

            TEST(ComputeLayout, DiamondCompletesWithoutError) {
                TestGraph g("compute_diamond");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                NodePtr C = g.createNode("C", 1, A);
                NodePtr D = g.createNode("D", 0, B);
                g.addConnection(C, D);
                EXPECT_NO_THROW(g.computeLayout());
            }

            TEST(ComputeLayout, DiamondLayersMonotone) {
                TestGraph g("compute_diamond_monotone");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                NodePtr C = g.createNode("C", 1, A);
                NodePtr D = g.createNode("D", 0, B);
                g.addConnection(C, D);
                g.computeLayout();
                EXPECT_LT(g.layerLayout().at(1), g.layerLayout().at(0));
                EXPECT_LT(g.layerLayout().at(2), g.layerLayout().at(1));
            }

            TEST(ComputeLayout, DiamondAllEdgesHaveY) {
                TestGraph g("compute_diamond_edges");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 0, A);
                NodePtr C = g.createNode("C", 1, A);
                NodePtr D = g.createNode("D", 0, B);
                g.addConnection(C, D);
                g.computeLayout();
                for (const auto& e : g.getAllHyperedges())
                    if (!e->isSegment())
                        EXPECT_TRUE(g.edgeLayout().count(e.get()))
                        << "edge " << e << " has no y coordinate";
            }


            // ════════════════════════════════════════════════════════════════════════
            // relocateNodeInLayer
            // ════════════════════════════════════════════════════════════════════════

            // ── NoOpWhenXUnchanged ────────────────────────────────────────────────────

            TEST(RelocateNode, NoSwapWhenXUnchanged) {
                TestGraph g("reloc_noop");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                g.computeLayout();

                double x_before = g.getX(A);
				ASSERT_THROW(g.relocateNodeInLayer(A, x_before), std::invalid_argument);
                EXPECT_NEAR(g.getX(A), x_before, 1e-9);
            }

            // ── LayoutRecomputedAfterRelocation ───────────────────────────────────────

            TEST(RelocateNode, LayoutIsRecomputedAfterMove) {
                // After relocating a node the layout must still be internally consistent:
                // layers must be monotone and all edges must have a y entry.
                TestGraph g("reloc_recompute");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                NodePtr D = g.createNode("D", 1, B);
                g.computeLayout();

                double xB = g.getX(B);
                // Move B left past A.
                g.relocateNodeInLayer(B, g.getX(A) - 1.0);

                // Layers must still be monotone.
                auto& ll = g.layerLayout();
                for (auto it = std::next(ll.begin()); it != ll.end(); ++it) {
                    auto prev = std::prev(it);
                    EXPECT_LT(it->second, prev->second)
                        << "layer " << it->first << " not below layer " << prev->first
                        << " after relocation";
                }

                // All edges must still have a y entry.
                for (const auto& e : g.getAllHyperedges())
                    if (!e->isSegment())
                        EXPECT_TRUE(g.edgeLayout().count(e.get()))
                        << "edge " << e << " lost its y after relocation";
            }

            // ── OrderPreservedWhenNotCrossing ─────────────────────────────────────────
            //
            // Moving a node to a position that does not cross any neighbour must not
            // change the order of nodes in the layer.

            TEST(RelocateNode, NoSwapWhenNotCrossingNeighbour) {
                TestGraph g("reloc_no_cross");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                g.computeLayout();

                double xA = g.getX(A);
                double xB = g.getX(B);
                // Move A slightly but not past B.
                double new_x = xA + (xB - xA) * 0.3;
                ASSERT_THROW(g.relocateNodeInLayer(A, new_x), std::invalid_argument);

                // A must still be to the left of B in the layer order.
                const auto& nodes = g.layers().at(0).nodes;
                auto itA = std::find(nodes.begin(), nodes.end(), A);
                auto itB = std::find(nodes.begin(), nodes.end(), B);
                ASSERT_NE(itA, nodes.end());
                ASSERT_NE(itB, nodes.end());
                EXPECT_LT(std::distance(nodes.begin(), itA),
                    std::distance(nodes.begin(), itB))
                    << "A should still precede B when not crossing it";
            }

            // ── SwapOccursWhenCrossing ────────────────────────────────────────────────
            //
            // Moving a node past its neighbour must swap their positions in the layer.

            TEST(RelocateNode, SwapOccursWhenMovingPastNeighbour) {
                TestGraph g("reloc_swap");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                g.computeLayout();

                double xA = g.getX(A);
                double xB = g.getX(B);
                ASSERT_LT(xA, xB) << "pre-condition: A is left of B";

                // Move A to the right of B's current position.
                g.relocateNodeInLayer(A, xB + 10.0);

                // In the layer vector, B should now precede A.
                const auto& nodes = g.layers().at(0).nodes;
                auto itA = std::find(nodes.begin(), nodes.end(), A);
                auto itB = std::find(nodes.begin(), nodes.end(), B);
                ASSERT_NE(itA, nodes.end());
                ASSERT_NE(itB, nodes.end());
                EXPECT_LT(std::distance(nodes.begin(), itB),
                    std::distance(nodes.begin(), itA))
                    << "B should precede A after A is moved past it";
            }

            TEST(RelocateNode, SwapOccursWhenMovingLeftPastNeighbour) {
                TestGraph g("reloc_swap_left");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                g.computeLayout();

                double xA = g.getX(A);
                double xB = g.getX(B);
                ASSERT_LT(xA, xB) << "pre-condition: A is left of B";

                // Move B to the left of A's current position.
                g.relocateNodeInLayer(B, xA - 10.0);

                const auto& nodes = g.layers().at(0).nodes;
                auto itA = std::find(nodes.begin(), nodes.end(), A);
                auto itB = std::find(nodes.begin(), nodes.end(), B);
                ASSERT_NE(itA, nodes.end());
                ASSERT_NE(itB, nodes.end());
                EXPECT_LT(std::distance(nodes.begin(), itB),
                    std::distance(nodes.begin(), itA))
                    << "B should precede A after B is moved left past A";
            }

            // ── OnlyOneSwapPerCall ────────────────────────────────────────────────────
            //
            // Even when moved past multiple neighbours in one call, only the immediate
            // one is swapped (the function breaks after the first crossing found).

            TEST(RelocateNode, OnlyOneSwapPerCall) {
                TestGraph g("reloc_one_swap");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 2, nullptr);
                g.computeLayout();

                // Order before: A B C (left to right in layer 0).
                double xC = g.getX(C);

                // Move A all the way past both B and C.
                g.relocateNodeInLayer(A, xC + 20.0);

                const auto& nodes = g.layers().at(0).nodes;
                // A should be after C
                auto itA = std::find(nodes.begin(), nodes.end(), A);
                EXPECT_NE(itA, nodes.begin()) << "A should have moved at least one position";
                // And there should be exactly one neighbour before it now (B swapped in).
                EXPECT_EQ(std::distance(nodes.begin(), itA), 2)
                    << "two swaps should have occurred";
            }

            // ── DoesNotThrowOnEdgeCases ───────────────────────────────────────────────

            TEST(RelocateNode, DoesNotThrowOnSingleNodeLayer) {
                TestGraph g("reloc_single");
                NodePtr A = g.createNode("A", 0, nullptr);
                g.createNode("B", 0, A);
                g.computeLayout();
                // Layer 0 has only A — moving it should not crash.
                EXPECT_THROW(g.relocateNodeInLayer(A, g.getX(A) + 50.0), std::invalid_argument);
            }
            // ── ConsistencyAfterMultipleRelocations ───────────────────────────────────

            TEST(RelocateNode, ConsistentAfterMultipleRelocations) {
                TestGraph g("reloc_multi");
                NodePtr A = g.createNode("A", 0, nullptr);
                NodePtr B = g.createNode("B", 1, nullptr);
                NodePtr C = g.createNode("C", 0, A);
                NodePtr D = g.createNode("D", 1, B);
                g.computeLayout();

                // Three sequential relocations.
                ASSERT_NO_THROW(g.relocateNodeInLayer(A, g.getX(B) + 10.0));
                g.assignXCoordinates();
                ASSERT_THROW(g.relocateNodeInLayer(B, g.getX(A) - 10.0), std::invalid_argument);
                g.assignXCoordinates();
                ASSERT_THROW(g.relocateNodeInLayer(A, g.getX(B) + 5.0), std::invalid_argument);
                g.assignXCoordinates();

                // After all relocations the layout must still be complete.
                for (int i = 0; i < 2; ++i)
                    EXPECT_TRUE(g.layerLayout().count(i)) << "layer " << i << " missing";
                for (const auto& e : g.getAllHyperedges())
                    if (!e->isSegment())
                        EXPECT_TRUE(g.edgeLayout().count(e.get()));
            }

            // ════════════════════════════════════════════════════════════════════════
            // Node shapes — widths, heights and boundary-aware port y-coordinates
            // ════════════════════════════════════════════════════════════════════════

            static NodeAttributes shaped(const std::string& name, NodeShape shape) {
                NodeAttributes a(name);
                a.shape = shape;
                return a;
            }

            static double layerHeight(TestGraph& g, int layer) {
                double h = 0.0;
                for (const auto& n : g.layers().at(layer).nodes) h = std::max(h, n->getHeight());
                return h;
            }

            // Builds a graph mixing every shape: a circle root with three children (one of
            // each shape), a rhombus root sharing a hyperedge with a rectangle root, and a
            // long edge that forces dummies.
            static void buildMixedShapeGraph(TestGraph& g) {
                NodePtr C = g.createNode(shaped("C", NodeShape::Circle), -1, nullptr);
                NodePtr R = g.createNode(shaped("R", NodeShape::Rhombus), -1, nullptr);
                NodePtr Q = g.createNode("Q", -1, nullptr);
                NodePtr c1 = g.createNode(shaped("c1", NodeShape::Circle), -1, C);
                NodePtr c2 = g.createNode(shaped("c2", NodeShape::Rhombus), -1, C);
                NodePtr c3 = g.createNode("c3", -1, C);
                NodePtr r1 = g.createNode(shaped("r1", NodeShape::Rhombus), -1, R);
                g.addSourceToEdge(findEdge(g, R, r1), Q);
                NodePtr deep = g.createNode(shaped("deep", NodeShape::Circle), -1, c1);
                g.addConnection(Q, deep); // long edge Q -> deep, split with a dummy
            }

            TEST(NodeGeometry, WidthAndHeightPerShape) {
                Node rect("r"), circle(shaped("c", NodeShape::Circle)), rhombus(shaped("h", NodeShape::Rhombus)), dummy;
                EXPECT_DOUBLE_EQ(rect.getWidth(), NODE_WIDTH);
                EXPECT_DOUBLE_EQ(rect.getHeight(), NODE_HEIGHT);
                EXPECT_DOUBLE_EQ(circle.getWidth(), CIRCLE_NODE_WIDTH);
                EXPECT_DOUBLE_EQ(circle.getHeight(), CIRCLE_NODE_HEIGHT);
                EXPECT_DOUBLE_EQ(rhombus.getWidth(), RHOMBUS_NODE_WIDTH);
                EXPECT_DOUBLE_EQ(rhombus.getHeight(), RHOMBUS_NODE_HEIGHT);
                EXPECT_DOUBLE_EQ(dummy.getWidth(), DUMMY_NODE_WIDTH);
                EXPECT_DOUBLE_EQ(dummy.getHeight(), DUMMY_NODE_HEIGHT);
                // Circle and rhombus are regular: inscribed in a square.
                EXPECT_DOUBLE_EQ(circle.getWidth(), circle.getHeight());
                EXPECT_DOUBLE_EQ(rhombus.getWidth(), rhombus.getHeight());
            }

            TEST(NodeGeometry, BoundaryHalfHeightPerShape) {
                Node rect("r"), circle(shaped("c", NodeShape::Circle)), rhombus(shaped("h", NodeShape::Rhombus)), dummy;
                const double r = REGULAR_NODE_SIZE / 2.0;

                for (double dx : { 0.0, 10.0, -30.0, NODE_WIDTH / 2.0 })
                    EXPECT_DOUBLE_EQ(rect.getBoundaryHalfHeight(dx), NODE_HEIGHT / 2.0);

                EXPECT_DOUBLE_EQ(circle.getBoundaryHalfHeight(0.0), r);
                EXPECT_NEAR(circle.getBoundaryHalfHeight(0.6 * r), 0.8 * r, 1e-9);  // 3-4-5 triangle
                EXPECT_NEAR(circle.getBoundaryHalfHeight(-0.6 * r), 0.8 * r, 1e-9);
                EXPECT_DOUBLE_EQ(circle.getBoundaryHalfHeight(r), 0.0);
                EXPECT_DOUBLE_EQ(circle.getBoundaryHalfHeight(3.0 * r), 0.0);        // clamped

                EXPECT_DOUBLE_EQ(rhombus.getBoundaryHalfHeight(0.0), r);
                EXPECT_DOUBLE_EQ(rhombus.getBoundaryHalfHeight(r / 2.0), r / 2.0);
                EXPECT_DOUBLE_EQ(rhombus.getBoundaryHalfHeight(-r / 4.0), 0.75 * r);
                EXPECT_DOUBLE_EQ(rhombus.getBoundaryHalfHeight(r), 0.0);
                EXPECT_DOUBLE_EQ(rhombus.getBoundaryHalfHeight(-2.0 * r), 0.0);      // clamped

                EXPECT_DOUBLE_EQ(dummy.getBoundaryHalfHeight(0.0), 0.0);
            }

            // Circle -> circle, single trivial edge: every distance follows from the
            // circle's height instead of NODE_HEIGHT.
            TEST(ShapeLayout, CircleChainUsesCircleHeightForLayerGap) {
                TestGraph g("circle_chain");
                NodePtr A = g.createNode(shaped("A", NodeShape::Circle), 0, nullptr);
                NodePtr B = g.createNode(shaped("B", NodeShape::Circle), 0, A);
                runFullPipeline(g);

                const double half = REGULAR_NODE_SIZE / 2.0;
                EXPECT_NEAR(g.layerLayout().at(1), -(half + LAYER_GAP + half), 1e-9);

                // Single centred ports touch the circles at their lowest/highest point.
                const Port& src = g.nodeLayout().at(A.get()).source_ports.at(0);
                const Port& tgt = g.nodeLayout().at(B.get()).target_ports.at(0);
                EXPECT_NEAR(src.x, g.nodeLayout().at(A.get()).x, 1e-9);
                EXPECT_NEAR(src.y, -half, 1e-9);
                EXPECT_NEAR(tgt.y, g.layerLayout().at(1) + half, 1e-9);
            }

            // Rectangles keep their previous geometry exactly.
            TEST(ShapeLayout, RectanglePortsSitOnBoxEdges) {
                TestGraph g("rect");
                NodePtr A = g.createNode("A", 0, nullptr);
                g.createNode("B", 0, A);
                g.createNode("C", 1, A);
                runFullPipeline(g);
                for (const auto& [layer_idx, data] : g.layers()) {
                    const double y = g.layerLayout().at(layer_idx);
                    for (const auto& n : data.nodes) {
                        for (const Port& p : g.nodeLayout().at(n.get()).source_ports)
                            EXPECT_NEAR(p.y, y - NODE_HEIGHT / 2.0, 1e-9);
                        for (const Port& p : g.nodeLayout().at(n.get()).target_ports)
                            EXPECT_NEAR(p.y, y + NODE_HEIGHT / 2.0, 1e-9);
                    }
                }
            }

            // A circle with three outgoing edges: ports stay within its width, and the
            // off-centre ones meet the boundary closer to the centre than the middle one.
            TEST(ShapeLayout, OffCentrePortsOfCircleMeetBoundaryLower) {
                TestGraph g("circle_fan");
                NodePtr P = g.createNode(shaped("P", NodeShape::Circle), 0, nullptr);
                g.createNode("a", 0, P);
                g.createNode("b", 1, P);
                g.createNode("c", 2, P);
                runFullPipeline(g);

                const NodeLayout& nl = g.nodeLayout().at(P.get());
                ASSERT_EQ(nl.source_ports.size(), 3u);
                double deepest = 0.0, shallowest = -std::numeric_limits<double>::max();
                for (const Port& p : nl.source_ports) {
                    EXPECT_LT(std::abs(p.x - nl.x), CIRCLE_NODE_WIDTH / 2.0);
                    deepest = std::min(deepest, p.y);
                    shallowest = std::max(shallowest, p.y);
                }
                EXPECT_LT(deepest, -0.75 * CIRCLE_NODE_HEIGHT / 2.0);  // the middle port, near the bottom
                EXPECT_GE(deepest, -CIRCLE_NODE_HEIGHT / 2.0 - 1e-9);   // never below the circle
                EXPECT_GT(shallowest, deepest + 1.0);                   // the outer ones sit higher
            }

            // Invariants over a graph mixing every shape and dummies.
            TEST(ShapeLayout, MixedShapes_PortsLieOnTheirNodeBoundary) {
                TestGraph g("mixed");
                buildMixedShapeGraph(g);
                runFullPipeline(g);

                for (const auto& [layer_idx, data] : g.layers()) {
                    const double y = g.layerLayout().at(layer_idx);
                    for (const auto& n : data.nodes) {
                        const NodeLayout& nl = g.nodeLayout().at(n.get());
                        for (const Port& p : nl.source_ports) {
                            EXPECT_LE(std::abs(p.x - nl.x), n->getWidth() / 2.0 + 1e-9);
                            EXPECT_NEAR(p.y, y - n->getBoundaryHalfHeight(p.x - nl.x), 1e-9);
                        }
                        for (const Port& p : nl.target_ports) {
                            EXPECT_LE(std::abs(p.x - nl.x), n->getWidth() / 2.0 + 1e-9);
                            EXPECT_NEAR(p.y, y + n->getBoundaryHalfHeight(p.x - nl.x), 1e-9);
                        }
                        if (n->isDummy()) {
                            for (const Port& p : nl.source_ports) EXPECT_NEAR(p.y, y, 1e-9);
                            for (const Port& p : nl.target_ports) EXPECT_NEAR(p.y, y, 1e-9);
                        }
                    }
                }
            }

            TEST(ShapeLayout, MixedShapes_LayerGapMeasuredFromTallestNode) {
                TestGraph g("mixed");
                buildMixedShapeGraph(g);
                runFullPipeline(g);

                for (const auto& [layer_idx, data] : g.layers()) {
                    if (layer_idx == 0) continue;
                    const double upper_bottom = g.layerLayout().at(layer_idx - 1) - layerHeight(g, layer_idx - 1) / 2.0;
                    const double lower_top = g.layerLayout().at(layer_idx) + layerHeight(g, layer_idx) / 2.0;
                    EXPECT_LE(lower_top, upper_bottom - LAYER_GAP + 1e-9) << "layer " << layer_idx;

                    // Every non-trivial bar keeps LAYER_GAP from both node rows.
                    for (const auto& e : g.layers().at(layer_idx - 1).outgoing_edges) {
                        double bar_y = g.edgeLayout().at(e.get());
                        if (std::abs(bar_y - upper_bottom) < 1e-9) continue; // trivial
                        EXPECT_LE(bar_y, upper_bottom - LAYER_GAP + 1e-9);
                        EXPECT_GE(bar_y, lower_top + LAYER_GAP - 1e-9);
                    }
                }
            }

            TEST(ShapeLayout, MixedShapes_NeighboursSeparatedByTheirOwnWidths) {
                TestGraph g("mixed");
                buildMixedShapeGraph(g);
                runFullPipeline(g);

                for (const auto& [layer_idx, data] : g.layers()) {
                    for (size_t i = 1; i < data.nodes.size(); ++i) {
                        const auto& l = data.nodes[i - 1];
                        const auto& r = data.nodes[i];
                        double gap = g.nodeLayout().at(r.get()).x - g.nodeLayout().at(l.get()).x;
                        EXPECT_GE(gap, (l->getWidth() + r->getWidth()) / 2.0 + MIN_BLOCK_SEP - 1e-6)
                            << "layer " << layer_idx << " between " << l->getName() << " and " << r->getName();
                    }
                }
            }

            TEST(ShapeLayout, PortYsSurviveCloneAndJsonRoundTrip) {
                TestGraph g("mixed");
                buildMixedShapeGraph(g);
                runFullPipeline(g);

                auto portYsByName = [](const GraphicalHypergraph& graph) {
                    std::map<std::string, std::vector<double>> ys;
                    for (const auto& n : graph.getAllNodes()) {
                        if (n->isDummy()) continue;
                        const NodeLayout& nl = graph.getNodeLayout().at(n.get());
                        for (const Port& p : nl.source_ports) ys[n->getName()].push_back(p.y);
                        for (const Port& p : nl.target_ports) ys[n->getName()].push_back(p.y);
                    }
                    return ys;
                };

                nlohmann::json j;
                g.toJSON(j);
                GraphicalHypergraph loaded = GraphicalHypergraph::fromJSON(j);
                EXPECT_EQ(portYsByName(loaded), portYsByName(g));
                EXPECT_EQ(portYsByName(g.clone()), portYsByName(g));
            }

        } // namespace layout
    } // namespace graphicalhypergraph_tests
} // namespace hypergraph_logic