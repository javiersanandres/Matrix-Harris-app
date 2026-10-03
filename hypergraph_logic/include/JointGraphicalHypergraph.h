#pragma once
#include "GraphicalHypergraph.h"

#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using json = nlohmann::json;

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
	// removeTargetsFromHyperedge, fuseNodes, computeLayout, ...) remain active.
	//
	// Layer merging: when a graph is incorporated its nodes and edges are placed
	// into the joint's layers by the same index (layer 0 of the incoming graph
	// goes into layer 0 of the joint, etc.).  Layers that do not yet exist in
	// the joint are created on demand.  Where in each layer its nodes go depends
	// on the clicked x and on the regions the other diagrams occupy (see
	// "Diagrams inside the joint" below).
	//
	// Singleton guard:
	// instance_exists_ tracks whether the live (non-snapshot) instance exists.
	// Snapshot instances created by cloneJoint() carry is_snapshot_ = true and
	// do not affect the guard: they are internal undo/redo bookkeeping objects,
	// not user-facing singletons.  Only the instance returned by create() has
	// is_snapshot_ = false and its destructor resets the guard.
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
		// Factory method — the only way to obtain a live JointGraphicalHypergraph.
		// At most one live instance may exist at any time within a single project
		// scope.  The caller (Project) is responsible for destroying the returned
		// object before creating a new one for a different project.
		//
		// Throws std::logic_error if a live instance already exists.
		static std::unique_ptr<JointGraphicalHypergraph> create(const std::string& name);

		// ── cloneJoint ───────────────────────────────────────────────────────────
		//
		// Produces a fully independent deep copy of this joint graph, including all
		// layout data and the incorporated_ids_ set. The clone is flagged as a
		// snapshot (is_snapshot_ = true) so its destructor does not release the
		// singleton slot — snapshot instances are internal undo/redo bookkeeping
		// objects and must not interfere with the live singleton guard.
		//
		// Called exclusively by JointHypergraphEditor::snapshot().
		std::unique_ptr<JointGraphicalHypergraph> cloneJoint() const;

		// Destructor: releases the singleton slot only if this is the live instance.
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
		NodePtr createNode(const NodeAttributes&, int, int, const NodePtr&) {
			throw std::logic_error(
				"JointGraphicalHypergraph: createNode is disabled. "
				"Add nodes via addHypergraph().");
		}
		NodePtr createNode(const NodeAttributes&, const HyperedgePtr&) {
			throw std::logic_error(
				"JointGraphicalHypergraph: createNode is disabled. "
				"Add nodes via addHypergraph().");
		}
		NodePtr createSource(const NodeAttributes&, int, const HyperedgePtr&) {
			throw std::logic_error(
				"JointGraphicalHypergraph: createSource is disabled. "
				"Add nodes via addHypergraph().");
		}
		NodePtr createTarget(const NodeAttributes&, int, const HyperedgePtr&) {
			throw std::logic_error(
				"JointGraphicalHypergraph: createTarget is disabled. "
				"Add nodes via addHypergraph().");
		}
		NodePtr createParent(const NodeAttributes&, const NodePtr&, std::set<int>* = nullptr) {
			throw std::logic_error(
				"JointGraphicalHypergraph: createParent is disabled. "
				"Add nodes via addHypergraph().");
		}
		NodePtr createNodeInEdge(const NodeAttributes&, const HyperedgePtr&, std::set<int>* = nullptr) {
			throw std::logic_error(
				"JointGraphicalHypergraph: createNodeInEdge is disabled. "
				"Add nodes via addHypergraph().");
		}
		NodePtr createNodeNextTo(const NodeAttributes&, const NodePtr&, bool) {
			throw std::logic_error(
				"JointGraphicalHypergraph: createNodeNextTo is disabled. "
				"Add nodes via addHypergraph().");
		}

		// ============================================================================
		// Diagrams inside the joint
		//
		// Every box remembers the diagram(s) it came from (graphsOf): one, or
		// several once boxes of different diagrams are fused. Dummy boxes belong
		// to no diagram.
		//
		// Components are the graph's blocks (see GraphicalHypergraph's "Blocks"),
		// each with the diagrams its boxes came from.
		//
		// A diagram is *mixed* when one of its components also holds boxes of
		// another diagram: a connection was added between them, or boxes of both
		// were fused. Removing those connections unmixes it again (a fusion can
		// only be undone). While a diagram is not mixed it can be removed from the
		// joint or moved to another place.
		//
		// Components and regions are computed from the current structure and
		// layout on every call (linear in the size of the graph), so they are
		// always consistent with whatever operation ran last.
		// ============================================================================

		struct Component {
			std::vector<Node*> nodes;           // dummies included
			std::set<std::string> graph_ids;    // diagrams of its real boxes
			double left = 0.0;                  // span (from the current layout)
			double right = 0.0;
		};

		// A whole diagram is placed at a clicked x like a block is (see
		// GraphicalHypergraph's "Blocks"); only the coordinates are then
		// recomputed (Brandes-Köpf and ports; no crossing minimization nor
		// bar-ordering MIP).

		// ── addHypergraph ─────────────────────────────────────────────────────────
		//
		// Incorporates a deep copy of the given GraphicalHypergraph into the joint,
		// placed at click_x as described above. The original graph is never
		// modified.
		//
		// The supplied graph is identified by its unique ID. Attempting to add a
		// graph whose ID has already been incorporated throws std::invalid_argument.
		// Because clone() preserves the ID, passing a clone of a previously added
		// graph is also rejected.
		//
		void addHypergraph(GraphicalHypergraph& g, double click_x);

		// Left (true) or right (false) of everything already in the joint.
		void addHypergraph(GraphicalHypergraph& g, bool left);

		// ── removeHypergraph ──────────────────────────────────────────────────────
		//
		// Takes a diagram that is not mixed out of the joint: all its boxes and
		// connections go, layers left empty are closed up, and the diagram can be
		// added again later. Throws std::invalid_argument if it was never added
		// and std::logic_error if it is mixed.
		//
		void removeHypergraph(const std::string& id);

		// ── Moving diagrams ───────────────────────────────────────────────────────
		//
		// A diagram that is not mixed has no connection to the rest of the joint,
		// so it moves as a whole exactly like a block does (see
		// GraphicalHypergraph's "Blocks"; the blocks themselves are moved and
		// removed with moveComponent / removeComponent, mixed or not). Removing a
		// block also stops counting as added every diagram left without any box
		// in the joint, so it can be added again (unless boxes of unknown origin
		// remain, which could be its). A diagram throws like removeHypergraph.
		//
		void moveHypergraph(const std::string& id, double click_x);
		void moveHypergraphToLayer(const std::string& id, int top_layer);
		void moveHypergraph(const std::string& id, double click_x, int top_layer);

		// ── Queries ───────────────────────────────────────────────────────────────

		std::vector<Component> getComponents() const;

		// Boxes (dummies included) that moveHypergraph would move. Throws as it does.
		std::unordered_set<Node*> getHypergraphNodes(const std::string& id) const { return separableNodesOf(id); }

		// Diagram(s) a box came from (empty for a dummy box).
		std::set<std::string> graphsOf(const Node* node) const;

		// True when the diagram was added and is not mixed (it can be removed or
		// moved). False for a diagram that is not in the joint, and for every
		// diagram while the joint holds boxes of unknown origin.
		bool isSeparable(const std::string& id) const;

		// ── getIncorporatedIds ────────────────────────────────────────────────────
		//
		// Returns the set of IDs of every GraphicalHypergraph that has been
		// incorporated into this joint so far.
		//
		const std::unordered_set<std::string>& getIncorporatedIds() const;

		// Name the diagram is known by in the joint: the one it had when it was
		// added, until renameIncorporated gives it the diagram's current one.
		std::string getIncorporatedName(const std::string& id) const;

		// Follows a rename of the diagram. Does nothing for a diagram that is not
		// in the joint. Not an operation of its own: the name belongs to the
		// diagram, and the joint only mirrors it (see Project::syncJointNames).
		void renameIncorporated(const std::string& id, const std::string& name);

		// ── Operations that change which diagram a box belongs to ────────────────
		//
		// fuseNodes: the surviving box belongs to the diagrams of both boxes.
		//
		void fuseNodes(const NodePtr& node1, const NodePtr& node2,
			const NodeAttributes& new_attributes, std::set<int>* out_altered_layers = nullptr);

		// ── Persistence ───────────────────────────────────────────────────────────

		// ── toJSON ────────────────────────────────────────────────────────────────
		//
		// Extends GraphicalHypergraph::toJSON(json&) by also writing the
		// incorporated_ids_ set under the key "incorporated_ids". This allows the
		// joint to be fully round-tripped without re-adding each diagram manually.
		//
		void toJSON(nlohmann::json& j) const;

		// Convenience overload: serializes to a JSON file at the given path.
		void toJSON(const std::string& path) const;

		// ── fromJSON ──────────────────────────────────────────────────────────────
		//
		// Static factory that reconstructs a live JointGraphicalHypergraph from a
		// json object previously produced by toJSON(json&), restoring both the
		// graph topology/layout and the incorporated_ids_ set.
		// Throws std::logic_error if a live instance already exists.
		//
		static std::unique_ptr<JointGraphicalHypergraph> fromJSON(const nlohmann::json& j);

		// Convenience overload: deserializes from a JSON file at the given path.
		// Throws std::runtime_error if the file cannot be opened or is malformed.
		static std::unique_ptr<JointGraphicalHypergraph> fromJSON(const std::string& path);

	private:
		// ── Private constructors ──────────────────────────────────────────────────

		// Constructor for live instances (called by create()).
		explicit JointGraphicalHypergraph(const std::string& name);

		// Constructor for snapshot instances (called by cloneJoint()).
		// Bypasses the singleton guard entirely; is_snapshot_ is set to true.
		explicit JointGraphicalHypergraph(
			const std::string& name,
			const std::unordered_set<std::string>& ids);

		// ── Singleton guard ───────────────────────────────────────────────────────
		//
		// true while the live (non-snapshot) instance exists.
		// Snapshot instances do not set or clear this flag.
		//
		static bool instance_exists_;

		// true for snapshot instances created by cloneJoint().
		// This allows multiple copies without really violating the singleton guarantee,
		// since logically there is only one joint graph in the current project state.
		bool is_snapshot_ = false;

		// IDs of graphs that have already been incorporated, used to enforce
		// the "no duplicate" rule in addHypergraph().
		std::unordered_set<std::string> incorporated_ids_;

		// Name of each incorporated graph when it was added.
		std::unordered_map<std::string, std::string> incorporated_names_;

		// Diagram(s) each real box came from. Keyed by the raw pointer of a box
		// that is (or was) in the graph; entries of boxes no longer in the graph
		// are ignored and pruned (see graphsOf / pruneOrigins).
		std::unordered_map<const Node*, std::set<std::string>> origins_;

		// ── Helpers ───────────────────────────────────────────────────────────────

		void pruneOrigins();

		// Boxes (dummies included) of the components that belong to diagram id
		// alone. Throws as removeHypergraph / moveHypergraph document.
		std::unordered_set<Node*> separableNodesOf(const std::string& id) const;

		// Before a block (or diagram) goes: forgets its boxes' origins, and stops
		// counting as added every diagram that had boxes in it and has none left.
		void beforeRemovingBoxes(const std::unordered_set<Node*>& doomed) override;
	};

} // namespace hypergraph_logic