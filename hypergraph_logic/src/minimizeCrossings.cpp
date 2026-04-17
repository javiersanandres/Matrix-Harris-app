#include "GraphicalHypergraph.h"
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <map>
#include <vector>
#include <climits>

// ==================================================================================
// All data structures and helper functions live in this anonymous namespace.
// They are internal to this translation unit and invisible everywhere else.
//  
//
// I've implemented the Global Sifting algorithm as described in the paper below:
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
namespace {

    using namespace hypergraph_logic;

    // ── G1 node ──────────────────────────────────────────────────────────────────
    struct G1Node {
        Node* original = nullptr;   // non-null for real/dummy nodes from the base class
        int   g1_layer = -1;        // virtual G1 layer: 2*L for nodes, 2*L+1 for hubs
        int   block_id = -1;        // which block owns this node

        G1Node(Node* original, int g1_layer)
            : original(original)
            , g1_layer(g1_layer)
            , block_id(-1)
        {
        }
    };

    // ── Block ─────────────────────────────────────────────────────────────────────
    //
    // The blocks we are considering are:
    //   • A dummy chain (dummy -> hub -> dummy -> …)
    //   • A singleton real/dummy node or hub.
    //
    // This is pretty similar to what the paper describes, except that we allow blocks
    // to be composed by dummy nodes. This is because we are dealing with a hypergraph.
    // To illustrate this, consider the following example: 
    // {Source1}->{Dummy1}
    // {Dummy1, Source2}->{Dummy2}
    // {Dummy2, Source3}->{Target1}
    // In this case, Dummy1 and Dummy2 would not form a dummy chain because they are
    // not directly linked together, there are some other non-dummy nodes which cannot
    // be avoided.
    // 
    // However, in a normal graph, a long edge splits in a chain of dummies by definition.
    // 
    // g1_nodes is ordered top-to-bottom by g1_layer number.
    // N_minus denotes the parent blocks of the block, sorted by their position in B.
	// N_plus denotes the child blocks of the block, sorted by their position in B.
    struct Block {
        std::vector<int> g1_nodes; // Indices of G1 nodes in this block.

        int upper() const { return g1_nodes.front(); }
        int lower() const { return g1_nodes.back(); }

        std::vector<int> N_minus; // parent blocks (sorted by pi)
        std::vector<int> N_plus;  // child  blocks (sorted by pi)
        std::vector<int> I_minus; // cross-ref: I_minus[i] = position of upper() in the vector N_plus(N_minus[i])
        std::vector<int> I_plus;  // cross-ref: I_plus[i]  = position of lower() in the vector N_minus(N_plus[i])

        Block(std::vector<int> nodes) : g1_nodes(std::move(nodes)) {}
    };

    using BlockList = std::vector<int>; // alias for ensuring clarity when we are talking about block orderings (pi)

    // ── Algorithm state ───────────────────────────────────────────────────────────

    struct SiftState {
        std::vector<G1Node> g1_nodes;
        std::vector<std::vector<int>> g1_in;        // g1_in[i] = parents of G1 node i (for quick lookup)
        std::vector<std::vector<int>> g1_out;       // g1_out[i] = children of G1 node i (for quick lookup)
        std::map<int, std::vector<int>> g1_layers;
        std::unordered_map<Node*, int> node_to_g1;  // original Node* -> G1 index
        std::vector<Block> blocks;
        std::vector<int> pi;                        // pi[block_id] = position in B
		int fixed_position_count;                   // number of blocks with fixed position (anchors)
    };

    // ── buildG1 ───────────────────────────────────────────────────────────────────
    //
    // Registers all hypergraph nodes at layers >= anchor_layer into G1.
    // Any node is sent to the virtual layer 2*L, where L is the original hypergraph layer.
    // Furthermore, for each short hyperedge, it inserts a hub node with odd virtual layer 
    // 2*L+1, with L being the original layer of the hyperedge in the hypergraph; and inserts
    // binary edges from each source to the hub and from the hub to each target in the form of
    // an adjacency cache.

