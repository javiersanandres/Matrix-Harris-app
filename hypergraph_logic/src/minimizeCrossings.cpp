#include "GlobalSifting.h"
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <map>
#include <vector>
#include <climits>

// ==================================================================================
// Implementation of the Global Sifting algorithm as described in the paper below:
// Bachmaier, C., Brandenburg, F. J., Brunner, W., & Hübner, F. (2011).
// "A Global k-Level Crossing Reduction Algorithm."
// In: Graph Drawing (GD 2010), LNCS 6502, pp. 70-81. Springer.
// DOI: 10.1007/978-3-642-18469-7_7
//
//
// Since we are dealing with hypergraphs, we first have to transform the original
// hypergraph into a bipartite graph G1 by replacing each short hyperedge with a
// hub node and |S| + |T| binary edges.
// This way, we can apply the algorithm to G1 and then translate the resulting
// G1 ordering back to the original hypergraph.
//
// ==================================================================================

namespace sifting_internal {

    using namespace hypergraph_logic;

    // ── buildG1 ───────────────────────────────────────────────────────────────────

    void buildG1(SiftState& S, const std::map<int, LayerData>& layers, int start_layer) {
        int anchor_layer = std::max(0, start_layer - 1);
        if (start_layer >= 1) {
            S.fixed_position_count = static_cast<int>(layers.at(anchor_layer).nodes.size());
        }

        std::vector<std::pair<int, int>> edges_to_add; // Temporary stores for connections in the adjacency cache.

        std::vector<HyperedgePtr> incoming_edges;
        for (const auto& [layer, data] : layers) {
            if (layer < anchor_layer) continue;

            // Register all nodes at this layer
            for (const auto& node : data.nodes) {
                int idx = static_cast<int>(S.g1_nodes.size());
                S.g1_nodes.emplace_back(node.get(), 2 * layer);
                S.node_to_g1[node.get()] = idx;
                S.g1_layers[2 * layer].push_back(idx);
            }

            // Process edges from the previous layer: sources are already registered
            // (previous iteration), targets are registered just above.
            if (!incoming_edges.empty()) {
                for (const auto& edge : incoming_edges) {
                    int hub_idx = static_cast<int>(S.g1_nodes.size());
                    S.g1_nodes.emplace_back(nullptr, 2 * layer - 1);
                    S.g1_layers[2 * layer - 1].push_back(hub_idx);

                    for (const auto& src : edge->getSources())
                        edges_to_add.push_back({ S.node_to_g1[src.get()], hub_idx });

                    for (const auto& tgt : edge->getTargets())
                        edges_to_add.push_back({ hub_idx, S.node_to_g1[tgt.get()] });
                }
            }

            incoming_edges = data.outgoing_edges;
        }

        // Build adjacency cache
        S.g1_in.assign(S.g1_nodes.size(), {});
        S.g1_out.assign(S.g1_nodes.size(), {});
        for (const auto& e : edges_to_add) {
            S.g1_in[e.second].push_back(e.first);
            S.g1_out[e.first].push_back(e.second);
        }
    }

    // ── Chain detection ───────────────────────────────────────────────────────────

    bool isDummyChainStart(Node* n) {
        if (!n->isDummy()) return false;
        auto children = n->getChildren();
        if (children.size() != 1) return false;
        Node* child = children[0].get();
        if (!child->isDummy()) return false;
        auto child_parents = child->getParents();
        return child_parents.size() == 1 && child_parents[0].get() == n;
    }

    std::vector<int> collectChainG1Nodes(int start_g1, const SiftState& S, std::unordered_set<int>& visited) {
        std::vector<int> chain;
        chain.push_back(start_g1);
        Node* current = S.g1_nodes[start_g1].original->getChildren()[0].get(); // We already know this is the only child, so we can directly access it.
        int curr_id = S.node_to_g1.at(current);
        while (true) {
            // Add the hub node between the current and the previous dummy.
            visited.insert(S.g1_in[curr_id][0]); // Hub node, which is the only parent of the current dummy.
            chain.push_back(S.g1_in[curr_id][0]); // Push back the hub node to the chain.

            // Add the current dummy node.
            visited.insert(curr_id);
            chain.push_back(curr_id);

            // Check if we can keep extending the chain.
            if (!isDummyChainStart(current)) break;

            current = current->getChildren()[0].get(); // We already know this is the only child, so we can directly access it.
            curr_id = S.node_to_g1.at(current);
        }
        return chain;
    }

