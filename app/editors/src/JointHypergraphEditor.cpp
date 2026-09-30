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

} // namespace app_logic