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

	std::vector<JointGraphicalHypergraph::Component> JointGraphicalHypergraph::getComponents() const {
		std::vector<Component> components;
		for (const auto& block : getBlocks()) {
			Component c;
			c.nodes = block.nodes;
			c.left = block.left;
			c.right = block.right;
			for (Node* n : block.nodes)
				for (const auto& id : graphsOf(n)) c.graph_ids.insert(id);
			components.push_back(std::move(c));
		}
		return components;
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

	void JointGraphicalHypergraph::renameIncorporated(const std::string& id, const std::string& name) {
		if (incorporated_ids_.count(id)) incorporated_names_[id] = name;
	}

	void JointGraphicalHypergraph::pruneOrigins() {
		std::unordered_set<const Node*> alive;
		for (const auto& n : all_nodes_) alive.insert(n.get());
		for (auto it = origins_.begin(); it != origins_.end();)
			it = alive.count(it->first) ? std::next(it) : origins_.erase(it);
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
		// Erased explicitly too: a diagram with no box at all is not caught by removeGroup.
		incorporated_ids_.erase(id);
		incorporated_names_.erase(id);
		removeGroup(doomed);
	}

	void JointGraphicalHypergraph::beforeRemovingBoxes(const std::unordered_set<Node*>& doomed) {
		// The diagrams that have boxes in the group, and those that keep some.
		std::set<std::string> touched_ids, remaining;
		for (const auto& n : all_nodes_) {
			const auto ids = graphsOf(n.get());
			if (doomed.count(n.get())) {
				for (const auto& id : ids)
					if (!id.empty()) touched_ids.insert(id);
			}
			else {
				remaining.insert(ids.begin(), ids.end());
			}
		}
		for (Node* n : doomed) origins_.erase(n);

		// A diagram with no box left can be added again, as long as no remaining box
		// is of unknown origin (it could be one of its boxes).
		if (remaining.count(std::string())) return;
		for (const auto& id : touched_ids) {
			if (remaining.count(id)) continue;
			incorporated_ids_.erase(id);
			incorporated_names_.erase(id);
		}
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