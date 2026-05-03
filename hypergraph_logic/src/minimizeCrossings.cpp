#include "GlobalSifting.h"
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <map>
#include <vector>
#include <stdexcept>
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

	// ── GlobalSifter: construction ────────────────────────────────────────────────

	GlobalSifter::GlobalSifter(int start_layer, int end_layer,
		std::map<int, LayerData>& layers, bool order)
		: start_layer_(start_layer)
		, end_layer_(end_layer)
		, layers_(layers)
	{
		buildG1();
		buildBlocks();
		buildBlockOrder(order);
		sortAdjacencies();
	}

	// ── buildG1 ───────────────────────────────────────────────────────────────────

	void GlobalSifter::buildG1() {
		int anchor_layer = std::max(0, start_layer_ - 1);

		if (start_layer_ >= 1) {
			S_.fixed_position_count = static_cast<int>(layers_.at(anchor_layer).nodes.size());
		}

		// We also include the layer just below end_layer_ (if any) because the edges
		// between end_layer_ and end_layer_+1 carry crossing information needed to
		// evaluate swaps that touch end_layer_.
		int read_until = (layers_.count(end_layer_ + 1) > 0) ? end_layer_ + 1 : end_layer_;

		std::vector<std::pair<int, int>> edges_to_add;
		std::vector<HyperedgePtr> incoming_edges;

		for (const auto& [layer, data] : layers_) {
			if (layer < anchor_layer) continue;
			if (layer > read_until) break;

			// Register all nodes at this layer
			for (const auto& node : data.nodes) {
				int idx = static_cast<int>(S_.g1_nodes.size());
				S_.g1_nodes.emplace_back(node.get(), 2 * layer);
				S_.node_to_g1[node.get()] = idx;
				S_.g1_layers[2 * layer].push_back(idx);
			}

			// Register hubs caused by edges from the previous layer: sources are already
			// registered (previous iteration), targets are registered just above.
			if (!incoming_edges.empty()) {
				for (const auto& edge : incoming_edges) {
					int hub_idx = static_cast<int>(S_.g1_nodes.size());
					S_.g1_nodes.emplace_back(nullptr, 2 * layer - 1);
					S_.g1_layers[2 * layer - 1].push_back(hub_idx);

					for (const auto& src : edge->getSources())
						edges_to_add.push_back({ S_.node_to_g1[src.get()], hub_idx });

					for (const auto& tgt : edge->getTargets())
						edges_to_add.push_back({ hub_idx, S_.node_to_g1[tgt.get()] });
				}
			}

			// Only load outgoing edges that go to a layer we are still going to read.
			if (layer < read_until)
				incoming_edges = data.outgoing_edges;
			else
				incoming_edges.clear();
		}

		// Build adjacency cache
		S_.g1_in.assign(S_.g1_nodes.size(), {});
		S_.g1_out.assign(S_.g1_nodes.size(), {});
		for (const auto& e : edges_to_add) {
			S_.g1_in[e.second].push_back(e.first);
			S_.g1_out[e.first].push_back(e.second);
		}
	}

	// ── Chain detection ───────────────────────────────────────────────────────────

	bool GlobalSifter::isDummyChainStart(Node* n) {
		if (!n->isDummy()) return false;
		auto children = n->getChildren();
		if (children.size() != 1) return false;
		Node* child = children[0].get();
		if (!child->isDummy()) return false;
		auto child_parents = child->getParents();
		return child_parents.size() == 1 && child_parents[0].get() == n;
	}

	std::vector<int> GlobalSifter::collectChainG1Nodes(int start_g1, std::unordered_set<int>& visited) const {
		std::vector<int> chain;
		chain.push_back(start_g1);
		auto children = S_.g1_nodes[start_g1].original->getChildren();
		Node* current = children[0].get(); // We already know this is the only child.

		// The child might be outside our G1 window (beyond end_layer_+1) if the chain
		// was truncated; guard against a missing node_to_g1 entry.
		auto it = S_.node_to_g1.find(current);
		if (it == S_.node_to_g1.end()) return chain;
		int curr_id = it->second;

		while (true) {
			// Add the hub node between the current and the previous dummy.
			if (S_.g1_in[curr_id].empty()) break; // Hub not in G1 window.
			int hub_id = S_.g1_in[curr_id][0];
			visited.insert(hub_id);
			chain.push_back(hub_id);

			// Add the current dummy node.
			visited.insert(curr_id);
			chain.push_back(curr_id);

			// Check if we can keep extending the chain.
			if (!isDummyChainStart(current)) break;

			children = current->getChildren();
			current = children[0].get();
			auto jt = S_.node_to_g1.find(current);
			if (jt == S_.node_to_g1.end()) break; // Next node outside G1 window.
			curr_id = jt->second;
		}
		return chain;
	}

	// ── buildBlocks ───────────────────────────────────────────────────────────────

	void GlobalSifter::buildBlocks() {
		int ng_size = static_cast<int>(S_.g1_nodes.size());
		std::unordered_set<int> visited;

		for (int idx = 0; idx < ng_size; idx++) {
			if (visited.count(idx)) continue;
			const auto& node = S_.g1_nodes[idx];
			if (node.original == nullptr || !node.original->isDummy() || !isDummyChainStart(node.original)) {
				int bid = static_cast<int>(S_.blocks.size());
				S_.g1_nodes[idx].block_id = bid;
				S_.blocks.emplace_back(std::vector<int>{idx});
			}
			else {
				std::vector<int> chain = collectChainG1Nodes(idx, visited);
				int bid = static_cast<int>(S_.blocks.size());
				for (int g1_idx : chain) S_.g1_nodes[g1_idx].block_id = bid;
				S_.blocks.emplace_back(std::move(chain));
			}
		}

		S_.pi.resize(S_.blocks.size(), 0);
	}

	// ── Layer and hub sorting ─────────────────────────────────────────────────────

	std::unordered_map<Node*, int> GlobalSifter::sortLayer(
		const std::unordered_map<Node*, int>& upper_pos,
		std::vector<NodePtr>& lower,
		std::vector<HyperedgePtr>& edges)
	{
		std::unordered_map<Node*, int> left_of;
		for (const auto& n : lower)
			left_of[n.get()] = INT_MAX;

		for (const auto& edge : edges) {
			int min_src_pos = INT_MAX;
			for (const auto& src : edge->getSources())
				min_src_pos = std::min(min_src_pos, upper_pos.at(src.get()));
			for (const auto& tgt : edge->getTargets())
				left_of[tgt.get()] = std::min(left_of.at(tgt.get()), min_src_pos);
		}

		std::stable_sort(lower.begin(), lower.end(),
			[&](const NodePtr& a, const NodePtr& b) {
				return left_of.at(a.get()) < left_of.at(b.get());
			});

		std::unordered_map<Node*, int> lower_pos;
		for (int i = 0; i < static_cast<int>(lower.size()); i++)
			lower_pos[lower[i].get()] = i;

		return lower_pos;
	}

	void GlobalSifter::sortHubs(
		const std::unordered_map<Node*, int>& upper_pos,
		const std::unordered_map<Node*, int>& lower_pos,
		int layer)
	{
		std::vector<int>& hub_indices = S_.g1_layers.at(layer);

		std::sort(hub_indices.begin(), hub_indices.end(),
			[&](int idx_a, int idx_b) {
				int min_parent_a = INT_MAX;
				for (const auto& p : S_.g1_in[idx_a])
					min_parent_a = std::min(min_parent_a, upper_pos.at(S_.g1_nodes[p].original));
				int min_parent_b = INT_MAX;
				for (const auto& p : S_.g1_in[idx_b])
					min_parent_b = std::min(min_parent_b, upper_pos.at(S_.g1_nodes[p].original));
				if (min_parent_a != min_parent_b) return min_parent_a < min_parent_b;

				int min_child_a = INT_MAX;
				for (const auto& c : S_.g1_out[idx_a])
					min_child_a = std::min(min_child_a, lower_pos.at(S_.g1_nodes[c].original));
				int min_child_b = INT_MAX;
				for (const auto& c : S_.g1_out[idx_b])
					min_child_b = std::min(min_child_b, lower_pos.at(S_.g1_nodes[c].original));

				return min_child_a < min_child_b;
			});
	}

	// ── buildBlockOrder ───────────────────────────────────────────────────────────

	void GlobalSifter::buildBlockOrder(bool no_restriction) {
		int anchor_layer = std::max(0, start_layer_ - 1);
		int num_blocks = static_cast<int>(S_.blocks.size());
		B_.clear();
		B_.reserve(num_blocks);

		std::unordered_set<int> visited;
		std::unordered_map<Node*, int> upper_layer_order;
		std::unordered_map<Node*, int> lower_layer_order;

		// Seed order on the anchor layer
		int pos = 0;
		for (const auto& node : layers_.at(anchor_layer).nodes)
			upper_layer_order[node.get()] = pos++;

		// Insert anchor-layer blocks first, in their original order.
		for (const auto& node : layers_.at(anchor_layer).nodes) {
			int node_block_id = S_.g1_nodes[S_.node_to_g1.at(node.get())].block_id;
			B_.push_back(node_block_id);
			visited.insert(node_block_id);
		}

		std::vector<HyperedgePtr> incoming_edges = layers_.at(anchor_layer).outgoing_edges;
		for (auto& [layer, data] : layers_) {
			if (layer <= anchor_layer) continue;
			if (layer > end_layer_ + 1) break;

			if (no_restriction) {
				lower_layer_order = sortLayer(upper_layer_order, data.nodes, incoming_edges);
			}
			else { // We are constrained to keep the original order of the nodes in this layer.
				for (int i = 0; i < static_cast<int>(data.nodes.size()); i++)
					lower_layer_order[data.nodes[i].get()] = i;

			}
			sortHubs(upper_layer_order, lower_layer_order, 2 * layer - 1);

			for (const auto& hub_id : S_.g1_layers[2 * layer - 1]) {
				int block_id = S_.g1_nodes.at(hub_id).block_id;
				if (visited.insert(block_id).second) B_.push_back(block_id);
			}

			for (const auto& node : data.nodes) {
				int block_id = S_.g1_nodes.at(S_.node_to_g1.at(node.get())).block_id;
				if (visited.insert(block_id).second) B_.push_back(block_id);
			}

			upper_layer_order = lower_layer_order;
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

	void GlobalSifter::sortAdjacencies() {
		for (int pos = 0; pos < static_cast<int>(B_.size()); pos++)
			S_.pi[B_[pos]] = pos;

		for (auto& blk : S_.blocks) {
			int n_minus_size = static_cast<int>(S_.g1_in[blk.upper()].size());
			int n_plus_size = static_cast<int>(S_.g1_out[blk.lower()].size());
			blk.N_minus.clear(); blk.N_minus.reserve(n_minus_size);
			blk.N_plus.clear();  blk.N_plus.reserve(n_plus_size);
			blk.I_minus.assign(n_minus_size, -1);
			blk.I_plus.assign(n_plus_size, -1);
		}

		std::unordered_map<std::pair<int, int>, int, SymmetricPairHash, SymmetricPairHash> cache;

		for (int bid : B_) {
			Block& blk = S_.blocks[bid];

			for (int parent : S_.g1_in[blk.upper()]) {
				int pb_id = S_.g1_nodes[parent].block_id;
				Block& pb = S_.blocks[pb_id];
				int j = static_cast<int>(pb.N_plus.size());
				pb.N_plus.push_back(blk.upper());
				if (S_.pi[bid] < S_.pi[pb_id]) {
					cache[{bid, pb_id}] = j;
				}
				else {
					int p = cache.at({ bid, pb_id });
					pb.I_plus[j] = p;
					blk.I_minus[p] = j;
				}
			}

			for (int child : S_.g1_out[blk.lower()]) {
				int cb_id = S_.g1_nodes[child].block_id;
				Block& cb = S_.blocks[cb_id];
				int j = static_cast<int>(cb.N_minus.size());
				cb.N_minus.push_back(blk.lower());
				if (S_.pi[bid] < S_.pi[cb_id]) {
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

	int GlobalSifter::uswap(const SiftState& S,
		const std::vector<int>& Na,
		const std::vector<int>& Nb,
		const std::vector<int>& pi)
	{
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

	void GlobalSifter::updateAdjacency(SiftState& S, Block& A, Block& B,
		int a, int b, bool minus_direction)
	{
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
				Block& z = S.blocks[x_i.block_id];
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

	int GlobalSifter::getNodeAtLevel(const SiftState& S, Block& block, int level) {
		for (int g1_idx : block.g1_nodes) {
			if (S.g1_nodes[g1_idx].g1_layer == level)
				return g1_idx;
		}
		return -1;
	}

	int GlobalSifter::siftingSwap(int a_id, int b_id) {
		Block& A = S_.blocks[a_id];
		Block& B = S_.blocks[b_id];
		int delta = 0;

		int upper_a_layer = S_.g1_nodes[A.upper()].g1_layer;
		int lower_a_layer = S_.g1_nodes[A.lower()].g1_layer;
		int upper_b_layer = S_.g1_nodes[B.upper()].g1_layer;
		int lower_b_layer = S_.g1_nodes[B.lower()].g1_layer;

		std::set<std::pair<int, bool>> L;
		if (upper_a_layer >= upper_b_layer && upper_a_layer <= lower_b_layer) L.insert({ upper_a_layer, true });
		if (lower_a_layer >= upper_b_layer && lower_a_layer <= lower_b_layer) L.insert({ lower_a_layer, false });
		if (upper_b_layer >= upper_a_layer && upper_b_layer <= lower_a_layer) L.insert({ upper_b_layer, true });
		if (lower_b_layer >= upper_a_layer && lower_b_layer <= lower_a_layer) L.insert({ lower_b_layer, false });

		for (const auto& [layer, is_minus] : L) {
			int a = getNodeAtLevel(S_, A, layer);
			int b = getNodeAtLevel(S_, B, layer);
			if (is_minus) {
				std::vector<int> Na = (a == A.upper()) ? A.N_minus : S_.g1_in[a];
				std::vector<int> Nb = (b == B.upper()) ? B.N_minus : S_.g1_in[b];
				delta += uswap(S_, Na, Nb, S_.pi);
				updateAdjacency(S_, A, B, a, b, true);
			}
			else {
				std::vector<int> Na = (a == A.lower()) ? A.N_plus : S_.g1_out[a];
				std::vector<int> Nb = (b == B.lower()) ? B.N_plus : S_.g1_out[b];
				delta += uswap(S_, Na, Nb, S_.pi);
				updateAdjacency(S_, A, B, a, b, false);
			}
		}

		S_.pi[a_id]++;
		S_.pi[b_id]--;
		return delta;
	}

	// ── siftingStep ───────────────────────────────────────────────────────────────

	int GlobalSifter::siftingStep(int a_id) {
		int numblocks = static_cast<int>(B_.size());

		int current_pos = S_.pi[a_id];
		std::rotate(B_.begin() + S_.fixed_position_count,
			B_.begin() + current_pos,
			B_.begin() + current_pos + 1);
		sortAdjacencies();

		int chi = 0, chi_star = 0, p_star = S_.fixed_position_count;
		for (int p = S_.fixed_position_count + 1; p < numblocks; p++) {
			chi += siftingSwap(a_id, B_[p]);
			std::swap(B_[p - 1], B_[p]);
			if (chi < chi_star) { chi_star = chi; p_star = p; }
		}
		// a_id is now at B_.back(); rotate it to p_star
		std::rotate(B_.begin() + p_star, B_.end() - 1, B_.end());

		for (int i = 0; i < numblocks; i++)
			S_.pi[B_[i]] = i;

		return chi_star;
	}

	// ── runSifting ────────────────────────────────────────────────────────────────

	void GlobalSifter::runSifting(int sifting_rounds) {
		int numblocks = static_cast<int>(B_.size());
		for (int round = 0; round < sifting_rounds; round++) {
			BlockList snapshot = B_;
			for (int i = S_.fixed_position_count; i < numblocks; i++)
				siftingStep(snapshot[i]);
		}
	}

	// ── siftNodesSearch ──────────────────────────────────────────────────────────
	//
	// Recursive helper for siftNodes. Places the movable block at B_[movable_start]
	// by sweeping it left through consecutive fixed blocks, stopping before another
	// movable block. At each position it recurses to place the next movable block.
	// S_.blocks and S_.pi are snapshotted before each recursive call and restored
	// afterwards, giving free backtracking.

	void GlobalSifter::siftNodesSearch(
		int movable_start, int nb, int last_block,
		const std::unordered_set<int>& movable_set,
		int chi, int& best_chi, BlockList& best_B)
	{
		int pos = movable_start;
		while (pos < nb && !movable_set.count(B_[pos]))
			pos++;

		int a_id = B_[pos];

		while (true) {
			if (a_id == last_block) {
				if (chi < best_chi) {
					best_chi = chi;
					best_B = B_;
				}
			}
			else {
				auto saved_blocks = S_.blocks;
				auto saved_pi = S_.pi;
				auto saved_B = B_;

				siftNodesSearch(movable_start + 1, nb, last_block, movable_set, chi, best_chi, best_B);

				S_.blocks = std::move(saved_blocks);
				S_.pi = std::move(saved_pi);
				B_ = std::move(saved_B);
			}

			if (pos == S_.fixed_position_count) break;
			if (movable_set.count(B_[pos - 1])) break;

			chi += siftingSwap(B_[pos - 1], a_id);
			std::swap(B_[pos - 1], B_[pos]);
			pos--;
		}
	}

	// ── siftNodes ────────────────────────────────────────────────────────────────
	//
	// This function is called for new connection additions, where we want to place
	// blocks whose levels are disjoint one from each other. This way, their relative
	// order in the blocklist does not affect the crossing count, and we can freely
	// permute them among the fixed blocks to find the optimal placement.
	// 
	// Finds the optimal placement of the movable blocks (those associated with the
	// given nodes, plus any hub whose every source and target are also movable) among
	// the fixed blocks by exhaustive search over all C(nb, mb) orderings.
	//
	// Algorithm (recursive, right-biased):
	//   Start with all movable blocks pushed to the rightmost positions in B_.
	//   At each recursion level, take the leftmost remaining movable block and sweep
	//   it one step left at a time through consecutive fixed blocks, stopping before
	//   hitting another movable block. At each intermediate position recurse on the
	//   remaining movable blocks. The base case (last movable block) records the
	//   crossing count at every reachable position and tracks the global minimum.
	//
	//   Because siftingSwap mutates S_.blocks (N±/I±) and S_.pi, we snapshot those
	//   two fields before each recursive call and restore them on return, giving us
	//   free backtracking without duplicating any other state.

	void GlobalSifter::siftNodes(const std::vector<Node*>& nodes) {
		// ── Collect movable block IDs ─────────────────────────────────────────────

		std::unordered_set<int> movable_set;
		for (Node* node : nodes) {
			auto it = S_.node_to_g1.find(node);
			if (it == S_.node_to_g1.end()) continue;
			int block_id = S_.g1_nodes[it->second].block_id;
			if (S_.pi[block_id] >= S_.fixed_position_count)
				movable_set.insert(block_id);
		}
		if (movable_set.empty()) return;

		// Promote hub blocks whose every source and target are already movable.
		for (const auto& [g1_layer, layer_nodes] : S_.g1_layers) {
			for (int hub_g1 : layer_nodes) {
				if (S_.g1_nodes[hub_g1].original != nullptr) continue;
				int hub_bid = S_.g1_nodes[hub_g1].block_id;
				if (movable_set.count(hub_bid)) continue;
				if (S_.pi[hub_bid] < S_.fixed_position_count) continue;
				auto all_movable = [&](const std::vector<int>& nb) {
					for (int n : nb)
						if (!movable_set.count(S_.g1_nodes[n].block_id)) return false;
					return true;
					};
				if (all_movable(S_.g1_in[hub_g1]) || all_movable(S_.g1_out[hub_g1]))
					movable_set.insert(hub_bid);
			}
		}

		// ── Right-bias: push all movable blocks to the end of B_ ─────────────────
		//
		// Stable-partition so fixed blocks keep their relative order, then movable
		// blocks fill the tail in their current relative order.
		std::stable_partition(B_.begin() + S_.fixed_position_count, B_.end(),
			[&](int bid) { return !movable_set.count(bid); });
		sortAdjacencies();

		// ── Recursive exhaustive search ───────────────────────────────────────────
		//
		// movable: the tail of B_ that still needs to be placed, given as the index
		//          of the first movable block in B_ (they occupy [movable_start, end)).
		// chi:     cumulative crossing delta from the initial right-biased position.
		// best_chi / best_B: tracking the global minimum found so far.

		int nb = static_cast<int>(B_.size());
		int mb = static_cast<int>(movable_set.size());

		// Guard: C(nb, mb) can grow extremely fast. If the number of positions to
		// explore exceeds the threshold we skip the exhaustive search entirely and
		// run the regular global sifting algorithm instead, at the cost of altering
		// the user's mental map.
		double combinations = 1.0;
		for (int i = 0; i < mb; i++) {
			combinations *= static_cast<double>(nb - S_.fixed_position_count - i);
			combinations /= static_cast<double>(i + 1);
			if (combinations > 1e6) {
				// Treat all movable blocks as one composite block and sweep it left as a
				// unit. Each step moves the composite one position left via mb consecutive
				// siftingSwaps. Cost: O(nb * mb) instead of O(C(nb, mb)).
				int best_chi = 0;
				BlockList best_B = B_;
				int chi = 0;

				for (int k = nb - mb; k > S_.fixed_position_count; k--) {
					// Move the composite block one step left: swap each of its mb elements
					// with the fixed block that just entered on the right.
					for (int j = k; j < mb + k; j++) {
						chi += siftingSwap(B_[j - 1], B_[j]);
						std::swap(B_[j - 1], B_[j]);
					}

					if (chi < best_chi) {
						best_chi = chi;
						best_B = B_;
					}
				}

				B_ = std::move(best_B);
				sortAdjacencies();
				return;
			}
		}

		int best_chi = 0;
		BlockList best_B = B_;

		siftNodesSearch(nb - mb, nb, B_.back(), movable_set, 0, best_chi, best_B);

		// Apply the best ordering found.
		B_ = std::move(best_B);
		sortAdjacencies();
	}


	// ── writeBack ────────────────────────────────────────────────────────────────

	void GlobalSifter::writeBack() {
		for (auto& [layer, data] : layers_) {
			if (layer < start_layer_ || layer > end_layer_) continue;

			std::sort(data.nodes.begin(), data.nodes.end(),
				[&](NodePtr a, NodePtr b) {
					G1Node ga = S_.g1_nodes[S_.node_to_g1.at(a.get())];
					G1Node gb = S_.g1_nodes[S_.node_to_g1.at(b.get())];
					return S_.pi[ga.block_id] < S_.pi[gb.block_id];
				});
		}
	}

	// ── countCrossings ────────────────────────────────────────────────────────────

	int GlobalSifter::countBilayerCrossings(
		const std::vector<int>& layer1,
		const std::vector<int>& layer2,
		const std::vector<std::vector<int>>& connections_out)
	{
		if (layer1.empty() || layer2.empty()) return 0;

		std::unordered_map<int, int> pos1, pos2;
		int p = static_cast<int>(layer1.size());
		int q = static_cast<int>(layer2.size());
		pos1.reserve(p);
		pos2.reserve(q);
		for (int i = 0; i < p; ++i) pos1[layer1[i]] = i;
		for (int j = 0; j < q; ++j) pos2[layer2[j]] = j;

		std::vector<std::pair<int, int>> edges;
		for (int u : layer1) {
			int nu = pos1.at(u);
			for (int v : connections_out[u])
				edges.push_back({ nu, pos2.at(v) });
		}
		std::sort(edges.begin(), edges.end());

		std::vector<int> pi;
		pi.reserve(edges.size());
		for (auto& [n, s] : edges)
			pi.push_back(s);

		int firstindex = 1;
		while (firstindex < q) firstindex *= 2;
		int treesize = 2 * firstindex - 1;
		int leafOffset = firstindex - 1;

		std::vector<int> tree(treesize, 0);
		int crosscount = 0;
		for (int southPos : pi) {
			int index = southPos + leafOffset;
			tree[index]++;
			while (index > 0) {
				if (index % 2 == 1) crosscount += tree[index + 1];
				index = (index - 1) / 2;
				tree[index]++;
			}
		}
		return crosscount;
	}

	void GlobalSifter::orderLayersByBlockOrder() {
		for (int pos = 0; pos < static_cast<int>(B_.size()); pos++)
			S_.pi[B_[pos]] = pos;

		for (auto& [layer, nodes] : S_.g1_layers) {
			std::sort(nodes.begin(), nodes.end(),
				[&](int idx_a, int idx_b) {
					return S_.pi[S_.g1_nodes[idx_a].block_id] <
						S_.pi[S_.g1_nodes[idx_b].block_id];
				});
		}
	}

	int GlobalSifter::countCrossings() {
		orderLayersByBlockOrder();

		int total = 0;
		auto it = S_.g1_layers.begin();
		auto prev = it++;
		for (; it != S_.g1_layers.end(); ++prev, ++it)
			total += countBilayerCrossings(prev->second, it->second, S_.g1_out);
		return total;
	}

} // namespace sifting_internal

// ============================================================================
// GraphicalHypergraph
// ============================================================================
namespace hypergraph_logic {

	using namespace sifting_internal;

	// ── minimizeCrossings ────────────────────────────────────────────────────────
	//
	// Runs the global sifting algorithm over [start_layer, last_layer], writing
	// the optimised order back to LayerData::nodes and returning the crossing count.
	int Hypergraph::minimizeCrossings(int sifting_rounds, int start_layer) {
		if (getLayers().empty()) return 0;
		int last_layer = static_cast<int>(layers_.rbegin()->first);
		GlobalSifter sifter(start_layer, last_layer, layers_, false);
		if (sifter.countCrossings() == 0) return 0; // No need to sift if we are already optimal.
		sifter.runSifting(sifting_rounds);
		sifter.writeBack();
		return sifter.countCrossings();
	}

	// ── minimizeCrossingsForNodes ────────────────────────────────────────────────
	//
	// Builds G1 over [start_layer, end_layer] (plus the one layer below end_layer
	// for crossing information), then sifts only the blocks that contain the given
	// nodes to find their locally best positions within the current ordering.
	// The result is written back to LayerData::nodes.

	int Hypergraph::minimizeCrossingsForNodes(
		const std::vector<Node*>& nodes,
		int start_layer,
		int end_layer)
	{
		if (getLayers().empty() || nodes.empty()) return 0;

		GlobalSifter sifter(start_layer, end_layer, layers_, false);
		if (sifter.countCrossings() == 0) return 0; // No need to sift if we are already optimal.
		sifter.siftNodes(nodes);
		sifter.writeBack();
		return sifter.countCrossings();
	}

} // namespace hypergraph_logic