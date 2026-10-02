#include "GurobiRuntime.h"
#include "GlobalSifting.h"
#include "GraphicalHypergraph.h"
#include "ILPBackendTestHook.h"
#include "ILPCancellationToken.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <vector>

// ============================================================================
// Gurobi, loaded at run time through its C API (GurobiRuntime.h).
//
// These need an installed, licensed Gurobi (any version) and are skipped
// elsewhere; HiGHS covers the same models in the rest of the suite.
// ============================================================================

namespace hypergraph_logic {
    namespace gurobi_runtime_tests {

#define REQUIRE_GUROBI()                                                        \
        if (gurobiStatus() != GurobiStatus::Ready)                              \
            GTEST_SKIP() << "Gurobi is not installed and licensed here"

        TEST(GurobiRuntime, VersionIsKnownWhenInstalled) {
            if (gurobiStatus() == GurobiStatus::NotInstalled) {
                EXPECT_TRUE(gurobiVersion().empty());
                return;
            }
            const std::string v = gurobiVersion();
            EXPECT_EQ(std::count(v.begin(), v.end(), '.'), 2) << v;
        }

        TEST(GurobiRuntime, NothingIsSolvedWithoutGurobi) {
            if (gurobiStatus() == GurobiStatus::Ready) return;
            GurobiMip mip;
            mip.lower = { 0 };
            mip.upper = { 1 };
            mip.cost = { -1 };
            mip.binary = { true };
            EXPECT_FALSE(solveMipWithGurobi(mip).success);
        }

        // min -x - 2y  s.t.  x + y <= 1,  x, y binary  ->  y = 1, objective -2.
        TEST(GurobiRuntime, SolvesASmallBinaryProgram) {
            REQUIRE_GUROBI();
            const std::vector<int> index{ 0, 1 };
            const std::vector<double> value{ 1.0, 1.0 };
            GurobiMip mip;
            mip.lower = { 0, 0 };
            mip.upper = { 1, 1 };
            mip.cost = { -1, -2 };
            mip.binary = { true, true };
            mip.start = { 1, 0 }; // a feasible, worse start
            mip.rows.push_back({ index.data(), value.data(), 2, -1e30, 1.0 });

            const GurobiSolution s = solveMipWithGurobi(mip);
            ASSERT_TRUE(s.success);
            ASSERT_EQ(s.x.size(), 2u);
            EXPECT_NEAR(s.x[0], 0.0, 1e-6);
            EXPECT_NEAR(s.x[1], 1.0, 1e-6);
            EXPECT_NEAR(s.objective, -2.0, 1e-6);
        }

        // Continuous columns and infinite bounds (HiGHS' 1e30) are passed on
        // as Gurobi's own infinity: min x  s.t.  x >= 2.5,  x free.
        TEST(GurobiRuntime, ContinuousColumnsAndInfiniteBounds) {
            REQUIRE_GUROBI();
            const std::vector<int> index{ 0 };
            const std::vector<double> value{ 1.0 };
            GurobiMip mip;
            mip.lower = { -1e30 };
            mip.upper = { 1e30 };
            mip.cost = { 1 };
            mip.binary = { false };
            mip.rows.push_back({ index.data(), value.data(), 1, 2.5, 1e30 });

            const GurobiSolution s = solveMipWithGurobi(mip);
            ASSERT_TRUE(s.success);
            EXPECT_NEAR(s.x[0], 2.5, 1e-6);
        }

        TEST(GurobiRuntime, InfeasibleProgramIsNotASolution) {
            REQUIRE_GUROBI();
            const std::vector<int> index{ 0 };
            const std::vector<double> value{ 1.0 };
            GurobiMip mip;
            mip.lower = { 0 };
            mip.upper = { 1 };
            mip.cost = { 1 };
            mip.binary = { true };
            mip.rows.push_back({ index.data(), value.data(), 1, 2.0, 3.0 }); // x >= 2: impossible

            EXPECT_FALSE(solveMipWithGurobi(mip).success);
        }

        // ── End to end, through the crossing ILP ──────────────────────────────

        class LayersGraph : public GraphicalHypergraph {
        public:
            explicit LayersGraph(const std::string& name) : GraphicalHypergraph(name) {}
            std::map<int, LayerData>& layers() { return layers_; }
        };

        // Two rows of eight boxes wired as a permutation, so there are
        // crossings for the ILP to work on.
        void buildPermutation(LayersGraph& g) {
            constexpr int N = 8;
            std::vector<NodePtr> top, bottom;
            for (int i = 0; i < N; ++i) top.push_back(g.createNode("t" + std::to_string(i), 0, i, nullptr));
            for (int i = 0; i < N; ++i) bottom.push_back(g.createNode("b" + std::to_string(i), 1, i, top[i]));
            for (int i = 0; i < N; ++i) {
                const int j = (i * 3 + 2) % N;
                if (j != i) g.addConnection(top[i], bottom[j]);
            }
        }

        TEST(GurobiRuntime, CrossingILPMatchesHighs) {
            REQUIRE_GUROBI();
            LayersGraph g("gurobi_vs_highs");
            buildPermutation(g);
            const int last = static_cast<int>(g.layers().rbegin()->first);

            sifting_internal::GlobalSifter with_highs(0, last, g.layers());
            sifting_internal::setILPBackendOverrideForTesting(sifting_internal::ILPBackendOverride::kForceHighs);
            const int highs = with_highs.runCrossingILP(60.0);

            sifting_internal::GlobalSifter with_gurobi(0, last, g.layers());
            sifting_internal::setILPBackendOverrideForTesting(sifting_internal::ILPBackendOverride::kForceGurobi);
            const int gurobi = with_gurobi.runCrossingILP(60.0);

            ASSERT_GE(highs, 0);
            ASSERT_GE(gurobi, 0);
            EXPECT_EQ(gurobi, highs) << "both prove the optimum of this small instance";
        }

        TEST(GurobiRuntime, CancelledCrossingILPStillReturnsItsSolution) {
            REQUIRE_GUROBI();
            LayersGraph g("paused_gurobi");
            buildPermutation(g);
            sifting_internal::GlobalSifter sifter(0, static_cast<int>(g.layers().rbegin()->first), g.layers());

            // "Pausar" pressed before the search starts: Gurobi stops at its
            // first callback, keeping the warm start as its incumbent.
            ILPCancellationToken token;
            beginInterruptibleILP(token);
            token.cancel();
            sifting_internal::setILPBackendOverrideForTesting(sifting_internal::ILPBackendOverride::kForceGurobi);

            EXPECT_GE(sifter.runCrossingILP(60.0), 0) << "a stopped Gurobi run must still hand back its best solution";
        }

#undef REQUIRE_GUROBI

    } // namespace gurobi_runtime_tests
} // namespace hypergraph_logic
