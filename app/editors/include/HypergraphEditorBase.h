#pragma once
#include "GraphicalHypergraph.h"

#include <deque>
#include <stdexcept>
#include <string>


using json = nlohmann::json;

namespace app_logic {
	using namespace hypergraph_logic;

	// ============================================================================
	// HypergraphEditorBase<Derived>
	//
	// CRTP base class that implements all shared undo/redo machinery and the full
	// set of forwarding methods that are common to both HypergraphEditor and
	// JointHypergraphEditor.
	//
	// We cannot keep the undo and redo stacks in the base class because the
	// snapshot type for JointGraphicalHypergraph is completely different from the
	// snapshot type for GraphicalHypergraph. So this logic is handled by the
	// derived classes.
	//
	// Transactional snapshot model:
	// Every mutating method in this base follows the same explicit sequence:
	//   1. Clone the live graph via derived().takeSnapshot() into a local variable.
	//   2. Attempt the mutation (and computeLayout if needed) inside a try block.
	//   3. On SUCCESS  -> call derived().commitSnapshot(std::move(saved)).
	//      The snapshot lands on past_ and future_ is cleared only now.
	//   4. On FAILURE  -> the catch block rethrows. The local clone is destroyed
	//      automatically. The live graph and the undo stack are left untouched.
	//
	// This guarantees that a snapshot is never committed for an operation that
	// failed, which would waste memory and corrupt the undo history.
	//
	// undo(): saves current state to future_, restores top of past_.
	// redo(): saves current state to past_,   restores top of future_.
	//
	// Read-only queries and toJSON() never snapshot.
	// ============================================================================
	template <typename Derived>
	class HypergraphEditorBase {
	public:

		// This is the maximum number of snapshots that the undo and redo stacks
		// can hold. When the cap is exceeded, the oldest snapshot is discarded.
		// It is unlikely that the user will want to undo more than 25 steps, and
		// keeping more snapshots in memory would be wasteful.
		static constexpr int MAX_HISTORY = 25;

		// ── Undo / Redo ───────────────────────────────────────────────────────────

		// ── canUndo ───────────────────────────────────────────────────────────────
		//
		// Returns true if there is at least one state in the undo stack.
		// The graphical engine can use this to enable or disable the undo
		// button in the UI.
		bool canUndo() const { return derived().canUndo(); }

		// ── canRedo ───────────────────────────────────────────────────────────────
		//
		// Returns true if there is at least one state in the redo stack.
		// The graphical engine can use this to enable or disable the redo
		// button in the UI.
		bool canRedo() const { return derived().canRedo(); }

		// ── undo ──────────────────────────────────────────────────────────────────
		//
		// Reverts the graph to the state before the last mutating operation.
		// The current state is pushed onto the redo stack so it can be recovered.
		// Throws std::logic_error if the undo stack is empty.
		void undo() {
			if (derived().past_.empty())
				throw std::logic_error("HypergraphEditorBase::undo: nothing to undo.");
			derived().restoreSnapshot(derived().past_, derived().future_);
		}

		// ── redo ──────────────────────────────────────────────────────────────────
		//
		// Re-applies the most recently undone mutating operation.
		// The current state is pushed onto the undo stack.
		// Throws std::logic_error if the redo stack is empty.
		void redo() {
			if (derived().future_.empty())
				throw std::logic_error("HypergraphEditorBase::redo: nothing to redo.");
			derived().restoreSnapshot(derived().future_, derived().past_);
		}

		// ── Read-only queries (no snapshot needed) ────────────────────────────────

		const std::string& getName() const {
			return derived().graph().getName();
		}
		const std::string& getId() const {
			return derived().graph().getId();
		}
		int getLayerCount() const {
			return derived().graph().getLayerCount();
		}
		const std::map<int, LayerData>& getLayers() const {
			return derived().graph().getLayers();
		}
		const LayerData& getLayerData(int layer) const {
			return derived().graph().getLayerData(layer);
		}
		std::vector<NodePtr> getNodesAt(int layer) const {
			return derived().graph().getNodesAt(layer);
		}
		std::vector<NodePtr> getAllNodes() const {
			return derived().graph().getAllNodes();
		}
		std::vector<HyperedgePtr> getAllHyperedges() const {
			return derived().graph().getAllHyperedges();
		}
		double getX(const NodePtr& node) const {
			return derived().graph().getX(node);
		}

		// ── toJSON ────────────────────────────────────────────────────────────────
		//
		// Serializes the current graph state to a JSON file.
		// Read-only — does not snapshot, does not affect the undo/redo stacks.
		void toJSON(json& j) const {
			derived().graph().toJSON(j);
		}

