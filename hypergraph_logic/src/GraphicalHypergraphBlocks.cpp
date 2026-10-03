#include "GraphicalHypergraph.h"

#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>

// ============================================================================
// Blocks (connected components) of a GraphicalHypergraph: finding them, the
// regions they occupy, and moving or removing one as a whole. See "Blocks" in
// GraphicalHypergraph.h.
// ============================================================================

namespace hypergraph_logic {

	std::vector<GraphicalHypergraph::Block>
		GraphicalHypergraph::blocksExcluding(const std::unordered_set<Node*>& excluded) const
	{
		// Union-find over the boxes, joining every box of each hyperedge
		// (originals and their segments, so dummies too).
		std::unordered_map<Node*, int> index;
		std::vector<Node*> nodes;
		for (const auto& n : all_nodes_) {
			if (excluded.count(n.get())) continue;
			index[n.get()] = static_cast<int>(nodes.size());
			nodes.push_back(n.get());
		}
		std::vector<int> parent(nodes.size());
		for (size_t i = 0; i < parent.size(); ++i) parent[i] = static_cast<int>(i);
		auto find = [&](int v) {
			while (parent[v] != v) v = parent[v] = parent[parent[v]];
			return v;
		};
		auto join = [&](const Hyperedge& e) {
			int first = -1;
			auto visit = [&](const NodePtr& n) {
				auto it = index.find(n.get());
				if (it == index.end()) return;
				if (first < 0) first = it->second;
				else parent[find(it->second)] = find(first);
			};
			for (const auto& s : e.getSources()) visit(s);
			for (const auto& t : e.getTargets()) visit(t);
		};
		for (const auto& [orig, segs] : all_hyperedges_) {
			join(*orig);
			for (const auto& seg : segs) join(*seg);
		}

		std::unordered_map<int, size_t> block_of_root;
		std::vector<Block> blocks;
		for (size_t i = 0; i < nodes.size(); ++i) {
			const int root = find(static_cast<int>(i));
			auto [it, is_new] = block_of_root.emplace(root, blocks.size());
			if (is_new) {
				blocks.emplace_back();
				blocks.back().left = std::numeric_limits<double>::infinity();
				blocks.back().right = -std::numeric_limits<double>::infinity();
			}
			Block& b = blocks[it->second];
			Node* n = nodes[i];
			b.nodes.push_back(n);
			auto layout = node_layout_.find(n);
			if (layout != node_layout_.end()) {
				b.left = std::min(b.left, layout->second.x - n->getWidth() / 2.0);
				b.right = std::max(b.right, layout->second.x + n->getWidth() / 2.0);
			}
		}
		for (auto& b : blocks)
			if (b.left > b.right) b.left = b.right = 0.0; // no coordinates yet
		return blocks;
	}

	std::vector<GraphicalHypergraph::Block> GraphicalHypergraph::getBlocks() const {
		return blocksExcluding({});
	}

	std::vector<std::pair<double, double>>
		GraphicalHypergraph::mergeSpans(std::vector<std::pair<double, double>> spans)
	{
		std::sort(spans.begin(), spans.end());
		std::vector<std::pair<double, double>> merged;
		for (const auto& s : spans) {
			// Closed intervals: touching ones are one region.
			if (!merged.empty() && s.first <= merged.back().second)
				merged.back().second = std::max(merged.back().second, s.second);
			else
				merged.push_back(s);
		}
		return merged;
	}

	std::vector<std::pair<double, double>> GraphicalHypergraph::getOccupiedRegions() const {
		return getOccupiedRegionsExcluding({});
	}

	std::vector<std::pair<double, double>>
		GraphicalHypergraph::getOccupiedRegionsExcluding(const std::unordered_set<Node*>& group) const
	{
		std::vector<std::pair<double, double>> spans;
		for (const auto& b : blocksExcluding(group)) spans.emplace_back(b.left, b.right);
		return mergeSpans(std::move(spans));
	}

	// Where to insert a group clicked at click_x, given the occupied regions of
	// the others: a point x such that its boxes go between the boxes whose centre
	// is left of x and those right of it.
	double GraphicalHypergraph::placementPoint(
		const std::vector<std::pair<double, double>>& regions, double click_x)
	{
		for (const auto& [lo, hi] : regions)
			if (lo <= click_x && click_x <= hi)
				return click_x < (lo + hi) / 2.0 ? lo : hi; // the exact middle goes right
		return click_x;
	}

