#pragma once

// ============================================================================
// EquivalenceBenchmark.h
//
// Equivalences for the full-pipeline benchmarks (fullPipelineResults.cpp and
// northDagsPipelineResults.cpp), shared by both.
//
// The equivalences of an instance are generated once (--generate-equivalences)
// and stored next to the benchmark, one file per instance, so that every run
// measures exactly the same problem. A file holds the addEquivalence calls that
// succeeded, in order, by node label:
//
//     a;b;continuous
//     n12;n7;1
//     n7;n30;0
//
// Replaying them on the freshly built graph, each followed by the same partial
// layout the editor runs (computeLayout over the layers it reports), gives back
// the same chains. A call the current code refuses is counted and skipped, so a
// change in behaviour shows up instead of breaking the run.
//
// Generation, per instance (fixed seed from the instance name):
//   - about one chain per CHAIN_EVERY real boxes, in layers with two or more;
//   - each chain aims at 2 (50%), 3 (30%) or 4 (20%) members;
//   - each new member is, with DUMMY_NEIGHBOUR_ODDS, the next real box past the
//     chain's end with at least one dummy between them (the dummy stays inside
//     the chain), and otherwise any real box of the layer, which the chain
//     then pulls next to it (or a chain end, which merges the two chains);
//   - each line is dashed with DASHED_ODDS.
//
// After every layout, checkChains verifies the rules a drawing must keep: every
// chain in one layer, its members in their layer's order, only dummies between
// two members (so no real box, and no member of another chain, inside it).
// ============================================================================

