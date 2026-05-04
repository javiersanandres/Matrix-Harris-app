#include "JointGraphicalHypergraph.h"

#include <atomic>
#include <stdexcept>

namespace hypergraph_logic {

	// ============================================================================
	// Singleton guard
	// ============================================================================
	bool JointGraphicalHypergraph::instance_exists_ = false;

	// ============================================================================
	// Construction / destruction
	// ============================================================================
	JointGraphicalHypergraph::JointGraphicalHypergraph(const std::string& name)
		: GraphicalHypergraph(name)
	{
		// id_ is assigned by GraphicalHypergraph's constructor via generateId(),
		// so the joint graph itself has a unique identity just like any other graph.
	}

	JointGraphicalHypergraph::~JointGraphicalHypergraph() {
		instance_exists_ = false;
	}

	std::unique_ptr<JointGraphicalHypergraph>
		JointGraphicalHypergraph::create(const std::string& name) {
		if (instance_exists_) {
			throw std::logic_error(
				"JointGraphicalHypergraph::create: a JointGraphicalHypergraph "
				"already exists for this project. Destroy it before creating another.");
		}
		instance_exists_ = true;
		// Cannot use make_unique because the constructor is private.
		return std::unique_ptr<JointGraphicalHypergraph>(
			new JointGraphicalHypergraph(name));
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


} // namespace hypergraph_logic