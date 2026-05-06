#pragma once
#include "HypergraphEditorBase.h"

namespace app_logic {
	using namespace hypergraph_logic;

	// ============================================================================
	// HypergraphEditor
	//
	// Editor for a single GraphicalHypergraph. Inherits all shared undo/redo
	// machinery and forwarding methods from HypergraphEditorBase<HypergraphEditor>
	// and adds the node-creation API (createNode, createSource, createTarget)
	// which is specific to regular graphs and disabled on the joint graph.
	// ============================================================================
	class HypergraphEditor : public HypergraphEditorBase<HypergraphEditor> {
	public:

		// ── Construction ──────────────────────────────────────────────────────────

		// ── HypergraphEditor ──────────────────────────────────────────────────────
		//
		// Takes ownership of the supplied graph (moved in). The undo and redo stacks
		// start empty.
		explicit HypergraphEditor(GraphicalHypergraph&& graph);

		// ── Node-creation API (specific to regular graphs) ────────────────────────

		// ── createNode (with parent) ──────────────────────────────────────────────
		//
		// Clones the graph, attempts createNode + computeLayout(), and commits the
		// snapshot to past_ only if both succeed.
		NodePtr createNode(const std::string& label, int layer_position,
			const NodePtr& parent);

		// ── createParent  ─────────────────────────────────────────────────────────
		//
		// Clones the graph, attemps createParent + computeLayout(), and commits the
		// snapshot to past_ only if both succeed.
		NodePtr createParent(const std::string& label, const NodePtr& child);

		// ── createNode (into edge) ────────────────────────────────────────────────
		//
		// Clones the graph, attempts createNode + computeLayout(), and commits the
		// snapshot to past_ only if both succeed.
		NodePtr createNode(const std::string& label, const HyperedgePtr& edge);

		// ── createSource ─────────────────────────────────────────────────────────
		//
		// Clones the graph, attempts createSource + computeLayout(), and commits the
		// snapshot to past_ only if both succeed.
		NodePtr createSource(const std::string& label, int layer_position,
			const HyperedgePtr& edge);

		// ── createTarget ─────────────────────────────────────────────────────────
		//
		// Clones the graph, attempts createTarget + computeLayout(), and commits the
		// snapshot to past_ only if both succeed.
		NodePtr createTarget(const std::string& label, int layer_position,
			const HyperedgePtr& edge);

		// ── canUndo / canRedo predicates ──────────────────────────────────────────
		bool canUndo() const { return !past_.empty(); }
		bool canRedo() const { return !future_.empty(); }

	private:
		friend class HypergraphEditorBase<HypergraphEditor>;

		// ── graph() — required by HypergraphEditorBase ────────────────────────────
		GraphicalHypergraph& graph() { return graph_; }
		const GraphicalHypergraph& graph() const { return graph_; }

		// ── takeSnapshot() — required by HypergraphEditorBase ─────────────────────
		//
		// Clones the current live graph and returns it by value. Called at the start
		// of every mutating method before the mutation is attempted. The returned
		// clone is only committed to past_ if the mutation succeeds.
		GraphicalHypergraph takeSnapshot() {
			return graph_.clone();
		}

		// ── commitSnapshot() — required by HypergraphEditorBase ───────────────────
		//
		// Receives a ready-made snapshot (produced by takeSnapshot() before the
		// mutation ran) and pushes it onto past_. Enforces the MAX_HISTORY cap and
		// clears future_ so that a new mutation after a sequence of undos discards
		// all redo states.
		//
		// Only called after a mutation has fully succeeded — never on failure.
		void commitSnapshot(GraphicalHypergraph&& snapshot) {
			past_.push_back(std::move(snapshot));
			if (static_cast<int>(past_.size()) > MAX_HISTORY)
				past_.pop_front();
			future_.clear();
			notifyMutated();
		}

		// ── restoreSnapshot() — required by HypergraphEditorBase ──────────────────
		//
		// Pops the top of src (the stack being restored from), saves the current
		// graph to dst (the opposite stack), and replaces the live graph with the
		// popped snapshot. Enforces the MAX_HISTORY cap on dst.
		// undo() → restoreSnapshot(past_, future_)
		// redo() → restoreSnapshot(future_, past_)
		void restoreSnapshot(std::deque<GraphicalHypergraph>& src,
			std::deque<GraphicalHypergraph>& dst)
		{
			dst.push_back(graph_.clone());
			if (static_cast<int>(dst.size()) > MAX_HISTORY)
				dst.pop_front();
			graph_ = std::move(src.back());
			src.pop_back();
			notifyMutated();
		}

		GraphicalHypergraph graph_;
		std::deque<GraphicalHypergraph> past_;
		std::deque<GraphicalHypergraph> future_;
	};

} // namespace app_logic