    // ── buildBlocks ───────────────────────────────────────────────────────────────

    void buildBlocks(SiftState& S) {
        int ng_size = static_cast<int>(S.g1_nodes.size());
        std::unordered_set<int> visited;

        for (int idx = 0; idx < ng_size; idx++) {
            if (visited.count(idx)) continue; // Already being taken care of in a dummy chain
            const auto& node = S.g1_nodes[idx];
            if (node.original == nullptr || !node.original->isDummy() || !isDummyChainStart(node.original)) {
                // Not a chain start, so it is a singleton block.
                int bid = static_cast<int>(S.blocks.size());
                S.g1_nodes[idx].block_id = bid;
                S.blocks.emplace_back(std::vector<int>{idx});
            }
            else {
                // Chain start, so it is a dummy chain block. We will collect the full chain and add it as a block.
                std::vector<int> chain = collectChainG1Nodes(idx, S, visited);
                int bid = static_cast<int>(S.blocks.size());
                for (int g1_idx : chain) S.g1_nodes[g1_idx].block_id = bid;
                S.blocks.emplace_back(std::move(chain));
            }
        }

        S.pi.resize(S.blocks.size(), 0);
    }

    // ── Layer and hub sorting ─────────────────────────────────────────────────────

    std::unordered_map<Node*, int> sortLayer(
        const std::unordered_map<Node*, int>& upper_pos,
        std::vector<NodePtr>& lower,
        std::vector<HyperedgePtr>& edges)
    {
        std::unordered_map<Node*, int> left_of;
        for (const auto& n : lower)
            left_of[n.get()] = INT_MAX;

        for (const auto& edge : edges) {
            // Find the minimum source position for this edge
            int min_src_pos = INT_MAX;
            for (const auto& src : edge->getSources()) {
                min_src_pos = std::min(min_src_pos, upper_pos.at(src.get()));
            }
            // Update left(A) for each target of this edge
            for (const auto& tgt : edge->getTargets()) {
                left_of[tgt.get()] = std::min(left_of.at(tgt.get()), min_src_pos);
            }
        }

        // Sort based on left(A) values, with ties broken by original position since we
        // are using a stable sort and the original order is the one in the LayerData.
        std::stable_sort(lower.begin(), lower.end(),
            [&](const NodePtr& a, const NodePtr& b) {
                return left_of.at(a.get()) < left_of.at(b.get());
            });

        std::unordered_map<Node*, int> lower_pos;
        for (int i = 0; i < static_cast<int>(lower.size()); i++)
            lower_pos[lower[i].get()] = i;

        return lower_pos;
    }

    void sortHubs(
        SiftState& S,
        const std::unordered_map<Node*, int>& upper_pos,
        const std::unordered_map<Node*, int>& lower_pos,
        int layer)
    {
        std::vector<int>& hub_indices = S.g1_layers.at(layer);

        // Sort based on lexicographical order of minimum parent position and minimum child position, as described above.
        std::sort(hub_indices.begin(), hub_indices.end(),
            [&](int idx_a, int idx_b) {
                int min_parent_a = INT_MAX;
                for (const auto& p : S.g1_in[idx_a]) {
                    min_parent_a = std::min(min_parent_a, upper_pos.at(S.g1_nodes[p].original));
                }
                int min_parent_b = INT_MAX;
                for (const auto& p : S.g1_in[idx_b]) {
                    min_parent_b = std::min(min_parent_b, upper_pos.at(S.g1_nodes[p].original));
                }
                if (min_parent_a != min_parent_b) return min_parent_a < min_parent_b;

                int min_child_a = INT_MAX;
                for (const auto& c : S.g1_out[idx_a]) {
                    min_child_a = std::min(min_child_a, lower_pos.at(S.g1_nodes[c].original));
                }
                int min_child_b = INT_MAX;
                for (const auto& c : S.g1_out[idx_b]) {
                    min_child_b = std::min(min_child_b, lower_pos.at(S.g1_nodes[c].original));
                }

                return min_child_a < min_child_b;
            });
    }

    // ── buildBlockOrder ───────────────────────────────────────────────────────────