	std::unordered_set<Node*> GraphicalHypergraph::componentNodesOf(const Node* box) const {
		for (const auto& b : getBlocks())
			if (std::find(b.nodes.begin(), b.nodes.end(), box) != b.nodes.end())
				return { b.nodes.begin(), b.nodes.end() };
		throw std::invalid_argument("La caja no forma parte del esquema.");
	}

	void GraphicalHypergraph::ensureLayout() {
		for (const auto& n : all_nodes_)
			if (!node_layout_.count(n.get())) { computeLayout({}); return; }
	}

	// ============================================================================
	// Moving and removing a block
	// ============================================================================

	void GraphicalHypergraph::moveComponent(const Node* box, double click_x) {
		moveGroup(componentNodesOf(box), click_x, std::nullopt);
	}

	void GraphicalHypergraph::moveComponentToLayer(const Node* box, int top_layer) {
		moveGroup(componentNodesOf(box), std::nullopt, top_layer);
	}

	void GraphicalHypergraph::moveComponent(const Node* box, double click_x, int top_layer) {
		moveGroup(componentNodesOf(box), click_x, top_layer);
	}

	void GraphicalHypergraph::removeComponent(const Node* box) {
		removeGroup(componentNodesOf(box));
	}

	void GraphicalHypergraph::moveGroup(const std::unordered_set<Node*>& group,
		std::optional<double> click_x, std::optional<int> top_layer)
	{
		if (group.empty()) return; // e.g. a diagram whose boxes were all deleted
		ensureLayout();

		// The group is closed: every hyperedge touching it (and its segments) is its own.
		std::unordered_set<Hyperedge*> group_edges;
		for (const auto& [orig, segs] : all_hyperedges_) {
			bool touches = false;
			for (const auto& s : orig->getSources()) touches = touches || group.count(s.get());
			for (const auto& t : orig->getTargets()) touches = touches || group.count(t.get());
			if (!touches) continue;
			group_edges.insert(orig.get());
			for (const auto& seg : segs) group_edges.insert(seg.get());
		}

		// Vertical shift of the group, and of everything when it opens layers above.
		int top = std::numeric_limits<int>::max();
		double left = std::numeric_limits<double>::infinity(), right = -left;
		for (Node* n : group) {
			top = std::min(top, n->getLayer());
			auto layout = node_layout_.find(n);
			if (layout == node_layout_.end()) continue;
			left = std::min(left, layout->second.x - n->getWidth() / 2.0);
			right = std::max(right, layout->second.x + n->getWidth() / 2.0);
		}
		const int target_top = top_layer.value_or(top);
		if (top_layer && !click_x && target_top == top)
			throw std::invalid_argument("Ya empieza en ese nivel.");
		const int shift_others = std::max(0, -target_top);
		const int shift_group = target_top - top + shift_others;

		// Horizontal place among the others' regions.
		const double wanted_x = click_x.value_or(left <= right ? (left + right) / 2.0 : 0.0);
		const double at_x = placementPoint(getOccupiedRegionsExcluding(group), wanted_x);

		// Rebuild the layers: first everything else (order kept), then the group,
		// each layer's block inserted among the boxes centred left of at_x.
		std::map<int, LayerData> rebuilt;
		for (const auto& [layer, data] : layers_) {
			LayerData& dst = rebuilt[layer + shift_others];
			for (const auto& n : data.nodes)
				if (!group.count(n.get())) dst.nodes.push_back(n);
			for (const auto& e : data.outgoing_edges)
				if (!group_edges.count(e.get())) dst.outgoing_edges.push_back(e);
		}
		for (const auto& [layer, data] : layers_) {
			std::vector<NodePtr> run;
			for (const auto& n : data.nodes)
				if (group.count(n.get())) run.push_back(n);
			std::vector<HyperedgePtr> edges;
			for (const auto& e : data.outgoing_edges)
				if (group_edges.count(e.get())) edges.push_back(e);
			if (run.empty() && edges.empty()) continue;

			LayerData& dst = rebuilt[layer + shift_group];
			size_t before = 0;
			for (const auto& n : dst.nodes) {
				auto layout = node_layout_.find(n.get());
				if (layout != node_layout_.end() && layout->second.x < at_x) ++before;
			}
			dst.nodes.insert(dst.nodes.begin() + static_cast<std::ptrdiff_t>(before), run.begin(), run.end());
			dst.outgoing_edges.insert(dst.outgoing_edges.end(), edges.begin(), edges.end());
		}

		for (const auto& [layer, data] : rebuilt) {
			for (const auto& n : data.nodes) n->setLayer(layer);
			for (const auto& e : data.outgoing_edges) e->setLayer(layer);
		}
		layers_ = std::move(rebuilt);
		cleanUp();                // layers left empty are closed up
		refreshLayerOverrides();  // moved roots keep their new layers
		computeLayout({});
	}

