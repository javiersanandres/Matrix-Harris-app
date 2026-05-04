#include "JointGraphicalHypergraph.h"

#include <fstream>
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

		return snap;
	}

	// ============================================================================
	// addHypergraph
	// ============================================================================
	void JointGraphicalHypergraph::addHypergraph(GraphicalHypergraph& g, bool left) {
		if (incorporated_ids_.count(g.getId()))
			throw std::invalid_argument(
				"JointGraphicalHypergraph::addHypergraph: graph with id '"
				+ g.getId() + "' has already been incorporated.");

		mergeFrom(g.clone(), left);
		incorporated_ids_.insert(g.getId());
		computeLayout();
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