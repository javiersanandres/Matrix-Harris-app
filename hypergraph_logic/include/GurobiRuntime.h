#pragma once

#include <functional>
#include <string>
#include <vector>

namespace hypergraph_logic {

	// ============================================================================
	// Gurobi, found at run time
	//
	// Gurobi is not linked into this program: its library is looked for when
	// first needed, whatever version is installed (9.0 or later), and used
	// through its C API. So the program builds without the Gurobi SDK, starts
	// on machines without Gurobi, and keeps working when Gurobi is upgraded to
	// a new major version (gurobi130.dll, gurobi140.dll, ...).
	//
	// The library searched for is gurobi<version>.dll on Windows, and
	// libgurobi<version>.so (.dylib on macOS) elsewhere: first in
	// GUROBI_HOME's bin (lib) folder, which the Gurobi installer sets, and then
	// in the folders of the PATH (LD_LIBRARY_PATH / DYLD_LIBRARY_PATH). The
	// newest version found wins.
	// ============================================================================

	// What Gurobi this process can use. HiGHS is always there as the fallback,
	// so none of these is an error: they only say whether the faster solver is
	// in play.
	enum class GurobiStatus {
		NotInstalled, // no usable Gurobi library on this machine
		NoLicense,    // Gurobi is installed, but it found no valid license
		Ready,        // Gurobi is installed and licensed
	};

	// Checked once and cached for the life of the process (thread-safe): the
	// license check can mean a round trip to a license server. Installing
	// Gurobi or a license therefore takes effect on the next start.
	GurobiStatus gurobiStatus();

	// The version of the Gurobi library in use ("13.0.1"), or empty when none
	// was found.
	std::string gurobiVersion();

	// One constraint lower <= sum(value[k] * x[index[k]]) <= upper. The arrays
	// belong to the caller and must outlive the solve.
	struct GurobiRow {
		const int* index;
		const double* value;
		int size;
		double lower;
		double upper;
	};

	// A mixed-integer program to minimize, in the shape both MIP backends
	// build. Bounds at or beyond +-1e30 (HiGHS' infinity) mean "unbounded".
	struct GurobiMip {
		std::vector<double> lower, upper, cost;
		std::vector<char> binary;  // per column: binary (true) or continuous
		std::vector<double> start; // warm start, one value per column (or empty)
		std::vector<GurobiRow> rows;
		double time_limit = -1.0;  // seconds; negative: no limit
		// Polled during the search; returning true stops it, keeping the best
		// solution found so far.
		std::function<bool()> should_stop;
	};

	struct GurobiSolution {
		bool success = false;  // a feasible solution was found (optimal or not)
		std::vector<double> x; // one value per column
		double objective = 0.0;
	};

	// Solves `mip` with Gurobi. Fails (success == false) when Gurobi is not
	// Ready or anything goes wrong on its side; never throws.
	GurobiSolution solveMipWithGurobi(const GurobiMip& mip);

} // namespace hypergraph_logic
