#include "JointGraphicalHypergraph.h"

#include <stdexcept>

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
		// id_ is assigned by GraphicalHypergraph's constructor via generateId(),
		// so the joint graph itself has a unique identity just like any other graph.
	}

	// ============================================================================
	// Construction — snapshot instance
	//
	// This is another constructor overload that "bypasses" the singleton guard,
	// used exclusively by cloneJoint() to produce snapshot instances that live 
	// inside the undo/redo system. 
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
		// Only the live instance resets the guard. Snapshot instances that live
		// inside the undo/redo deques must not clear it — the live instance is still
		// alive when they are created and destroyed.
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
	//
	// Produces a fully independent snapshot of this joint graph. The structural
	// deep copy is delegated to GraphicalHypergraph::clone(), which handles all
	// nodes, edges, segments, layer data, and layout maps. The snapshot constructor
	// then copies incorporated_ids_ on top of the cloned base state.
	// ============================================================================
	std::unique_ptr<JointGraphicalHypergraph>
		JointGraphicalHypergraph::cloneJoint() const {
		// Use the snapshot constructor to allocate the clone without touching the
		// singleton guard.
		auto snap = std::unique_ptr<JointGraphicalHypergraph>(
			new JointGraphicalHypergraph(name_, incorporated_ids_));

		// Delegate the deep structural copy to the base class helper. mergeFrom
		// accepts an rvalue GraphicalHypergraph, so we produce a base clone first
		// and move it in. This replicates all nodes, edges, layers, and layout maps.
		snap->mergeFrom(GraphicalHypergraph::clone(), false);

		// Restore the original id_ (mergeFrom preserves its, but just for clarity).
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

} // namespace hypergraph_logic