#include "GraphicalHypergraph.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace equivalence_benchmark {

    namespace fs = std::filesystem;
    using namespace hypergraph_logic;

    inline constexpr int    CHAIN_EVERY = 8;
    inline constexpr double DUMMY_NEIGHBOUR_ODDS = 0.4;
    inline constexpr double DASHED_ODDS = 0.3;
    inline constexpr int    ATTEMPTS_PER_MEMBER = 12;

    struct Operation {
        std::string a, b;
        bool continuous = true;
    };

    // ── Files ────────────────────────────────────────────────────────────────

    inline fs::path fileFor(const fs::path& dir, const std::string& instance) {
        return dir / (instance + ".equiv.csv");
    }

    inline void write(const fs::path& path, const std::vector<Operation>& ops) {
        fs::create_directories(path.parent_path());
        std::ofstream f(path);
        if (!f) throw std::runtime_error("Cannot write " + path.string());
        f << "a;b;continuous\n";
        for (const auto& op : ops) f << op.a << ";" << op.b << ";" << (op.continuous ? 1 : 0) << "\n";
    }

    inline std::vector<Operation> read(const fs::path& path) {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("Cannot open " + path.string());
        std::vector<Operation> ops;
        std::string line;
        bool header = true;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (header) { header = false; continue; }
            std::istringstream ss(line);
            Operation op;
            std::string flag;
            if (!std::getline(ss, op.a, ';') || !std::getline(ss, op.b, ';') || !std::getline(ss, flag))
                throw std::runtime_error("Malformed line in " + path.string() + ": " + line);
            op.continuous = flag != "0";
            ops.push_back(std::move(op));
        }
        return ops;
    }

    // ── Applying them ────────────────────────────────────────────────────────

    inline std::unordered_map<std::string, NodePtr> byLabel(const GraphicalHypergraph& g) {
        std::unordered_map<std::string, NodePtr> nodes;
        for (const auto& n : g.getAllNodes())
            if (!n->isDummy()) nodes.emplace(n->getName(), n);
        return nodes;
    }

    // One call, as the editor makes it. False if it was refused.
    inline bool apply(GraphicalHypergraph& g, const NodePtr& a, const NodePtr& b, bool continuous) {
        if (!a || !b || !g.canAddEquivalence(a, b)) return false;
        std::set<int> altered;
        g.addEquivalence(a, b, false, &altered);
        g.computeLayout(altered);
        return true;
    }

    // Replays ops on g, which must be laid out. Returns how many were refused.
    inline int replay(GraphicalHypergraph& g, const std::vector<Operation>& ops) {
        const auto nodes = byLabel(g);
        auto find = [&](const std::string& label) {
            auto it = nodes.find(label);
            return it == nodes.end() ? NodePtr{} : it->second;
        };
        int refused = 0;
        for (const auto& op : ops)
            if (!apply(g, find(op.a), find(op.b), op.continuous)) ++refused;
        return refused;
    }

    int checkChains(const GraphicalHypergraph& g, std::vector<std::string>* problems = nullptr);

    // ── Generating them ──────────────────────────────────────────────────────

    inline std::uint32_t seedFor(const std::string& instance) {
        std::uint32_t h = 2166136261u; // FNV-1a: the same on every platform
        for (unsigned char c : instance) { h ^= c; h *= 16777619u; }
        return h;
    }

    // g must be laid out; the generated equivalences are left applied on it.
    inline std::vector<Operation> generate(GraphicalHypergraph& g, const std::string& instance) {
        std::mt19937 rng(seedFor(instance));
        std::uniform_real_distribution<double> unit(0.0, 1.0);

        auto realsOf = [&](int layer) {
            std::vector<NodePtr> reals;
            for (const auto& n : g.getNodesAt(layer)) if (!n->isDummy()) reals.push_back(n);
            return reals;
        };
        std::vector<int> layers;
        int real_boxes = 0;
        for (const auto& [layer, _] : g.getLayers()) {
            const int count = static_cast<int>(realsOf(layer).size());
            real_boxes += count;
            if (count >= 2) layers.push_back(layer);
        }
        std::vector<Operation> ops;
        if (layers.empty()) return ops;

        // The next real box past node on the given side, if a dummy lies between them.
        auto acrossDummies = [&](const NodePtr& node, bool rightwards) -> NodePtr {
            const auto row = g.getNodesAt(node->getLayer());
            auto at = std::find(row.begin(), row.end(), node);
            if (at == row.end()) return nullptr;
            bool dummy_between = false;
            if (rightwards) {
                for (auto it = std::next(at); it != row.end(); ++it) {
                    if ((*it)->isDummy()) { dummy_between = true; continue; }
                    return dummy_between ? *it : nullptr;
                }
            }
            else {
                for (auto it = at; it != row.begin();) {
                    --it;
                    if ((*it)->isDummy()) { dummy_between = true; continue; }
                    return dummy_between ? *it : nullptr;
                }
            }
            return nullptr;
        };
        auto chainOf = [&](const NodePtr& n) {
            const EquivalenceChain* c = g.getEquivalenceChain(n.get());
            return c ? c->members : std::vector<NodePtr>{ n };
        };

        const int chains = std::max(1, real_boxes / CHAIN_EVERY);
        for (int c = 0; c < chains; ++c) {
            const int layer = layers[rng() % layers.size()];
            const auto reals = realsOf(layer);
            NodePtr start = reals[rng() % reals.size()];
            if (g.getEquivalenceChain(start.get())) continue; // already taken: one chain fewer

            const double size_roll = unit(rng);
            const int target = size_roll < 0.5 ? 2 : size_roll < 0.8 ? 3 : 4;
            for (int added = 1; added < target;) {
                bool done = false;
                for (int attempt = 0; attempt < ATTEMPTS_PER_MEMBER && !done; ++attempt) {
                    const auto members = chainOf(start);
                    const bool rightwards = rng() % 2 == 0;
                    const NodePtr& end = rightwards ? members.back() : members.front();
                    NodePtr other = unit(rng) < DUMMY_NEIGHBOUR_ODDS
                        ? acrossDummies(end, rightwards)
                        : reals[rng() % reals.size()];
                    if (!other || std::find(members.begin(), members.end(), other) != members.end()) continue;
                    const bool continuous = unit(rng) >= DASHED_ODDS;
                    // Whichever end of the chain the other box is on the side of.
                    for (const NodePtr& from : { end, rightwards ? members.front() : members.back() }) {
                        const bool broken_before = checkChains(g) > 0;
                        if (!apply(g, from, other, continuous)) continue;
                        ops.push_back({ from->getName(), other->getName(), continuous });
                        if (!broken_before && checkChains(g) > 0)
                            std::cerr << "  [RULE BROKEN] " << instance << ": right after call " << ops.size()
                                << " (" << from->getName() << ";" << other->getName() << ")\n";
                        done = true;
                        break;
                    }
                }
                if (!done) break;
                added = static_cast<int>(chainOf(start).size());
            }
        }
        return ops;
    }

    // ── Measuring them ───────────────────────────────────────────────────────

    // One crossing per dummy between two members of a chain: the line hops over it
    // (see HypergraphRenderer's "Equivalence lines"), and the minimizations count it.
    inline int lineCrossings(const GraphicalHypergraph& g) {
        int crossings = 0;
        for (const auto& chain : g.getEquivalences()) {
            const auto row = g.getNodesAt(chain.members.front()->getLayer());
            const auto first = std::find(row.begin(), row.end(), chain.members.front());
            const auto last = std::find(row.begin(), row.end(), chain.members.back());
            if (first == row.end() || last == row.end() || last < first) continue; // reported by checkChains
            crossings += static_cast<int>(std::count_if(first, last, [](const NodePtr& n) { return n->isDummy(); }));
        }
        return crossings;
    }

    struct ChainStats {
        int chains = 0;
        int members = 0;
        int longest = 0;
        int dashed_lines = 0;
    };

    inline ChainStats stats(const GraphicalHypergraph& g) {
        ChainStats s;
        for (const auto& chain : g.getEquivalences()) {
            ++s.chains;
            s.members += static_cast<int>(chain.members.size());
            s.longest = std::max(s.longest, static_cast<int>(chain.members.size()));
            for (size_t k = 0; k + 1 < chain.members.size(); ++k)
                if (!g.isEquivalenceContinuous(chain.members[k], chain.members[k + 1])) ++s.dashed_lines;
        }
        return s;
    }

    // The rules every drawing must keep; each broken one is described in problems.
    // Returns how many there are.
    inline int checkChains(const GraphicalHypergraph& g, std::vector<std::string>* problems) {
        int broken = 0;
        auto report = [&](const std::string& what) {
            ++broken;
            if (problems) problems->push_back(what);
        };
        for (const auto& chain : g.getEquivalences()) {
            const std::string name = "chain of " + chain.members.front()->getName();
            const int layer = chain.members.front()->getLayer();
            const auto row = g.getNodesAt(layer);
            std::vector<long> at;
            bool in_layer = true;
            for (const auto& m : chain.members) {
                const auto it = std::find(row.begin(), row.end(), m);
                if (m->getLayer() != layer || it == row.end()) {
                    report(name + ": " + m->getName() + " is not in layer " + std::to_string(layer));
                    in_layer = false;
                    continue;
                }
                at.push_back(static_cast<long>(it - row.begin()));
            }
            if (!in_layer) continue;
            for (size_t k = 0; k + 1 < at.size(); ++k) {
                if (at[k] >= at[k + 1]) {
                    report(name + ": " + chain.members[k]->getName() + " and " + chain.members[k + 1]->getName()
                        + " are out of order");
                    continue;
                }
                for (long i = at[k] + 1; i < at[k + 1]; ++i) {
                    if (row[i]->isDummy()) continue;
                    report(name + ": " + row[i]->getName() + " stands between " + chain.members[k]->getName()
                        + " and " + chain.members[k + 1]->getName()
                        + (g.getEquivalenceChain(row[i].get()) ? " (a member of another chain)" : ""));
                }
            }
        }
        return broken;
    }

} // namespace equivalence_benchmark
