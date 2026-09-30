#include "JointGraphicalHypergraph.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>

using json = nlohmann::json;

namespace hypergraph_logic {

	// ============================================================================
	// Singleton guard
	// ============================================================================
	bool JointGraphicalHypergraph::instance_exists_ = false;

	// ============================================================================
	// Construction — live instance
	// ============================================================================
	JointGraphicalHypergraph::JointGraphicalHypergraph(const std::string& name)
		: GraphicalHypergraph(name)
		, is_snapshot_(false)
	{
	}

	// ============================================================================
	// Construction — snapshot instance
	//
	// The two-parameter overload is used exclusively by cloneJoint(). The extra
	// ids parameter distinguishes it from the live constructor without needing a
	// tag type or bool — if ids is being supplied, this is a snapshot.
	// ============================================================================
	JointGraphicalHypergraph::JointGraphicalHypergraph(
		const std::string& name,
		const std::unordered_set<std::string>& ids)
		: GraphicalHypergraph(name)
		, is_snapshot_(true)
		, incorporated_ids_(ids)
	{
	}

	// ============================================================================
	// Destruction
	// ============================================================================
	JointGraphicalHypergraph::~JointGraphicalHypergraph() {
		if (!is_snapshot_)
			instance_exists_ = false;
	}

	// ============================================================================
	// create
	// ============================================================================
	std::unique_ptr<JointGraphicalHypergraph>
		JointGraphicalHypergraph::create(const std::string& name) {
		if (instance_exists_) {
			throw std::logic_error(
				"JointGraphicalHypergraph::create: a JointGraphicalHypergraph "
				"already exists for this project. Destroy it before creating another.");
		}
		instance_exists_ = true;
		return std::unique_ptr<JointGraphicalHypergraph>(
			new JointGraphicalHypergraph(name));
	}

	// ============================================================================
	// cloneJoint
	// ============================================================================
	std::unique_ptr<JointGraphicalHypergraph>
		JointGraphicalHypergraph::cloneJoint() const {
		// Allocate the snapshot with the two-parameter constructor, which sets
		// is_snapshot_ = true and copies incorporated_ids_.
		auto snap = std::unique_ptr<JointGraphicalHypergraph>(
			new JointGraphicalHypergraph(name_, incorporated_ids_));

		// Deep-copy the structural and layout data via the base class helper.
		snap->mergeFrom(GraphicalHypergraph::clone(), false);

		// Restore the original id_ (clone() already carries it, but be explicit).
		snap->id_ = id_;

		// clone() keeps the order of all_nodes_, and mergeFrom appends it, so the
		// i-th box of the copy is the copy of our i-th box.
		snap->incorporated_names_ = incorporated_names_;
		for (size_t i = 0; i < all_nodes_.size(); ++i) {
			auto it = origins_.find(all_nodes_[i].get());
			if (it != origins_.end()) snap->origins_[snap->all_nodes_[i].get()] = it->second;
		}

		return snap;
	}

	// ============================================================================
	// Diagrams inside the joint: components and regions
	// ============================================================================

	std::set<std::string> JointGraphicalHypergraph::graphsOf(const Node* node) const {
		if (!node || node->isDummy()) return {};
		auto it = origins_.find(node);
		if (it == origins_.end()) return { std::string() }; // unknown (older files)
		return it->second;
	}

	std::vector<JointGraphicalHypergraph::Component>
		JointGraphicalHypergraph::componentsExcluding(const std::unordered_set<Node*>& excluded) const
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

