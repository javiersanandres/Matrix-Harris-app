// ============================================================================
// minimizeCrossingsResults.cpp
//
// Efficiency measurements for the Global Sifting algorithm.
//
// For each .asp instance:
//
//   (a) Natural — 50 distinct random permutations of the block list.
//       Each permutation is sifted for 10 rounds. Averaged over all 50 runs:
//       avg_before, avg_after, avg_ratio%, avg_time_ms.
//       ratio% = 100 * after / before
//
//   (b) Propagation — one run starting from orderBlocksByLayerPropagation.
//       The reported time includes both the ordering and the sifting phases.
//       Reports: crossings_before, crossings_after, ratio%, time_ms.
//
// Outputs (written next to this source file):
//   results_natural.csv
//   results_propagation.csv
//   (semicolon-separated, comma as decimal separator — Spanish Excel format)
//
// Prints to stdout: progress + summary averages only.
//
// .asp format:
//   in_layer(<layer>, <node_name>)  ->  G1 node at g1_layer = layer
//   edge(<src>, <tgt>)              ->  direct g1_out[src] -> tgt  (no hubs)
//
// Usage:
//   ./minimizeCrossingsResults [path/to/AllInstances]
// ============================================================================

#include "GlobalSifting.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using namespace sifting_internal;

static constexpr int SIFTING_ROUNDS = 10;
static constexpr int RANDOM_RUNS = 50;
static constexpr int RNG_SEED = 42;

// Directory of this source file — CSVs are written here.
static const fs::path SOURCE_DIR = fs::path(__FILE__).parent_path();

// ============================================================================
// Formatting helper: double with comma as decimal separator
// ============================================================================

static std::string fmtDouble(double v, int precision = 4) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << v;
    std::string s = ss.str();
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

// ============================================================================
// Parser
// ============================================================================

struct ParsedInstance {
    std::unordered_map<std::string, int>             node_layer;
    std::vector<std::pair<std::string, std::string>> edges;
    bool        ok = false;
    std::string error;
};

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string innerParens(const std::string& line) {
    size_t a = line.find('('), b = line.rfind(')');
    if (a == std::string::npos || b == std::string::npos || b <= a) return "";
    return line.substr(a + 1, b - a - 1);
}

static std::vector<std::string> splitComma(const std::string& s) {
    std::vector<std::string> parts;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) parts.push_back(trim(tok));
    return parts;
}

static ParsedInstance parseAsp(const fs::path& path) {
    ParsedInstance inst;
    std::ifstream f(path);
    if (!f) { inst.error = "cannot open file"; return inst; }
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '%') continue;
        if (line.rfind("in_layer(", 0) == 0) {
            auto p = splitComma(innerParens(line));
            if (p.size() == 2) inst.node_layer[p[1]] = std::stoi(p[0]);
        }
        else if (line.rfind("edge(", 0) == 0) {
            auto p = splitComma(innerParens(line));
            if (p.size() == 2) inst.edges.push_back({ p[0], p[1] });
        }
    }
    inst.ok = true;
    return inst;
}

// ============================================================================
// SiftState builder
// ============================================================================

static bool buildState(const ParsedInstance& inst, SiftState& S) {
    std::vector<std::pair<int, std::string>> ordered;
    ordered.reserve(inst.node_layer.size());
    for (const auto& [name, layer] : inst.node_layer)
        ordered.push_back({ layer, name });
    std::sort(ordered.begin(), ordered.end());

    std::unordered_map<std::string, int> name_to_g1;
    name_to_g1.reserve(ordered.size());
    for (const auto& [layer, name] : ordered) {
        int idx = static_cast<int>(S.g1_nodes.size());
        S.g1_nodes.emplace_back(nullptr, layer);
        S.g1_layers[layer].push_back(idx);
        name_to_g1[name] = idx;
    }

    int n = static_cast<int>(S.g1_nodes.size());
    S.g1_in.assign(n, {});
    S.g1_out.assign(n, {});

    for (const auto& [src_name, tgt_name] : inst.edges) {
        auto sit = name_to_g1.find(src_name);
        auto tit = name_to_g1.find(tgt_name);
        if (sit == name_to_g1.end() || tit == name_to_g1.end()) {
            S = SiftState{};
            return false;
        }
        S.g1_out[sit->second].push_back(tit->second);
        S.g1_in[tit->second].push_back(sit->second);
    }

    for (int i = 0; i < n; ++i) {
        S.g1_nodes[i].block_id = i;
        S.blocks.emplace_back(std::vector<int>{i});
    }
    S.pi.resize(n, 0);
    S.fixed_position_count = 0;
    return true;
}

// ============================================================================
// orderBlocksByLayerPropagation
// ============================================================================

