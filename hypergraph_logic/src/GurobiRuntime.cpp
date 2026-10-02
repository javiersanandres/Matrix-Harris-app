#include "GurobiRuntime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define GRB_CALL __stdcall
#else
#include <cstdlib>
#include <dlfcn.h>
#define GRB_CALL
#endif

namespace hypergraph_logic {

	namespace {

		namespace fs = std::filesystem;

		// Opaque Gurobi handles.
		struct GrbEnv;
		struct GrbModel;

		using GrbCallback = int(GRB_CALL*)(GrbModel* model, void* cbdata, int where, void* usrdata);

		// The Gurobi C functions used here, with the signatures of gurobi_c.h
		// (unchanged since Gurobi 9.0).
		struct Api {
			void(GRB_CALL* version)(int* major, int* minor, int* technical);
			int(GRB_CALL* emptyenvinternal)(GrbEnv** envP, int major, int minor, int technical);
			int(GRB_CALL* startenv)(GrbEnv* env);
			void(GRB_CALL* freeenv)(GrbEnv* env);
			int(GRB_CALL* setintparam)(GrbEnv* env, const char* name, int value);
			int(GRB_CALL* setdblparam)(GrbEnv* env, const char* name, double value);
			int(GRB_CALL* newmodel)(GrbEnv* env, GrbModel** modelP, const char* name, int numvars,
				double* obj, double* lb, double* ub, char* vtype, char** varnames);
			GrbEnv*(GRB_CALL* getenv)(GrbModel* model);
			int(GRB_CALL* addrangeconstr)(GrbModel* model, int numnz, int* cind, double* cval,
				double lower, double upper, const char* name);
			int(GRB_CALL* setintattr)(GrbModel* model, const char* name, int value);
			int(GRB_CALL* getintattr)(GrbModel* model, const char* name, int* valueP);
			int(GRB_CALL* getdblattr)(GrbModel* model, const char* name, double* valueP);
			int(GRB_CALL* setdblattrarray)(GrbModel* model, const char* name, int first, int len, double* values);
			int(GRB_CALL* getdblattrarray)(GrbModel* model, const char* name, int first, int len, double* values);
			int(GRB_CALL* setcallbackfunc)(GrbModel* model, GrbCallback cb, void* usrdata);
			int(GRB_CALL* optimize)(GrbModel* model);
			void(GRB_CALL* terminate)(GrbModel* model);
			int(GRB_CALL* freemodel)(GrbModel* model);
		};

		// Gurobi's own constants (gurobi_c.h).
		constexpr double GRB_INFINITY = 1e100;
		constexpr int GRB_MINIMIZE = 1;
		constexpr int GRB_OPTIMAL = 2;
		constexpr int GRB_SUBOPTIMAL = 13;
		constexpr double INPUT_INFINITY = 1e30; // HiGHS' infinity, used by the models handed in

		// ── Finding the library ────────────────────────────────────────────────

#ifdef _WIN32
		constexpr const char* LIB_PREFIX = "gurobi";
		constexpr const char* LIB_SUFFIX = ".dll";
		constexpr const char* HOME_SUBDIR = "bin";
		constexpr wchar_t PATH_SEPARATOR = L';';
#else
		constexpr const char* LIB_PREFIX = "libgurobi";
#ifdef __APPLE__
		constexpr const char* LIB_SUFFIX = ".dylib";
#else
		constexpr const char* LIB_SUFFIX = ".so";
#endif
		constexpr const char* HOME_SUBDIR = "lib";
		constexpr char PATH_SEPARATOR = ':';
#endif

