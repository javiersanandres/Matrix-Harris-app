#include "Hypergraph.h"

#include <algorithm>
#include <stdexcept>

// ============================================================================
// Equivalences between nodes: the chains, the rules they follow (one layer,
// side by side, no cycle through them) and the operations on them. See
// "Equivalence management" in Hypergraph.h.
// ============================================================================

namespace hypergraph_logic {

	// ============================================================================
	// Queries
	// ============================================================================

	int Hypergraph::chainIndexOf(const Node* node) const {
		if (!node) return -1;
		for (size_t c = 0; c < equivalences_.size(); ++c)
			for (const auto& m : equivalences_[c].members)
				if (m.get() == node) return static_cast<int>(c);
		return -1;
	}

	const EquivalenceChain* Hypergraph::getEquivalenceChain(const Node* node) const {
		const int c = chainIndexOf(node);
		return c < 0 ? nullptr : &equivalences_[c];
	}

	std::vector<NodePtr> Hypergraph::equivalentNodes(const NodePtr& node) const {
		const int c = chainIndexOf(node.get());
		return c < 0 ? std::vector<NodePtr>{ node } : equivalences_[c].members;
	}

	std::vector<NodePtr> Hypergraph::withEquivalents(const std::vector<NodePtr>& nodes) const {
		std::vector<NodePtr> result;
		std::unordered_set<Node*> seen;
		for (const auto& n : nodes) {
			if (!n) continue;
			for (const auto& m : equivalentNodes(n))
				if (seen.insert(m.get()).second) result.push_back(m);
		}
		return result;
	}

	double Hypergraph::horizontalPosition(const NodePtr& node) const {
		const auto& nodes = getLayerData(node->getLayer()).nodes;
		auto it = std::find(nodes.begin(), nodes.end(), node);
		return static_cast<double>(it - nodes.begin());
	}

	bool Hypergraph::classReaches(const Node* from, const Node* to) const {
		if (!from || !to) return false;

		std::unordered_map<const Node*, const EquivalenceChain*> chain_of;
		for (const auto& chain : equivalences_)
			for (const auto& m : chain.members) chain_of[m.get()] = &chain;
		const auto target_chain = chain_of.find(to);
		auto isTarget = [&](const Node* n) {
			if (n == to) return true;
			if (target_chain == chain_of.end()) return false;
			const auto it = chain_of.find(n);
			return it != chain_of.end() && it->second == target_chain->second;
		};

		// A path may go on from any member of a chain it enters.
		std::unordered_set<const Node*> seen;
		std::vector<const Node*> pending;
		auto reach = [&](const Node* n) {
			if (!seen.insert(n).second) return;
			pending.push_back(n);
			const auto it = chain_of.find(n);
			if (it == chain_of.end()) return;
			for (const auto& m : it->second->members)
				if (seen.insert(m.get()).second) pending.push_back(m.get());
		};
		reach(from);
		while (!pending.empty()) {
			const Node* n = pending.back();
			pending.pop_back();
			if (isTarget(n)) return true;
			for (const auto& c : n->getChildren()) reach(c.get());
		}
		return false;
	}

	// ============================================================================
	// Keeping the members of a chain together
	// ============================================================================

	namespace {
		// Whether a chain's members, all in the layer (index: position of every node in nodes),
		// are in order with only dummies between them.
		bool followsTheRule(const std::vector<NodePtr>& members, const std::vector<NodePtr>& nodes,
			const std::unordered_map<Node*, int>& index)
		{
			int previous = index.at(members.front().get());
			for (size_t k = 1; k < members.size(); ++k) {
				const int at = index.at(members[k].get());
				if (at <= previous) return false;
				for (int i = previous + 1; i < at; ++i)
					if (!nodes[i]->isDummy()) return false;
				previous = at;
			}
			return true;
		}
	}

	void Hypergraph::keepEquivalencesTogether() {
		if (equivalences_.empty() || !keep_equivalences_together_) return;
		for (const auto& [layer, _] : layers_) keepEquivalencesTogether(layer);
	}

