#include "JointHypergraphEditor.h"

namespace app_logic {
	using namespace hypergraph_logic;

	JointHypergraphEditor::JointHypergraphEditor(
		std::unique_ptr<JointGraphicalHypergraph> joint, InitialLayout layout)
		: joint_(std::move(joint))
	{
		if (!joint_)
			throw std::invalid_argument(
				"JointHypergraphEditor: supplied joint pointer is null.");
		if (layout == InitialLayout::Compute) joint_->computeLayout();
	}

	void JointHypergraphEditor::addHypergraph(GraphicalHypergraph& g, bool left) {
		auto saved = takeSnapshot();
		try {
			joint_->addHypergraph(g, left);
			// addHypergraph already calls computeLayout() internally,
			// so we do not need to call it here again.
		}
		catch (...) {
			throw;
		}
		commitSnapshot(std::move(saved));
	}

	void JointHypergraphEditor::addHypergraph(GraphicalHypergraph& g, double click_x) {
		auto saved = takeSnapshot();
		joint_->addHypergraph(g, click_x); // lays the joint out itself
		commitSnapshot(std::move(saved));
	}

	void JointHypergraphEditor::removeHypergraph(const std::string& id) {
		auto saved = takeSnapshot();
		joint_->removeHypergraph(id);
		commitSnapshot(std::move(saved));
	}

	void JointHypergraphEditor::moveHypergraph(const std::string& id, double click_x) {
		auto saved = takeSnapshot();
		joint_->moveHypergraph(id, click_x);
		commitSnapshot(std::move(saved));
	}

	void JointHypergraphEditor::moveHypergraphToLayer(const std::string& id, int top_layer) {
		auto saved = takeSnapshot();
		joint_->moveHypergraphToLayer(id, top_layer);
		commitSnapshot(std::move(saved));
	}

	void JointHypergraphEditor::moveHypergraph(const std::string& id, double click_x, int top_layer) {
		auto saved = takeSnapshot();
		joint_->moveHypergraph(id, click_x, top_layer);
		commitSnapshot(std::move(saved));
	}

	void JointHypergraphEditor::moveComponent(const NodePtr& box, double click_x) {
		auto saved = takeSnapshot();
		joint_->moveComponent(box.get(), click_x);
		commitSnapshot(std::move(saved));
	}

	void JointHypergraphEditor::moveComponentToLayer(const NodePtr& box, int top_layer) {
		auto saved = takeSnapshot();
		joint_->moveComponentToLayer(box.get(), top_layer);
		commitSnapshot(std::move(saved));
	}

	void JointHypergraphEditor::moveComponent(const NodePtr& box, double click_x, int top_layer) {
		auto saved = takeSnapshot();
		joint_->moveComponent(box.get(), click_x, top_layer);
		commitSnapshot(std::move(saved));
	}

} // namespace app_logic