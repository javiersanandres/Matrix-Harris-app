#pragma once

#include <atomic>

// ============================================================================
// Public control header for the two ways Hypergraph::minimizeCrossingsILP()
// can be run:
//
//   * "Fast": the default. The exact-solve attempt is capped at a fixed time
//     budget and always falls back to the heuristic if that runs out.
//
//   * "Slow": the exact-solve attempt runs with NO time limit, but can be
//     stopped from outside at any point via an ILPCancellationToken.
//
// ============================================================================

namespace hypergraph_logic {

	// The "fast" button's time budget, in seconds. Single source of truth,
	// referenced by minimizeCrossingsILP.cpp (the actual solver cap), by
	// MinimizingProgressDialog (the UI's countdown display), and by the "?"
	// help tooltip's wording.
	constexpr double kILPTimeBudgetSeconds = 5.0;

	class ILPCancellationToken {
	public:
		void cancel() { cancelled_.store(true, std::memory_order_relaxed); }

		// Clears a previous cancellation so the SAME token object can be
		// reused for a later solve.
		void reset() { cancelled_.store(false, std::memory_order_relaxed); }

		bool isCancelled() const { return cancelled_.load(std::memory_order_relaxed); }

	private:
		std::atomic<bool> cancelled_{ false };
	};

	// Arms interruptible ("slow") mode for the NEXT minimizeCrossingsILP() call.
	void beginInterruptibleILP(ILPCancellationToken& token);

} // namespace hypergraph_logic