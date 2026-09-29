#include "GraphicalHypergraph.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

// ============================================================================
// Operation feasibility queries
//
// Every can* query must answer exactly whether the operation of the same name
// succeeds. Each test builds a graph and, for every candidate (pair of boxes, or
// hyperedge and box), runs the real operation on a fresh clone and compares.
// ============================================================================

namespace hypergraph_logic {
    namespace operation_feasibility_tests {

        using Graph = GraphicalHypergraph;

        std::map<std::string, NodePtr> nodesByName(const Graph& g) {
            std::map<std::string, NodePtr> out;
            for (const auto& n : g.getAllNodes())
                if (!n->isDummy()) out[n->getName()] = n;
            return out;
        }

        std::string edgeKey(const HyperedgePtr& e) {
            std::vector<std::string> s, t;
            for (const auto& n : e->getSources()) s.push_back(n->getName());
            for (const auto& n : e->getTargets()) t.push_back(n->getName());
            std::sort(s.begin(), s.end());
            std::sort(t.begin(), t.end());
            std::string key;
            for (const auto& x : s) key += x + ",";
            key += "->";
            for (const auto& x : t) key += x + ",";
            return key;
        }

        std::map<std::string, HyperedgePtr> edgesByKey(const Graph& g) {
            std::map<std::string, HyperedgePtr> out;
            for (const auto& e : g.getAllHyperedges())
                if (!e->isSegment()) out[edgeKey(e)] = e;
            return out;
        }

        bool succeeds(const std::function<void()>& op) {
            try { op(); return true; }
            catch (const std::exception&) { return false; }
        }

        struct Tally { int yes = 0, no = 0; };

        // Compares every query against the real operation on a clone of g.
        void expectQueriesMatchOperations(const Graph& g) {
            const auto nodes = nodesByName(g);
            Tally connection, removal, fusion, source, target;

            for (const auto& [a_name, a] : nodes) {
                for (const auto& [b_name, b] : nodes) {
                    const std::string ctx = a_name + " / " + b_name;

                    {
                        Graph c = g.clone();
                        auto cn = nodesByName(c);
                        const bool ok = succeeds([&] { c.addConnection(cn[a_name], cn[b_name]); });
                        EXPECT_EQ(g.canAddConnection(a, b), ok) << "addConnection " << ctx;
                        (ok ? connection.yes : connection.no)++;
                    }
                    {
                        Graph c = g.clone();
                        auto cn = nodesByName(c);
                        const bool ok = a != b && succeeds([&] { c.removeConnection(cn[a_name], cn[b_name]); });
                        EXPECT_EQ(g.canRemoveConnection(a, b), ok) << "removeConnection " << ctx;
                        (ok ? removal.yes : removal.no)++;
                    }
                    {
                        Graph c = g.clone();
                        auto cn = nodesByName(c);
                        const bool ok = succeeds([&] { c.fuseNodes(cn[a_name], cn[b_name], NodeAttributes("fused")); });
                        EXPECT_EQ(g.canFuseNodes(a, b), ok) << "fuseNodes " << ctx;
                        (ok ? fusion.yes : fusion.no)++;
                    }
                }
            }

            for (const auto& [key, e] : edgesByKey(g)) {
                for (const auto& [n_name, n] : nodes) {
                    const std::string ctx = key + " / " + n_name;
                    {
                        Graph c = g.clone();
                        auto cn = nodesByName(c);
                        auto ce = edgesByKey(c).at(key);
                        const bool ok = succeeds([&] { c.addSourceToEdge(ce, cn[n_name]); });
                        EXPECT_EQ(g.canAddSourceToEdge(e, n), ok) << "addSourceToEdge " << ctx;
                        (ok ? source.yes : source.no)++;
                    }
                    {
                        Graph c = g.clone();
                        auto cn = nodesByName(c);
                        auto ce = edgesByKey(c).at(key);
                        const bool ok = succeeds([&] { c.addTargetToEdge(ce, cn[n_name]); });
                        EXPECT_EQ(g.canAddTargetToEdge(e, n), ok) << "addTargetToEdge " << ctx;
                        (ok ? target.yes : target.no)++;
                    }
                }
            }

            // The graphs are chosen so that every query is exercised both ways.
            for (const Tally* t : { &connection, &removal, &fusion, &source, &target }) {
                EXPECT_GT(t->yes, 0);
                EXPECT_GT(t->no, 0);
            }
        }

        HyperedgePtr edgeBetween(const Graph& g, const NodePtr& s, const NodePtr& t) {
            for (const auto& e : g.getAllHyperedges())
                if (!e->isSegment() && e->containsSource(s) && e->containsTarget(t)) return e;
            return nullptr;
        }

        // a -> {b, c}, {b, c} -> d, e -> f -> g, a -> g (long), h alone.
        TEST(OperationFeasibility, HyperedgesLongEdgeAndIsolatedBox) {
            Graph g("feasibility_1");
            auto a = g.createNode("a", 0, -1, nullptr);
            auto b = g.createNode("b", 1, -1, a);
            auto c = g.createNode("c", 0, -1, nullptr);
            g.addTargetToEdge(edgeBetween(g, a, b), c);
            auto d = g.createNode("d", 2, -1, b);
            g.addSourceToEdge(edgeBetween(g, b, d), c);
            auto e = g.createNode("e", 0, -1, nullptr);
            auto f = g.createNode("f", 1, -1, e);
            auto gg = g.createNode("g", 2, -1, f);
            g.addConnection(a, gg);
            g.createNode("h", 0, -1, nullptr);

            expectQueriesMatchOperations(g);
        }

        // Diamond x -> {y, w} -> z, plus a chain z -> u and a box v below x only.
        TEST(OperationFeasibility, DiamondAndChain) {
            Graph g("feasibility_2");
            auto x = g.createNode("x", 0, -1, nullptr);
            auto y = g.createNode("y", 1, -1, x);
            auto w = g.createNode("w", 1, -1, x);
            auto z = g.createNode("z", 2, -1, y);
            g.addConnection(w, z);
            g.createNode("u", 3, -1, z);
            g.createNode("v", 1, -1, x);

            expectQueriesMatchOperations(g);
        }

    } // namespace operation_feasibility_tests
} // namespace hypergraph_logic