		// ── getGraph ──────────────────────────────────────────────────────────────
		//
		// Returns a const reference to the current graph so that the graphical
		// engine can read all layout and topology data for rendering without being
		// able to mutate the graph directly. The reference is valid until the next
		// mutating call on this editor, so the graphical engine should call
		// getGraph() every time.
		const auto& getGraph() const {
			return derived().graph();
		}

		// ── Mutating API shared by both editors ───────────────────────────────────

		// ── setName ───────────────────────────────────────────────────────────────
		//
		// Clones the graph, attempts setName, and commits the snapshot only on success.
		void setName(const std::string& name) {
			auto saved = derived().takeSnapshot();
			try {
				derived().graph().setName(name);
			}
			catch (...) {
				throw;
			}
			derived().commitSnapshot(std::move(saved));
		}

		// ── renameNode ────────────────────────────────────────────────────────────
		//
		// Clones the graph, attempts setName on the node, and commits the snapshot
		// only on success.
		void renameNode(const NodePtr& node, const std::string& new_name) {
			auto saved = derived().takeSnapshot();
			try {
				node->setName(new_name);
			}
			catch (...) {
				throw;
			}
			derived().commitSnapshot(std::move(saved));
		}

		// ── addConnection ─────────────────────────────────────────────────────────
		//
		// Clones the graph, attempts addConnection + computeLayout(), and commits
		// the snapshot only if both succeed.
		HyperedgePtr addConnection(const NodePtr& parent, const NodePtr& child) {
			auto saved = derived().takeSnapshot();
			try {
				HyperedgePtr result = derived().graph().addConnection(parent, child);
				derived().graph().computeLayout();
				derived().commitSnapshot(std::move(saved));
				return result;
			}
			catch (...) {
				throw;
			}
		}

		// ── addSourceToEdge ───────────────────────────────────────────────────────
		//
		// Clones the graph, attempts addSourceToEdge (resolving segments to their
		// origin) + computeLayout(), and commits the snapshot only if both succeed.
		void addSourceToEdge(const HyperedgePtr& edge, const NodePtr& source) {
			auto saved = derived().takeSnapshot();
			try {
				if (edge->isSegment()) {
					HyperedgePtr origin = edge->getOrigin().lock();
					derived().graph().addSourceToEdge(origin, source);
				}
				else {
					derived().graph().addSourceToEdge(edge, source);
				}
				derived().graph().computeLayout();
			}
			catch (...) {
				throw;
			}
			derived().commitSnapshot(std::move(saved));
		}

		// ── addTargetToEdge ───────────────────────────────────────────────────────
		//
		// Clones the graph, attempts addTargetToEdge (resolving segments to their
		// origin) + computeLayout(), and commits the snapshot only if both succeed.
		void addTargetToEdge(const HyperedgePtr& edge, const NodePtr& target) {
			auto saved = derived().takeSnapshot();
			try {
				if (edge->isSegment()) {
					HyperedgePtr origin = edge->getOrigin().lock();
					derived().graph().addTargetToEdge(origin, target);
				}
				else {
					derived().graph().addTargetToEdge(edge, target);
				}
				derived().graph().computeLayout();
			}
			catch (...) {
				throw;
			}
			derived().commitSnapshot(std::move(saved));
		}

		// ── removeNode ────────────────────────────────────────────────────────────
		//
		// Clones the graph, attempts removeNode + computeLayout(), and commits the
		// snapshot only if both succeed.
		void removeNode(const NodePtr& node) {
			auto saved = derived().takeSnapshot();
			try {
				derived().graph().removeNode(node);
				derived().graph().computeLayout();
			}
			catch (...) {
				throw;
			}
			derived().commitSnapshot(std::move(saved));
		}

		// ── removeConnection ──────────────────────────────────────────────────────
		//
		// Clones the graph, attempts removeConnection + computeLayout(), and commits
		// the snapshot only if both succeed.
		void removeConnection(const NodePtr& parent, const NodePtr& child) {
			auto saved = derived().takeSnapshot();
			try {
				derived().graph().removeConnection(parent, child);
				derived().graph().computeLayout();
			}
			catch (...) {
				throw;
			}
			derived().commitSnapshot(std::move(saved));
		}

		// ── removeSourceFromHyperedge ────────────────────────────────────────────
		//
		// Clones the graph, attempts removeSourceFromHyperedge (resolving segments)
		// + computeLayout(), and commits the snapshot only if both succeed.
		void removeSourceFromHyperedge(const HyperedgePtr& edge, const NodePtr& source) {
			auto saved = derived().takeSnapshot();
			try {
				if (edge->isSegment()) {
					HyperedgePtr origin = edge->getOrigin().lock();
					derived().graph().removeSourcesFromHyperedge(origin, {source.get()}, true);
				}
				else {
					derived().graph().removeSourcesFromHyperedge(edge, {source.get()}, true);
				}
				derived().graph().computeLayout();
			}
			catch (...) {
				throw;
			}
			derived().commitSnapshot(std::move(saved));
		}

