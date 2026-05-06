#include "HypergraphEditor.h"

namespace app_logic {
	using namespace hypergraph_logic;

	HypergraphEditor::HypergraphEditor(GraphicalHypergraph&& graph)
		: graph_(std::move(graph))
	{
		graph_.computeLayout();
	}

	// Though none of this operations should throw under normal circumstances,
	// just in case some code changes, at leas we are prepared to guarantee that
	// the undo stack is not corrupted by failed operations.
	NodePtr HypergraphEditor::createNode(
		const std::string& label, int layer_position, const NodePtr& parent)
	{
		auto saved = takeSnapshot();
		try {
			NodePtr result = graph_.createNode(label, layer_position, parent);
			graph_.computeLayout();
			commitSnapshot(std::move(saved));
			return result;
		}
		catch (...) {
			throw;
		}
	}

	NodePtr HypergraphEditor::createNode(
		const std::string& label, const HyperedgePtr& edge)
	{
		auto saved = takeSnapshot();
		try {
			NodePtr result;
			if (edge->isSegment()) {
				result = graph_.createNode(label, edge->getOrigin().lock());
			}
			else {
				result = graph_.createNode(label, edge);
			}
			graph_.computeLayout();
			commitSnapshot(std::move(saved));
			return result;
		}
		catch (...) {
			throw;
		}
	}

	NodePtr HypergraphEditor::createSource(
		const std::string& label, int layer_position, const HyperedgePtr& edge)
	{
		auto saved = takeSnapshot();
		try {
			NodePtr result;
			if (edge->isSegment()) {
				result = graph_.createSource(label, layer_position, edge->getOrigin().lock());
			}
			else {
				result = graph_.createSource(label, layer_position, edge);
			}
			graph_.computeLayout();
			commitSnapshot(std::move(saved));
			return result;
		}
		catch (...) {
			throw;
		}
	}

	NodePtr HypergraphEditor::createTarget(
		const std::string& label, int layer_position, const HyperedgePtr& edge)
	{
		auto saved = takeSnapshot();
		try {
			NodePtr result;
			if (edge->isSegment()) {
				result = graph_.createTarget(label, layer_position, edge->getOrigin().lock());
			}
			else {
				result = graph_.createTarget(label, layer_position, edge);
			}
			graph_.computeLayout();
			commitSnapshot(std::move(saved));
			return result;
		}
		catch (...) {
			throw;
		}
	}

} // namespace app_logic