static BlockList orderBlocksByLayerPropagation(SiftState& S) {
    S.pi.assign(S.blocks.size(), INT_MAX);
    {
        const auto& first = S.g1_layers.begin()->second;
        for (int pos = 0; pos < static_cast<int>(first.size()); ++pos)
            S.pi[S.g1_nodes[first[pos]].block_id] = pos;
    }

    std::unordered_set<int> visited;
    for (int idx : S.g1_layers.begin()->second)
        visited.insert(S.g1_nodes[idx].block_id);

    bool first = true;
    for (auto& [key, nodes] : S.g1_layers) {
        if (first) { first = false; continue; }

        std::vector<int> new_nodes;
        for (int idx : nodes)
            if (!visited.count(S.g1_nodes[idx].block_id))
                new_nodes.push_back(idx);

        std::unordered_map<int, int> left_of;
        for (int idx : new_nodes) left_of[idx] = INT_MAX;
        for (int idx : new_nodes)
            for (int par : S.g1_in[idx])
                left_of[idx] = std::min(left_of[idx],
                    S.pi[S.g1_nodes[par].block_id]);

        std::stable_sort(new_nodes.begin(), new_nodes.end(),
            [&](int a, int b) { return left_of.at(a) < left_of.at(b); });

        for (int pos = 0; pos < static_cast<int>(new_nodes.size()); ++pos) {
            int bid = S.g1_nodes[new_nodes[pos]].block_id;
            S.pi[bid] = pos;
            visited.insert(bid);
        }
    }

    BlockList B;
    B.reserve(S.blocks.size());
    std::unordered_set<int> seen;
    for (auto& [key, nodes] : S.g1_layers)
        for (int idx : nodes) {
            int bid = S.g1_nodes[idx].block_id;
            if (seen.insert(bid).second) B.push_back(bid);
        }

    for (int pos = 0; pos < static_cast<int>(B.size()); ++pos)
        S.pi[B[pos]] = pos;
    return B;
}

// ============================================================================
// Sifting runner — works on copies so S_base is never modified
// ============================================================================

struct SingleRunResult {
    int    crossings_before;
    int    crossings_after;
    double elapsed_ms;

    double crossingRatio() const {
        if (crossings_before == 0) return 0.0;
        return 100.0 * crossings_after / static_cast<double>(crossings_before);
    }
};

