#pragma once
#include "HypergraphEditorBase.h"
#include "JointGraphicalHypergraph.h"

#include <memory>

namespace app_logic {
	using namespace hypergraph_logic;

	// ============================================================================
	// JointHypergraphEditor
	//
	// Editor for the per-project JointGraphicalHypergraph singleton. Inherits all
	// shared undo/redo machinery and forwarding methods from
	// HypergraphEditorBase<JointHypergraphEditor> and adds addHypergraph(), which
	// is specific to the joint graph.
	//
	// The node-creation API (createNode, createSource, createTarget) is
	// intentionally absent — it is disabled at the JointGraphicalHypergraph level
	// and there is no reason to expose it here at all.
	// ============================================================================
	class JointHypergraphEditor
		: public HypergraphEditorBase<JointHypergraphEditor> {
	public:

		// ── Construction ──────────────────────────────────────────────────────────

		// ── JointHypergraphEditor ─────────────────────────────────────────────────
		//
		// Takes ownership of the supplied joint graph and lays it out unless told
		// to keep the layout it carries (see InitialLayout). The undo and redo
		// stacks start empty.
		explicit JointHypergraphEditor(
			std::unique_ptr<JointGraphicalHypergraph> joint,
			InitialLayout layout = InitialLayout::Compute);

		// ── Joint-specific API ────────────────────────────────────────────────────

		// ── addHypergraph ─────────────────────────────────────────────────────────
		//
		// Clones the joint graph, attempts addHypergraph on the live joint, and
		// commits the snapshot to past_ only if the operation succeeds.
		void addHypergraph(GraphicalHypergraph& g, bool left);

		// Same, placed where the user clicked (see
		// JointGraphicalHypergraph::addHypergraph).
		void addHypergraph(GraphicalHypergraph& g, double click_x);

		// ── removeHypergraph / moveHypergraph ─────────────────────────────────────
		//
		// Take a diagram that is not mixed with others out of the joint, or move
		// it as a whole to where the user clicked. Undoable; the joint's
		// getGraph().isSeparable(id) says whether they are possible.
		void removeHypergraph(const std::string& id);
		void moveHypergraph(const std::string& id, double click_x);

		// ── Moving up/down, and moving connected components ───────────────────────
		//
		// See JointGraphicalHypergraph's "Moving diagrams and connected
		// components". A component is named by any of its boxes. Undoable.
		void moveHypergraphToLayer(const std::string& id, int top_layer);
		void moveHypergraph(const std::string& id, double click_x, int top_layer);
		void moveComponent(const NodePtr& box, double click_x);
		void moveComponentToLayer(const NodePtr& box, int top_layer);
		void moveComponent(const NodePtr& box, double click_x, int top_layer);

		// ── canUndo / canRedo predicates ──────────────────────────────────────────
		bool canUndo() const { return !past_.empty(); }
		bool canRedo() const { return !future_.empty(); }

	private:
		friend class HypergraphEditorBase<JointHypergraphEditor>;

		// ── graph() — required by HypergraphEditorBase ────────────────────────────
		JointGraphicalHypergraph& graph() { return *joint_; }
		const JointGraphicalHypergraph& graph() const { return *joint_; }

		// ── takeSnapshot() — required by HypergraphEditorBase ─────────────────────
		//
		// Clones the live joint graph via cloneJoint() and returns the clone as a
		// unique_ptr. Called at the start of every structural mutation before it
		// is attempted. The returned clone is only committed to past_ if the
		// mutation succeeds.
		std::unique_ptr<JointGraphicalHypergraph> takeSnapshot() {
			return joint_->cloneJoint();
		}

		// ── installSnapshot() — required by HypergraphEditorBase ──────────────────
		//
		// Makes snapshot the live joint graph (undo/redo of a structural change).
		void installSnapshot(std::unique_ptr<JointGraphicalHypergraph>&& snapshot) {
			joint_ = std::move(snapshot);
		}

		using Snapshot = std::unique_ptr<JointGraphicalHypergraph>;

		std::unique_ptr<JointGraphicalHypergraph> joint_;
		std::deque<HistoryEntry<Snapshot>> past_;
		std::deque<HistoryEntry<Snapshot>> future_;
	};

} // namespace app_logic