		// ── removeTargetFromHyperedge ────────────────────────────────────────────
		//
		// Clones the graph, attempts removeTargetFromHyperedge (resolving segments)
		// + computeLayout(), and commits the snapshot only if both succeed.
		void removeTargetFromHyperedge(const HyperedgePtr& edge, const NodePtr& target) {
			auto saved = derived().takeSnapshot();
			try {
				if (edge->isSegment()) {
					HyperedgePtr origin = edge->getOrigin().lock();
					derived().graph().removeTargetsFromHyperedge(origin, {target.get()}, true);
				}
				else {
					derived().graph().removeTargetsFromHyperedge(edge, {target.get()}, true);
				}
				derived().graph().computeLayout();
			}
			catch (...) {
				throw;
			}
			derived().commitSnapshot(std::move(saved));
		}

		// ── removeHyperedge ───────────────────────────────────────────────────────
		//
		// Clones the graph, collects all targets of the origin edge, removes them
		// all (destroying the entire hyperedge) + computeLayout(), and commits the
		// snapshot only if both succeed.
		void removeHyperedge(const HyperedgePtr& edge) {
			auto saved = derived().takeSnapshot();
			try {
				HyperedgePtr origin = edge->isSegment() ? edge->getOrigin().lock() : edge;
				auto targets = origin->getTargets();
				std::unordered_set<Node*> targets_set;
				for (const auto& t : targets)
					targets_set.insert(t.get());
				derived().graph().removeTargetsFromHyperedge(origin, targets_set, true);
				derived().graph().computeLayout();
			}
			catch (...) {
				throw;
			}
			derived().commitSnapshot(std::move(saved));
		}

		// ── fuseNodes ─────────────────────────────────────────────────────────────
		//
		// Clones the graph, attempts fuseNodes + computeLayout(), and commits the
		// snapshot only if both succeed.
		void fuseNodes(const NodePtr& node1, const NodePtr& node2,
			const std::string& new_label)
		{
			auto saved = derived().takeSnapshot();
			try {
				derived().graph().fuseNodes(node1, node2, new_label);
				derived().graph().computeLayout();
			}
			catch (...) {
				throw;
			}
			derived().commitSnapshot(std::move(saved));
		}

		// ── minimizeCrossings ─────────────────────────────────────────────────────
		//
		// Clones the graph, runs the global sifting algorithm + computeLayout(), and
		// commits the snapshot only if both succeed. Although minimizeCrossings does
		// not change the topology it does change the visible ordering of nodes, which
		// the user may want to undo.
		int minimizeCrossings(int sifting_rounds = 10) {
			auto saved = derived().takeSnapshot();
			try {
				int crossings = derived().graph().minimizeCrossings(sifting_rounds);
				derived().graph().computeLayout();
				derived().commitSnapshot(std::move(saved));
				return crossings;
			}
			catch (...) {
				throw;
			}
		}

		// ── relocateNodeInLayer ───────────────────────────────────────────────────
		//
		// Clones the graph, attempts relocateNodeInLayer (which calls computeLayout()
		// internally), and commits the snapshot only on success.
		void relocateNodeInLayer(const NodePtr& node, double new_x_coordinate) {
			auto saved = derived().takeSnapshot();
			try {
				derived().graph().relocateNodeInLayer(node, new_x_coordinate);
			}
			catch (...) {
				throw;
			}
			derived().commitSnapshot(std::move(saved));
		}


		// -─ onMutated callback ───────────────────────────────────────────────────────
		//
		// Callback function for when the graph is mutated. The graphical engine uses
		// this to know when the project has unsaved changes.
		void setOnMutated(std::function<void()> callback) {
			on_mutated_ = std::move(callback);
		}

		// This should be called by the derived class at the end of commitSnapshot() after
		void notifyMutated() {
			if (on_mutated_) on_mutated_();
		}

	private:
		// Safely downcast to the derived class. This is a common CRTP pattern that
		// allows the base class to call methods implemented in the derived class
		// without virtual dispatch.
		Derived& derived() { return static_cast<Derived&>(*this); }

		// Const overload — used by read-only methods (getId, getLayerCount, etc.).
		const Derived& derived() const { return static_cast<const Derived&>(*this); }

		// The callback function for notifying the project of unsaved mutations.
		std::function<void()> on_mutated_;
	};

} // namespace app_logic