    void buildBlockOrder(BlockList& B, SiftState& S, std::map<int, LayerData>& layers, int start_layer) {
        int anchor_layer = std::max(0, start_layer - 1);
        int num_blocks = static_cast<int>(S.blocks.size());
        B.clear();
        B.reserve(num_blocks);

        std::unordered_set<int> visited;
        std::unordered_map<Node*, int> upper_layer_order;
        std::unordered_map<Node*, int> lower_layer_order;

        // Initialize the seed order on the anchor layer, which will be used to sort all layers
        // below it and thus, indirectly, all blocks.
        int pos = 0;
        for (const auto& node : layers.at(anchor_layer).nodes) {
            upper_layer_order[node.get()] = pos++;
        }

        // We insert the blocks of the anchor layer first, in their original order. So the resulting
        // B will start with the blocks of the anchor layer in the same order as they appear in LayerData.
        for (const auto& node : layers.at(anchor_layer).nodes) {
            int node_block_id = S.g1_nodes[S.node_to_g1.at(node.get())].block_id;
            B.push_back(node_block_id);
            visited.insert(node_block_id);
        }

        std::vector<HyperedgePtr> incoming_edges = layers.at(anchor_layer).outgoing_edges;
        for (auto& [layer, data] : layers) {
            if (layer <= anchor_layer) continue;
            // Sort this layer's nodes based on the order of the upper layer and store the hubs blocks
            // between the upper and this layer, as well as this layer's blocks.
            lower_layer_order = sortLayer(upper_layer_order, data.nodes, incoming_edges);
            sortHubs(S, upper_layer_order, lower_layer_order, 2 * layer - 1);

            // Add the blocks of hub nodes between the upper and this layer, in their new order
            for (const auto& hub_id : S.g1_layers[2 * layer - 1]) {
                int block_id = S.g1_nodes.at(hub_id).block_id;
                if (visited.insert(block_id).second) B.push_back(block_id);
            }

            // Add the blocks of this layer's nodes, in their new order
            for (const auto& node : data.nodes) {
                int block_id = S.g1_nodes.at(S.node_to_g1.at(node.get())).block_id;
                if (visited.insert(block_id).second) B.push_back(block_id);
            }

            // The new seed order for the next iteration is the one we just computed for this layer
            upper_layer_order = std::move(lower_layer_order);

            incoming_edges = data.outgoing_edges;
        }
    }

    // ── sortAdjacencies ───────────────────────────────────────────────────────────

    struct SymmetricPairHash {
        size_t operator()(const std::pair<int, int>& p) const {
            std::hash<int> h;
            return h(p.first) ^ h(p.second);
        }

        bool operator()(const std::pair<int, int>& x, const std::pair<int, int>& y) const {
            return (x.first == y.first && x.second == y.second) ||
                (x.first == y.second && x.second == y.first);
        }
    };

    void sortAdjacencies(SiftState& S, const BlockList& B) {
        int num_blocks = static_cast<int>(S.blocks.size());

        for (int pos = 0; pos < num_blocks; pos++)
            S.pi[B[pos]] = pos;

        // Pre-size all adjacency arrays before any traversal
        for (auto& blk : S.blocks) {
            int n_minus_size = static_cast<int>(S.g1_in[blk.upper()].size());
            int n_plus_size = static_cast<int>(S.g1_out[blk.lower()].size());
            blk.N_minus.clear(); blk.N_minus.reserve(n_minus_size);
            blk.N_plus.clear();  blk.N_plus.reserve(n_plus_size);
            blk.I_minus.assign(n_minus_size, -1);
            blk.I_plus.assign(n_plus_size, -1);
        }

        std::unordered_map<std::pair<int, int>, int, SymmetricPairHash, SymmetricPairHash> cache;

        for (int bid : B) {
            Block& blk = S.blocks[bid];

            for (int parent : S.g1_in[blk.upper()]) {
                int pb_id = S.g1_nodes[parent].block_id;
                Block& pb = S.blocks[pb_id];
                int j = static_cast<int>(pb.N_plus.size());
                pb.N_plus.push_back(blk.upper());
                if (S.pi[bid] < S.pi[pb_id]) {
                    cache[{bid, pb_id}] = j;
                }
                else {
                    int p = cache.at({ bid, pb_id });
                    pb.I_plus[j] = p;
                    blk.I_minus[p] = j;
                }
            }

            for (int child : S.g1_out[blk.lower()]) {
                int cb_id = S.g1_nodes[child].block_id;
                Block& cb = S.blocks[cb_id];
                int j = static_cast<int>(cb.N_minus.size());
                cb.N_minus.push_back(blk.lower());
                if (S.pi[bid] < S.pi[cb_id]) {
                    cache[{bid, cb_id}] = j;
                }
                else {
                    int p = cache.at({ bid, cb_id });
                    cb.I_minus[j] = p;
                    blk.I_plus[p] = j;
                }
            }
        }
    }