		// The version encoded in a Gurobi library's name (gurobi130.dll -> 130),
		// or -1 for any other file (Gurobi130.NET.dll, gurobi_c++.dll, ...).
		int libraryVersion(const fs::path& file) {
			std::string name = file.filename().string();
			std::transform(name.begin(), name.end(), name.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			const std::string prefix = LIB_PREFIX, suffix = LIB_SUFFIX;
			if (name.size() <= prefix.size() + suffix.size()
				|| name.compare(0, prefix.size(), prefix) != 0
				|| name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
				return -1;
			const std::string digits = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
			if (digits.size() > 5 || !std::all_of(digits.begin(), digits.end(),
				[](unsigned char c) { return std::isdigit(c) != 0; }))
				return -1;
			return std::stoi(digits);
		}

#ifdef _WIN32
		std::wstring environmentVariable(const wchar_t* name) {
			const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
			if (size == 0) return {};
			std::wstring value(size, L'\0');
			value.resize(GetEnvironmentVariableW(name, value.data(), size));
			return value;
		}
#else
		std::string environmentVariable(const char* name) {
			const char* value = std::getenv(name);
			return value ? value : "";
		}
#endif

		// Where to look: GUROBI_HOME's library folder (empty when not set) and
		// the folders of the search path.
		struct Folders {
			fs::path home;
			std::vector<fs::path> path;
		};

		Folders searchFolders() {
#ifdef _WIN32
			const auto home = environmentVariable(L"GUROBI_HOME");
			const auto path = environmentVariable(L"PATH");
#else
			const auto home = environmentVariable("GUROBI_HOME");
#ifdef __APPLE__
			const auto path = environmentVariable("DYLD_LIBRARY_PATH");
#else
			const auto path = environmentVariable("LD_LIBRARY_PATH");
#endif
#endif
			Folders folders;
			if (!home.empty()) folders.home = fs::path(home) / HOME_SUBDIR;
			size_t from = 0;
			while (from <= path.size()) {
				size_t to = path.find(PATH_SEPARATOR, from);
				if (to == path.npos) to = path.size();
				if (to > from) folders.path.emplace_back(path.substr(from, to - from));
				from = to + 1;
			}
			return folders;
		}

		// Every Gurobi library found, best first: those in GUROBI_HOME before
		// the rest (it names the installation in use), newest versions first.
		std::vector<fs::path> candidateLibraries() {
			struct Found { bool in_home; int version; fs::path file; };
			std::vector<Found> found;
			auto scan = [&](const fs::path& folder, bool in_home) {
				std::error_code ec;
				for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
					const int version = libraryVersion(it->path());
					if (version >= 0) found.push_back({ in_home, version, it->path() });
				}
			};
			const Folders folders = searchFolders();
			if (!folders.home.empty()) scan(folders.home, true);
			for (const fs::path& folder : folders.path) scan(folder, false);

			std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
				if (a.in_home != b.in_home) return a.in_home;
				return a.version > b.version;
			});
			std::vector<fs::path> files;
			for (const Found& f : found) files.push_back(f.file);
			return files;
		}

		// ── Loading it ─────────────────────────────────────────────────────────

		struct Loaded {
			Api api;
			int major, minor, technical;
		};

		// Opens `file` and resolves every function of Api; nothing if any is
		// missing (a Gurobi older than 9.0, or not Gurobi at all).
		std::optional<Loaded> load(const fs::path& file) {
#ifdef _WIN32
			// Its own folder first, so the library's dependencies next to it are found.
			HMODULE handle = LoadLibraryExW(file.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
			if (!handle) return std::nullopt;
			auto symbol = [&](const char* name) { return reinterpret_cast<void*>(GetProcAddress(handle, name)); };
			auto close = [&] { FreeLibrary(handle); };
#else
			void* handle = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
			if (!handle) return std::nullopt;
			auto symbol = [&](const char* name) { return dlsym(handle, name); };
			auto close = [&] { dlclose(handle); };
#endif
			Loaded loaded{};
			bool complete = true;
			auto resolve = [&](auto& fn, const char* name) {
				void* address = symbol(name);
				if (!address) complete = false;
				fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(address);
			};
			Api& a = loaded.api;
			resolve(a.version, "GRBversion");
			resolve(a.emptyenvinternal, "GRBemptyenvinternal");
			resolve(a.startenv, "GRBstartenv");
			resolve(a.freeenv, "GRBfreeenv");
			resolve(a.setintparam, "GRBsetintparam");
			resolve(a.setdblparam, "GRBsetdblparam");
			resolve(a.newmodel, "GRBnewmodel");
			resolve(a.getenv, "GRBgetenv");
			resolve(a.addrangeconstr, "GRBaddrangeconstr");
			resolve(a.setintattr, "GRBsetintattr");
			resolve(a.getintattr, "GRBgetintattr");
			resolve(a.getdblattr, "GRBgetdblattr");
			resolve(a.setdblattrarray, "GRBsetdblattrarray");
			resolve(a.getdblattrarray, "GRBgetdblattrarray");
			resolve(a.setcallbackfunc, "GRBsetcallbackfunc");
			resolve(a.optimize, "GRBoptimize");
			resolve(a.terminate, "GRBterminate");
			resolve(a.freemodel, "GRBfreemodel");
			if (!complete) {
				close();
				return std::nullopt;
			}
			// Kept loaded for the rest of the process.
			a.version(&loaded.major, &loaded.minor, &loaded.technical);
			return loaded;
		}

		// The Gurobi in use, loaded on first need (thread-safe); null if none.
		const Loaded* gurobi() {
			static const std::optional<Loaded> loaded = [] {
				for (const fs::path& file : candidateLibraries())
					if (auto l = load(file)) return l;
				return std::optional<Loaded>();
				}();
			return loaded ? &*loaded : nullptr;
		}

		// A started (licensed) environment that prints nothing, or null. The
		// library is told its own version, which is what it checks.
		GrbEnv* startEnvironment(const Loaded& g) {
			GrbEnv* env = nullptr;
			if (g.api.emptyenvinternal(&env, g.major, g.minor, g.technical) != 0) {
				if (env) g.api.freeenv(env);
				return nullptr;
			}
			g.api.setintparam(env, "OutputFlag", 0);
			if (g.api.startenv(env) != 0) {
				g.api.freeenv(env);
				return nullptr;
			}
			return env;
		}

		// Handed to the callback: what to ask, and whether it said stop.
		struct StopRequest {
			const std::function<bool()>* should_stop;
			bool stopped = false;
		};

		int GRB_CALL stopWhenAsked(GrbModel* model, void*, int, void* usrdata) {
			auto& request = *static_cast<StopRequest*>(usrdata);
			if (!request.stopped && (*request.should_stop)()) {
				request.stopped = true;
				gurobi()->api.terminate(model);
			}
			return 0;
		}

		// Whether `x` meets every bound, integrality and row of `mip`.
		bool isFeasible(const GurobiMip& mip, const std::vector<double>& x) {
			constexpr double TOL = 1e-6;
			for (size_t i = 0; i < x.size(); ++i) {
				if (x[i] < mip.lower[i] - TOL || x[i] > mip.upper[i] + TOL) return false;
				if (mip.binary[i] && std::abs(x[i] - std::round(x[i])) > TOL) return false;
			}
			for (const GurobiRow& row : mip.rows) {
				double sum = 0.0;
				for (int k = 0; k < row.size; ++k) sum += row.value[k] * x[row.index[k]];
				if (sum < row.lower - TOL || sum > row.upper + TOL) return false;
			}
			return true;
		}

		double clampInfinity(double v) {
			if (v <= -INPUT_INFINITY) return -GRB_INFINITY;
			if (v >= INPUT_INFINITY) return GRB_INFINITY;
			return v;
		}

	} // namespace

