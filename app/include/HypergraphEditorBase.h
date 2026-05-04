#pragma once
#include "GraphicalHypergraph.h"

#include <deque>
#include <stdexcept>
#include <string>

namespace app_logic {
	using namespace hypergraph_logic;

	// ============================================================================
	// HypergraphEditorBase<Derived>
	//
	// CRTP base class that implements all shared undo/redo machinery and the full
	// set of forwarding methods that are common to both HypergraphEditor and
	// JointHypergraphEditor.
	//
	// Template parameter:
	// Derived must provide two protected members that the base uses:
	//
	//   GraphType& graph()             — returns the live graph being edited.
	//   const GraphType& graph() const — const overload of the above.
	//   void pushSnapshot()            — clones the current state and pushes it
	//                                    onto past_, enforcing the MAX_HISTORY cap.
	//   void restoreSnapshot(          — pops the top of src, moves it into the
	//       std::deque<SnapshotPtr>&,    live graph, and pushes a snapshot of the
	//       std::deque<SnapshotPtr>&)    old live graph onto dst.
	// We cannot keep the undo and redo stacks in the base class because the snapshot
	// type for JointGraphicalHypergraph is completely different from the snapshot type 
	// for GraphicalHypergraph. So, this logic is handled by the derived classes, and the
	// base class just needs to call pushSnapshot() and restoreSnapshot() at the right times.
	// 
	// Undo / redo model:
	// Two bounded deques: past_ and future_. I have used deques to allow O(1) 
	// push and pop from both ends. The logic is as follows:
	//   1. pushSnapshot() clones the live graph onto past_ and clears future_.
	//   2. The mutation is applied on the live graph.
	//   3. computeLayout() is called so coordinates are always current.
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
		// It is unlikely that the user will want to undo more than 15 steps, and
		// keeping more snapshots in memory would be wasteful.
		static constexpr int MAX_HISTORY = 15;

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
		// The graphical engine can use this to enable or disable the undo 
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

		// ── Read-only queries (no snapshot needed) ────────────────────────────────────

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
		void toJSON(const std::string& path) const {
			derived().graph().toJSON(path);
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

		// ── addConnection ─────────────────────────────────────────────────────────
		//
		// Snapshots, then delegates to the graph's addConnection.
		// computeLayout() is called after the mutation.
		HyperedgePtr addConnection(const NodePtr& parent, const NodePtr& child) {
			derived().pushSnapshot();
			auto result = derived().graph().addConnection(parent, child);
			derived().graph().computeLayout();
			return result;
		}

		// ── addSourceToEdge ───────────────────────────────────────────────────────
		//
		// Snapshots, then delegates to the graph's addSourceToEdge.
		// computeLayout() is called after the mutation.
		void addSourceToEdge(const HyperedgePtr& edge, const NodePtr& source) {
			derived().pushSnapshot();
			derived().graph().addSourceToEdge(edge, source);
			derived().graph().computeLayout();
		}

		// ── addTargetToEdge ───────────────────────────────────────────────────────
		//
		// Snapshots, then delegates to the graph's addTargetToEdge.
		// computeLayout() is called after the mutation.
		void addTargetToEdge(const HyperedgePtr& edge, const NodePtr& target) {
			derived().pushSnapshot();
			derived().graph().addTargetToEdge(edge, target);
			derived().graph().computeLayout();
		}

		// ── removeNode ────────────────────────────────────────────────────────────
		//
		// Snapshots, then delegates to the graph's removeNode.
		// computeLayout() is called after the mutation.
		void removeNode(const NodePtr& node) {
			derived().pushSnapshot();
			derived().graph().removeNode(node);
			derived().graph().computeLayout();
		}

		// ── removeConnection ──────────────────────────────────────────────────────
		//
		// Snapshots, then delegates to the graph's removeConnection.
		// computeLayout() is called after the mutation.
		void removeConnection(const NodePtr& parent, const NodePtr& child) {
			derived().pushSnapshot();
			derived().graph().removeConnection(parent, child);
			derived().graph().computeLayout();
		}

		// ── removeSourcesFromHyperedge ────────────────────────────────────────────
		//
		// Snapshots, then delegates to the graph's removeSourcesFromHyperedge.
		// computeLayout() is called after the mutation.
		void removeSourcesFromHyperedge(const HyperedgePtr& edge,
			const std::unordered_set<Node*>& sources_to_remove)
		{
			derived().pushSnapshot();
			derived().graph().removeSourcesFromHyperedge(edge, sources_to_remove, true);
			derived().graph().computeLayout();
		}

		// ── removeTargetsFromHyperedge ────────────────────────────────────────────
		//
		// Snapshots, then delegates to the graph's removeTargetsFromHyperedge.
		// computeLayout() is called after the mutation.
		//
		void removeTargetsFromHyperedge(const HyperedgePtr& edge,
			const std::unordered_set<Node*>& targets_to_remove)
		{
			derived().pushSnapshot();
			derived().graph().removeTargetsFromHyperedge(edge, targets_to_remove, true);
			derived().graph().computeLayout();
		}

		// ── fuseNodes ─────────────────────────────────────────────────────────────
		//
		// Snapshots, then delegates to the graph's fuseNodes.
		// computeLayout() is called after the mutation.
		//
		void fuseNodes(const NodePtr& node1, const NodePtr& node2,
			const std::string& new_label)
		{
			derived().pushSnapshot();
			derived().graph().fuseNodes(node1, node2, new_label);
			derived().graph().computeLayout();
		}

		// ── minimizeCrossings ─────────────────────────────────────────────────────
		//
		// Snapshots, runs the global sifting algorithm, then calls computeLayout().
		// Although minimizeCrossings does not change the topology it does change the
		// visible ordering of nodes, which the user may want to undo.
		int minimizeCrossings(int sifting_rounds = 10) {
			derived().pushSnapshot();
			int crossings = derived().graph().minimizeCrossings(sifting_rounds);
			derived().graph().computeLayout();
			return crossings;
		}

		// ── relocateNodeInLayer ───────────────────────────────────────────────────
		//
		// Snapshots, then delegates to the graph's relocateNodeInLayer, which
		// internally calls computeLayout() itself. We do not need to call
		// computeLayout() again here.
		void relocateNodeInLayer(const NodePtr& node, double new_x_coordinate) {
			derived().pushSnapshot();
			derived().graph().relocateNodeInLayer(node, new_x_coordinate);
		}

	private:
		// Safely downcast to the derived class. This is a common CRTP pattern that allows
		// the base class to call methods implemented in the derived class without virtual dispatch.
		Derived& derived() { return static_cast<Derived&>(*this); }

		// This is used for methods which are const in the base. For example, readOnly methods like
		// getId() or getLayerCount() are const in the base, so they need to be called by a const version of derived().
		const Derived& derived() const { return static_cast<const Derived&>(*this); } 
	};

} // namespace app_logic