	void GraphicalHypergraph::removeGroup(const std::unordered_set<Node*>& doomed) {
		if (doomed.empty()) return;
		beforeRemovingBoxes(doomed); // while the boxes are still alive

		// The group is closed: every hyperedge touching it is its own.
		std::unordered_set<Hyperedge*> doomed_edges;
		std::vector<HyperedgePtr> doomed_originals;
		for (const auto& [orig, segs] : all_hyperedges_) {
			bool touches = false;
			for (const auto& s : orig->getSources()) touches = touches || doomed.count(s.get());
			for (const auto& t : orig->getTargets()) touches = touches || doomed.count(t.get());
			if (!touches) continue;
			doomed_originals.push_back(orig);
			doomed_edges.insert(orig.get());
			for (const auto& seg : segs) doomed_edges.insert(seg.get());
		}

		for (auto& [layer, data] : layers_) {
			std::erase_if(data.nodes, [&](const NodePtr& n) { return doomed.count(n.get()) > 0; });
			std::erase_if(data.outgoing_edges, [&](const HyperedgePtr& e) { return doomed_edges.count(e.get()) > 0; });
		}
		for (Node* n : doomed) node_layout_.erase(n);
		for (Hyperedge* e : doomed_edges) edge_layout_.erase(e);
		for (const auto& e : doomed_originals) all_hyperedges_.erase(e);
		std::erase_if(all_nodes_, [&](const NodePtr& n) { return doomed.count(n.get()) > 0; });

		cleanUp(); // layers left empty are closed up
		computeLayout({});
	}

	// ============================================================================
	// Copying and pasting
	// ============================================================================

	GraphicalHypergraph GraphicalHypergraph::copyOf(const std::unordered_set<Node*>& group) const {
		for (const auto& [orig, segs] : all_hyperedges_) {
			bool inside = false, outside = false;
			for (const auto& s : orig->getSources()) (group.count(s.get()) ? inside : outside) = true;
			for (const auto& t : orig->getTargets()) (group.count(t.get()) ? inside : outside) = true;
			if (inside && outside)
				throw std::invalid_argument("Las cajas que se copian están conectadas con otras.");
		}

		// clone() keeps all_nodes_ in order: the copy's i-th box is this graph's i-th.
		GraphicalHypergraph copy = clone();
		copy.id_ = generateId();
		std::unordered_set<Node*> others;
		for (size_t i = 0; i < all_nodes_.size(); ++i)
			if (!group.count(all_nodes_[i].get())) others.insert(copy.all_nodes_[i].get());
		if (!others.empty()) {
			copy.removeGroup(others); // closes up the layers left empty, lays it out again
			copy.refreshLayerOverrides();
		}
		return copy;
	}

	GraphicalHypergraph GraphicalHypergraph::duplicate() const {
		GraphicalHypergraph copy = clone();
		copy.id_ = generateId();
		return copy;
	}

	std::vector<NodePtr> GraphicalHypergraph::paste(GraphicalHypergraph&& piece, double click_x, int top_layer) {
		std::unordered_set<Node*> group;
		std::vector<NodePtr> boxes;
		for (const auto& n : piece.all_nodes_) {
			group.insert(n.get());
			if (!n->isDummy()) boxes.push_back(n);
		}
		if (group.empty()) return boxes;

		ensureLayout();
		piece.ensureLayout();
		mergeFrom(std::move(piece), false); // after everything: moveGroup places it
		moveGroup(group, click_x, top_layer); // places it, closes up layers, lays everything out
		return boxes;
	}

	void GraphicalHypergraph::refreshLayerOverrides() {
		for (const auto& n : all_nodes_) {
			if (n->isDummy()) continue;
			int depth_rule_layer = 0;
			for (const auto& p : n->getParents())
				depth_rule_layer = std::max(depth_rule_layer, p->getLayer() + 1);
			n->setDesiredLayer(n->getLayer() > depth_rule_layer ? n->getLayer() : -1);
		}
	}

} // namespace hypergraph_logic