    // ── uswap ─────────────────────────────────────────────────────────────────────

    int uswap(const SiftState& S, const std::vector<int>& Na, const std::vector<int>& Nb, const std::vector<int>& pi) {
        int r = static_cast<int>(Na.size());
        int s = static_cast<int>(Nb.size());
        int c = 0, i = 0, j = 0;
        while (i < r && j < s) {
            G1Node x_i = S.g1_nodes[Na[i]], y_j = S.g1_nodes[Nb[j]];
            int pa = pi[x_i.block_id], pb = pi[y_j.block_id];
            if (pa < pb) { c += (s - j); i++; }
            else if (pa > pb) { c -= (r - i); j++; }
            else { c += (s - j) - (r - i); i++; j++; }
        }

        return c;
    }

    // ── updateAdjacency ───────────────────────────────────────────────────────────

    void updateAdjacency(SiftState& S, Block& A, Block& B, int a, int b, bool minus_direction) {
        // Check if a or b are an intermediate node in a dummy chain, in which case there is
        // nothing to be updated since they cannot share any neighbour with the other block.
        if (minus_direction && (a != A.upper() || b != B.upper())) return;
        if (!minus_direction && (a != A.lower() || b != B.lower())) return;

        auto& Na = minus_direction ? A.N_minus : A.N_plus;
        auto& Nb = minus_direction ? B.N_minus : B.N_plus;
        auto& Ia = minus_direction ? A.I_minus : A.I_plus;
        auto& Ib = minus_direction ? B.I_minus : B.I_plus;

        int i = 0, j = 0;
        int r = static_cast<int>(Na.size()), s = static_cast<int>(Nb.size());
        while (i < r && j < s) {
            G1Node x_i = S.g1_nodes[Na[i]], y_j = S.g1_nodes[Nb[j]];
            int pa = S.pi[x_i.block_id], pb = S.pi[y_j.block_id];
            if (pa < pb) { i++; continue; }
            else if (pa > pb) { j++; continue; }
            else {
                Block& z = S.blocks[x_i.block_id]; // == S.blocks[y_j.block_id]
                auto& Nz = minus_direction ? z.N_plus : z.N_minus;
                auto& Iz = minus_direction ? z.I_plus : z.I_minus;
                std::swap(Nz[Ia[i]], Nz[Ib[j]]);
                std::swap(Iz[Ia[i]], Iz[Ib[j]]);
                Ia[i]++;
                Ib[j]--;
                i++; j++;
            }
        }
    }

    // ── siftingSwap ───────────────────────────────────────────────────────────────

    std::set<int> levels(const SiftState& S, Block& block) {
        std::set<int> lvls;
        for (int g1_idx : block.g1_nodes)
            lvls.insert(S.g1_nodes[g1_idx].g1_layer);
        return lvls;
    }

    int getNodeAtLevel(const SiftState& S, Block& block, int level) {
        for (int g1_idx : block.g1_nodes) {
            if (S.g1_nodes[g1_idx].g1_layer == level) {
                return g1_idx;
            }
        }
        return -1; // Not found
    }

