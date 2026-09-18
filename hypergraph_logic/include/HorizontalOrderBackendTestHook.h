#pragma once

// ============================================================================
// Test-only hook for comparing the horizontal-order MIP's two backends
// head-to-head, mirroring ILPBackendTestHook.h for the crossing-minimization
// ILP.
//
// HorizontalOrderSolver::solve()'s public contract never changes: by default
// (kAuto) it still picks Gurobi when this build has it and a valid license is
// usable on this machine right now, and falls back to HiGHS otherwise -- see
// horizontalOrder.cpp. This header exists purely so a benchmark can pin ONE
// call to a specific backend, to measure HiGHS and Gurobi separately on the
// exact same instance instead of always getting whichever one auto-detect
// would have picked.
//
// Deliberately NOT part of HorizontalOrder.h / HorizontalOrderSolver's public
// API -- production code should never call
// setHorizontalOrderBackendOverrideForTesting(). Included only by
// horizontalOrder.cpp (which defines these) and by test/benchmark code (e.g.
// horizontalOrderResults.cpp) that wants to pin the backend for one call.
// ============================================================================

namespace horizontal_overlapping_internal {

	enum class ILPBackendOverride {
		kAuto,        // default: Gurobi if usable, else HiGHS -- unchanged production behavior
		kForceHighs,  // always use HiGHS for the next solve() call, even if Gurobi is usable
		kForceGurobi, // always use Gurobi for the next solve() call; that call fails (throws)
		// if Gurobi wasn't compiled in or isn't licensed on this machine (it does
		// NOT silently fall back to HiGHS -- that would defeat the point of forcing it)
	};

	// Not thread-safe by design: this is a single-threaded benchmarking hook, not
	// a runtime feature toggle. Set it immediately before the one solve() call it
	// should apply to; it resets itself back to kAuto as soon as that call
	// returns (or throws), so it can never leak into a later, unrelated call.
	void setHorizontalOrderBackendOverrideForTesting(ILPBackendOverride mode);

	// Lets test/benchmark code check, up front, whether a forced-Gurobi call is
	// even worth attempting on this machine (compiled in AND currently
	// licensed), so it can report "N/A" instead of a thrown exception for a
	// Gurobi column when Gurobi just isn't available here.
	bool isGurobiUsableForHorizontalOrderTesting();

} // namespace horizontal_overlapping_internal