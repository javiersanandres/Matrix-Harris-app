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