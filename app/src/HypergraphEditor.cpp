#include "HypergraphEditor.h"

namespace app_logic {
	using namespace hypergraph_logic;

	HypergraphEditor::HypergraphEditor(GraphicalHypergraph&& graph)
		: graph_(std::move(graph))
	{
		graph_.computeLayout();
	}

	NodePtr HypergraphEditor::createNode(
		const std::string& label, int layer_position, const NodePtr& parent)
	{
		pushSnapshot();
		NodePtr result = graph_.createNode(label, layer_position, parent);
		graph_.computeLayout();
		return result;
	}

	NodePtr HypergraphEditor::createNode(
		const std::string& label, const HyperedgePtr& edge)
	{
		pushSnapshot();
		NodePtr result = graph_.createNode(label, edge);
		graph_.computeLayout();
		return result;
	}

	NodePtr HypergraphEditor::createSource(
		const std::string& label, int layer_position, const HyperedgePtr& edge)
	{
		pushSnapshot();
		NodePtr result = graph_.createSource(label, layer_position, edge);
		graph_.computeLayout();
		return result;
	}

	NodePtr HypergraphEditor::createTarget(
		const std::string& label, int layer_position, const HyperedgePtr& edge)
	{
		pushSnapshot();
		NodePtr result = graph_.createTarget(label, layer_position, edge);
		graph_.computeLayout();
		return result;
	}

} // namespace app_logic