static SingleRunResult runSifting(SiftState S, BlockList B) {
    sortAdjacencies(S, B);
    SingleRunResult r;
    r.crossings_before = countTotalCrossings(S, B);

    auto t0 = std::chrono::high_resolution_clock::now();
    int numblocks = static_cast<int>(B.size());
    for (int round = 0; round < SIFTING_ROUNDS; ++round) {
        int chi = 0;
        BlockList snap = B;
        for (int i = S.fixed_position_count; i < numblocks; ++i)
            chi += siftingStep(S, B, snap[i]);
        if (chi >= 0) break;
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    r.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    r.crossings_after = countTotalCrossings(S, B);
    return r;
}

// ============================================================================
// Aggregated result for the natural (random) condition
// ============================================================================

struct NaturalAggResult {
    double avg_before;
    double avg_after;
    double avg_crossing_ratio;
    double avg_time_ms;
};

static NaturalAggResult runNatural(const SiftState& S_base, std::mt19937& rng) {
    int n = static_cast<int>(S_base.blocks.size());
    BlockList base_B(n);
    std::iota(base_B.begin(), base_B.end(), 0);

    std::vector<BlockList> perms;
    perms.reserve(RANDOM_RUNS);
    std::set<BlockList> seen_perms;

    const int max_attempts = RANDOM_RUNS * 20;
    int attempts = 0;
    while (static_cast<int>(perms.size()) < RANDOM_RUNS && attempts < max_attempts) {
        BlockList p = base_B;
        std::shuffle(p.begin(), p.end(), rng);
        if (seen_perms.insert(p).second)
            perms.push_back(p);
        ++attempts;
    }

    double sum_before = 0, sum_after = 0, sum_ratio = 0, sum_time = 0;
    for (const auto& perm : perms) {
        SingleRunResult r = runSifting(S_base, perm);
        sum_before += r.crossings_before;
        sum_after += r.crossings_after;
        sum_ratio += r.crossingRatio();
        sum_time += r.elapsed_ms;
    }

    int runs = static_cast<int>(perms.size());
    return { sum_before / runs, sum_after / runs, sum_ratio / runs, sum_time / runs };
}

// ============================================================================
// CSV writers
// Semicolon-separated, comma as decimal separator (Spanish Excel format).
// Files are written next to this source file.
// ============================================================================

static void writeNaturalCsv(
    const std::vector<std::string>& names,
    const std::vector<NaturalAggResult>& results)
{
    fs::path out = SOURCE_DIR / "results_natural.csv";
    std::ofstream f(out);
    f << "instance;avg_crossings_before;avg_crossings_after;avg_crossing_ratio_pct;avg_time_ms\n";
    for (size_t i = 0; i < names.size(); ++i)
        f << names[i] << ";"
        << fmtDouble(results[i].avg_before) << ";"
        << fmtDouble(results[i].avg_after) << ";"
        << fmtDouble(results[i].avg_crossing_ratio) << ";"
        << fmtDouble(results[i].avg_time_ms) << "\n";
    std::cout << "Natural results      -> " << fs::absolute(out) << "\n";
}

static void writePropCsv(
    const std::vector<std::string>& names,
    const std::vector<SingleRunResult>& results)
{
    fs::path out = SOURCE_DIR / "results_propagation.csv";
    std::ofstream f(out);
    f << "instance;crossings_before;crossings_after;crossing_ratio_pct;time_ms\n";
    for (size_t i = 0; i < names.size(); ++i)
        f << names[i] << ";"
        << results[i].crossings_before << ";"
        << results[i].crossings_after << ";"
        << fmtDouble(results[i].crossingRatio()) << ";"
        << fmtDouble(results[i].elapsed_ms) << "\n";
    std::cout << "Propagation results  -> " << fs::absolute(out) << "\n";
}

// ============================================================================
// Summary printer
// ============================================================================

static void printSummary(
    const std::vector<NaturalAggResult>& nat,
    const std::vector<SingleRunResult>& prop)
{
    auto avg = [](const auto& v, auto fn) {
        if (v.empty()) return 0.0;
        double s = 0;
        for (const auto& r : v) s += fn(r);
        return s / static_cast<double>(v.size());
        };

    double nat_ratio = avg(nat, [](const NaturalAggResult& r) { return r.avg_crossing_ratio; });
    double nat_time = avg(nat, [](const NaturalAggResult& r) { return r.avg_time_ms; });
    double prop_ratio = avg(prop, [](const SingleRunResult& r) { return r.crossingRatio(); });
    double prop_time = avg(prop, [](const SingleRunResult& r) { return r.elapsed_ms; });

    std::cout << "\n";
    std::cout << std::string(48, '=') << "\n";
    std::cout << "SUMMARY  (ratio% = 100 * after / before)\n";
    std::cout << std::string(48, '-') << "\n";
    std::cout << std::left << std::setw(24) << ""
        << std::right << std::setw(10) << "Avg.Rat%"
        << std::setw(14) << "Avg.Time(ms)"
        << "\n";
    std::cout << std::string(48, '-') << "\n";

    auto row = [&](const std::string& label, double ratio, double time) {
        std::cout << std::left << std::setw(24) << label
            << std::right
            << std::setw(9) << std::fixed << std::setprecision(1) << ratio << "%"
            << std::setw(14) << std::fixed << std::setprecision(2) << time
            << "\n";
        };

    row("Natural", nat_ratio, nat_time);
    row("Propagation", prop_ratio, prop_time);
    std::cout << std::string(48, '=') << "\n";
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char* argv[]) {
    fs::path dir = (argc > 1) ? argv[1] : INSTANCES_DIR;

    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::cerr << "Error: directory not found: " << dir << "\n";
        return 1;
    }

    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ".asp")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());

    if (files.empty()) {
        std::cerr << "No .asp files found in " << dir << "\n";
        return 1;
    }

    std::cout << "Global Sifting — Efficiency Measurements\n";
    std::cout << "Instances  : " << fs::absolute(dir) << "\n";
    std::cout << "Rounds     : " << SIFTING_ROUNDS << "\n";
    std::cout << "Random runs: " << RANDOM_RUNS << "  (seed " << RNG_SEED << ")\n\n";

    std::mt19937 rng(RNG_SEED);

    std::vector<std::string>       instance_names;
    std::vector<NaturalAggResult>  nat_results;
    std::vector<SingleRunResult>   prop_results;
    int skipped = 0;

    for (const auto& path : files) {
        const std::string name = path.stem().string();

        ParsedInstance inst = parseAsp(path);
        if (!inst.ok) {
            std::cerr << "[SKIP] " << name << ": " << inst.error << "\n";
            ++skipped;
            continue;
        }

        SiftState S_base;
        if (!buildState(inst, S_base) || S_base.blocks.empty()) {
            std::cerr << "[SKIP] " << name << "\n";
            ++skipped;
            continue;
        }

        // (a) Natural: 50 random permutations
        nat_results.push_back(runNatural(S_base, rng));

        // (b) Propagation: time includes ordering + sifting
        SiftState S_prop = S_base;

        auto t_ord_start = std::chrono::high_resolution_clock::now();
        BlockList B_prop = orderBlocksByLayerPropagation(S_prop);
        auto t_ord_end = std::chrono::high_resolution_clock::now();
        double ordering_ms = std::chrono::duration<double, std::milli>(
            t_ord_end - t_ord_start).count();

        SingleRunResult r_prop = runSifting(S_prop, B_prop);
        r_prop.elapsed_ms += ordering_ms;   // fold ordering cost into total time
        prop_results.push_back(r_prop);

        instance_names.push_back(name);
        std::cout << "  processed: " << name << "\n";
    }

    printSummary(nat_results, prop_results);

    std::cout << "\n";
    writeNaturalCsv(instance_names, nat_results);
    writePropCsv(instance_names, prop_results);

    if (skipped > 0)
        std::cout << "\n(" << skipped << " instance(s) skipped)\n";

    return 0;
}