		std::unordered_map<int, size_t> component_of_root;
		std::vector<Component> components;
		for (size_t i = 0; i < nodes.size(); ++i) {
			const int root = find(static_cast<int>(i));
			auto [it, is_new] = component_of_root.emplace(root, components.size());
			if (is_new) {
				components.emplace_back();
				components.back().left = std::numeric_limits<double>::infinity();
				components.back().right = -std::numeric_limits<double>::infinity();
			}
			Component& c = components[it->second];
			Node* n = nodes[i];
			c.nodes.push_back(n);
			for (const auto& id : graphsOf(n)) c.graph_ids.insert(id);
			auto layout = node_layout_.find(n);
			if (layout != node_layout_.end()) {
				c.left = std::min(c.left, layout->second.x - n->getWidth() / 2.0);
				c.right = std::max(c.right, layout->second.x + n->getWidth() / 2.0);
			}
		}
		for (auto& c : components)
			if (c.left > c.right) c.left = c.right = 0.0; // no coordinates yet
		return components;
	}

	std::vector<JointGraphicalHypergraph::Component> JointGraphicalHypergraph::getComponents() const {
		return componentsExcluding({});
	}

	std::vector<std::pair<double, double>>
		JointGraphicalHypergraph::mergeSpans(std::vector<std::pair<double, double>> spans)
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

	std::vector<std::pair<double, double>> JointGraphicalHypergraph::getOccupiedRegions() const {
		std::vector<std::pair<double, double>> spans;
		for (const auto& c : getComponents()) spans.emplace_back(c.left, c.right);
		return mergeSpans(std::move(spans));
	}

	std::vector<std::pair<double, double>>
		JointGraphicalHypergraph::getOccupiedRegionsExcluding(const std::unordered_set<Node*>& group) const
	{
		std::vector<std::pair<double, double>> spans;
		for (const auto& c : componentsExcluding(group)) spans.emplace_back(c.left, c.right);
		return mergeSpans(std::move(spans));
	}

	// Where to insert a group clicked at click_x, given the occupied regions of
	// the others: a point x such that its boxes go between the boxes whose centre
	// is left of x and those right of it.
	double JointGraphicalHypergraph::placementPoint(
		const std::vector<std::pair<double, double>>& regions, double click_x)
	{
		for (const auto& [lo, hi] : regions)
			if (lo <= click_x && click_x <= hi)
				return click_x < (lo + hi) / 2.0 ? lo : hi; // the exact middle goes right
		return click_x;
	}

	bool JointGraphicalHypergraph::isSeparable(const std::string& id) const {
		if (!incorporated_ids_.count(id)) return false;
		for (const auto& c : getComponents()) {
			// Boxes of unknown origin (older files) could be this diagram's.
			if (c.graph_ids.count(std::string())) return false;
			if (c.graph_ids.count(id) && c.graph_ids.size() > 1) return false;
		}
		return true;
	}

	std::unordered_set<Node*> JointGraphicalHypergraph::separableNodesOf(const std::string& id) const {
		if (!incorporated_ids_.count(id))
			throw std::invalid_argument("Ese esquema no forma parte del esquema conjunto.");
		std::unordered_set<Node*> nodes;
		for (const auto& c : getComponents()) {
			if (c.graph_ids.count(std::string()))
				throw std::logic_error("Este esquema conjunto se guardó con una versión anterior y no se "
				                       "sabe a qué esquema pertenece cada caja.");
			if (!c.graph_ids.count(id)) continue;
			if (c.graph_ids.size() > 1) {
				const std::string name = getIncorporatedName(id);
				throw std::logic_error("El esquema" + (name.empty() ? std::string() : " «" + name + "»")
					+ " está unido a otros esquemas del conjunto (por conexiones o cajas fusionadas). "
					  "Quita antes esas conexiones.");
			}
			nodes.insert(c.nodes.begin(), c.nodes.end());
		}
		return nodes;
	}

	std::string JointGraphicalHypergraph::getIncorporatedName(const std::string& id) const {
		auto it = incorporated_names_.find(id);
		return it == incorporated_names_.end() ? std::string() : it->second;
	}

	void JointGraphicalHypergraph::pruneOrigins() {
		std::unordered_set<const Node*> alive;
		for (const auto& n : all_nodes_) alive.insert(n.get());
		for (auto it = origins_.begin(); it != origins_.end();)
			it = alive.count(it->first) ? std::next(it) : origins_.erase(it);
	}

	void JointGraphicalHypergraph::ensureLayout() {
		for (const auto& n : all_nodes_)
			if (!node_layout_.count(n.get())) { computeLayout({}); return; }
	}

	// ============================================================================
	// addHypergraph
	// ============================================================================
	void JointGraphicalHypergraph::addHypergraph(GraphicalHypergraph& g, double click_x) {
		if (incorporated_ids_.count(g.getId()))
			throw std::invalid_argument(
				"El esquema «" + g.getName() + "» ya forma parte del esquema conjunto.");

		ensureLayout();
		pruneOrigins();
		const double at_x = placementPoint(getOccupiedRegions(), click_x);

		GraphicalHypergraph copy = g.clone();
		for (const auto& n : copy.getAllNodes())
			if (!n->isDummy()) origins_[n.get()] = { g.getId() };

		// In every layer, between the boxes centred left of at_x and the rest.
		mergeFrom(std::move(copy), [this, at_x](int layer) {
			auto it = layers_.find(layer);
			if (it == layers_.end()) return size_t{ 0 };
			size_t before = 0;
			for (const auto& n : it->second.nodes) {
				auto layout = node_layout_.find(n.get());
				if (layout != node_layout_.end() && layout->second.x < at_x) ++before;
			}
			return before;
		});
		incorporated_ids_.insert(g.getId());
		incorporated_names_[g.getId()] = g.getName();
		computeLayout({});
	}

	void JointGraphicalHypergraph::addHypergraph(GraphicalHypergraph& g, bool left) {
		addHypergraph(g, left ? -std::numeric_limits<double>::infinity()
		                      : std::numeric_limits<double>::infinity());
	}

	// ============================================================================
	// removeHypergraph
	// ============================================================================
	void JointGraphicalHypergraph::removeHypergraph(const std::string& id) {
		const std::unordered_set<Node*> doomed = separableNodesOf(id);

		// Their components are closed: every hyperedge touching them is theirs.
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
		std::erase_if(all_nodes_, [&](const NodePtr& n) { return doomed.count(n.get()) > 0; });
		for (const auto& e : doomed_originals) all_hyperedges_.erase(e);
		for (Node* n : doomed) { node_layout_.erase(n); origins_.erase(n); }
		for (Hyperedge* e : doomed_edges) edge_layout_.erase(e);

		incorporated_ids_.erase(id);
		incorporated_names_.erase(id);
		cleanUp(); // layers left empty are closed up
		computeLayout({});
	}

	// ============================================================================
	// moveHypergraph
	// ============================================================================
	void JointGraphicalHypergraph::moveHypergraph(const std::string& id, double click_x) {
		moveGroup(separableNodesOf(id), click_x, std::nullopt);
	}

	void JointGraphicalHypergraph::moveHypergraphToLayer(const std::string& id, int top_layer) {
		moveGroup(separableNodesOf(id), std::nullopt, top_layer);
	}

	void JointGraphicalHypergraph::moveHypergraph(const std::string& id, double click_x, int top_layer) {
		moveGroup(separableNodesOf(id), click_x, top_layer);
	}

	std::unordered_set<Node*> JointGraphicalHypergraph::componentNodesOf(const Node* box) const {
		for (const auto& c : getComponents())
			if (std::find(c.nodes.begin(), c.nodes.end(), box) != c.nodes.end())
				return { c.nodes.begin(), c.nodes.end() };
		throw std::invalid_argument("La caja no forma parte del esquema conjunto.");
	}

	void JointGraphicalHypergraph::moveComponent(const Node* box, double click_x) {
		moveGroup(componentNodesOf(box), click_x, std::nullopt);
	}

	void JointGraphicalHypergraph::moveComponentToLayer(const Node* box, int top_layer) {
		moveGroup(componentNodesOf(box), std::nullopt, top_layer);
	}

	void JointGraphicalHypergraph::moveComponent(const Node* box, double click_x, int top_layer) {
		moveGroup(componentNodesOf(box), click_x, top_layer);
	}

	void JointGraphicalHypergraph::moveGroup(const std::unordered_set<Node*>& group,
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
			std::vector<NodePtr> block;
			for (const auto& n : data.nodes)
				if (group.count(n.get())) block.push_back(n);
			std::vector<HyperedgePtr> edges;
			for (const auto& e : data.outgoing_edges)
				if (group_edges.count(e.get())) edges.push_back(e);
			if (block.empty() && edges.empty()) continue;

			LayerData& dst = rebuilt[layer + shift_group];
			size_t before = 0;
			for (const auto& n : dst.nodes) {
				auto layout = node_layout_.find(n.get());
				if (layout != node_layout_.end() && layout->second.x < at_x) ++before;
			}
			dst.nodes.insert(dst.nodes.begin() + static_cast<std::ptrdiff_t>(before), block.begin(), block.end());
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

	void JointGraphicalHypergraph::refreshLayerOverrides() {
		for (const auto& n : all_nodes_) {
			if (n->isDummy()) continue;
			int depth_rule_layer = 0;
			for (const auto& p : n->getParents())
				depth_rule_layer = std::max(depth_rule_layer, p->getLayer() + 1);
			n->setDesiredLayer(n->getLayer() > depth_rule_layer ? n->getLayer() : -1);
		}
	}

	// ============================================================================
	// fuseNodes
	// ============================================================================
	void JointGraphicalHypergraph::fuseNodes(const NodePtr& node1, const NodePtr& node2,
		const NodeAttributes& new_attributes, std::set<int>* out_altered_layers)
	{
		std::set<std::string> both = graphsOf(node1.get());
		for (const auto& id : graphsOf(node2.get())) both.insert(id);
		Node* a = node1.get();
		Node* b = node2.get();

		GraphicalHypergraph::fuseNodes(node1, node2, new_attributes, out_altered_layers);

		// One of them survives (see Hypergraph::fuseNodes) and belongs to both diagrams.
		const bool a_alive = std::any_of(all_nodes_.begin(), all_nodes_.end(),
			[a](const NodePtr& n) { return n.get() == a; });
		origins_[a_alive ? a : b] = std::move(both);
		origins_.erase(a_alive ? b : a);
	}

	// ============================================================================
	// getIncorporatedIds
	// ============================================================================
	const std::unordered_set<std::string>&
		JointGraphicalHypergraph::getIncorporatedIds() const {
		return incorporated_ids_;
	}

	// ============================================================================
	// toJSON (json& overload)
	//
	// Writes everything the base class writes, then appends the incorporated_ids_
	// set so the joint can be fully restored without re-adding each diagram.
	// ============================================================================
	void JointGraphicalHypergraph::toJSON(nlohmann::json& j) const {
		GraphicalHypergraph::toJSON(j);   // topology, layout, id, name

		nlohmann::json ids = nlohmann::json::array();
		for (const auto& id : incorporated_ids_)
			ids.push_back(id);
		j["incorporated_ids"] = std::move(ids);

		nlohmann::json names = nlohmann::json::object();
		for (const auto& [id, name] : incorporated_names_)
			names[id] = name;
		j["incorporated_names"] = std::move(names);

		// The diagram(s) of every box, in the order of "nodes" (a box's id is
		// its index there): [] for dummies and boxes of unknown origin.
		nlohmann::json node_graphs = nlohmann::json::array();
		for (const auto& n : all_nodes_) {
			nlohmann::json graphs = nlohmann::json::array();
			auto it = origins_.find(n.get());
			if (!n->isDummy() && it != origins_.end())
				for (const auto& id : it->second) graphs.push_back(id);
			node_graphs.push_back(std::move(graphs));
		}
		j["node_graphs"] = std::move(node_graphs);
	}

	// ============================================================================
	// toJSON (file-path overload — thin wrapper)
	// ============================================================================
	void JointGraphicalHypergraph::toJSON(const std::string& path) const {
		nlohmann::json j;
		toJSON(j);
		std::ofstream file(path);
		if (!file.is_open())
			throw std::runtime_error(
				"JointGraphicalHypergraph::toJSON: cannot open file: " + path);
		file << j.dump(2);
	}

	// ============================================================================
	// fromJSON (json& overload)
	// ============================================================================
	std::unique_ptr<JointGraphicalHypergraph>
		JointGraphicalHypergraph::fromJSON(const nlohmann::json& j) {
		if (instance_exists_)
			throw std::logic_error(
				"JointGraphicalHypergraph::fromJSON: a live instance already exists.");

		GraphicalHypergraph base = GraphicalHypergraph::fromJSON(j);

		std::unordered_set<std::string> ids;
		if (j.contains("incorporated_ids")) {
			for (const auto& entry : j.at("incorporated_ids"))
				ids.insert(entry.get<std::string>());
		}

		instance_exists_ = true;
		auto joint = std::unique_ptr<JointGraphicalHypergraph>(
			new JointGraphicalHypergraph(base.getName(), ids));
		joint->is_snapshot_ = false;

		// Reuse the same pattern as cloneJoint: mergeFrom accepts an rvalue
		// GraphicalHypergraph and replicates all structural and layout data.
		joint->mergeFrom(std::move(base), false);
		joint->id_ = j.at("id").get<std::string>();

		// Files saved before these existed have no names and no box origins: their
		// boxes come from an unknown diagram, so no diagram is separable.
		if (j.contains("incorporated_names"))
			for (const auto& [id, name] : j.at("incorporated_names").items())
				joint->incorporated_names_[id] = name.get<std::string>();
		if (j.contains("node_graphs")) {
			const auto& node_graphs = j.at("node_graphs");
			// fromJSON builds all_nodes_ in the order of "nodes", as saved.
			for (size_t i = 0; i < joint->all_nodes_.size() && i < node_graphs.size(); ++i) {
				std::set<std::string> graphs;
				for (const auto& id : node_graphs[i]) graphs.insert(id.get<std::string>());
				if (!graphs.empty()) joint->origins_[joint->all_nodes_[i].get()] = std::move(graphs);
			}
		}

		return joint;
	}

	// ============================================================================
	// fromJSON (file-path overload — thin wrapper)
	// ============================================================================
	std::unique_ptr<JointGraphicalHypergraph>
		JointGraphicalHypergraph::fromJSON(const std::string& path) {
		std::ifstream file(path);
		if (!file.is_open())
			throw std::runtime_error(
				"JointGraphicalHypergraph::fromJSON: cannot open file: " + path);

		nlohmann::json j;
		try {
			file >> j;
		}
		catch (const nlohmann::json::parse_error& e) {
			throw std::runtime_error(
				std::string("JointGraphicalHypergraph::fromJSON: JSON parse error: ")
				+ e.what());
		}

		return fromJSON(j);
	}

} // namespace hypergraph_logic