	void Hypergraph::keepEquivalencesTogether(int layer) {
		if (equivalences_.empty() || !keep_equivalences_together_) return;
		auto layer_it = layers_.find(layer);
		if (layer_it == layers_.end()) return;
		auto& nodes = layer_it->second.nodes;

		std::unordered_map<Node*, int> index;
		for (int i = 0; i < static_cast<int>(nodes.size()); ++i) index[nodes[i].get()] = i;

		// The chains wholly in this layer (one being relocated may be split for a moment).
		std::vector<const EquivalenceChain*> chains;
		for (const auto& chain : equivalences_) {
			bool here = true;
			for (const auto& m : chain.members) here = here && index.count(m.get()) > 0;
			if (here) chains.push_back(&chain);
		}
		if (chains.empty()) return;

		if (std::all_of(chains.begin(), chains.end(),
			[&](const EquivalenceChain* c) { return followsTheRule(c->members, nodes, index); }))
			return;

		// Every chain is one unit at the middle of its span; every other node, a unit at its
		// own position, except dummies within a span, which stay in the gap they were in.
		struct Unit {
			double key;
			const EquivalenceChain* chain = nullptr;
			NodePtr node;
			std::vector<std::vector<NodePtr>> gaps;  // dummies after each member but the last
		};
		std::vector<Unit> units;
		std::unordered_map<Node*, size_t> unit_of_member;
		std::sort(chains.begin(), chains.end(), [&](const EquivalenceChain* a, const EquivalenceChain* b) {
			return index.at(a->members.front().get()) < index.at(b->members.front().get());
		});
		std::vector<std::pair<int, int>> spans;
		for (const EquivalenceChain* chain : chains) {
			int lo = static_cast<int>(nodes.size()), hi = -1;
			for (const auto& m : chain->members) {
				lo = std::min(lo, index.at(m.get()));
				hi = std::max(hi, index.at(m.get()));
				unit_of_member[m.get()] = units.size();
			}
			// A node right at the middle goes to the left.
			units.push_back({ (lo + hi) / 2.0 + 0.25, chain, nullptr,
				std::vector<std::vector<NodePtr>>(chain->members.size() - 1) });
			spans.emplace_back(lo, hi);
		}
		for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
			const NodePtr& n = nodes[i];
			if (unit_of_member.count(n.get())) continue;
			bool kept_in_gap = false;
			if (n->isDummy()) {
				for (size_t c = 0; c < chains.size() && !kept_in_gap; ++c) {
					if (i <= spans[c].first || i >= spans[c].second) continue;
					size_t before = 0;
					for (const auto& m : chains[c]->members)
						if (index.at(m.get()) < i) ++before;
					units[c].gaps[before - 1].push_back(n);
					kept_in_gap = true;
				}
			}
			if (!kept_in_gap) units.push_back({ static_cast<double>(i), nullptr, n, {} });
		}

