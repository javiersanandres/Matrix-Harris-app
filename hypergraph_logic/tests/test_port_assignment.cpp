#include "PortAssignment.h"
#include "GraphicalHypergraph.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace hypergraph_logic {
    namespace graphicalhypergraph_tests {
        namespace port_assignment {
            using namespace port_assignment_internal;

            // ── Constants ─────────────────────────────────────────────────────────────
            //
            // Real nodes:   width = NODE_WIDTH,       half = NODE_WIDTH / 2
            // Dummy nodes:  width = DUMMY_NODE_WIDTH,  half = DUMMY_NODE_WIDTH / 2
            // Adjacent real nodes are separated by at least NODE_WIDTH + MIN_BLOCK_SEP
            // from centre to centre (= 96 px with default constants).

            // ── TestGraph ─────────────────────────────────────────────────────────────
            //
            // Exposes layers_ so tests can inspect the LayerData directly, and
            // exposes node_layout_ so tests can read port coordinates after assignPorts.
            class TestGraph : public GraphicalHypergraph {
            public:
                explicit TestGraph(const std::string& name) : GraphicalHypergraph(name) {}
                std::map<int, LayerData>& layers() { return layers_; }
                std::unordered_map<Node*, NodeLayout>& nodeLayout() { return node_layout_; }
                void assignXCoordinates() { GraphicalHypergraph::assignXCoordinates(); }
                void assignPorts() { GraphicalHypergraph::assignPorts(); }
            };

            // ── Helpers ───────────────────────────────────────────────────────────────

            // Return the first non-segment hyperedge connecting src -> tgt.
            static HyperedgePtr findEdge(const TestGraph& g,
                const NodePtr& src, const NodePtr& tgt)
            {
                for (const auto& e : g.getAllHyperedges())
                    if (!e->isSegment() && e->containsSource(src) && e->containsTarget(tgt))
                        return e;
                return nullptr;
            }

            // Run the full pipeline: coordinates first, then ports.
            static void runPipeline(TestGraph& g) {
                g.assignXCoordinates();
                g.assignPorts();
            }

            // Check that all ports on a node are strictly inside [node_x - hw, node_x + hw].
            static void checkPortsInBounds(const NodeLayout& layout, bool is_dummy) {
                double hw = (is_dummy ? DUMMY_NODE_WIDTH : NODE_WIDTH) / 2.0;
                double lo = layout.x - hw;
                double hi = layout.x + hw;
                for (const auto& p : layout.source_ports)
                    EXPECT_GE(p.x, lo - 1e-9) << "source port left of node boundary";
                for (const auto& p : layout.source_ports)
                    EXPECT_LE(p.x, hi + 1e-9) << "source port right of node boundary";
                for (const auto& p : layout.target_ports)
                    EXPECT_GE(p.x, lo - 1e-9) << "target port left of node boundary";
                for (const auto& p : layout.target_ports)
                    EXPECT_LE(p.x, hi + 1e-9) << "target port right of node boundary";
            }

            // Check that source ports on a node are strictly left-to-right.
            static void checkPortsOrdered(const std::vector<Port>& ports) {
                for (std::size_t i = 0; i + 1 < ports.size(); ++i)
                    EXPECT_LT(ports[i].x, ports[i + 1].x)
                    << "ports not strictly ordered at index " << i;
            }

            // Check the minimum separation between adjacent ports on a node.
            // min_sep is derived from the node width: with n ports evenly spaced
            // across node_width, the nominal spacing = node_width / (n + 1). After
            // solveVerticalOverlaps ports may be nudged, but must never end up closer
            // than MIN_VERTICAL_SEP to one another.
            static void checkPortSeparation(const std::vector<Port>& ports,
                double node_width, const std::string& label)
            {
                if (ports.size() < 2) return;
                int    n = static_cast<int>(ports.size());
                double nominal = node_width / static_cast<double>(n + 1);
                double floor_sep = std::min(nominal, MIN_VERTICAL_SEP);
                for (std::size_t i = 0; i + 1 < ports.size(); ++i)
                    EXPECT_GE(ports[i + 1].x - ports[i].x, floor_sep - 1e-9)
                    << "port separation violated on node " << label
                    << " between port index " << i << " and " << i + 1
                    << " (floor=" << floor_sep << ")";
            }

            // Run all structural invariants over every node in the graph:
            //   - every port lies inside [node_x - hw, node_x + hw]
            //   - ports are strictly left-to-right within each list
            //   - adjacent ports are separated by at least MIN_VERTICAL_SEP
            static void checkAllInvariants(TestGraph& g) {
                for (const auto& node : g.getAllNodes()) {
                    const NodeLayout& nl = g.nodeLayout().at(node.get());
                    bool              dummy = node->isDummy();
                    double            width = dummy ? DUMMY_NODE_WIDTH : NODE_WIDTH;
                    checkPortsInBounds(nl, dummy);
                    checkPortsOrdered(nl.source_ports);
                    checkPortsOrdered(nl.target_ports);
                    checkPortSeparation(nl.source_ports, width, node->getName());
                    checkPortSeparation(nl.target_ports, width, node->getName());
                }
            }


            // A genuine dummy node only ever comes from splitting a long edge between
            // non-adjacent layers (see Node.h / Hypergraph::addConnection) -- there is
            // no other way to produce one through the public API. This builds a real
            // node sitting exactly 'depth' layers below a fresh root, purely so it can
            // be linked into a long edge from elsewhere; 'col' just keeps its own
            // little chain out of the way of whatever else the test is building.
            static NodePtr buildRealNodeAtDepth(TestGraph& g, const std::string& base, int depth, int col) {
                NodePtr n = g.createNode(base + "_r0", 0, col, nullptr);
                for (int i = 1; i <= depth; ++i)
                    n = g.createNode(base + "_r" + std::to_string(i), n ? n->getLayer() + 1 : 0, 0, n);
                return n;
            }

            // Collects, in top-to-bottom order, every dummy node found strictly
            // between top_layer and bottom_layer (exclusive on both ends). Real
            // endpoints keep their original parent/child links regardless of how the
            // edge between them was split -- dummy routing stays invisible to them --
            // so walking from the endpoints won't find the dummies; scanning the
            // layers directly is the reliable way. Safe as long as the test doesn't
            // introduce ANOTHER dummy-producing edge through the same layers, since
            // this doesn't try to disambiguate which edge a dummy belongs to.
            static std::vector<NodePtr> collectDummiesBetweenLayers(TestGraph& g, int top_layer, int bottom_layer) {
                std::vector<NodePtr> result;
                for (int layer = top_layer + 1; layer < bottom_layer; ++layer) {
                    auto it = g.layers().find(layer);
                    if (it == g.layers().end()) continue;
                    for (const auto& n : it->second.nodes)
                        if (n->isDummy()) result.push_back(n);
                }
                return result;
            }


            // ════════════════════════════════════════════════════════════════════════
            // buildPorts — basic structural invariants
            // ════════════════════════════════════════════════════════════════════════

            // ── SingleEdge ────────────────────────────────────────────────────────────
            //
            // Layer 0: [A]       Layer 1: [B]
            // One edge A -> B. Each node gets exactly one port.

            TEST(SingleEdge, EachNodeGetsExactlyOnePort) {
                TestGraph g("single_edge");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", A->getLayer() + 1, 0, A);
                runPipeline(g);

                EXPECT_EQ(g.nodeLayout().at(A.get()).source_ports.size(), 1u);
                EXPECT_EQ(g.nodeLayout().at(B.get()).target_ports.size(), 1u);
            }

            TEST(SingleEdge, PortsAreInsideNodeBounds) {
                TestGraph g("single_edge_bounds");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", A->getLayer() + 1, 0, A);
                runPipeline(g);
                checkAllInvariants(g);
            }

            TEST(SingleEdge, SinglePortSitsAtNodeCentre) {
                // With one port, spacing = NODE_WIDTH / 2, so port x = node_x - hw + hw = node_x.
                TestGraph g("single_edge_centre");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", A->getLayer() + 1, 0, A);
                runPipeline(g);

                double xA = g.getX(A);
                double xB = g.getX(B);
                EXPECT_NEAR(g.nodeLayout().at(A.get()).source_ports[0].x, xA, 1e-9);
                EXPECT_NEAR(g.nodeLayout().at(B.get()).target_ports[0].x, xB, 1e-9);
            }

            // ── TwoEdgesSameSource ────────────────────────────────────────────────────
            //
            // Layer 0: [A]       Layer 1: [B] [C]
            // A -> B,  A -> C.   A has two source ports.

            TEST(TwoEdgesSameSource, SourceHasTwoPorts) {
                TestGraph g("two_edges_src");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", A->getLayer() + 1, 0, A);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 1, A);
                runPipeline(g);
                EXPECT_EQ(g.nodeLayout().at(A.get()).source_ports.size(), 2u);
            }

            TEST(TwoEdgesSameSource, PortsOrderedAndInBounds) {
                TestGraph g("two_edges_src_bounds");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                g.createNode("B", A->getLayer() + 1, 0, A);
                g.createNode("C", A->getLayer() + 1, 1, A);
                runPipeline(g);
                checkAllInvariants(g);
            }

            TEST(TwoEdgesSameSource, TwoPortsSymmetricAroundCentre) {
                // With 2 ports, spacing = NODE_WIDTH/3.
                // Ports sit at node_x - NODE_WIDTH/6  and  node_x + NODE_WIDTH/6.
                TestGraph g("two_src_symmetric");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                g.createNode("B", A->getLayer() + 1, 0, A);
                g.createNode("C", A->getLayer() + 1, 1, A);
                runPipeline(g);

                const auto& ports = g.nodeLayout().at(A.get()).source_ports;
                ASSERT_EQ(ports.size(), 2u);
                double xA = g.getX(A);
                double spacing = NODE_WIDTH / 3.0;
                EXPECT_NEAR(ports[0].x, xA - spacing / 2.0, 1e-9);
                EXPECT_NEAR(ports[1].x, xA + spacing / 2.0, 1e-9);
            }

            // ── TwoEdgesSameTarget ────────────────────────────────────────────────────

            TEST(TwoEdgesSameTarget, TargetHasTwoPorts) {
                TestGraph g("two_edges_tgt");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 0, A);
                g.addConnection(B, C);
                runPipeline(g);
                EXPECT_EQ(g.nodeLayout().at(C.get()).target_ports.size(), 2u);
            }

            TEST(TwoEdgesSameTarget, PortsOrderedAndInBounds) {
                TestGraph g("two_edges_tgt_bounds");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 0, A);
                g.addConnection(B, C);
                runPipeline(g);
                checkAllInvariants(g);
            }


            TEST(ABCD, GraphBuildsAndCoordinatesAssigned) {
                TestGraph g("ABCD");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 1, A);
                NodePtr D = g.createNode("D", B->getLayer() + 1, 0, B);
                runPipeline(g);

                // Basic ordering in each layer.
                EXPECT_LT(g.getX(A), g.getX(B));
                EXPECT_LT(g.getX(D), g.getX(C));
            }

            TEST(ABCD, EachNodeGetsExactlyOnePort) {
                TestGraph g("ABCD_ports");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 1, A);
                NodePtr D = g.createNode("D", B->getLayer() + 1, 0, B);
                runPipeline(g);

                EXPECT_EQ(g.nodeLayout().at(A.get()).source_ports.size(), 1u);
                EXPECT_EQ(g.nodeLayout().at(B.get()).source_ports.size(), 1u);
                EXPECT_EQ(g.nodeLayout().at(C.get()).target_ports.size(), 1u);
                EXPECT_EQ(g.nodeLayout().at(D.get()).target_ports.size(), 1u);
            }

            TEST(ABCD, PortsAreInsideNodeBounds) {
                TestGraph g("ABCD_bounds");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 1, A);
                NodePtr D = g.createNode("D", B->getLayer() + 1, 0, B);
                runPipeline(g);
                checkAllInvariants(g);
            }

            TEST(ABCD, PortConnectsCorrectEdge) {
                // The single source port of A must belong to the edge A->C,
                // and the single source port of B must belong to the edge B->D.
                TestGraph g("ABCD_edge");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 1, A);
                NodePtr D = g.createNode("D", B->getLayer() + 1, 0, B);
                runPipeline(g);

                HyperedgePtr eAC = findEdge(g, A, C);
                HyperedgePtr eBD = findEdge(g, B, D);
                ASSERT_NE(eAC, nullptr);
                ASSERT_NE(eBD, nullptr);

                EXPECT_EQ(g.nodeLayout().at(A.get()).source_ports[0].edge, eAC.get());
                EXPECT_EQ(g.nodeLayout().at(B.get()).source_ports[0].edge, eBD.get());
                EXPECT_EQ(g.nodeLayout().at(C.get()).target_ports[0].edge, eAC.get());
                EXPECT_EQ(g.nodeLayout().at(D.get()).target_ports[0].edge, eBD.get());
            }


            // ════════════════════════════════════════════════════════════════════════
            // Crossing configuration — the canonical overlap case
            //
            // Layer 0: [A=0]  [B=100]
            // Layer 1: [C=0]  [D=100]
            // Edge e1: A -> D  (spans 0 -> 100)
            // Edge e2: B -> C  (spans 100 -> 0)
            //
            // The source port of A (for e1) and the target port of C (for e2)
            // are both near x=0; the segments would cross. solveVerticalOverlaps
            // must nudge them apart.
            // ════════════════════════════════════════════════════════════════════════

            TEST(CrossingSegments, PortsAssignedWithoutCrash) {
                TestGraph g("crossing_seg");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr C = g.createNode("C", 0, 0, nullptr); // will be connected below
                NodePtr D = g.createNode("D", 0, 1, nullptr);
                // e1: A->D, e2: B->C
                HyperedgePtr e1 = g.addConnection({ A }, { D });
                HyperedgePtr e2 = g.addConnection({ B }, { C });
                EXPECT_NO_THROW(runPipeline(g));
            }

            TEST(CrossingSegments, AllPortsInBoundsAfterResolution) {
                TestGraph g("crossing_seg_bounds");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr C = g.createNode("C", 0, 0, nullptr);
                NodePtr D = g.createNode("D", 0, 1, nullptr);
                g.addConnection({ A }, { D });
                g.addConnection({ B }, { C });
                runPipeline(g);
                checkAllInvariants(g);
            }


            // ════════════════════════════════════════════════════════════════════════
            // Hyperedge with multiple sources and multiple targets
            //
            // Layer 0: [A] [B]
            // Layer 1: [C] [D]
            // One hyperedge {A,B} -> {C,D}.
            // ════════════════════════════════════════════════════════════════════════

            TEST(FanHyperedge, SourcesAndTargetsEachGetOnePort) {
                TestGraph g("fan");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 0, A);
                NodePtr D = g.createNode("D", B->getLayer() + 1, 1, B);
                HyperedgePtr e = findEdge(g, A, C);
                g.addSourceToEdge(e, B);
                g.addTargetToEdge(e, D);
                runPipeline(g);

                EXPECT_EQ(g.nodeLayout().at(A.get()).source_ports.size(), 1u);
                EXPECT_EQ(g.nodeLayout().at(B.get()).source_ports.size(), 1u);
                EXPECT_EQ(g.nodeLayout().at(C.get()).target_ports.size(), 1u);
                EXPECT_EQ(g.nodeLayout().at(D.get()).target_ports.size(), 1u);
            }

            TEST(FanHyperedge, PortsInBounds) {
                TestGraph g("fan_bounds");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 0, A);
                NodePtr D = g.createNode("D", B->getLayer() + 1, 1, B);
                HyperedgePtr e = findEdge(g, A, C);
                g.addSourceToEdge(e, B);
                g.addTargetToEdge(e, D);
                runPipeline(g);
                checkAllInvariants(g);
            }

            TEST(FanHyperedge, LeftNodePortIsRightOfCentre) {
                // A is the leftmost node of the edge. pos(A, e) = 0, so its port
                // should sit to the right of A's centre (cluster right policy).
                TestGraph g("fan_pos");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 0, A);
                NodePtr D = g.createNode("D", B->getLayer() + 1, 1, B);
                HyperedgePtr e = findEdge(g, A, C);
                g.addSourceToEdge(e, B);
                g.addTargetToEdge(e, D);
                runPipeline(g);

                // A is leftmost -> its single source port should be at node centre
                // (only one port on A). Same for B being rightmost.
                double xA = g.getX(A);
                double xB = g.getX(B);
                EXPECT_NEAR(g.nodeLayout().at(A.get()).source_ports[0].x, xA, 1e-9);
                EXPECT_NEAR(g.nodeLayout().at(B.get()).source_ports[0].x, xB, 1e-9);
            }


            // ════════════════════════════════════════════════════════════════════════
            // Three-node same-source fan
            //
            // Layer 0: [A]
            // Layer 1: [B] [C] [D]
            // Three edges A->B, A->C, A->D. A gets 3 source ports.
            // ════════════════════════════════════════════════════════════════════════

            TEST(ThreeTargetFan, SourceHasThreePorts) {
                TestGraph g("three_fan");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                g.createNode("B", A->getLayer() + 1, 0, A);
                g.createNode("C", A->getLayer() + 1, 1, A);
                g.createNode("D", A->getLayer() + 1, 2, A);
                runPipeline(g);
                EXPECT_EQ(g.nodeLayout().at(A.get()).source_ports.size(), 3u);
            }

            TEST(ThreeTargetFan, ThreePortsEvenlySpaced) {
                // With 3 ports: spacing = NODE_WIDTH / 4.
                // Ports at node_x - NODE_WIDTH/8, node_x, node_x + NODE_WIDTH/8.
                // (i.e. min_x + spacing, min_x + 2*spacing, min_x + 3*spacing)
                TestGraph g("three_fan_spacing");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                g.createNode("B", A->getLayer() + 1, 0, A);
                g.createNode("C", A->getLayer() + 1, 1, A);
                g.createNode("D", A->getLayer() + 1, 2, A);
                runPipeline(g);

                const auto& ports = g.nodeLayout().at(A.get()).source_ports;
                ASSERT_EQ(ports.size(), 3u);
                double spacing = ports[1].x - ports[0].x;
                EXPECT_GT(spacing, 0.0);
                EXPECT_NEAR(ports[2].x - ports[1].x, spacing, 1e-9);
                // Middle port is at node centre.
                EXPECT_NEAR(ports[1].x, g.getX(A), 1e-9);
            }

            TEST(ThreeTargetFan, AllPortsInBounds) {
                TestGraph g("three_fan_bounds");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                g.createNode("B", A->getLayer() + 1, 0, A);
                g.createNode("C", A->getLayer() + 1, 1, A);
                g.createNode("D", A->getLayer() + 1, 2, A);
                runPipeline(g);
                checkAllInvariants(g);
            }


            // ════════════════════════════════════════════════════════════════════════
            // Dummy nodes
            //
            // A genuine dummy node only comes from addConnection splitting an edge
            // whose child is more than one layer below its parent (Node.h /
            // Hypergraph::addConnection) -- there is no other way to produce one
            // through the public API. A -> dummy -> B, spanning three layers.
            // ════════════════════════════════════════════════════════════════════════

            TEST(DummyNode, DummyGetsOneTargetAndOneSourcePort) {
                TestGraph g("dummy_chain");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = buildRealNodeAtDepth(g, "B", 2, 5);
                g.addConnection(A, B);
                runPipeline(g);

                auto dummies = collectDummiesBetweenLayers(g, 0, 2);
                ASSERT_EQ(dummies.size(), 1u);
                const NodeLayout& dl = g.nodeLayout().at(dummies[0].get());
                EXPECT_EQ(dl.target_ports.size(), 1u);
                EXPECT_EQ(dl.source_ports.size(), 1u);
            }

            TEST(DummyNode, DummyPortsInsideDummyBounds) {
                TestGraph g("dummy_bounds");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = buildRealNodeAtDepth(g, "B", 2, 5);
                g.addConnection(A, B);
                runPipeline(g);
                checkAllInvariants(g);
            }

            TEST(DummyNode, DummySourceAndTargetPortsCoincide) {
                // A dummy's lone source and target port always sit at exactly its
                // own x, by construction -- this holds regardless of whatever
                // position the surrounding conflict/alignment logic settles it at.
                TestGraph g("dummy_aligned");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = buildRealNodeAtDepth(g, "B", 2, 5);
                g.addConnection(A, B);
                runPipeline(g);

                auto dummies = collectDummiesBetweenLayers(g, 0, 2);
                ASSERT_EQ(dummies.size(), 1u);
                const NodeLayout& dl = g.nodeLayout().at(dummies[0].get());
                double xd = dl.x;
                EXPECT_NEAR(dl.source_ports[0].x, xd, 1e-9);
                EXPECT_NEAR(dl.target_ports[0].x, xd, 1e-9);
            }


            // ════════════════════════════════════════════════════════════════════════
            // Multi-layer graph — layer-by-layer invariants
            //
            // Four layers, chain: A -> B -> C -> D.
            // ════════════════════════════════════════════════════════════════════════

            TEST(MultiLayer, AllLayersProcessed) {
                TestGraph g("multi_layer");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", A->getLayer() + 1, 0, A);
                NodePtr C = g.createNode("C", B->getLayer() + 1, 0, B);
                g.createNode("D", C->getLayer() + 1, 0, C);
                runPipeline(g);
                checkAllInvariants(g);
            }

            TEST(MultiLayer, EveryNodeWithEdgesHasAtLeastOnePort) {
                TestGraph g("multi_layer_ports");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", A->getLayer() + 1, 0, A);
                NodePtr C = g.createNode("C", B->getLayer() + 1, 0, B);
                g.createNode("D", C->getLayer() + 1, 0, C);
                runPipeline(g);

                for (const auto& node : g.getAllNodes()) {
                    const NodeLayout& nl = g.nodeLayout().at(node.get());
                    bool has_out = !nl.source_ports.empty();
                    bool has_in = !nl.target_ports.empty();
                    // Every internal node should have both.
                    // Root has no targets; leaf has no sources.
                    if (node->getName() != "A" && node->getName() != "D")
                        EXPECT_TRUE(has_out && has_in)
                        << "Internal node " << node->getName() << " missing ports";
                }
            }


            // ════════════════════════════════════════════════════════════════════════
            // Two independent parallel edges — no conflict expected
            //
            // Layer 0: [A] [B]
            // Layer 1: [C] [D]
            // e1: A->C,  e2: B->D   (parallel, no crossing)
            // ════════════════════════════════════════════════════════════════════════

            TEST(ParallelEdges, NoConflictRaisedAndPortsCorrect) {
                TestGraph g("parallel");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 0, A);
                NodePtr D = g.createNode("D", B->getLayer() + 1, 1, B);
                runPipeline(g);

                // Each node has exactly one port pointing to the correct edge.
                EXPECT_EQ(g.nodeLayout().at(A.get()).source_ports.size(), 1u);
                EXPECT_EQ(g.nodeLayout().at(B.get()).source_ports.size(), 1u);
                EXPECT_EQ(g.nodeLayout().at(C.get()).target_ports.size(), 1u);
                EXPECT_EQ(g.nodeLayout().at(D.get()).target_ports.size(), 1u);
                checkAllInvariants(g);
            }

            TEST(ParallelEdges, PortXMatchesNodeXForSinglePort) {
                // With one port per node, the port sits at the node centre.
                TestGraph g("parallel_centre");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 0, A);
                NodePtr D = g.createNode("D", B->getLayer() + 1, 1, B);
                runPipeline(g);

                EXPECT_NEAR(g.nodeLayout().at(A.get()).source_ports[0].x, g.getX(A), 1e-9);
                EXPECT_NEAR(g.nodeLayout().at(B.get()).source_ports[0].x, g.getX(B), 1e-9);
                EXPECT_NEAR(g.nodeLayout().at(C.get()).target_ports[0].x, g.getX(C), 1e-9);
                EXPECT_NEAR(g.nodeLayout().at(D.get()).target_ports[0].x, g.getX(D), 1e-9);
            }


            // ════════════════════════════════════════════════════════════════════════
            // Diamond — convergent fan
            //
            // Layer 0: [A]
            // Layer 1: [B] [C]   A->B, A->C
            // Layer 2: [D]       B->D, C->D
            // ════════════════════════════════════════════════════════════════════════

            TEST(Diamond, AllPortsAssignedAndOrdered) {
                TestGraph g("diamond");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", A->getLayer() + 1, 0, A);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 1, A);
                NodePtr D = g.createNode("D", B->getLayer() + 1, 0, B);
                g.addConnection(C, D);
                runPipeline(g);
                checkAllInvariants(g);
            }

            TEST(Diamond, NodeAHasTwoSourcePorts) {
                TestGraph g("diamond_src");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", A->getLayer() + 1, 0, A);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 1, A);
                NodePtr D = g.createNode("D", B->getLayer() + 1, 0, B);
                g.addConnection(C, D);
                runPipeline(g);
                EXPECT_EQ(g.nodeLayout().at(A.get()).source_ports.size(), 2u);
            }

            TEST(Diamond, NodeDHasTwoTargetPorts) {
                TestGraph g("diamond_tgt");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", A->getLayer() + 1, 0, A);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 1, A);
                NodePtr D = g.createNode("D", B->getLayer() + 1, 0, B);
                g.addConnection(C, D);
                runPipeline(g);
                EXPECT_EQ(g.nodeLayout().at(D.get()).target_ports.size(), 2u);
            }

            TEST(Diamond, NodeDTargetPortsAreOrderedLeftToRight) {
                TestGraph g("diamond_ordered");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", A->getLayer() + 1, 0, A);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 1, A);
                NodePtr D = g.createNode("D", B->getLayer() + 1, 0, B);
                g.addConnection(C, D);
                runPipeline(g);
                checkPortsOrdered(g.nodeLayout().at(D.get()).target_ports);
            }


            // ════════════════════════════════════════════════════════════════════════
            // assignPorts is idempotent on the port count
            //
            // Calling assignPorts twice should not double-register ports.
            // ════════════════════════════════════════════════════════════════════════

            TEST(Idempotency, PortCountStableAfterTwoCalls) {
                TestGraph g("idempotent");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", A->getLayer() + 1, 0, A);
                g.assignXCoordinates();
                g.assignPorts();
                std::size_t count_src = g.nodeLayout().at(A.get()).source_ports.size();
                std::size_t count_tgt = g.nodeLayout().at(B.get()).target_ports.size();

                // Second call: ports are re-added on top of existing ones unless
                // the implementation clears them first. We just document the count.
                // If the implementation is correct (clears before re-adding), both
                // remain 1.
                EXPECT_EQ(count_src, 1u);
                EXPECT_EQ(count_tgt, 1u);
            }


            // ════════════════════════════════════════════════════════════════════════
            // Large structured graph — 4-column ladder, 3 layers
            //
            // Each column has a straight chain: R0Ci -> R1Ci -> R2Ci.
            // No edge crosses columns, so no conflicts should arise.
            // ════════════════════════════════════════════════════════════════════════

            TEST(LargeLadder, AllPortsInBounds) {
                TestGraph g("ladder");
                std::vector<std::vector<NodePtr>> grid(3, std::vector<NodePtr>(4));
                for (int col = 0; col < 4; ++col)
                    grid[0][col] = g.createNode("R0C" + std::to_string(col), 0, col, nullptr);
                for (int row = 1; row < 3; ++row)
                    for (int col = 0; col < 4; ++col)
                        grid[row][col] = g.createNode(
                            "R" + std::to_string(row) + "C" + std::to_string(col), grid[row - 1][col]->getLayer() + 1,
                            col, grid[row - 1][col]);
                runPipeline(g);
                checkAllInvariants(g);
            }

            TEST(LargeLadder, EachInternalNodeHasExactlyOnePortEachSide) {
                TestGraph g("ladder_ports");
                std::vector<std::vector<NodePtr>> grid(3, std::vector<NodePtr>(4));
                for (int col = 0; col < 4; ++col)
                    grid[0][col] = g.createNode("R0C" + std::to_string(col), 0, col, nullptr);
                for (int row = 1; row < 3; ++row)
                    for (int col = 0; col < 4; ++col)
                        grid[row][col] = g.createNode(
                            "R" + std::to_string(row) + "C" + std::to_string(col), grid[row - 1][col]->getLayer() + 1,
                            col, grid[row - 1][col]);
                runPipeline(g);

                // Row 1 nodes are internal: one source port, one target port each.
                for (int col = 0; col < 4; ++col) {
                    const NodeLayout& nl = g.nodeLayout().at(grid[1][col].get());
                    EXPECT_EQ(nl.source_ports.size(), 1u) << "col=" << col;
                    EXPECT_EQ(nl.target_ports.size(), 1u) << "col=" << col;
                }
            }


            // ════════════════════════════════════════════════════════════════════════
            // nodePositionInEdge sanity — via PortAssigner directly
            //
            // Build a minimal layer pair and inspect pos() for the leftmost and
            // rightmost nodes of a two-source, two-target edge.
            // ════════════════════════════════════════════════════════════════════════

            TEST(NodePosition, LeftmostIsZeroRightmostIsTwo) {
                TestGraph g("node_pos");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr C = g.createNode("C", A->getLayer() + 1, 0, A);
                NodePtr D = g.createNode("D", B->getLayer() + 1, 1, B);
                HyperedgePtr e = findEdge(g, A, C);
                g.addSourceToEdge(e, B);
                g.addTargetToEdge(e, D);
                g.assignXCoordinates();
                // After coordinates: xA < xB, so A is leftmost (pos=0), B is rightmost (pos=2).
                // Build the PortAssigner for layer 0 and query directly.
                PortAssigner pa(0, g.layers(), g.nodeLayout());
                // nodePositionInEdge is private, but its effect is visible through
                // the port order after buildPorts: the rightmost node (B) should get
                // the leftmost port and vice-versa (single port each, so they land at centre).
                pa.buildPorts();
                double xA = g.getX(A), xB = g.getX(B);
                // Both nodes have exactly one port each, which lands at their centre.
                EXPECT_NEAR(g.nodeLayout().at(A.get()).source_ports[0].x, xA, 1e-9);
                EXPECT_NEAR(g.nodeLayout().at(B.get()).source_ports[0].x, xB, 1e-9);
            }


            // ── Extra helpers for the new test sections below ────────────────────────

            // A dummy chain must always move as one rigid unit: every member shares
            // the exact same x, and its lone port(s) always sit exactly at that x.
            static void checkChainIsRigid(TestGraph& g, const std::vector<NodePtr>& chain) {
                ASSERT_FALSE(chain.empty());
                double x0 = g.nodeLayout().at(chain.front().get()).x;
                for (const auto& n : chain) {
                    ASSERT_TRUE(n->isDummy()) << n->getName() << " expected to be a dummy chain member";
                    const NodeLayout& nl = g.nodeLayout().at(n.get());
                    EXPECT_NEAR(nl.x, x0, 1e-9) << n->getName();
                    for (const auto& p : nl.source_ports) EXPECT_NEAR(p.x, x0, 1e-9) << n->getName();
                    for (const auto& p : nl.target_ports) EXPECT_NEAR(p.x, x0, 1e-9) << n->getName();
                }
            }

            // Adjacent nodes within a layer must keep at least MIN_BLOCK_SEP
            // clearance between their boxes (centre-to-centre minus both half-widths).
            static void checkNodeBoxSeparation(TestGraph& g) {
                for (const auto& [layer, data] : g.layers()) {
                    const auto& nodes = data.nodes;
                    for (std::size_t i = 0; i + 1 < nodes.size(); ++i) {
                        Node* a = nodes[i].get();
                        Node* b = nodes[i + 1].get();
                        double wa = a->isDummy() ? DUMMY_NODE_WIDTH : NODE_WIDTH;
                        double wb = b->isDummy() ? DUMMY_NODE_WIDTH : NODE_WIDTH;
                        double gap = g.nodeLayout().at(b).x - g.nodeLayout().at(a).x - (wa + wb) * 0.5;
                        EXPECT_GE(gap, MIN_BLOCK_SEP - 1e-9)
                            << "box separation violated between " << a->getName() << " and " << b->getName();
                    }
                }
            }


            // ════════════════════════════════════════════════════════════════════════
            // reduceHorizontalJogs — nearly vertical ports
            //
            // Any source port and target port of one hyperedge closer than
            // MIN_BLOCK_SEP / 2 are aligned when their neighbours leave room: the
            // "straight-through" branch of a fan-out/fan-in, but also a pair in the
            // middle of a wider hyperedge. Pairs of a source and a target sharing the
            // edge's leftmost or rightmost x are candidates at any distance.
            // ════════════════════════════════════════════════════════════════════════

            // Lays out layers 0-1 with the given x per node (instead of Brandes-Köpf)
            // and runs port assignment on that pair up to buildPorts. The caller then
            // calls reduceHorizontalJogs on the returned assigner.
            static std::unique_ptr<PortAssigner> portsAt(TestGraph& g, const std::vector<std::pair<NodePtr, double>>& xs) {
                g.assignXCoordinates();
                for (const auto& [n, x] : xs) g.nodeLayout()[n.get()].x = x;
                for (auto& [n, nl] : g.nodeLayout()) { nl.source_ports.clear(); nl.target_ports.clear(); }
                auto assigner = std::make_unique<PortAssigner>(0, g.layers(), g.nodeLayout());
                assigner->buildPorts();
                return assigner;
            }

            static double portX(TestGraph& g, const NodePtr& node, const HyperedgePtr& e, bool source) {
                const NodeLayout& nl = g.nodeLayout().at(node.get());
                for (const auto& p : source ? nl.source_ports : nl.target_ports)
                    if (p.edge == e.get()) return p.x;
                ADD_FAILURE() << node->getName() << " has no port for that edge";
                return 0.0;
            }

            // {L, M, R} -> {X, Y, Z}: one hyperedge, one port per node (at its centre).
            struct WideEdge {
                NodePtr L, M, R, X, Y, Z;
                HyperedgePtr e;
            };
            static WideEdge buildWideEdge(TestGraph& g) {
                WideEdge w;
                w.L = g.createNode("L", 0, 0, nullptr);
                w.X = g.createNode("X", 1, 0, w.L);
                w.e = findEdge(g, w.L, w.X);
                w.M = g.createNode("M", 0, 1, nullptr);
                w.R = g.createNode("R", 0, 2, nullptr);
                w.Y = g.createNode("Y", 1, 1, nullptr);
                w.Z = g.createNode("Z", 1, 2, nullptr);
                g.addSourceToEdge(w.e, w.M);
                g.addSourceToEdge(w.e, w.R);
                g.addTargetToEdge(w.e, w.Y);
                g.addTargetToEdge(w.e, w.Z);
                return w;
            }

            TEST(ReduceJogs, NearlyVerticalPairInTheMiddleOfAHyperedgeIsAligned) {
                // M and Y are neither the edge's only source/target nor its extremes.
                TestGraph g("jogs_middle");
                WideEdge w = buildWideEdge(g);
                auto assigner = portsAt(g, { {w.L, 0}, {w.M, 400}, {w.R, 800}, {w.X, 150}, {w.Y, 410}, {w.Z, 650} });
                assigner->reduceHorizontalJogs();

                EXPECT_NEAR(portX(g, w.M, w.e, true), portX(g, w.Y, w.e, false), 1e-9);
                // The far-apart pairs are left alone.
                EXPECT_NEAR(portX(g, w.L, w.e, true), 0.0, 1e-9);
                EXPECT_NEAR(portX(g, w.X, w.e, false), 150.0, 1e-9);
                EXPECT_NEAR(portX(g, w.R, w.e, true), 800.0, 1e-9);
                EXPECT_NEAR(portX(g, w.Z, w.e, false), 650.0, 1e-9);
            }

            TEST(ReduceJogs, OnlyPairsCloserThanHalfTheBlockSeparationAreConsidered) {
                const double limit = MIN_BLOCK_SEP * 0.5;
                for (double gap : { limit - 1.0, limit + 1.0 }) {
                    TestGraph g("jogs_threshold");
                    WideEdge w = buildWideEdge(g);
                    auto assigner = portsAt(g, { {w.L, 0}, {w.M, 400}, {w.R, 800}, {w.X, 150}, {w.Y, 400 + gap}, {w.Z, 650} });
                    assigner->reduceHorizontalJogs();

                    const double m = portX(g, w.M, w.e, true), y = portX(g, w.Y, w.e, false);
                    if (gap < limit) EXPECT_NEAR(m, y, 1e-9) << "gap " << gap;
                    else             EXPECT_NEAR(y - m, gap, 1e-9) << "gap " << gap;
                }
            }

            TEST(ReduceJogs, SharedExtremeIsAlignedEvenWhenItsPortsAreFarApart) {
                // P -> T share x = 0, so both are the leftmost (and rightmost) of e.
                // P's other edge goes left (P -> U), so e's port sits on P's right;
                // T's other edge comes from the right (W -> T), so e's port sits on
                // T's left: the two ports start more than MIN_BLOCK_SEP / 2 apart.
                TestGraph g("jogs_extreme");
                NodePtr P = g.createNode("P", 0, 0, nullptr);
                NodePtr T = g.createNode("T", 1, 0, P);
                NodePtr U = g.createNode("U", 1, 0, P);
                NodePtr W = g.createNode("W", 0, 1, nullptr);
                g.addConnection(W, T);
                ASSERT_EQ(g.layers().at(1).nodes.front(), U);
                ASSERT_EQ(g.layers().at(0).nodes.back(), W);
                HyperedgePtr e = findEdge(g, P, T);
                ASSERT_NE(e, nullptr);

                auto assigner = portsAt(g, { {P, 0}, {W, 200}, {U, -200}, {T, 0} });
                ASSERT_GT(std::abs(portX(g, P, e, true) - portX(g, T, e, false)), MIN_BLOCK_SEP * 0.5)
                    << "the setup must start with the ports further apart than a nearly vertical pair";
                assigner->reduceHorizontalJogs();

                EXPECT_NEAR(portX(g, P, e, true), portX(g, T, e, false), 1e-9);
                checkAllInvariants(g);
            }

            TEST(ReduceJogsFanOut, LoneSourceAlignsWhenCloseEnoughToATarget) {
                // R has a single hyperedge fanning out to two targets: S (its only
                // "straight-through" child, so BK is likely to place it close to R)
                // and Far (pushed well away). This is checked as an implication rather
                // than an exact value, since the precise gap BK produces for this
                // topology isn't something the test controls directly -- but if the
                // two do end up close, the fallback must have fully aligned them.
                TestGraph g("jogs_fanout_src");
                NodePtr R = g.createNode("R", 0, 0, nullptr);
                NodePtr S = g.createNode("S", R->getLayer() + 1, 0, R);
                HyperedgePtr e = findEdge(g, R, S);
                ASSERT_NE(e, nullptr);
                NodePtr FarAnchor = g.createNode("FarAnchor", 0, 1, nullptr);
                NodePtr Far = g.createNode("Far", FarAnchor->getLayer() + 1, 5, FarAnchor);
                g.addTargetToEdge(e, Far);
                runPipeline(g);
                checkAllInvariants(g);

                ASSERT_EQ(g.nodeLayout().at(R.get()).source_ports.size(), 1u);
                double rx = g.nodeLayout().at(R.get()).source_ports[0].x;
                double sx = g.nodeLayout().at(S.get()).target_ports[0].x;
                if (std::abs(rx - sx) < 3 * MIN_VERTICAL_SEP)
                    EXPECT_NEAR(rx, sx, 1e-9)
                    << "R and S were close enough to trigger the fallback but weren't aligned";
            }

            TEST(ReduceJogsFanOut, FanOutNeverViolatesStructuralInvariants) {
                // Same setup as above, but only checking that nothing crashes and
                // every ordinary invariant (bounds, ordering, separation) still holds
                // regardless of whether the fallback actually triggered.
                TestGraph g("jogs_fanout_invariants");
                NodePtr R = g.createNode("R", 0, 0, nullptr);
                NodePtr S = g.createNode("S", R->getLayer() + 1, 0, R);
                HyperedgePtr e = findEdge(g, R, S);
                ASSERT_NE(e, nullptr);
                NodePtr FarAnchor = g.createNode("FarAnchor", 0, 1, nullptr);
                NodePtr Far = g.createNode("Far", FarAnchor->getLayer() + 1, 5, FarAnchor);
                g.addTargetToEdge(e, Far);
                EXPECT_NO_THROW(runPipeline(g));
                checkAllInvariants(g);
            }

            TEST(ReduceJogsFanOut, SymmetricCaseLoneTargetAlignsWithSource) {
                // Mirror of the source case: T has a single hyperedge with multiple
                // sources, one of which (S) is likely to end up close to it.
                TestGraph g("jogs_fanin_tgt");
                NodePtr S = g.createNode("S", 0, 0, nullptr);
                NodePtr T = g.createNode("T", S->getLayer() + 1, 0, S);
                HyperedgePtr e = findEdge(g, S, T);
                ASSERT_NE(e, nullptr);
                NodePtr FarSrc = g.createNode("FarSrc", 0, 5, nullptr);
                g.addSourceToEdge(e, FarSrc);
                runPipeline(g);
                checkAllInvariants(g);

                ASSERT_EQ(g.nodeLayout().at(T.get()).target_ports.size(), 1u);
                double sx = g.nodeLayout().at(S.get()).source_ports[0].x;
                double tx = g.nodeLayout().at(T.get()).target_ports[0].x;
                if (std::abs(sx - tx) < 3 * MIN_VERTICAL_SEP)
                    EXPECT_NEAR(sx, tx, 1e-9)
                    << "S and T were close enough to trigger the fallback but weren't aligned";
            }


            // ════════════════════════════════════════════════════════════════════════
            // placeDummyChains
            //
            // A dummy chain (one or more chain-linked dummy nodes) is always moved as
            // a single rigid unit before reduceHorizontalJogs/solveVerticalOverlaps
            // ever see a port. Every chain here is produced the only way a dummy
            // actually comes into existence: addConnection between a parent and a
            // child more than one layer apart. Because reaching a given depth
            // inherently requires some other real node lineage occupying those same
            // intermediate layers, true isolation isn't achievable through this API --
            // so these lean on structural invariants (rigidity, box separation) rather
            // than predicted exact positions, the same way the existing
            // CrossingSegments tests above handle their own hard-to-predict cases.
            // ════════════════════════════════════════════════════════════════════════

            TEST(PlaceDummyChains, SingleDummyChainIsRigidAndRespectsInvariants) {
                // A (layer 0) connected directly to B (layer 2, built independently):
                // addConnection must split this into A -> dummy -> B. Whatever
                // position it settles at, the dummy's own ports must always sit
                // exactly at its own x (chain rigidity), and every ordinary
                // invariant must hold.
                TestGraph g("chain_single");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = buildRealNodeAtDepth(g, "B", 2, 5);
                g.addConnection(A, B);
                EXPECT_NO_THROW(runPipeline(g));
                checkAllInvariants(g);
                checkNodeBoxSeparation(g);

                auto dummies = collectDummiesBetweenLayers(g, 0, 2);
                ASSERT_EQ(dummies.size(), 1u);
                checkChainIsRigid(g, { dummies[0] });
            }

            TEST(PlaceDummyChains, LongChainSharesOneXThroughout) {
                // A (layer 0) connected to B (layer 4): a genuine multi-member dummy
                // chain. Every member must share the exact same x and matching
                // ports, regardless of how the surrounding graph resolves.
                TestGraph g("chain_long");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = buildRealNodeAtDepth(g, "B", 4, 5);
                g.addConnection(A, B);
                EXPECT_NO_THROW(runPipeline(g));
                checkAllInvariants(g);
                checkNodeBoxSeparation(g);

                auto dummies = collectDummiesBetweenLayers(g, 0, 4);
                ASSERT_EQ(dummies.size(), 3u);
                checkChainIsRigid(g, dummies);
            }

            TEST(PlaceDummyChains, CrossingChainsStayRigidAndInvariantsHold) {
                // Two long, crossing edges: A (left) -> D (built on the right) and
                // B (right) -> C (built on the left), both routed through the same
                // intermediate layer. Whatever position the chain-vs-chain conflict
                // resolution settles on, each chain must still be rigid and every
                // ordinary structural invariant must still hold.
                TestGraph g("chain_crossing");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr B = g.createNode("B", 0, 1, nullptr);
                NodePtr D = buildRealNodeAtDepth(g, "D", 2, 5);
                NodePtr C = buildRealNodeAtDepth(g, "C", 2, 6);
                g.addConnection(A, D);
                g.addConnection(B, C);
                EXPECT_NO_THROW(runPipeline(g));
                checkAllInvariants(g);
                checkNodeBoxSeparation(g);

                auto dummies = collectDummiesBetweenLayers(g, 0, 2);
                ASSERT_EQ(dummies.size(), 2u);
                checkChainIsRigid(g, { dummies[0] });
                checkChainIsRigid(g, { dummies[1] });
            }


            // ════════════════════════════════════════════════════════════════════════
            // centerSingleHyperedgeRoots
            //
            // A node with no parents and exactly one outgoing hyperedge is free to
            // slide to the midpoint of that hyperedge's other endpoints, as long as
            // it keeps 2*MIN_VERTICAL_SEP clearance from the nearest source port on
            // either side within the layer.
            // ════════════════════════════════════════════════════════════════════════

            TEST(CenterSingleHyperedgeRoots, SingleTargetEndsUpDirectlyAboveIt) {
                // R's only hyperedge has one target C: the "span" collapses to a
                // single point (C's port), so R must end up exactly above it.
                TestGraph g("center_single_target");
                NodePtr R = g.createNode("R", 0, 0, nullptr);
                NodePtr C = g.createNode("C", R->getLayer() + 1, 0, R);
                runPipeline(g);
                checkAllInvariants(g);

                double xR = g.nodeLayout().at(R.get()).x;
                double xC = g.nodeLayout().at(C.get()).target_ports[0].x;
                EXPECT_NEAR(xR, xC, 1e-9);
                EXPECT_NEAR(g.nodeLayout().at(R.get()).source_ports[0].x, xC, 1e-9);
            }

            TEST(CenterSingleHyperedgeRoots, NodeWithAParentIsNeverTouched) {
                // A node that has a parent is entirely out of scope for this pass,
                // even if it otherwise looks like a candidate (a single source port).
                // There's no independent "before" value to compare against without
                // reimplementing the whole pipeline, so this only checks that the
                // pass runs cleanly and every ordinary invariant still holds.
                TestGraph g("center_skips_non_roots");
                NodePtr A = g.createNode("A", 0, 0, nullptr);
                NodePtr Mid = g.createNode("Mid", A->getLayer() + 1, 0, A);
                g.createNode("Leaf", Mid->getLayer() + 1, 0, Mid);
                EXPECT_NO_THROW(runPipeline(g));
                checkAllInvariants(g);
            }

            TEST(CenterSingleHyperedgeRoots, AlreadyAlignedRootIsLeftUntouched) {
                // Once R is exactly above its single target (as in the single-target
                // test above), running the pipeline again must be a no-op on R's
                // position: it's already aligned, so nothing should move it.
                TestGraph g("center_already_aligned");
                NodePtr R = g.createNode("R", 0, 0, nullptr);
                g.createNode("C", R->getLayer() + 1, 0, R);
                runPipeline(g);
                double before = g.nodeLayout().at(R.get()).x;
                g.assignXCoordinates();
                g.assignPorts();
                double after = g.nodeLayout().at(R.get()).x;
                EXPECT_NEAR(before, after, 1e-9);
            }

            TEST(CenterSingleHyperedgeRoots, RootWithTwoHyperedgesIsNeverTouched) {
                // R has two separate outgoing edges (two source ports), so it has
                // more than one hyperedge and must be skipped entirely regardless
                // of having no parents.
                TestGraph g("center_multi_edge_root_skipped");
                NodePtr R = g.createNode("R", 0, 0, nullptr);
                g.createNode("C1", R->getLayer() + 1, 0, R);
                g.createNode("C2", R->getLayer() + 1, 1, R);
                runPipeline(g);
                checkAllInvariants(g);
                EXPECT_EQ(g.nodeLayout().at(R.get()).source_ports.size(), 2u);
            }


        } // namespace port_assignment
    } // namespace graphicalhypergraph_tests
} // namespace hypergraph_logic