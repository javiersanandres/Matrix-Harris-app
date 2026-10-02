#include "GraphicalHypergraph.h"
#include <gtest/gtest.h>

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// ============================================================================
// A box that moves to another layer after a removal must have that layer
// sifted (Hypergraph::relocateAndMinimize), not just be left where
// choosePositionForRelocatedNode guessed. Cases that used to skip it:
//   - removeConnection, when the relocated child had no children of its own;
//   - removeSourcesFromHyperedge, when the edge lost every source and its
//     targets were left with no parent at all;
//   - removeTargetsFromHyperedge, which sifted from the parents of the targets
//     that stayed, not from where the removed ones went.
//
// Every crossing minimization first calls placeUnpositionedNodes(first, last)
// with the layers it is about to sift, so the graph below records them.
// ============================================================================

namespace hypergraph_logic {
    namespace relocation_sifting_tests {

        class RecordingGraph : public GraphicalHypergraph {
        public:
            explicit RecordingGraph(const std::string& name) : GraphicalHypergraph(name) {}

            std::vector<std::pair<int, int>> sifted; // [first, last] of every pass

            bool siftedLayer(int layer) const {
                for (const auto& [first, last] : sifted)
                    if (first <= layer && layer <= last) return true;
                return false;
            }

        protected:
            void placeUnpositionedNodes(int first_layer, int last_layer) override {
                sifted.emplace_back(first_layer, last_layer);
                GraphicalHypergraph::placeUnpositionedNodes(first_layer, last_layer);
            }
        };

        NodePtr box(RecordingGraph& g, const char* name, int layer, const NodePtr& parent = nullptr) {
            return g.createNode(NodeAttributes(name), layer, -1, parent);
        }

        HyperedgePtr edgeBetween(RecordingGraph& g, const NodePtr& source, const NodePtr& target) {
            for (const auto& e : g.getAllHyperedges())
                if (!e->isSegment() && e->containsSource(source) && e->containsTarget(target)) return e;
            return nullptr;
        }

        TEST(RelocationSifting, RemovingAConnectionSiftsTheLeafThatMovesUp) {
            // q and r on top, p under r, and the leaf c under p and (directly) under q.
            RecordingGraph g("leaf");
            auto q = box(g, "q", 0);
            auto r = box(g, "r", 0);
            auto p = box(g, "p", 1, r);
            auto c = box(g, "c", 2, p);
            g.addConnection(q, c);
            ASSERT_EQ(c->getLayer(), 2);

            g.sifted.clear();
            g.removeConnection(p, c); // only q is left above c: it moves up a layer
            ASSERT_EQ(c->getLayer(), 1);
            EXPECT_TRUE(g.siftedLayer(c->getLayer()));
        }

        TEST(RelocationSifting, RemovingAConnectionSiftsAChildWithChildrenThatMovesUp) {
            RecordingGraph g("inner");
            auto q = box(g, "q", 0);
            auto r = box(g, "r", 0);
            auto p = box(g, "p", 1, r);
            auto c = box(g, "c", 2, p);
            box(g, "d", 3, c);
            g.addConnection(q, c);

            g.sifted.clear();
            g.removeConnection(p, c);
            ASSERT_EQ(c->getLayer(), 1);
            EXPECT_TRUE(g.siftedLayer(c->getLayer()));
        }

        TEST(RelocationSifting, DissolvingAnEdgeSiftsTheTargetsThatBecomeRoots) {
            // s above t (its only parent), t above u; a second little tree beside them.
            RecordingGraph g("roots");
            auto s = box(g, "s", 0);
            auto t = box(g, "t", 1, s);
            box(g, "u", 2, t);
            auto a = box(g, "a", 0);
            box(g, "b", 1, a);

            g.sifted.clear();
            g.removeSourcesFromHyperedge(edgeBetween(g, s, t), { s.get() }, true);
            ASSERT_EQ(t->getLayer(), 0) << "t has no parent left";
            EXPECT_TRUE(g.siftedLayer(0)) << "used to sift nothing at all: no parent to start from";
        }

        TEST(RelocationSifting, RemovingATargetSiftsWhereItWentNotWhereTheOthersAre) {
            // One edge from s to t and w; t also has children, w stays.
            RecordingGraph g("target");
            auto s = box(g, "s", 0);
            auto w = box(g, "w", 1, s);
            g.createTarget(NodeAttributes("t"), -1, edgeBetween(g, s, w));
            NodePtr t;
            for (const auto& n : g.getAllNodes()) if (n->getName() == "t") t = n;
            box(g, "v", 2, t);
            box(g, "x", 0);

            g.sifted.clear();
            g.removeTargetsFromHyperedge(edgeBetween(g, s, w), { t.get() }, true);
            ASSERT_EQ(t->getLayer(), 0) << "t has no parent left";
            EXPECT_TRUE(g.siftedLayer(0));
        }

    } // namespace relocation_sifting_tests
} // namespace hypergraph_logic