		std::stable_sort(units.begin(), units.end(), [](const Unit& a, const Unit& b) { return a.key < b.key; });
		std::vector<NodePtr> ordered;
		ordered.reserve(nodes.size());
		for (const auto& u : units) {
			if (!u.chain) { ordered.push_back(u.node); continue; }
			for (size_t k = 0; k < u.chain->members.size(); ++k) {
				ordered.push_back(u.chain->members[k]);
				if (k < u.gaps.size()) ordered.insert(ordered.end(), u.gaps[k].begin(), u.gaps[k].end());
			}
		}
		nodes = std::move(ordered);
	}

	// ============================================================================
	// Changing the chains
	// ============================================================================

	std::vector<NodePtr> Hypergraph::leaveEquivalence(const NodePtr& node) {
		const int c = chainIndexOf(node.get());
		if (c < 0) return {};
		EquivalenceChain& chain = equivalences_[c];
		const size_t k = std::find(chain.members.begin(), chain.members.end(), node) - chain.members.begin();

		// The line closing the gap is discontinuous only if both of the node's were.
		if (k == 0) chain.continuous.erase(chain.continuous.begin());
		else if (k + 1 == chain.members.size()) chain.continuous.pop_back();
		else {
			chain.continuous[k - 1] = chain.continuous[k - 1] || chain.continuous[k];
			chain.continuous.erase(chain.continuous.begin() + static_cast<std::ptrdiff_t>(k));
		}
		chain.members.erase(chain.members.begin() + static_cast<std::ptrdiff_t>(k));

		std::vector<NodePtr> others = chain.members;
		if (chain.members.size() < 2) equivalences_.erase(equivalences_.begin() + c);
		return others;
	}

	void Hypergraph::fuseEquivalences(const NodePtr& survivor, const NodePtr& absorbed) {
		const int cs = chainIndexOf(survivor.get()), ca = chainIndexOf(absorbed.get());
		if (ca < 0) return; // survivor's chain, if any, stays as it is
		if (cs == ca) { leaveEquivalence(absorbed); return; }

		auto fused = [&](const NodePtr& n) { return n == absorbed ? survivor : n; };
		std::vector<NodePtr> members;
		std::map<std::pair<Node*, Node*>, bool> styles; // of the lines between former neighbours
		for (const int c : { cs, ca }) {
			if (c < 0) continue;
			const EquivalenceChain& chain = equivalences_[c];
			for (const auto& m : chain.members)
				if (std::find(members.begin(), members.end(), fused(m)) == members.end())
					members.push_back(fused(m));
			for (size_t k = 0; k < chain.continuous.size(); ++k) {
				Node* l = fused(chain.members[k]).get();
				Node* r = fused(chain.members[k + 1]).get();
				styles[{ l, r }] = styles[{ r, l }] = chain.continuous[k];
			}
		}

		std::unordered_map<Node*, double> position;
		for (const auto& m : members) position[m.get()] = horizontalPosition(m);
		std::stable_sort(members.begin(), members.end(), [&](const NodePtr& a, const NodePtr& b) {
			return position.at(a.get()) < position.at(b.get());
		});

		EquivalenceChain merged{ members, {} };
		for (size_t k = 0; k + 1 < members.size(); ++k) {
			auto it = styles.find({ members[k].get(), members[k + 1].get() });
			merged.continuous.push_back(it == styles.end() || it->second);
		}
		for (const int c : { std::max(cs, ca), std::min(cs, ca) })
			if (c >= 0) equivalences_.erase(equivalences_.begin() + c);
		equivalences_.push_back(std::move(merged));
	}

	bool Hypergraph::canAddEquivalence(const NodePtr& a, const NodePtr& b) const {
		if (!a || !b || a == b || a->isDummy() || b->isDummy()) return false;
		const int ca = chainIndexOf(a.get());
		if (ca >= 0 && ca == chainIndexOf(b.get())) return false;
		const bool a_left = horizontalPosition(a) <= horizontalPosition(b);
		const NodePtr& left = a_left ? a : b;
		const NodePtr& right = a_left ? b : a;
		if (equivalentNodes(left).back() != left || equivalentNodes(right).front() != right) return false;
		return !classReaches(a.get(), b.get()) && !classReaches(b.get(), a.get());
	}

	void Hypergraph::addEquivalence(const NodePtr& a, const NodePtr& b, bool continuous, std::set<int>* out_altered_layers) {
		if (!a || !b) throw std::invalid_argument("Faltan las cajas que se quieren hacer equivalentes.");
		if (a == b) throw std::invalid_argument("Una caja no puede ser equivalente a sí misma.");
		if (a->isDummy() || b->isDummy()) throw std::invalid_argument("Solo las cajas del esquema pueden ser equivalentes.");
		const int ca = chainIndexOf(a.get()), cb = chainIndexOf(b.get());
		if (ca >= 0 && ca == cb) throw std::logic_error("Estas cajas ya son equivalentes.");

		const bool a_left = horizontalPosition(a) <= horizontalPosition(b);
		const NodePtr& left = a_left ? a : b;
		const NodePtr& right = a_left ? b : a;
		const std::vector<NodePtr> left_chain = equivalentNodes(left);
		const std::vector<NodePtr> right_chain = equivalentNodes(right);
		if (left_chain.back() != left)
			throw std::logic_error("La caja de la izquierda ya tiene una caja equivalente a su derecha.");
		if (right_chain.front() != right)
			throw std::logic_error("La caja de la derecha ya tiene una caja equivalente a su izquierda.");
		if (classReaches(a.get(), b.get()) || classReaches(b.get(), a.get()))
			throw std::logic_error("Hacer equivalentes estas cajas crearía un ciclo en el esquema.");

		// The chain that moves: the one that has to come down, or else the shorter (the right one on a tie).
		const bool left_moves = left->getLayer() != right->getLayer()
			? left->getLayer() < right->getLayer()
			: left_chain.size() < right_chain.size();
		const std::vector<NodePtr>& mover = left_moves ? left_chain : right_chain;
		const NodePtr& anchor = left_moves ? right : left;

		EquivalenceChain merged;
		merged.members = left_chain;
		merged.members.insert(merged.members.end(), right_chain.begin(), right_chain.end());
		const int cl = a_left ? ca : cb, cr = a_left ? cb : ca;
		if (cl >= 0) merged.continuous = equivalences_[cl].continuous;
		merged.continuous.push_back(continuous);
		if (cr >= 0) merged.continuous.insert(merged.continuous.end(),
			equivalences_[cr].continuous.begin(), equivalences_[cr].continuous.end());
		for (const int c : { std::max(cl, cr), std::min(cl, cr) })
			if (c >= 0) equivalences_.erase(equivalences_.begin() + c);
		equivalences_.push_back(merged);

		// Down to the deeper layer first, if needed, without gathering the chain yet: the mover
		// is placed by hand below.
		LayerSpan touched;
		bool relocated;
		{
			const bool keeping = keep_equivalences_together_;
			keep_equivalences_together_ = false;
			relocated = relocateNodes(merged.members, &touched, out_altered_layers);
			keep_equivalences_together_ = keeping;
		}

		// The mover goes towards the other chain's end only as far as needed: past the real nodes
		// between them, which take the place it left, but not past the dummies next to that end,
		// which may stay in the gap. Already with only dummies between them, nothing moves. On
		// the wrong side of the other chain (where a relocation left it), it goes right next to it.
		const int layer = anchor->getLayer();
		auto& nodes = layers_.at(layer).nodes;
		const std::vector<NodePtr> order_before = nodes;
		std::unordered_map<Node*, int> index;
		for (int i = 0; i < static_cast<int>(nodes.size()); ++i) index[nodes[i].get()] = i;
		const int first = index.at(mover.front().get()), last = index.at(mover.back().get());
		const int anchor_at = index.at(anchor.get());
		const bool wrong_side = left_moves ? first > anchor_at : last < anchor_at;

		// The mover with the dummies between its members, when it already keeps the rule.
		std::vector<NodePtr> segment = mover;
		if (followsTheRule(mover, nodes, index))
			segment.assign(nodes.begin() + first, nodes.begin() + last + 1);
		std::unordered_set<Node*> moving;
		for (const auto& n : segment) moving.insert(n.get());
		std::erase_if(nodes, [&](const NodePtr& n) { return moving.count(n.get()) > 0; });

		auto at = std::find(nodes.begin(), nodes.end(), anchor);
		if (left_moves) {
			if (!wrong_side)
				while (at != nodes.begin() && (*std::prev(at))->isDummy()) --at;
		}
		else {
			++at;
			if (!wrong_side)
				while (at != nodes.end() && (*at)->isDummy()) ++at;
		}
		nodes.insert(at, segment.begin(), segment.end());
		const bool moved_sideways = nodes != order_before;
		keepEquivalencesTogether(); // other chains the relocation may have moved

		// The merged chain's layer stays as placed; the layers above and below are sifted
		// around it, as after moving nodes sideways (relocateNodeInLayer): the mover's
		// subgraph follows it.
		std::vector<Node*> affected;
		for (const auto& m : merged.members) affected.push_back(m.get());
		std::vector<Node*> followed;
		if (moved_sideways)
			for (const auto& m : mover) followed.push_back(m.get());
		const bool has_children = std::any_of(mover.begin(), mover.end(),
			[](const NodePtr& m) { return !m->getChildren().empty(); });
		if (relocated) {
			if (touched.min < layer) minimizeCrossings(10, touched.min, layer - 1, affected);
			if (touched.max > layer) minimizeCrossings(10, layer + 1, touched.max, affected, followed);
		}
		else if (moved_sideways && has_children) {
			minimizeCrossings(3, layer + 1, -1, {}, followed);
		}

		if (out_altered_layers && moved_sideways) {
			// The hyperedges reaching the moved nodes, and leaving them, may span differently.
			if (layer > 0) out_altered_layers->insert(layer - 1);
			if (has_children) {
				for (const auto& [l, _] : layers_)
					if (l >= layer) out_altered_layers->insert(l);
			}
		}
	}

	bool Hypergraph::canRemoveEquivalence(const NodePtr& a, const NodePtr& b) const {
		if (!a || !b) return false;
		const int c = chainIndexOf(a.get());
		if (c < 0 || c != chainIndexOf(b.get())) return false;
		const auto& members = equivalences_[c].members;
		const auto ia = std::find(members.begin(), members.end(), a) - members.begin();
		const auto ib = std::find(members.begin(), members.end(), b) - members.begin();
		return ia - ib == 1 || ib - ia == 1;
	}

	// The gap between neighbours a and b in their chain (index into continuous).
	static size_t gapBetween(const EquivalenceChain& chain, const NodePtr& a, const NodePtr& b) {
		const auto ia = std::find(chain.members.begin(), chain.members.end(), a) - chain.members.begin();
		const auto ib = std::find(chain.members.begin(), chain.members.end(), b) - chain.members.begin();
		return static_cast<size_t>(std::min(ia, ib));
	}

	void Hypergraph::removeEquivalence(const NodePtr& a, const NodePtr& b, std::set<int>* out_altered_layers) {
		if (!canRemoveEquivalence(a, b))
			throw std::logic_error("Estas cajas no están unidas por una equivalencia.");
		const int c = chainIndexOf(a.get());
		const EquivalenceChain chain = equivalences_[c];
		const size_t gap = gapBetween(chain, a, b);
		equivalences_.erase(equivalences_.begin() + c);

		const auto split = static_cast<std::ptrdiff_t>(gap + 1);
		EquivalenceChain left_part{ { chain.members.begin(), chain.members.begin() + split },
			{ chain.continuous.begin(), chain.continuous.begin() + static_cast<std::ptrdiff_t>(gap) } };
		EquivalenceChain right_part{ { chain.members.begin() + split, chain.members.end() },
			{ chain.continuous.begin() + split, chain.continuous.end() } };
		if (left_part.members.size() > 1) equivalences_.push_back(std::move(left_part));
		if (right_part.members.size() > 1) equivalences_.push_back(std::move(right_part));

		// Each part may now go back up to the layer its own members ask for.
		relocateAndMinimize(chain.members, out_altered_layers);
	}

	bool Hypergraph::isEquivalenceContinuous(const NodePtr& a, const NodePtr& b) const {
		if (!canRemoveEquivalence(a, b))
			throw std::logic_error("Estas cajas no están unidas por una equivalencia.");
		const EquivalenceChain& chain = equivalences_[chainIndexOf(a.get())];
		return chain.continuous[gapBetween(chain, a, b)];
	}

	void Hypergraph::setEquivalenceContinuous(const NodePtr& a, const NodePtr& b, bool continuous) {
		if (!canRemoveEquivalence(a, b))
			throw std::logic_error("Estas cajas no están unidas por una equivalencia.");
		EquivalenceChain& chain = equivalences_[chainIndexOf(a.get())];
		chain.continuous[gapBetween(chain, a, b)] = continuous;
	}

} // namespace hypergraph_logic