    int siftingSwap(SiftState& S, int a_id, int b_id) {
        Block& A = S.blocks[a_id];
        Block& B = S.blocks[b_id];
        int delta = 0;

        auto levels_A = levels(S, A);
        auto levels_B = levels(S, B);

        int upper_a_layer = S.g1_nodes[A.upper()].g1_layer;
        int lower_a_layer = S.g1_nodes[A.lower()].g1_layer;
        int upper_b_layer = S.g1_nodes[B.upper()].g1_layer;
        int lower_b_layer = S.g1_nodes[B.lower()].g1_layer;

        std::set<std::pair<int, bool>> L;
        if (levels_B.count(upper_a_layer)) L.insert({ upper_a_layer, true });
        if (levels_B.count(lower_a_layer)) L.insert({ lower_a_layer, false });
        if (levels_A.count(upper_b_layer)) L.insert({ upper_b_layer, true });
        if (levels_A.count(lower_b_layer)) L.insert({ lower_b_layer, false });

        for (const auto& [layer, is_minus] : L) {
            int a = getNodeAtLevel(S, A, layer);
            int b = getNodeAtLevel(S, B, layer);
            if (is_minus) {
                std::vector<int> Na = (a == A.upper()) ? A.N_minus : S.g1_in[a];
                std::vector<int> Nb = (b == B.upper()) ? B.N_minus : S.g1_in[b];

                delta += uswap(S, Na, Nb, S.pi);
                updateAdjacency(S, A, B, a, b, true);
            }
            else {
                std::vector<int> Na = (a == A.lower()) ? A.N_plus : S.g1_out[a];
                std::vector<int> Nb = (b == B.lower()) ? B.N_plus : S.g1_out[b];

                delta += uswap(S, Na, Nb, S.pi);
                updateAdjacency(S, A, B, a, b, false);
            }
        }

        S.pi[a_id]++;
        S.pi[b_id]--;
        return delta;
    }

    // ── siftingStep ───────────────────────────────────────────────────────────────

    int siftingStep(SiftState& S, BlockList& B, int a_id) {
        int numblocks = static_cast<int>(B.size());

        // Build B' with A at front (except for the anchors, which are fixed and cannot be sifted)
        BlockList Bp;
        Bp.reserve(numblocks);
        for (int i = 0; i < S.fixed_position_count; i++)
            Bp.push_back(B[i]);
        Bp.push_back(a_id);
        for (int i = S.fixed_position_count; i < numblocks; i++)
            if (B[i] != a_id) Bp.push_back(B[i]);

        sortAdjacencies(S, Bp);

        int chi = 0, chi_star = 0, p_star = S.fixed_position_count;

        for (int p = S.fixed_position_count + 1; p < numblocks; p++) {
            chi += siftingSwap(S, a_id, Bp[p]);
            std::swap(Bp[p - 1], Bp[p]);
            if (chi < chi_star) { chi_star = chi; p_star = p; }
        }

        // Locate A at position p*. Right now A is at the end of Bp.
        std::rotate(Bp.begin() + p_star, Bp.end() - 1, Bp.end());
        B = std::move(Bp);

        return chi_star;
    }

    // ── crossing count ────────────────────────────────────────────────────────────

    // TODO: Implement a function to compute the actual crossing count after the final ordering is applied.

} // namespace sifting_internal

// ============================================================================
// GraphicalHypergraph::minimizeCrossings
// ============================================================================
namespace hypergraph_logic {

    int GraphicalHypergraph::minimizeCrossings(int sifting_rounds, int start_layer) {
        if (getLayers().empty()) return 0;

        using namespace sifting_internal;

        SiftState S;
        // 1. Build G1
        buildG1(S, layers_, start_layer);
        if (S.g1_nodes.empty()) return 0;

        // 2. Build blocks (dummy chain detection + singletons)
        buildBlocks(S);
        if (S.blocks.empty()) return 0;

        // 3. Initialize block order
        BlockList B;
        buildBlockOrder(B, S, layers_, start_layer);

        // 4. Initial sorted adjacencies
        sortAdjacencies(S, B);

        // 5. ρ rounds of global sifting (early exit on convergence)
        int numblocks = static_cast<int>(B.size());
        for (int round = 0; round < sifting_rounds; round++) {
            int chi = 0;
            BlockList snapshot = B;
            for (int i = S.fixed_position_count; i < numblocks; i++)
                chi += siftingStep(S, B, snapshot[i]);

            if (chi >= 0) break; // No improvement, so we can stop.
        }

        // 6. Write final ordering back to LayerData::nodes
        for (auto& [layer, data] : layers_) {
            if (layer < start_layer) continue;

            std::sort(data.nodes.begin(), data.nodes.end(),
                [&](NodePtr a, NodePtr b) {
                    G1Node ga = S.g1_nodes[S.node_to_g1.at(a.get())];
                    G1Node gb = S.g1_nodes[S.node_to_g1.at(b.get())];
                    return S.pi[ga.block_id] < S.pi[gb.block_id];
                });
        }

        return 0; // TODO: compute and return the actual crossing count after the final ordering is applied.
    }

} // namespace hypergraph_logic