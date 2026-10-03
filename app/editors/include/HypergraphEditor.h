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
		// Takes ownership of the supplied graph (moved in) and lays it out unless
		// told to keep the layout it carries (see InitialLayout). The undo and
		// redo stacks start empty.
		explicit HypergraphEditor(GraphicalHypergraph&& graph,
			InitialLayout layout = InitialLayout::Compute);

		// ── Node-creation API (specific to regular graphs) ────────────────────────

		// ── createNode (with parent) ──────────────────────────────────────────────
		//
		// Clones the graph, attempts createNode + computeLayout(), and commits the
		// snapshot to past_ only if both succeed.
		NodePtr createNode(const NodeAttributes& attributes, int layer, int layer_position,
			const NodePtr& parent);

		// ── createParent  ─────────────────────────────────────────────────────────
		//
		// Clones the graph, attemps createParent + computeLayout(), and commits the
		// snapshot to past_ only if both succeed.
		NodePtr createParent(const NodeAttributes& attributes, const NodePtr& child);

		// ── createNodeInEdge  ─────────────────────────────────────────────────────
		//
		// Clones the graph, attempts createNodeInEdge + computeLayout(), and commits 
		// the snapshot to past_ only if both succeed.
		NodePtr createNodeInEdge(const NodeAttributes& attributes, const HyperedgePtr& edge);

		// ── createNodeNextTo ──────────────────────────────────────────────────────
		//
		// Clones the graph, attempts createNodeNextTo + computeLayout(), and commits 
		// the snapshot to past_ only if both succeed.
		NodePtr createNodeNextTo(const NodeAttributes& attributes, const NodePtr& node, 
			bool left);

		// ── createSource ─────────────────────────────────────────────────────────
		//
		// Clones the graph, attempts createSource + computeLayout(), and commits the
		// snapshot to past_ only if both succeed.
		NodePtr createSource(const NodeAttributes& attributes, int layer_position,
			const HyperedgePtr& edge);

		// ── createTarget ─────────────────────────────────────────────────────────
		//
		// Clones the graph, attempts createTarget + computeLayout(), and commits the
		// snapshot to past_ only if both succeed.
		NodePtr createTarget(const NodeAttributes& attributes, int layer_position,
			const HyperedgePtr& edge);

		// ── paste ─────────────────────────────────────────────────────────────────
		//
		// Pastes a copied piece (see GraphicalHypergraph::copyOf / paste) as a new
		// block: its shallowest box in top_layer, at click_x. Undoable. Returns
		// the pasted real boxes.
		std::vector<NodePtr> paste(GraphicalHypergraph&& piece, double click_x, int top_layer);

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
		// of every structural mutation before it is attempted. The returned clone is
		// only committed to past_ if the mutation succeeds.
		GraphicalHypergraph takeSnapshot() {
			return graph_.clone();
		}

		// ── installSnapshot() — required by HypergraphEditorBase ──────────────────
		//
		// Makes snapshot the live graph (undo/redo of a structural change).
		void installSnapshot(GraphicalHypergraph&& snapshot) {
			graph_ = std::move(snapshot);
		}

		GraphicalHypergraph graph_;
		std::deque<HistoryEntry<GraphicalHypergraph>> past_;
		std::deque<HistoryEntry<GraphicalHypergraph>> future_;
	};

} // namespace app_logic