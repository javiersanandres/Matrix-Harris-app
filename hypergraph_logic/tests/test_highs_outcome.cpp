#include "HighsOutcome.h"
#include "GlobalSifting.h"
#include "GraphicalHypergraph.h"
#include "ILPBackendTestHook.h"
#include "ILPCancellationToken.h"
#include <gtest/gtest.h>

#include <string>
#include <vector>

// ============================================================================
// HiGHS stopping early must not throw away its solution.
//
// Highs::run() returns kWarning (not kOk) when the time limit is reached or the
// user interrupts the search. Both HiGHS backends used to treat any non-kOk
// return as a failure, so a timed-out horizontal-order MIP threw ("neither the
// requested nor the fallback solver found a feasible solution") and a paused
// or timed-out crossing ILP silently fell back to the heuristic.
// ============================================================================

namespace hypergraph_logic {
    namespace highs_outcome_tests {

        using highs_outcome::hasUsableSolution;

        // ── The decision itself ───────────────────────────────────────────────

        TEST(HighsOutcome, OptimalRunIsUsable) {
            EXPECT_TRUE(hasUsableSolution(HighsStatus::kOk, HighsModelStatus::kOptimal, kSolutionStatusFeasible));
        }

        TEST(HighsOutcome, TimeLimitWithIncumbentIsUsable) {
            EXPECT_TRUE(hasUsableSolution(HighsStatus::kWarning, HighsModelStatus::kTimeLimit, kSolutionStatusFeasible));
        }

        TEST(HighsOutcome, UserInterruptWithIncumbentIsUsable) {
            EXPECT_TRUE(hasUsableSolution(HighsStatus::kWarning, HighsModelStatus::kInterrupt, kSolutionStatusFeasible));
        }

        TEST(HighsOutcome, StopWithoutIncumbentIsNotUsable) {
            EXPECT_FALSE(hasUsableSolution(HighsStatus::kWarning, HighsModelStatus::kTimeLimit, kSolutionStatusNone));
            EXPECT_FALSE(hasUsableSolution(HighsStatus::kWarning, HighsModelStatus::kInterrupt, kSolutionStatusInfeasible));
        }

        TEST(HighsOutcome, ErrorIsNeverUsable) {
            EXPECT_FALSE(hasUsableSolution(HighsStatus::kError, HighsModelStatus::kOptimal, kSolutionStatusFeasible));
        }

        TEST(HighsOutcome, InfeasibleIsNotUsable) {
            EXPECT_FALSE(hasUsableSolution(HighsStatus::kOk, HighsModelStatus::kInfeasible, kSolutionStatusNone));
        }

        // ── End to end: a paused crossing ILP keeps its solution ──────────────

        class LayersGraph : public GraphicalHypergraph {
        public:
            explicit LayersGraph(const std::string& name) : GraphicalHypergraph(name) {}
            std::map<int, LayerData>& layers() { return layers_; }
        };

        TEST(HighsOutcome, CancelledCrossingILPStillReturnsItsSolution) {
            // Two rows of eight boxes wired as a permutation, so there are
            // crossings for the ILP to work on.
            LayersGraph g("paused_ilp");
            constexpr int N = 8;
            std::vector<NodePtr> top, bottom;
            for (int i = 0; i < N; ++i) top.push_back(g.createNode("t" + std::to_string(i), 0, i, nullptr));
            for (int i = 0; i < N; ++i) bottom.push_back(g.createNode("b" + std::to_string(i), 1, i, top[i]));
            for (int i = 0; i < N; ++i) {
                const int j = (i * 3 + 2) % N;
                if (j != i) g.addConnection(top[i], bottom[j]);
            }

            sifting_internal::GlobalSifter sifter(0, static_cast<int>(g.layers().rbegin()->first), g.layers());
            ASSERT_GT(sifter.countCrossings(), 0);

            // "Pausar" pressed before the search even starts: HiGHS stops at
            // its first interrupt check with the warm start as its incumbent.
            ILPCancellationToken token;
            beginInterruptibleILP(token);
            token.cancel();
            sifting_internal::setILPBackendOverrideForTesting(sifting_internal::ILPBackendOverride::kForceHighs);

            const int crossings = sifter.runCrossingILP(60.0);
            EXPECT_GE(crossings, 0) << "a stopped HiGHS run must still hand back its best solution";
        }

    } // namespace highs_outcome_tests
} // namespace hypergraph_logic
