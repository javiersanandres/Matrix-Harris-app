#pragma once

#include "Highs.h"

// ============================================================================
// HighsOutcome
//
// Shared by the two HiGHS backends (the crossing-minimization ILP in
// minimizeCrossingsILP.cpp and the horizontal-order MIP in horizontalOrder.cpp)
// to decide whether a finished Highs::run() left a solution worth using.
//
// Highs::run() returns HighsStatus::kWarning, not kOk, whenever the solve
// stops early: when the time limit is reached (model status kTimeLimit) and
// when the user interrupts it through a callback (kInterrupt), among others.
// Such a stop still leaves the best solution found so far -- which, with the
// warm starts both backends give HiGHS, is never worse than the starting
// order. So only kError means "nothing usable"; otherwise the solution is
// usable if it is optimal or at least primal feasible.
// ============================================================================

namespace hypergraph_logic::highs_outcome {

    inline bool hasUsableSolution(HighsStatus run_status, HighsModelStatus model_status,
        HighsInt primal_solution_status)
    {
        if (run_status == HighsStatus::kError) return false;
        return model_status == HighsModelStatus::kOptimal
            || primal_solution_status == kSolutionStatusFeasible;
    }

    inline bool hasUsableSolution(Highs& highs, HighsStatus run_status) {
        return hasUsableSolution(run_status, highs.getModelStatus(),
            highs.getInfo().primal_solution_status);
    }

} // namespace hypergraph_logic::highs_outcome