    void buildG1(SiftState& S, const std::map<int, LayerData>& layers, int start_layer) {
        int anchor_layer = std::max(0, start_layer - 1);
        if (start_layer == 0) {
            S.fixed_position_count = 0;
        }
        else {
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
    //
    // A dummy chain starts at a dummy node with exactly one child that is also a
    // dummy and whose only parent is this node.  Detection uses Node::getChildren /
    // Node::getParents on the original node graph.  In G1 the hub nodes inserted
    // between adjacent dummies are automatically absorbed because they satisfy
    // isChainContinuation (one G1-predecessor, one G1-successor, predecessor has
    // only one successor).

    bool isDummyChainStart(Node* n) {
        if (!n->isDummy()) return false;
        auto children = n->getChildren();
        if (children.size() != 1) return false;
        Node* child = children[0].get();
        if (!child->isDummy()) return false;
        auto child_parents = child->getParents();
        return child_parents.size() == 1 && child_parents[0].get() == n;
    }

    bool isDummyChainEnd(int g1_idx, const SiftState& S) {
        if (S.g1_out[g1_idx].size() != 1) return false;
		int succ = S.g1_out[g1_idx][0];
		return S.g1_in[succ].size() == 1;
	}

    std::vector<int> collectChainG1Nodes(int start_g1, const SiftState& S, std::unordered_set<int>& visited) {
        std::vector<int> chain;
        chain.push_back(start_g1);
		int current = S.g1_out[start_g1][0]; // Guaranteed to exist by isDummyChainStart and guarenteed to be part of the chain
        while (true) {
            if (!visited.insert(current).second) break; // Already visited, this comprobation is just for safety, it should not occur.
            chain.push_back(current);
			if (!isDummyChainEnd(current, S)) break; // Not a chain end, so we can keep extending
            current = S.g1_out[current][0];
        }
        return chain;
    }

    // ── buildBlocks ───────────────────────────────────────────────────────────────
    //
    // 1) Identify dummy chains -> one block each.
    // 2) Every remaining G1 node -> singleton block.

    void buildBlocks(SiftState& S) {
        int ng_size = static_cast<int>(S.g1_nodes.size());
        std::unordered_set<int> visited;

		for (int idx = 0; idx < ng_size; idx++) {
            if (visited.count(idx)) continue; // Already being taken care of in a dummy chain
			const auto& node = S.g1_nodes[idx];
            if (node.original == nullptr || !node.original->isDummy() || !isDummyChainStart(node.original)){
                // Not a chain start, so it is a singleton block.
                int bid = static_cast<int>(S.blocks.size());
                S.g1_nodes[idx].block_id = bid;
                S.blocks.emplace_back(std::vector<int>(idx));
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

    
	// -- Layer and hub sorting ───────────────────────────────────────────────────────────
    //
    // We sort the original nodes in each layer based on the position of their parents in the upper
    // layer, with ties broken by the original position in the LayerData. In order to do this, given some
    // node A in the lower layer, we compute left(A) = minimum position of the parents of A in the upper layer.
	// Then, we sort the nodes in the lower layer based on left(A) values in ascending order. 
    // 
    // We sort the hubs lexicographically based on the minimum position of their parents in the upper layer and
    // in case of ties, based on the minimum position of their children in the lower layer. This order is well defined
    // due to imposed restrictions on the hypergraph structure: if two hyperedges share a source, then they cannot share
    // a target, and vice versa. Therefore, if two hubs share a parent, they cannot share a child, so there cannot be 
    // ties in both the upper and lower layer positions.
    //

    std::unordered_map<Node*, int> sortLayer(const std::unordered_map<Node*, int>& upper_pos, std::vector<NodePtr>& lower, std::vector<HyperedgePtr>& edges)
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

    void sortHubs(SiftState& S, const std::unordered_map<Node*, int>& upper_pos, const std::unordered_map<Node*, int>& lower_pos, int layer) {
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

	// ── buildBlockOrder -────────────────────────────────────────────────────────
    //
    // As they suggest in the paper, to aim for better results in the sifting phase, it is
    // important to have some good initial order. They also suggest a topological sort of
    // blocks based on the lexicographical order determined by the layer number and the 
    // position of the block in that layer. My implementation takes this into account.
    //
    // The key to understand the initialization policy developed here is to understand
    // how we locate nodes in layers when we have to relocate them. This happens in many
    // situations, all which involve relocation of nodes or splitting of edges. In that
    // case, what we do is adding the new nodes at the very end of the layer they belong to.
    // This means that many crossings will appear because the targets are located far away
    // from their sources. In order to mitigate this, we want to have an initial order where
    // sources and targets are as close as possible.
    // In this sense, we process the first layer with the seed order given by the original 
	// order of the nodes in LayerData, and then we short each subsequent layer based on the 
    // order of the upper layer, with the function sortLayer. The hubs are located between
    // the upper and lower layer and they are sorted with the function sortHubs based on the
    // order of the upper and lower layers.
    // 
    // This initialization policy ensures, for example, that whenever the client wants to change
    // the order between two nodes in the same layer, the minimmizeCrossings function will not
    // oppose to this change by taking the nodes back to their original order (where they get less
    // crossings), but it will try to find a good order where the subgraph of the first node goes 
    // before the subgraph of the second node. This is the intended behaviour.
    // 

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

		for (int i = 0; i < num_blocks; i++) {
			S.pi[B[i]] = i;
        }
    }


    // ── sortAdjacencies ───────────────────────────────────────────────────────────
    //
    // Builds N±/I± for every block from the G1 adjacency cache.
    // N_minus(A) = blocks connected to top(A) from above, sorted by pi.
    // N_plus(A)  = blocks connected to bottom(A) from below, sorted by pi.
    // I_minus/I_plus are cross-reference arrays for O(1) post-swap updates.

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
                if (pb_id < 0) continue;
                Block& pb = S.blocks[pb_id];
                int j = static_cast<int>(pb.N_plus.size());
                pb.N_plus.push_back(bid);
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
                if (cb_id < 0) continue;
                Block& cb = S.blocks[cb_id];
                int j = static_cast<int>(cb.N_minus.size());
                cb.N_minus.push_back(bid);
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
    //
    // Net crossing delta when block A (with neighbours Na) swaps right past block B
    // (with neighbours Nb).  Both lists are sorted by ascending pi.
    // Positive = more crossings after swap; negative = fewer.

    int uswap(const std::vector<int>& Na, const std::vector<int>& Nb, const std::vector<int>& pi) {
        int r = static_cast<int>(Na.size());
        int s = static_cast<int>(Nb.size());
        int c = 0, i = 0, j = 0;
        while (i < r && j < s) {
            int pa = pi[Na[i]], pb = pi[Nb[j]];
            if (pa < pb) { c += (s - j); i++; }
            else if (pa > pb) { c -= (r - i); j++; }
            else { c +=  (s - j) - (r - i); i++; j++; }
        }
        
        return c;
    }

    // ── updateAdjacency ───────────────────────────────────────────────────────────
    //
    // After swapping A and B, repair the adjacency lists of their common neighbours
    // in direction d so N± lists remain sorted by pi.

    void updateAdjacency(SiftState& S, int a_id, int b_id, bool minus_direction) {
        auto& Na = minus_direction ? S.blocks[a_id].N_minus : S.blocks[a_id].N_plus;
        auto& Nb = minus_direction ? S.blocks[b_id].N_minus : S.blocks[b_id].N_plus;
        auto& Ia = minus_direction ? S.blocks[a_id].I_minus : S.blocks[a_id].I_plus;
        auto& Ib = minus_direction ? S.blocks[b_id].I_minus : S.blocks[b_id].I_plus;

        int i = 0, j = 0;
        int r = static_cast<int>(Na.size()), s = static_cast<int>(Nb.size());
        while (i < r && j < s) {
            int pa = S.pi[Na[i]], pb = S.pi[Nb[j]];
            if (pa < pb) { i++; continue; }
            else if (pa > pb) { j++; continue; }
            else {
                int z = Na[i]; // == Nb[j]
                auto& Nz = minus_direction ? S.blocks[z].N_plus : S.blocks[z].N_minus;
                auto& Iz = minus_direction ? S.blocks[z].I_plus : S.blocks[z].I_minus;
                std::swap(Nz[Ia[i]], Nz[Ib[j]]);
                std::swap(Iz[Ia[i]], Iz[Ib[j]]);
                Ia[i]++;
                Ib[j]--;
                i++; j++;
            }
        }
    }

    // ── siftingSwap ───────────────────────────────────────────────────────────────
    //
    // Swaps adjacent blocks A (left) and B (right), updates pi and adjacency lists,
    // returns the change in crossing count.

	std::set<int> levels(const SiftState& S, Block& block) {
        std::set<int> levels;
        for (int g1_idx : block.g1_nodes)
			levels.insert(S.g1_nodes[g1_idx].g1_layer);
        return levels;
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
            if (is_minus) {
                delta += uswap(A.N_minus, B.N_minus, S.pi);
                updateAdjacency(S, a_id, b_id, true);
            }
            else {
                delta += uswap(A.N_plus, B.N_plus, S.pi);
                updateAdjacency(S, a_id, b_id, false);
            }
        }

        S.pi[a_id]++;
        S.pi[b_id]--;
        return delta;
    }

    // ── siftingStep ───────────────────────────────────────────────────────────────
    //
    // Places A at the front of B', sweeps it right one swap at a time, records the
    // position p* with the minimum cumulative crossing delta, then rotates A to p*.

    int siftingStep(SiftState& S, BlockList& B, int a_id) {
        int numblocks = static_cast<int>(B.size());

        int cur_pos = -1;
        for (int i = 0; i < numblocks; i++)
            if (B[i] == a_id) { cur_pos = i; break; }

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

        for (int p = S.fixed_position_count+1; p < numblocks; p++) {
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

} // anonymous namespace

// ============================================================================
// GraphicalHypergraph::minimizeCrossings
// ============================================================================
namespace hypergraph_logic {

    int GraphicalHypergraph::minimizeCrossings(int sifting_rounds, int start_layer) {
        if (getLayers().empty()) return 0;

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
            for (int i = S.fixed_position_count; i < numblocks; i++)
                chi = siftingStep(S, B, B[i]);

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