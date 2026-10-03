#include "HypergraphEditor.h"

namespace app_logic {
	using namespace hypergraph_logic;

	HypergraphEditor::HypergraphEditor(GraphicalHypergraph&& graph, InitialLayout layout)
		: graph_(std::move(graph))
	{
		if (layout == InitialLayout::Compute) graph_.computeLayout();
	}

	// Though none of this operations should throw under normal circumstances,
	// just in case some code changes, at leas we are prepared to guarantee that
	// the undo stack is not corrupted by failed operations.
	NodePtr HypergraphEditor::createNode(
		const NodeAttributes& attributes, int layer, int layer_position, const NodePtr& parent)
	{
		auto saved = takeSnapshot();
		try {
			std::set<int> mip_layers;
			NodePtr result = graph_.createNode(attributes, layer, layer_position, parent, &mip_layers);
			graph_.computeLayout(mip_layers);
			commitSnapshot(std::move(saved));
			return result;
		}
		catch (...) {
			throw;
		}
	}

	NodePtr HypergraphEditor::createParent(
		const NodeAttributes& attributes, const NodePtr& child)
	{
		auto saved = takeSnapshot();
		try {
			std::set<int> mip_layers;
			NodePtr result = graph_.createParent(attributes, child, &mip_layers);
			graph_.computeLayout(mip_layers);
			commitSnapshot(std::move(saved));
			return result;
		}
		catch (...) {
			throw;
		}
	}

	NodePtr HypergraphEditor::createNodeInEdge(
		const NodeAttributes& attributes, const HyperedgePtr& edge)
	{
		auto saved = takeSnapshot();
		try {
			NodePtr result;
			std::set<int> mip_layers;
			if (edge->isSegment()) {
				result = graph_.createNodeInEdge(attributes, edge->getOrigin().lock(), &mip_layers);
			}
			else {
				result = graph_.createNodeInEdge(attributes, edge, &mip_layers);
			}
			graph_.computeLayout(mip_layers);
			commitSnapshot(std::move(saved));
			return result;
		}
		catch (...) {
			throw;
		}
	}

	NodePtr HypergraphEditor::createNodeNextTo(
		const NodeAttributes& attributes, const NodePtr& node, bool left)
	{
		auto saved = takeSnapshot();
		try {
			NodePtr result;
			result = graph_.createNodeNextTo(attributes, node, left);
			graph_.computeLayout({});
			commitSnapshot(std::move(saved));
			return result;
		}
		catch (...) {
			throw;
		}
	}

	NodePtr HypergraphEditor::createSource(
		const NodeAttributes& attributes, int layer_position, const HyperedgePtr& edge)
	{
		auto saved = takeSnapshot();
		try {
			NodePtr result;
			std::set<int> mip_layers;
			if (edge->isSegment()) {
				result = graph_.createSource(attributes, layer_position, edge->getOrigin().lock(), &mip_layers);
			}
			else {
				result = graph_.createSource(attributes, layer_position, edge, &mip_layers);
			}
			graph_.computeLayout(mip_layers);
			commitSnapshot(std::move(saved));
			return result;
		}
		catch (...) {
			throw;
		}
	}

	NodePtr HypergraphEditor::createTarget(
		const NodeAttributes& attributes, int layer_position, const HyperedgePtr& edge)
	{
		auto saved = takeSnapshot();
		try {
			NodePtr result;
			std::set<int> mip_layers;
			if (edge->isSegment()) {
				result = graph_.createTarget(attributes, layer_position, edge->getOrigin().lock(), &mip_layers);
			}
			else {
				result = graph_.createTarget(attributes, layer_position, edge, &mip_layers);
			}
			graph_.computeLayout(mip_layers);
			commitSnapshot(std::move(saved));
			return result;
		}
		catch (...) {
			throw;
		}
	}

	std::vector<NodePtr> HypergraphEditor::paste(GraphicalHypergraph&& piece, double click_x, int top_layer) {
		auto saved = takeSnapshot();
		std::vector<NodePtr> result = graph_.paste(std::move(piece), click_x, top_layer);
		commitSnapshot(std::move(saved));
		return result;
	}

} // namespace app_logic