	GurobiStatus gurobiStatus() {
		static const GurobiStatus status = [] {
			const Loaded* g = gurobi();
			if (!g) return GurobiStatus::NotInstalled;
			GrbEnv* env = startEnvironment(*g);
			if (!env) return GurobiStatus::NoLicense;
			g->api.freeenv(env);
			return GurobiStatus::Ready;
			}();
		return status;
	}

	std::string gurobiVersion() {
		const Loaded* g = gurobi();
		if (!g) return {};
		return std::to_string(g->major) + "." + std::to_string(g->minor) + "." + std::to_string(g->technical);
	}

	GurobiSolution solveMipWithGurobi(const GurobiMip& mip) {
		GurobiSolution result;
		if (gurobiStatus() != GurobiStatus::Ready) return result;
		const Loaded& g = *gurobi();
		const Api& api = g.api;

		GrbEnv* env = startEnvironment(g);
		if (!env) return result;
		GrbModel* model = nullptr;

		const int n = static_cast<int>(mip.lower.size());
		std::vector<double> lower(n), upper(n), cost(mip.cost);
		std::vector<char> type(n);
		for (int i = 0; i < n; ++i) {
			lower[i] = clampInfinity(mip.lower[i]);
			upper[i] = clampInfinity(mip.upper[i]);
			type[i] = mip.binary[i] ? 'B' : 'C';
		}
		std::vector<double> start(mip.start);

		bool ok = api.newmodel(env, &model, "mip", n, cost.data(), lower.data(), upper.data(), type.data(), nullptr) == 0;
		if (ok) {
			// The model works on its own copy of the environment.
			GrbEnv* model_env = api.getenv(model);
			api.setintparam(model_env, "OutputFlag", 0);
			if (mip.time_limit >= 0.0) ok = api.setdblparam(model_env, "TimeLimit", mip.time_limit) == 0;
		}
		for (size_t r = 0; ok && r < mip.rows.size(); ++r) {
			const GurobiRow& row = mip.rows[r];
			ok = api.addrangeconstr(model, row.size, const_cast<int*>(row.index), const_cast<double*>(row.value),
				clampInfinity(row.lower), clampInfinity(row.upper), nullptr) == 0;
		}
		if (ok) ok = api.setintattr(model, "ModelSense", GRB_MINIMIZE) == 0;
		if (ok && static_cast<int>(start.size()) == n && n > 0)
			ok = api.setdblattrarray(model, "Start", 0, n, start.data()) == 0;
		StopRequest stop{ &mip.should_stop };
		if (ok && mip.should_stop) ok = api.setcallbackfunc(model, &stopWhenAsked, &stop) == 0;
		if (ok) ok = api.optimize(model) == 0;

		if (ok) {
			int status = 0, solutions = 0;
			api.getintattr(model, "Status", &status);
			api.getintattr(model, "SolCount", &solutions);
			if (status == GRB_OPTIMAL || status == GRB_SUBOPTIMAL || solutions > 0) {
				result.x.resize(n);
				if (n == 0 || api.getdblattrarray(model, "X", 0, n, result.x.data()) == 0) {
					api.getdblattr(model, "ObjVal", &result.objective);
					result.success = true;
				}
				else {
					result.x.clear();
				}
			}
		}

		// Stopped on request before Gurobi even took in the warm start (it can
		// be told to stop before its first look at it): the warm start is then
		// still the best order known, as HiGHS would hand back.
		if (ok && !result.success && stop.stopped && static_cast<int>(mip.start.size()) == n && isFeasible(mip, mip.start)) {
			result.x = mip.start;
			result.objective = 0.0;
			for (int i = 0; i < n; ++i) result.objective += mip.cost[i] * mip.start[i];
			result.success = true;
		}

		if (model) api.freemodel(model);
		api.freeenv(env);
		return result;
	}

} // namespace hypergraph_logic
