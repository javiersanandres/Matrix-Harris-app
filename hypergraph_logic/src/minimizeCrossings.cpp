#include "GlobalSifting.h"
#include <algorithm>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <map>
#include <vector>
#include <stdexcept>
#include <climits>
#include <limits>
#include <queue>

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
		std::map<int, LayerData>& layers, bool order, const std::vector<Node*>& moved, std::unordered_set<Node*> group,
		const std::vector<EquivalenceChain>& equivalences)
		: start_layer_(start_layer)
		, end_layer_(end_layer)
		, layers_(layers)
		, group_(std::move(group))
	{
		buildG1();
		buildBlocks();
		buildChains(equivalences);
		buildBlockOrder(moved);
		if (order) runEfficientBarycenter();
		sortAdjacencies();
		initial_pi_ = S_.pi;
	}

	std::vector<NodePtr> GlobalSifter::groupRow(const LayerData& data, std::vector<int>* slots) const {
		std::vector<NodePtr> row;
		for (int i = 0; i < static_cast<int>(data.nodes.size()); i++) {
			if (!inGroup(data.nodes[i].get())) continue;
			row.push_back(data.nodes[i]);
			if (slots) slots->push_back(i);
		}
		return row;
	}

	// ── buildG1 ───────────────────────────────────────────────────────────────────

	void GlobalSifter::buildG1() {
		int anchor_layer = std::max(0, start_layer_ - 1);

		// We also include the layer just below end_layer_ (if any) because the edges
		// between end_layer_ and end_layer_+1 carry crossing information needed to
		// evaluate swaps that touch end_layer_.
		int read_until = (layers_.count(end_layer_ + 1) > 0) ? end_layer_ + 1 : end_layer_;

		std::vector<std::pair<int, int>> edges_to_add;
		std::vector<HyperedgePtr> incoming_edges;

		for (const auto& [layer, data] : layers_) {
			if (layer < anchor_layer) continue;
			if (layer > read_until) break;

			// Register all nodes at this layer (those of the group, when there is one)
			for (const auto& node : groupRow(data)) {
				int idx = static_cast<int>(S_.g1_nodes.size());
				S_.g1_nodes.emplace_back(node.get(), 2 * layer);
				S_.node_to_g1[node.get()] = idx;
				S_.g1_layers[2 * layer].push_back(idx);
			}

			// Register hubs caused by edges from the previous layer: sources are already
			// registered (previous iteration), targets are registered just above.
			if (!incoming_edges.empty()) {
				for (const auto& edge : incoming_edges) {
					// The group is closed, so an edge is either wholly in it or wholly out.
					if (!inGroup(edge->getSources().front().get())) continue;
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

		// Blocks reaching the anchor layer (only when there is one, i.e. start_layer_ >= 1)
		// or end_layer_+1 (only when it was read) are never sifted.
		const int fixed_top = start_layer_ >= 1 ? 2 * (start_layer_ - 1) : INT_MIN;
		const int fixed_bottom = layers_.count(end_layer_ + 1) > 0 ? 2 * (end_layer_ + 1) : INT_MAX;
		for (auto& blk : S_.blocks) {
			for (int g1_idx : blk.g1_nodes) {
				const int row = S_.g1_nodes[g1_idx].g1_layer;
				if (row == fixed_top || row == fixed_bottom) { blk.movable = false; break; }
			}
		}

		S_.pi.resize(S_.blocks.size(), 0);
	}

	// ── Equivalence chains ────────────────────────────────────────────────────────

	void GlobalSifter::buildChains(const std::vector<EquivalenceChain>& equivalences) {
		chain_of_.assign(S_.blocks.size(), -1);
		for (const auto& equivalence : equivalences) {
			Chain chain;
			bool here = equivalence.members.size() >= 2;
			for (const auto& m : equivalence.members) {
				auto it = S_.node_to_g1.find(m.get());
				if (it == S_.node_to_g1.end()) { here = false; break; }
				const G1Node& g1 = S_.g1_nodes[it->second];
				if (chain.row >= 0 && g1.g1_layer != chain.row) { here = false; break; }
				chain.row = g1.g1_layer;
				chain.blocks.push_back(g1.block_id);
			}
			if (!here) continue;

			const int c = static_cast<int>(chains_.size());
			for (int bid : chain.blocks) chain_of_[bid] = c;
			for (const auto& m : equivalence.members) chain_of_node_[m.get()] = c;
			chains_at_row_[chain.row].push_back(c);
			chains_.push_back(std::move(chain));
		}
	}

	std::vector<int> GlobalSifter::orderedMembers(int c) const {
		std::vector<int> members = chains_[c].blocks;
		std::sort(members.begin(), members.end(), [&](int a, int b) { return S_.pi[a] < S_.pi[b]; });
		return members;
	}

	int GlobalSifter::equivalenceCrossings() const {
		int total = 0;
		for (const auto& [row, chain_ids] : chains_at_row_) {
			const auto& ids = S_.g1_layers.at(row);
			for (int c : chain_ids) {
				int lo = INT_MAX, hi = -1;
				for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
					if (chain_of_[S_.g1_nodes[ids[i]].block_id] != c) continue;
					lo = std::min(lo, i);
					hi = std::max(hi, i);
				}
				for (int i = lo + 1; i < hi; ++i)
					if (S_.g1_nodes[ids[i]].original->isDummy()) ++total;
			}
		}
		return total;
	}

	void GlobalSifter::gatherChains(bool keep_member_order) {
		if (chains_.empty()) return;
		orderLayersByBlockOrder();

		for (const auto& [row, chain_ids] : chains_at_row_) {
			const auto& ids = S_.g1_layers.at(row);
			auto isReal = [&](int g1) { return !S_.g1_nodes[g1].original->isDummy(); };
			auto chainAt = [&](int i) { return chain_of_[S_.g1_nodes[ids[i]].block_id]; };

			// Every chain's span in the row, and whether a real node of another is inside it.
			std::map<int, std::pair<int, int>> spans;
			for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
				const int c = chainAt(i);
				if (c < 0) continue;
				auto [it, fresh] = spans.try_emplace(c, i, i);
				if (!fresh) it->second.second = i;
			}
			// Where each member stands in its chain's stored order.
			auto storedIndex = [&](int i) {
				const auto& blocks = chains_[chainAt(i)].blocks;
				return static_cast<int>(std::find(blocks.begin(), blocks.end(), S_.g1_nodes[ids[i]].block_id) - blocks.begin());
			};
			bool broken = false;
			for (const auto& [c, span] : spans)
				for (int i = span.first + 1; i < span.second && !broken; ++i)
					broken = isReal(ids[i]) && chainAt(i) != c;
			if (keep_member_order && !broken) {
				std::map<int, int> last_index;  // chain -> stored index of its last member seen
				for (int i = 0; i < static_cast<int>(ids.size()) && !broken; ++i) {
					if (chainAt(i) < 0) continue;
					auto [it, fresh] = last_index.try_emplace(chainAt(i), storedIndex(i));
					if (!fresh) { broken = storedIndex(i) < it->second; it->second = storedIndex(i); }
				}
			}
			if (!broken) continue;

			// As Hypergraph::keepEquivalencesTogether: every chain is one unit at the middle of
			// its span, in its order; every other node, a unit at its own position, except
			// dummies within a span, which stay in the gap they were in.
			struct Unit {
				double key;
				std::vector<int> nodes;
			};
			std::vector<Unit> units;
			std::map<int, size_t> unit_of_chain;
			std::map<int, std::vector<int>> members;  // chain -> positions of its members
			for (const auto& [c, span] : spans) {
				unit_of_chain[c] = units.size();
				units.push_back({ (span.first + span.second) / 2.0 + 0.25, {} });
			}
			std::vector<std::vector<int>> after(ids.size());  // dummies kept right after a member
			for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
				const int c = chainAt(i);
				if (c >= 0) { members[c].push_back(i); continue; }
				bool kept_in_gap = false;
				if (!isReal(ids[i])) {
					for (const auto& [d, span] : spans) {
						if (i <= span.first || i >= span.second) continue;
						int last_member = span.first;
						for (int j = span.first; j < i; ++j)
							if (chainAt(j) == d) last_member = j;
						after[last_member].push_back(i);
						kept_in_gap = true;
						break;
					}
				}
				if (!kept_in_gap) units.push_back({ static_cast<double>(i), { i } });
			}
			for (const auto& [c, positions] : members) {
				auto& nodes = units[unit_of_chain[c]].nodes;
				// The members in their stored order when it is kept; the dummies of each gap
				// stay in that gap whichever members end up around it.
				std::vector<int> in_order = positions;
				if (keep_member_order)
					std::sort(in_order.begin(), in_order.end(), [&](int a, int b) { return storedIndex(a) < storedIndex(b); });
				for (size_t k = 0; k < positions.size(); ++k) {
					nodes.push_back(in_order[k]);
					if (k + 1 < positions.size())
						nodes.insert(nodes.end(), after[positions[k]].begin(), after[positions[k]].end());
				}
			}
			std::stable_sort(units.begin(), units.end(), [](const Unit& a, const Unit& b) { return a.key < b.key; });
			std::vector<int> ordered;  // positions in ids, in their new order
			for (const auto& u : units) ordered.insert(ordered.end(), u.nodes.begin(), u.nodes.end());

			// Into B. The dummies keep their relative order and their places in B; the real
			// nodes, blocks of their own that only show in this row, go right after the dummy
			// before them in the new order (those before every dummy, right before the first).
			std::unordered_set<int> real_blocks;
			for (int g1 : ids) if (isReal(g1)) real_blocks.insert(S_.g1_nodes[g1].block_id);
			std::vector<int> leading;
			std::unordered_map<int, std::vector<int>> trailing;  // dummy's block -> real blocks after it
			int previous_dummy = -1;
			for (int i : ordered) {
				const int bid = S_.g1_nodes[ids[i]].block_id;
				if (!isReal(ids[i])) previous_dummy = bid;
				else if (previous_dummy < 0) leading.push_back(bid);
				else trailing[previous_dummy].push_back(bid);
			}
			const bool any_dummy = previous_dummy >= 0;
			BlockList rebuilt;
			rebuilt.reserve(B_.size());
			bool leading_placed = false;
			auto placeLeading = [&] {
				rebuilt.insert(rebuilt.end(), leading.begin(), leading.end());
				leading_placed = true;
			};
			for (int bid : B_) {
				if (real_blocks.count(bid)) {
					// With no dummy in the row, the real nodes go where the first of them was.
					if (!any_dummy && !leading_placed) placeLeading();
					continue;
				}
				if (any_dummy && !leading_placed && getNodeAtLevel(S_, S_.blocks[bid], row) >= 0)
					placeLeading();
				rebuilt.push_back(bid);
				if (auto it = trailing.find(bid); it != trailing.end())
					rebuilt.insert(rebuilt.end(), it->second.begin(), it->second.end());
			}
			if (!leading_placed) rebuilt.insert(rebuilt.end(), leading.begin(), leading.end());
			B_ = std::move(rebuilt);
			orderLayersByBlockOrder();
		}
	}

	// ── Layer and hub sorting ─────────────────────────────────────────────────────

	void GlobalSifter::followMovedNode(
		const std::unordered_map<Node*, int>& upper_pos,
		std::unordered_set<Node*>& followers,
		std::vector<NodePtr>& lower) const
	{
		const int n = static_cast<int>(lower.size());
		std::vector<double> key(n, 0.0);
		std::vector<char> follows(n, 0);
		double previous_key = -std::numeric_limits<double>::infinity(); // a root at the very left keeps its place
		for (int i = 0; i < n; i++) {
			// Every node right above, through the hubs (so the dummies of a long connection
			// count too), weighs once; the moved node and its descendants weigh more.
			double sum = 0.0, weight = 0.0;
			for (int hub : S_.g1_in[S_.node_to_g1.at(lower[i].get())]) {
				for (int u : S_.g1_in[hub]) {
					Node* parent = S_.g1_nodes[u].original;
					bool follower = followers.count(parent) > 0;
					const double w = follower ? FOLLOW_WEIGHT : 1.0;
					follows[i] |= follower;
					sum += w * upper_pos.at(parent);
					weight += w;
				}
			}
			key[i] = weight > 0.0 ? sum / weight : previous_key; // root: stick to the left-hand neighbour
			previous_key = key[i];
		}

		// A chain is one unit: it follows if any member does, at its members' average key,
		// in its own order (ties keep the order the layer had).
		std::unordered_map<int, std::vector<int>> chain_members;
		for (int i = 0; i < n; i++)
			if (auto it = chain_of_node_.find(lower[i].get()); it != chain_of_node_.end())
				chain_members[it->second].push_back(i);
		for (const auto& [c, positions] : chain_members) {
			double sum = 0.0;
			char follower = 0;
			for (int i : positions) { sum += key[i]; follower |= follows[i]; }
			for (int i : positions) { key[i] = sum / static_cast<double>(positions.size()); follows[i] = follower; }
		}

		std::vector<int> moving, staying;
		for (int i = 0; i < n; i++) (follows[i] ? moving : staying).push_back(i);
		if (moving.empty()) return;

		// The descendants are sorted among themselves; the rest keep their relative order,
		// and the two sequences are merged by key (ties: the order the layer had).
		std::stable_sort(moving.begin(), moving.end(), [&](int a, int b) { return key[a] < key[b]; });
		auto before = [&](int a, int b) { return key[a] < key[b] || (key[a] == key[b] && a < b); };
		std::vector<NodePtr> merged;
		merged.reserve(n);
		size_t m = 0, s = 0;
		while (m < moving.size() || s < staying.size()) {
			const bool take_moving = s == staying.size()
				|| (m < moving.size() && before(moving[m], staying[s]));
			merged.push_back(lower[take_moving ? moving[m++] : staying[s++]]);
		}
		for (int i : moving) followers.insert(lower[i].get());
		lower = std::move(merged);
	}

	void GlobalSifter::sortHubs(
		const std::unordered_map<Node*, int>& upper_pos,
		const std::unordered_map<Node*, int>& lower_pos,
		int layer)
	{
		auto row = S_.g1_layers.find(layer);
		if (row == S_.g1_layers.end()) return;
		std::vector<int>& hub_indices = row->second;

		// (barycenter, leftmost parent, leftmost child) for every hub. Positions are
		// normalized by row size so both rows weigh the same, and every endpoint counts
		// once: a side with more edges crosses more, so it pulls harder.
		struct HubKey { double bary; int min_parent; int min_child; };
		const double upper_size = static_cast<double>(std::max<size_t>(upper_pos.size(), 1));
		const double lower_size = static_cast<double>(std::max<size_t>(lower_pos.size(), 1));
		std::unordered_map<int, HubKey> keys;
		for (int hub : hub_indices) {
			HubKey key{ 0.0, INT_MAX, INT_MAX };
			double sum = 0.0;
			int count = 0;
			for (int p : S_.g1_in[hub]) {
				const int pos = upper_pos.at(S_.g1_nodes[p].original);
				key.min_parent = std::min(key.min_parent, pos);
				sum += (pos + 0.5) / upper_size;
				++count;
			}
			for (int c : S_.g1_out[hub]) {
				const int pos = lower_pos.at(S_.g1_nodes[c].original);
				key.min_child = std::min(key.min_child, pos);
				sum += (pos + 0.5) / lower_size;
				++count;
			}
			key.bary = count > 0 ? sum / count : 0.0;
			keys[hub] = key;
		}

		std::sort(hub_indices.begin(), hub_indices.end(),
			[&](int idx_a, int idx_b) {
				const HubKey& a = keys.at(idx_a);
				const HubKey& b = keys.at(idx_b);
				if (a.bary != b.bary) return a.bary < b.bary;
				if (a.min_parent != b.min_parent) return a.min_parent < b.min_parent;
				return a.min_child < b.min_child;
			});
	}

	// ── buildBlockOrder ───────────────────────────────────────────────────────────

	void GlobalSifter::buildBlockOrder(const std::vector<Node*>& moved) {
		const int anchor_layer = std::max(0, start_layer_ - 1);
		const int read_until = (layers_.count(end_layer_ + 1) > 0) ? end_layer_ + 1 : end_layer_;

		// The moved nodes and, as they are found layer by layer, their descendants.
		std::unordered_set<Node*> followers(moved.begin(), moved.end());

		// 1. Rows: every node row takes its layer's order (with the moved node's descendants
		//    re-placed first when asked, only inside the range), and every hub row is sorted
		//    from its neighbours.
		std::unordered_map<Node*, int> upper_pos;
		for (auto& [layer, data] : layers_) {
			if (layer < anchor_layer) continue;
			if (layer > read_until) break;

			// Positions are counted within the group: the other nodes are not in G1.
			std::vector<int> slots;
			std::vector<NodePtr> nodes = groupRow(data, &slots);
			if (!moved.empty() && layer > anchor_layer && layer <= end_layer_) {
				followMovedNode(upper_pos, followers, nodes);
				for (size_t i = 0; i < slots.size(); i++) data.nodes[slots[i]] = nodes[i];
			}

			std::unordered_map<Node*, int> lower_pos;
			for (int i = 0; i < static_cast<int>(nodes.size()); i++)
				lower_pos[nodes[i].get()] = i;

			if (auto row = S_.g1_layers.find(2 * layer); row != S_.g1_layers.end()) {
				row->second.clear();
				for (const auto& node : nodes)
					row->second.push_back(S_.node_to_g1.at(node.get()));
			}
			if (layer > anchor_layer)
				sortHubs(upper_pos, lower_pos, 2 * layer - 1);

			upper_pos = std::move(lower_pos);
		}

		// 2. B from the rows. The chains keep the order they have: only the global
		//    minimization reorders their members, and it does so on purpose afterwards.
		buildBlockListFromRows();
		gatherChains(true);
	}

	// ── buildBlockListFromRows ────────────────────────────────────────────────────

	void GlobalSifter::buildBlockListFromRows() {
		// Topological sort of "left neighbour's block before right neighbour's block"
		// over every row, ties broken by (top row, position in it).
		const int num_blocks = static_cast<int>(S_.blocks.size());
		std::vector<std::pair<int, int>> key(num_blocks, { INT_MAX, INT_MAX });
		std::vector<std::vector<int>> successors(num_blocks);
		std::vector<int> pending(num_blocks, 0);
		for (const auto& [row, ids] : S_.g1_layers) {
			for (int i = 0; i < static_cast<int>(ids.size()); i++) {
				const int bid = S_.g1_nodes[ids[i]].block_id;
				key[bid] = std::min(key[bid], std::pair<int, int>{ row, i });
				if (i > 0) {
					successors[S_.g1_nodes[ids[i - 1]].block_id].push_back(bid);
					pending[bid]++;
				}
			}
		}

		using Entry = std::pair<std::pair<int, int>, int>; // (key, block)
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> ready;
		for (int bid = 0; bid < num_blocks; bid++)
			if (pending[bid] == 0) ready.push({ key[bid], bid });

		std::vector<char> placed(num_blocks, 0);
		B_.clear();
		B_.reserve(num_blocks);
		while (static_cast<int>(B_.size()) < num_blocks) {
			if (ready.empty()) {
				// Contradicting rows (a cycle): take the stuck block with the smallest key.
				int best = -1;
				for (int bid = 0; bid < num_blocks; bid++)
					if (!placed[bid] && (best < 0 || key[bid] < key[best])) best = bid;
				ready.push({ key[best], best });
			}
			const int bid = ready.top().second;
			ready.pop();
			if (placed[bid]) continue;
			placed[bid] = 1;
			B_.push_back(bid);
			for (int next : successors[bid])
				if (!placed[next] && --pending[next] == 0) ready.push({ key[next], next });
		}
	}

	
	// ── runEfficientBarycenter ────────────────────────────────────────────────────

	void GlobalSifter::runEfficientBarycenter(int max_iterations) {
		if (B_.empty()) return;

		// The order we were given is the reference: the result is only kept if it is better.
		const BlockList original = B_;
		const int original_crossings = countCrossings(); // also lays every row out in B's order

		// Normalized position of every G1 node in its row, in (0, 1), so that positions
		// taken from rows of different sizes can be averaged together.
		std::vector<double> x(S_.g1_nodes.size(), 0.0);
		auto locate = [&](const std::vector<int>& ids) {
			const double size = static_cast<double>(ids.size());
			for (int i = 0; i < static_cast<int>(ids.size()); i++)
				x[ids[i]] = (i + 0.5) / size;
		};
		for (const auto& [row, ids] : S_.g1_layers) locate(ids);

		auto mean = [&](const std::vector<int>& ids) -> std::optional<double> {
			if (ids.empty()) return std::nullopt;
			double sum = 0.0;
			for (int id : ids) sum += x[id];
			return sum / static_cast<double>(ids.size());
		};

		for (int iter = 0; iter < max_iterations; iter++) {
			bool changed = false;

			for (auto& [row, ids] : S_.g1_layers) {
				// Every node present in the row takes part, dummies of chains passing through
				// included (their neighbours are the chain's own nodes, which keeps it straight).
				// Nodes of blocks that are not movable keep their place.
				std::vector<int> slots;
				for (int i = 0; i < static_cast<int>(ids.size()); i++)
					if (S_.blocks[S_.g1_nodes[ids[i]].block_id].movable) slots.push_back(i);

				const int size = static_cast<int>(slots.size());
				if (size < 2) continue;

				auto barycenter = [&](const std::vector<int>& up_ids, const std::vector<int>& down_ids, double own_x) {
					const auto up = mean(up_ids);
					const auto down = mean(down_ids);
					if (up && down) return (*up + *down) / 2.0;
					if (up)         return *up;
					if (down)       return *down;
					return own_x; // isolated: don't move it
				};
				std::vector<double> bary(size);
				for (int k = 0; k < size; k++) {
					const int g1_idx = ids[slots[k]];
					bary[k] = barycenter(S_.g1_in[g1_idx], S_.g1_out[g1_idx], x[g1_idx]);
				}

				// A chain is sorted as one unit, by all its members' neighbours together; its
				// members then by their own barycenters, within the places it takes.
				std::vector<double> unit_bary = bary;
				std::vector<int> unit(size, -1);
				if (auto chains = chains_at_row_.find(row); chains != chains_at_row_.end()) {
					for (int c : chains->second) {
						std::vector<int> members, up_ids, down_ids;
						double x_sum = 0.0;
						for (int k = 0; k < size; k++) {
							const int g1_idx = ids[slots[k]];
							if (chain_of_[S_.g1_nodes[g1_idx].block_id] != c) continue;
							members.push_back(k);
							up_ids.insert(up_ids.end(), S_.g1_in[g1_idx].begin(), S_.g1_in[g1_idx].end());
							down_ids.insert(down_ids.end(), S_.g1_out[g1_idx].begin(), S_.g1_out[g1_idx].end());
							x_sum += x[g1_idx];
						}
						if (members.empty()) continue;
						const double b = barycenter(up_ids, down_ids, x_sum / static_cast<double>(members.size()));
						for (int k : members) { unit_bary[k] = b; unit[k] = c; }
					}
				}

				std::vector<int> order(size);
				std::iota(order.begin(), order.end(), 0);
				std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
					if (unit_bary[a] != unit_bary[b]) return unit_bary[a] < unit_bary[b];
					if (unit[a] != unit[b]) return unit[a] < unit[b]; // a node before a chain at the same place
					return unit[a] >= 0 && bary[a] < bary[b];
				});

				for (int k = 0; k < size; k++) {
					if (order[k] != k) changed = true;
				}

				std::vector<int> sorted(size);
				for (int k = 0; k < size; k++)
					sorted[k] = ids[slots[order[k]]];
				for (int k = 0; k < size; k++)
					ids[slots[k]] = sorted[k];
				locate(ids);
			}

			if (!changed) break;
		}

		// Rows sorted on their own may disagree about a chain; the merge settles it. The
		// barycenter has just sorted the members on purpose: their row order stays.
		buildBlockListFromRows();
		gatherChains(false);
		if (countCrossings() > original_crossings) {
			B_ = original;
			orderLayersByBlockOrder();
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

	int GlobalSifter::siftingStep(int a_id, bool stability) {
		int numblocks = static_cast<int>(B_.size());

		// No snapshot of where sifting started (a sifter set up by hand): from here, then.
		if (initial_pi_.size() != S_.pi.size()) initial_pi_ = S_.pi;

		int current_pos = S_.pi[a_id];
		std::rotate(B_.begin(), B_.begin() + current_pos, B_.begin() + current_pos + 1);
		sortAdjacencies();

		// Whether a block shares a row with A, so that their relative order shows.
		const Block& A = S_.blocks[a_id];
		const int a_top = S_.g1_nodes[A.upper()].g1_layer, a_bottom = S_.g1_nodes[A.lower()].g1_layer;
		auto sharesRow = [&](int bid) {
			const Block& b = S_.blocks[bid];
			return S_.g1_nodes[b.upper()].g1_layer <= a_bottom && a_top <= S_.g1_nodes[b.lower()].g1_layer;
		};
		auto wasBefore = [&](int bid) { return initial_pi_[bid] < initial_pi_[a_id]; };

		// disorder: blocks sharing a row with A that are on the other side of A than they
		// were when sifting started. At the front, those that started before A.
		int disorder = 0;
		for (int p = 1; p < numblocks; p++)
			if (sharesRow(B_[p]) && wasBefore(B_[p])) ++disorder;

		// At the front, every chain is to A's right.
		std::vector<ChainWatch> watch = watchChains({ a_id }, false);
		int blocked = 0;

		int chi = 0, chi_star = 0, p_star = 0, disorder_star = disorder;
		for (int p = 1; p < numblocks; p++) {
			chi += siftingSwap(a_id, B_[p]);
			chi += passMember(watch, B_[p], +1, blocked);
			if (sharesRow(B_[p])) disorder += wasBefore(B_[p]) ? -1 : 1;
			std::swap(B_[p - 1], B_[p]);
			if (blocked == 0 && (chi < chi_star || (chi == chi_star && (stability ? disorder < disorder_star : true)))) {
				chi_star = chi; p_star = p; disorder_star = disorder;
			}
		}
		// a_id is now at B_.back(); rotate it to p_star
		std::rotate(B_.begin() + p_star, B_.end() - 1, B_.end());

		for (int i = 0; i < numblocks; i++)
			S_.pi[B_[i]] = i;

		return chi_star;
	}

	// ── Chains while sifting ──────────────────────────────────────────────────────

	std::vector<GlobalSifter::ChainWatch> GlobalSifter::watchChains(const std::vector<int>& moving, bool members_to_the_left) const {
		std::vector<ChainWatch> watch;
		if (chains_.empty()) return watch;
		const std::unordered_set<int> moving_set(moving.begin(), moving.end());
		for (int bid : moving) {
			for (int g1 : S_.blocks[bid].g1_nodes) {
				auto chains = chains_at_row_.find(S_.g1_nodes[g1].g1_layer);
				if (chains == chains_at_row_.end()) continue;
				for (int c : chains->second) {
					if (moving_set.count(chains_[c].blocks.front())) continue; // its own chain
					auto w = std::find_if(watch.begin(), watch.end(), [&](const ChainWatch& x) { return x.chain == c; });
					if (w == watch.end()) {
						const int left = members_to_the_left ? static_cast<int>(chains_[c].blocks.size()) : 0;
						watch.push_back({ c, 0, 0, left });
						w = watch.end() - 1;
					}
					(S_.g1_nodes[g1].original->isDummy() ? w->dummies : w->reals)++;
				}
			}
		}
		return watch;
	}

	int GlobalSifter::passMember(std::vector<ChainWatch>& watch, int member, int direction, int& blocked) const {
		const int c = chainOf(member);
		if (c < 0) return 0;
		auto w = std::find_if(watch.begin(), watch.end(), [&](const ChainWatch& x) { return x.chain == c; });
		if (w == watch.end()) return 0;
		const int size = static_cast<int>(chains_[c].blocks.size());
		const int was_inside = w->left > 0 && w->left < size;
		w->left += direction;
		const int inside = (w->left > 0 && w->left < size) - was_inside;
		blocked += w->reals * inside;
		return w->dummies * inside;
	}

	bool GlobalSifter::dummyFitsBetween(int left, int right, int x) {
		const int c = chainOf(left);
		if (c < 0 || chainOf(right) != c) return false;
		const int g1 = getNodeAtLevel(S_, S_.blocks[x], chains_[c].row);
		return g1 >= 0 && S_.g1_nodes[g1].original && S_.g1_nodes[g1].original->isDummy();
	}

	int GlobalSifter::crossingsAround(int row) {
		auto sorted = [&](int r) -> const std::vector<int>* {
			auto it = S_.g1_layers.find(r);
			if (it == S_.g1_layers.end()) return nullptr;
			std::sort(it->second.begin(), it->second.end(), [&](int a, int b) {
				return S_.pi[S_.g1_nodes[a].block_id] < S_.pi[S_.g1_nodes[b].block_id];
			});
			return &it->second;
		};
		const auto* above = sorted(row - 1);
		const auto* here = sorted(row);
		const auto* below = sorted(row + 1);
		int total = 0;
		if (above && here) total += countBilayerCrossings(*above, *here, S_.g1_out);
		if (here && below) total += countBilayerCrossings(*here, *below, S_.g1_out);
		return total;
	}

	// ── chainStep ─────────────────────────────────────────────────────────────────

	void GlobalSifter::chainStep(int c, bool stability, bool reorder) {
		if (initial_pi_.size() != S_.pi.size()) initial_pi_ = S_.pi;
		const BlockList before_B = B_;
		const int before = countCrossings(); // also sets pi from B

		// The members to the front, side by side and in their order.
		const std::vector<int> members = orderedMembers(c);
		const int k = static_cast<int>(members.size());
		const int numblocks = static_cast<int>(B_.size());
		const std::unordered_set<int> member_set(members.begin(), members.end());
		std::stable_partition(B_.begin(), B_.end(), [&](int bid) { return member_set.count(bid) > 0; });
		sortAdjacencies();

		// disorder, as in siftingStep, over every member: blocks of the chain's row on the
		// other side of a member than they were when sifting started.
		const int row = chains_[c].row;
		auto sharesRow = [&](int bid) { return getNodeAtLevel(S_, S_.blocks[bid], row) >= 0; };
		auto wasBefore = [&](int bid, int m) { return initial_pi_[bid] < initial_pi_[m]; };
		int disorder = 0;
		for (int p = k; p < numblocks; p++)
			if (sharesRow(B_[p]))
				for (int m : members) disorder += wasBefore(B_[p], m);

		std::vector<ChainWatch> watch = watchChains(members, false);
		int blocked = 0;
		// split_star: how many members stay left of the block before p_star, a dummy of the
		// chain's row stopped between them (0: none).
		int chi = 0, chi_star = 0, p_star = 0, split_star = 0, disorder_star = disorder;
		auto better = [&](int at_chi) {
			return blocked == 0 && (at_chi < chi_star || (at_chi == chi_star && (stability ? disorder < disorder_star : true)));
		};
		for (int p = k; p < numblocks; p++) {
			// The next block passes the whole chain, right to left. A dummy of the chain's
			// row may stop between two members, crossing their line.
			const int x = B_[p];
			const bool in_row = sharesRow(x);
			for (int j = p - 1; j >= p - k; j--) {
				chi += siftingSwap(B_[j], x);
				if (in_row) disorder += wasBefore(x, B_[j]) ? -1 : 1;
				std::swap(B_[j], B_[j + 1]);
				if (j > 0 && dummyFitsBetween(B_[j - 1], B_[j + 1], x) && better(chi + 1)) {
					chi_star = chi + 1; p_star = p - k + 1; split_star = j - (p - k); disorder_star = disorder;
				}
			}
			chi += passMember(watch, x, +1, blocked);
			if (better(chi)) {
				chi_star = chi; p_star = p - k + 1; split_star = 0; disorder_star = disorder;
			}
		}
		// The chain is now at the back; to p_star, and the block before it into its gap.
		std::rotate(B_.begin() + p_star, B_.end() - k, B_.end());
		if (split_star > 0)
			std::rotate(B_.begin() + p_star - 1, B_.begin() + p_star, B_.begin() + p_star + split_star);
		for (int i = 0; i < numblocks; i++) S_.pi[B_[i]] = i;

		if (reorder) reorderMembers(c);

		const int after = countCrossings();
		if (after > before || (stability && after == before)) {
			B_ = before_B;
			orderLayersByBlockOrder();
		}
	}

	void GlobalSifter::reorderMembers(int c) {
		std::vector<int> order = orderedMembers(c);
		std::vector<int> places;
		for (int m : order) places.push_back(S_.pi[m]);
		auto apply = [&](const std::vector<int>& o) {
			for (size_t i = 0; i < o.size(); i++) { B_[places[i]] = o[i]; S_.pi[o[i]] = places[i]; }
		};

		const int row = chains_[c].row;
		int best = crossingsAround(row);
		const std::vector<int> members = order;
		for (int m : members) {
			std::vector<int> rest = order;
			rest.erase(std::find(rest.begin(), rest.end(), m));
			std::vector<int> best_order = order;
			for (size_t t = 0; t <= rest.size(); t++) {
				std::vector<int> candidate = rest;
				candidate.insert(candidate.begin() + static_cast<std::ptrdiff_t>(t), m);
				if (candidate == order) continue;
				apply(candidate);
				const int crossings = crossingsAround(row);
				if (crossings < best) { best = crossings; best_order = candidate; }
			}
			order = best_order;
			apply(order);
		}
	}

	// ── runSifting ────────────────────────────────────────────────────────────────

	void GlobalSifter::runSifting(int sifting_rounds, bool stability, bool reorder_chains) {
		for (int round = 0; round < sifting_rounds; round++) {
			BlockList snapshot = B_;
			std::vector<char> chain_done(chains_.size(), 0);
			for (int bid : snapshot) {
				if (!S_.blocks[bid].movable) continue;
				const int c = chainOf(bid);
				if (c < 0) siftingStep(bid, stability);
				else if (!chain_done[c]) { chain_done[c] = 1; chainStep(c, stability, reorder_chains); }
			}
		}
	}

	// ── siftNodesSearch ──────────────────────────────────────────────────────────
	//
	// Recursive helper for siftNodes. Places movable unit u (a block, or a chain's
	// members side by side), which starts at B_[starts[u]], by sweeping it left through
	// consecutive fixed blocks, stopping before another movable unit. At each position
	// it recurses to place the next unit, and also where a fixed block with a dummy in a
	// chain's row stops between two of its members as it passes (one more crossing, with
	// their line). A position where some unit has a real node inside a chain (blocked)
	// is not taken.
	// S_.blocks and S_.pi are snapshotted before each recursive call and restored
	// afterwards, giving free backtracking.

	void GlobalSifter::siftNodesSearch(
		const std::vector<std::vector<int>>& units, const std::vector<int>& starts, size_t u,
		const std::unordered_set<int>& movable_set,
		int chi, int blocked, int& best_chi, BlockList& best_B)
	{
		const int k = static_cast<int>(units[u].size());
		int pos = starts[u];
		// Every fixed block is still to the unit's left.
		std::vector<ChainWatch> watch = watchChains(units[u], true);
		int unit_blocked = 0;

		// The unit stops here, at_chi: the last unit records it, any other places the next.
		auto visit = [&](int at_chi) {
			if (u + 1 == units.size()) {
				if (blocked + unit_blocked == 0 && at_chi < best_chi) {
					best_chi = at_chi;
					best_B = B_;
				}
				return;
			}
			auto saved_blocks = S_.blocks;
			auto saved_pi = S_.pi;
			auto saved_B = B_;

			siftNodesSearch(units, starts, u + 1, movable_set, at_chi, blocked + unit_blocked, best_chi, best_B);

			S_.blocks = std::move(saved_blocks);
			S_.pi = std::move(saved_pi);
			B_ = std::move(saved_B);
		};

		while (true) {
			visit(chi);

			if (pos == 0) break;
			const int fixed = B_[pos - 1];
			if (movable_set.count(fixed)) break;

			// The fixed block passes the unit, left to right. A dummy of a chain's row may
			// stop between two of its members, crossing their line.
			for (int j = pos - 1; j < pos - 1 + k; j++) {
				chi += siftingSwap(B_[j], B_[j + 1]);
				std::swap(B_[j], B_[j + 1]);
				if (j + 2 < static_cast<int>(B_.size()) && dummyFitsBetween(B_[j], B_[j + 2], fixed))
					visit(chi + 1);
			}
			chi += passMember(watch, fixed, -1, unit_blocked);
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
			if (!S_.blocks[block_id].movable) continue;
			// A chain moves whole.
			if (const int c = chainOf(block_id); c >= 0)
				movable_set.insert(chains_[c].blocks.begin(), chains_[c].blocks.end());
			else
				movable_set.insert(block_id);
		}
		if (movable_set.empty()) return;
		const BlockList original_B = B_;
		const int original_crossings = countCrossings(); // also sets pi from B

		// Promote hub blocks whose every source and target are already movable.
		for (const auto& [g1_layer, layer_nodes] : S_.g1_layers) {
			for (int hub_g1 : layer_nodes) {
				if (S_.g1_nodes[hub_g1].original != nullptr) continue;
				int hub_bid = S_.g1_nodes[hub_g1].block_id;
				if (movable_set.count(hub_bid)) continue;
				if (!S_.blocks[hub_bid].movable) continue;
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
		std::stable_partition(B_.begin(), B_.end(),
			[&](int bid) { return !movable_set.count(bid); });

		// The units: every movable block on its own, but a chain's members side by side,
		// in their order, where its first member was.
		int nb = static_cast<int>(B_.size());
		int mb = static_cast<int>(movable_set.size());
		std::vector<std::vector<int>> units;
		{
			std::vector<char> chain_placed(chains_.size(), 0);
			for (int p = nb - mb; p < nb; p++) {
				const int c = chainOf(B_[p]);
				if (c < 0) { units.push_back({ B_[p] }); continue; }
				if (chain_placed[c]) continue;
				chain_placed[c] = 1;
				units.push_back(orderedMembers(c));
			}
			int p = nb - mb;
			for (const auto& unit : units)
				for (int bid : unit) B_[p++] = bid;
		}
		std::vector<int> starts;
		{
			int p = nb - mb;
			for (const auto& unit : units) { starts.push_back(p); p += static_cast<int>(unit.size()); }
		}
		sortAdjacencies();

		// ── Recursive exhaustive search ───────────────────────────────────────────
		//
		// units:   the movable units, at the tail of B_, each starting at starts[u].
		// chi:     cumulative crossing delta from the initial right-biased position.
		// best_chi / best_B: tracking the global minimum found so far.

		// Guard: C(nf + U, U) (nf fixed blocks, U units) can grow extremely fast. If the
		// number of positions to explore exceeds the threshold we skip the exhaustive search
		// entirely and run the regular global sifting algorithm instead, at the cost of
		// altering the user's mental map.
		const int nf = nb - mb;
		const int nu = static_cast<int>(units.size());
		double combinations = 1.0;
		for (int i = 0; i < nu; i++) {
			combinations *= static_cast<double>(nf + nu - i);
			combinations /= static_cast<double>(i + 1);
			if (combinations > 1e6) {
				// Treat all movable blocks as one composite block and sweep it left as a
				// unit. Each step moves the composite one position left via mb consecutive
				// siftingSwaps. Cost: O(nb * mb) instead of O(C(nb, mb)). A dummy of a
				// chain's row may stop between two of its members, crossing their line.
				int best_chi = 0;
				BlockList best_B = B_;
				int chi = 0;
				std::vector<int> composite(B_.begin() + nf, B_.end());
				std::vector<ChainWatch> watch = watchChains(composite, true);
				int blocked = 0;

				for (int k = nf; k > 0; k--) {
					// Move the composite block one step left: swap each of its mb elements
					// with the fixed block that just entered on the right.
					const int fixed = B_[k - 1];
					for (int j = k; j < mb + k; j++) {
						chi += siftingSwap(B_[j - 1], B_[j]);
						std::swap(B_[j - 1], B_[j]);
						if (blocked == 0 && j + 1 < nb && chi + 1 < best_chi && dummyFitsBetween(B_[j - 1], B_[j + 1], fixed)) {
							best_chi = chi + 1;
							best_B = B_;
						}
					}
					chi += passMember(watch, fixed, -1, blocked);

					if (blocked == 0 && chi < best_chi) {
						best_chi = chi;
						best_B = B_;
					}
				}

				B_ = std::move(best_B);
				break;
			}
		}
		if (combinations <= 1e6) {
			int best_chi = 0;
			BlockList best_B = B_;

			siftNodesSearch(units, starts, 0, movable_set, 0, 0, best_chi, best_B);

			// Apply the best ordering found.
			B_ = std::move(best_B);
		}

		// Chains are moved with at most one dummy between their members: never worse than
		// the order we were given.
		if (!chains_.empty() && countCrossings() > original_crossings) B_ = original_B;
		sortAdjacencies();
	}


	// ── writeBack ────────────────────────────────────────────────────────────────

	void GlobalSifter::writeBack() {
		for (auto& [layer, data] : layers_) {
			if (layer < start_layer_ || layer > end_layer_) continue;

			// The group's nodes are sorted among themselves and go back into the places
			// they occupied (all of them when there is no group).
			std::vector<int> slots;
			std::vector<NodePtr> nodes = groupRow(data, &slots);
			std::sort(nodes.begin(), nodes.end(),
				[&](const NodePtr& a, const NodePtr& b) {
					const G1Node& ga = S_.g1_nodes[S_.node_to_g1.at(a.get())];
					const G1Node& gb = S_.g1_nodes[S_.node_to_g1.at(b.get())];
					return S_.pi[ga.block_id] < S_.pi[gb.block_id];
				});
			for (size_t i = 0; i < slots.size(); i++) data.nodes[slots[i]] = nodes[i];
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
		return total + equivalenceCrossings();
	}

} // namespace sifting_internal

// ============================================================================
// GraphicalHypergraph
// ============================================================================
namespace hypergraph_logic {

	using namespace sifting_internal;

	// ── crossingGroup ────────────────────────────────────────────────────────────
	//
	// Components come from the connections between consecutive layers (segments
	// included, so a long connection's dummies join its ends), over every layer:
	// two nodes linked outside the rows read are still one component.
	std::unordered_set<Node*> Hypergraph::crossingGroup(const std::vector<Node*>& seeds,
		int first_layer, int last_layer) const
	{
		std::unordered_set<Node*> group;
		if (seeds.empty()) return group;

		// Union-find over every node of the graph.
		std::unordered_map<Node*, Node*> parent;
		for (const auto& [layer, data] : layers_)
			for (const auto& n : data.nodes) parent[n.get()] = n.get();
		auto find = [&](Node* n) {
			Node* root = n;
			while (parent.at(root) != root) root = parent.at(root);
			while (parent.at(n) != root) { Node* next = parent.at(n); parent[n] = root; n = next; }
			return root;
		};
		for (const auto& [layer, data] : layers_) {
			for (const auto& e : data.outgoing_edges) {
				Node* first = nullptr;
				auto join = [&](const NodePtr& n) {
					if (!parent.count(n.get())) return;
					if (!first) { first = find(n.get()); return; }
					parent[find(n.get())] = first;
					first = find(first);
				};
				for (const auto& s : e->getSources()) join(s);
				for (const auto& t : e->getTargets()) join(t);
			}
		}
		// Equivalent nodes are linked too: they stay side by side.
		for (const auto& chain : equivalences_) {
			Node* first = chain.members.front().get();
			for (const auto& m : chain.members)
				if (parent.count(m.get()) && parent.count(first)) parent[find(m.get())] = find(first);
		}
		std::unordered_map<Node*, std::vector<Node*>> components;
		for (const auto& [n, _] : parent) components[find(n)].push_back(n);

		auto addComponentOf = [&](Node* n) {
			if (group.count(n) || !parent.count(n)) return false;
			for (Node* m : components.at(find(n))) group.insert(m);
			return true;
		};
		for (Node* n : seeds) addComponentOf(n);

		// Overlap closure. In every row that is read, another component is to the left of
		// the group, to its right, or mixed with it. It joins the group if it is mixed in
		// some row, or to the left in one row and to the right in another (their
		// connections cross), until none does: only components that stay on one side of
		// the group all the way down are left out.
		enum Side : unsigned char { Left = 1, Right = 2, Mixed = 4 };
		bool grew = true;
		while (grew) {
			grew = false;
			std::unordered_map<Node*, unsigned char> sides; // component root -> sides seen
			for (const auto& [layer, data] : layers_) {
				if (layer < first_layer || layer > last_layer) continue;
				const auto& nodes = data.nodes;
				int lo = -1, hi = -1;
				for (int i = 0; i < static_cast<int>(nodes.size()); i++)
					if (group.count(nodes[i].get())) { if (lo < 0) lo = i; hi = i; }
				if (lo < 0) continue; // nothing of the group here to be on a side of
				for (int i = 0; i < static_cast<int>(nodes.size()); i++) {
					Node* n = nodes[i].get();
					if (group.count(n) || !parent.count(n)) continue;
					sides[find(n)] |= i < lo ? Left : i > hi ? Right : Mixed;
				}
			}
			for (const auto& [root, seen] : sides)
				if ((seen & Mixed) || (seen & (Left | Right)) == (Left | Right))
					grew |= addComponentOf(root);
		}
		return group;
	}

	// ── minimizeCrossings ────────────────────────────────────────────────────────
	//
	// Runs the global sifting algorithm over [start_layer, end_layer] (the last layer
	// when end_layer is -1), writing the optimised order back to LayerData::nodes and
	// returning the crossing count (of the group sifted, when there is one).
	int Hypergraph::minimizeCrossings(int sifting_rounds, int start_layer, int end_layer,
		const std::vector<Node*>& affected, const std::vector<Node*>& moved)
	{
		if (getLayers().empty()) return 0;
		int last_layer = static_cast<int>(layers_.rbegin()->first);
		if (end_layer < 0 || end_layer > last_layer) end_layer = last_layer;
		if (start_layer > end_layer) return 0;
		placeUnpositionedNodes(start_layer, end_layer);

		std::vector<Node*> seeds = affected;
		seeds.insert(seeds.end(), moved.begin(), moved.end());
		std::unordered_set<Node*> group = crossingGroup(seeds, std::max(0, start_layer - 1), end_layer + 1);
		if (!seeds.empty() && group.empty()) return 0; // none of them is in the graph any more

		// No barycenter seeding here: it would reorder every layer, and inner calls are
		// meant to keep the current orders as much as possible.
		GlobalSifter sifter(start_layer, end_layer, layers_, false, moved, std::move(group), equivalencesToSift());
		// No need to sift if we are already optimal, but the seed order may differ from
		// the layers (the moved node's descendants), so it is still written back.
		if (sifter.countCrossings() == 0) { sifter.writeBack(); return 0; }
		sifter.runSifting(sifting_rounds);
		sifter.writeBack();
		return sifter.countCrossings();
	}

	// ── minimizeCrossingsForNodes ────────────────────────────────────────────────
	//
	// Builds G1 over [start_layer, end_layer] (plus the one layer below end_layer
	// for crossing information), restricted to the nodes' components (crossingGroup),
	// then sifts only the blocks that contain the given nodes to find their locally
	// best positions within the current ordering. The result is written back to
	// LayerData::nodes.

	int Hypergraph::minimizeCrossingsForNodes(
		const std::vector<Node*>& nodes,
		int start_layer,
		int end_layer)
	{
		if (getLayers().empty() || nodes.empty()) return 0;

		placeUnpositionedNodes(start_layer, end_layer);
		std::unordered_set<Node*> group = crossingGroup(nodes, std::max(0, start_layer - 1), end_layer + 1);
		if (group.empty()) return 0;
		GlobalSifter sifter(start_layer, end_layer, layers_, false, {}, std::move(group), equivalencesToSift());
		if (sifter.countCrossings() == 0) return 0; // No need to sift if we are already optimal.
		sifter.siftNodes(nodes);
		sifter.writeBack();
		return sifter.countCrossings();
	}

} // namespace hypergraph_logic