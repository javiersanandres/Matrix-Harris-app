#pragma once
#include "GraphicalHypergraph.h"

#include <stdexcept>
#include <string>
#include <unordered_set>
#include <memory>

namespace hypergraph_logic {
	// ============================================================================
	// JointGraphicalHypergraph
	//
	// A per-project singleton that aggregates several GraphicalHypergraph
	// instances into a single joint view, side-by-side within shared layers.
	//
	// Ownership: JointGraphicalHypergraph is owned exclusively by the Project
	// object.  The Project constructs it via JointGraphicalHypergraph::create()
	// and holds the unique_ptr.  No second instance can be created while one is
	// alive; attempting to do so throws std::logic_error.
	//
	// Snapshots: addHypergraph() deep-copies the supplied graph before merging
	// it.  Subsequent mutations on the original graph are never reflected here,
	// and mutations performed on the joint graph are never reflected back.
	//
	// Node-creation API: createNode, createSource and createTarget are disabled.
	// All other public Hypergraph / GraphicalHypergraph methods (addConnection,
	// removeNode, removeConnection, removeSourcesFromHyperedge,
	// removeTargetsFromHyperedge, fuseNodes, computeLayout, …) remain active.
	//
	// Layer merging: when a graph is incorporated its nodes and edges are placed
	// into the joint's layers by the same index (layer 0 of the incoming graph
	// goes into layer 0 of the joint, etc.).  Layers that do not yet exist in
	// the joint are created on demand.  Depending on the 'left' argument the
	// incoming nodes and edges are prepended (left=true) or appended (left=false)
	// to the existing LayerData vectors, reflecting whether the user dropped the
	// graph to the left or right of all currently present content.
	// ============================================================================

#ifdef JGH_TEST
	namespace jointgraphicalhypergraph_tests { class TestableJoint; }
#endif
	class JointGraphicalHypergraph : public GraphicalHypergraph {
#ifdef JGH_TEST
		friend class jointgraphicalhypergraph_tests::TestableJoint;
#endif
	public:
		// ── Singleton management ─────────────────────────────────────────────────

		// ── create ───────────────────────────────────────────────────────────────
		//
		// Factory method — the only way to obtain a JointGraphicalHypergraph.
		// At most one instance may exist at any time within a single project
		// scope.  The caller (Project) is responsible for destroying the returned
		// object before creating a new one for a different project.
		//
		// Throws std::logic_error if an instance already exists.
		//
		static std::unique_ptr<JointGraphicalHypergraph> create(const std::string& name);

		// Destructor releases the singleton slot so a new instance can be created.
		~JointGraphicalHypergraph();

		// Non-copyable, non-movable — the singleton guarantee would be violated.
		JointGraphicalHypergraph(const JointGraphicalHypergraph&) = delete;
		JointGraphicalHypergraph& operator=(const JointGraphicalHypergraph&) = delete;
		JointGraphicalHypergraph(JointGraphicalHypergraph&&) = delete;
		JointGraphicalHypergraph& operator=(JointGraphicalHypergraph&&) = delete;

		// ── Disabled node-creation API ────────────────────────────────────────────
		//
		// Nodes may only enter the joint graph through addHypergraph().
		// Calling any of these methods throws std::logic_error.

		NodePtr createNode(const std::string&, int, const NodePtr&) {
			throw std::logic_error(
				"JointGraphicalHypergraph: createNode is disabled. "
				"Add nodes via addHypergraph().");
		}
		NodePtr createNode(const std::string&, const HyperedgePtr&) {
			throw std::logic_error(
				"JointGraphicalHypergraph: createNode is disabled. "
				"Add nodes via addHypergraph().");
		}
		NodePtr createSource(const std::string&, int, const HyperedgePtr&) {
			throw std::logic_error(
				"JointGraphicalHypergraph: createSource is disabled. "
				"Add nodes via addHypergraph().");
		}
		NodePtr createTarget(const std::string&, int, const HyperedgePtr&) {
			throw std::logic_error(
				"JointGraphicalHypergraph: createTarget is disabled. "
				"Add nodes via addHypergraph().");
		}

		// ── addHypergraph ─────────────────────────────────────────────────────────────
		//
		// Incorporates a deep copy of the given GraphicalHypergraph into the joint.
		//
		// The supplied graph is identified by its unique ID. Attempting to add a graph
		// whose ID has already been incorporated throws std::invalid_argument. Because
		// clone() preserves the ID, passing a clone of a previously added graph is
		// also rejected — the check is on logical identity, not pointer equality.
		//
		// Internally, addHypergraph clones the source graph and delegates the actual
		// structural merge to GraphicalHypergraph::mergeFrom(), which splices all
		// nodes, edges, segments, layer memberships, and layout data into the joint.
		// The original graph is never modified.
		//
		// The left flag controls the horizontal placement of the incoming graph
		// relative to the content already present in the joint:
		//   left = true  — the incoming graph is placed to the left of all existing
		//                  content (nodes and edges are prepended in each layer).
		//   left = false — the incoming graph is placed to the right of all existing
		//                  content (nodes and edges are appended in each layer).
		void addHypergraph(GraphicalHypergraph& g, bool left);

		// ── getIncorporatedIds ────────────────────────────────────────────────────
		//
		// Returns the set of IDs of every GraphicalHypergraph that has been
		// incorporated into this joint so far.
		//
		const std::unordered_set<std::string>& getIncorporatedIds() const;

	private:
		// Private constructor — use create().
		explicit JointGraphicalHypergraph(const std::string& name);

		// Singleton guard: true while any instance is alive.
		static bool instance_exists_;

		// IDs of graphs that have already been incorporated, used to enforce
		// the "no duplicate" rule in addHypergraph().
		std::unordered_set<std::string> incorporated_ids_;
	};

} // namespace hypergraph_logic