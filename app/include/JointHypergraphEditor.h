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
	// intentionally absentm it is disabled at the JointGraphicalHypergraph level
	// and there is no reason to expose it here at all.
	// ============================================================================
	class JointHypergraphEditor
		: public HypergraphEditorBase<JointHypergraphEditor> {
	public:

		// ── Construction ──────────────────────────────────────────────────────────

		// ── JointHypergraphEditor ─────────────────────────────────────────────────
		//
		// Takes ownership of the supplied joint graph. The undo and redo stacks start empty.
		explicit JointHypergraphEditor(
			std::unique_ptr<JointGraphicalHypergraph> joint);

		// ── Joint-specific API ────────────────────────────────────────────────────

		// ── addHypergraph ─────────────────────────────────────────────────────────
		//
		// Snapshots, then delegates to JointGraphicalHypergraph::addHypergraph.
		void addHypergraph(GraphicalHypergraph& g, bool left);
	private:
		friend class HypergraphEditorBase<JointHypergraphEditor>;

		// ── graph() — required by HypergraphEditorBase ────────────────────────────
		JointGraphicalHypergraph& graph() { return *joint_; }
		const JointGraphicalHypergraph& graph() const { return *joint_; }

		// ── pushSnapshot() — required by HypergraphEditorBase ─────────────────────
		//
		// Clones the live joint graph via cloneJoint() onto past_, enforces the
		// MAX_HISTORY cap, and clears future_.
		void pushSnapshot() {
			past_.push_back(joint_->cloneJoint());
			if (static_cast<int>(past_.size()) > MAX_HISTORY)
				past_.pop_front();
			future_.clear();
		}

		// ── restoreSnapshot() — required by HypergraphEditorBase ──────────────────
		//
		// Pops the top of src, saves the current joint to dst via cloneJoint(),
		// and replaces the live joint with the popped snapshot. This will be called
		// by the base class in undo() and redo() with the appropriate stacks.
		// undo() - restoreSnapshot(past_, future_)
		// redo() - restoreSnapshot(future_, past_)
		void restoreSnapshot(
			std::deque<std::unique_ptr<JointGraphicalHypergraph>>& src,
			std::deque<std::unique_ptr<JointGraphicalHypergraph>>& dst)
		{
			dst.push_back(joint_->cloneJoint());
			if (static_cast<int>(dst.size()) > MAX_HISTORY)
				dst.pop_front();
			joint_ = std::move(src.back());
			src.pop_back();
		}

		// ── canUndo / canRedo predicates ──────────────────────────────────────────
		bool canUndo() const { return !past_.empty(); }
		bool canRedo() const { return !future_.empty(); }

		std::unique_ptr<JointGraphicalHypergraph> joint_;
		std::deque<std::unique_ptr<JointGraphicalHypergraph>> past_;
		std::deque<std::unique_ptr<JointGraphicalHypergraph>> future_;
	};

} // namespace app_logic