#pragma once

#include "Node.h"
#include "Hyperedge.h"

#include <map>
#include <vector>

namespace hypergraph_logic {

	// ============================================================================
	// LayerData
	//
	// Data associated with a single layer in the hypergraph.
	// ============================================================================
	struct LayerData {
		std::vector<NodePtr> nodes;                  // Nodes in this layer (in order)
		std::vector<HyperedgePtr> outgoing_edges;    // Hyperedges from this layer to the next
	};

	// ============================================================================
	// Hypergraph
	//
	// A directed layered hierarchical hypergraph  H = (V, E, λ).
	//
	// Layer assignment follows the depth rule:
	// layer(v) = max{ layer(p) : p is a parent of v } + 1.
	// There is no function λ explicitly stored, the fact that a node belongs
	// to a certain layer is encoded through a dictionary in this class.
	//
	// Ownership model:
	//	 - The graph owns all nodes and hyperedges. 
	// ============================================================================
	class Hypergraph {
	public:
		explicit Hypergraph(std::string name);

		// ====================================================================
		// Node management
		// ====================================================================

		/// Create a new real node and add it to the graph
		NodePtr createNode(const std::string& label, int layer_position, const NodePtr& parent);
		void addConnection(const NodePtr& parent, const NodePtr& child);


		/// Get all nodes at a specific layer
		std::vector<NodePtr> getNodesAt(int layer) const;

		/// Get all nodes in the graph
		std::vector<NodePtr> getAllNodes() const;

		// ====================================================================
		// Layer queries
		// ====================================================================

		/// Get the number of layers in the graph
		int getLayerCount() const;

		/// Get all layers with their associated data
		const std::map<int, LayerData>& getLayers() const;

		/// Get layer data for a specific layer
		const LayerData& getLayerData(int layer) const;

		// ====================================================================
		// Hyperedge management
		// ====================================================================

		/// Create a hyperedge with the given sources and targets
		HyperedgePtr createHyperedge(const WeakHyperedgePtr& origin, const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets, int layer);
		HyperedgePtr createHyperedge(const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets, int layer);

		/// Get all hyperedges
		std::vector<HyperedgePtr> getAllHyperedges() const;

		/// Check if an edge is "short" (connects layer k to layer k+1).
		/// Returns the source layer k if true, -1 if the edge is not short or invalid.
		int edgeIsShort(const HyperedgePtr& edge);

	protected:
		std::string name_;

		// Layer assignment: layer number -> LayerData (nodes + edges)
		std::map<int, LayerData> layers_;

		// All nodes owned by this graph
		std::vector<NodePtr> all_nodes_;

		// All hyperedges owned by this graph
		std::vector<HyperedgePtr> all_hyperedges_;

		void addNodeToLayer(int layer, int position, const NodePtr& node);
		void removeNodeFromLayer(int layer, const NodePtr& node);
		/// Remove all nodes in the set from the specified layer
		void removeNodeFromLayer(int layer, const std::unordered_set<Node*>& nodes);

		void addHyperedgeToLayer(int layer, const HyperedgePtr& edge);
		void removeHyperedgeFromLayer(int layer, const HyperedgePtr& edge);

		/// Remove all hyperedges in the set from the specified layer
		void removeHyperedgeFromLayer(int layer, const std::unordered_set<Hyperedge*>& edges);
		void applyRelocationAndPropagate(const NodePtr& node, int new_layer);

		//NodePtr createNode(const NodePtr& parent, const NodePtr& oldChild); -- to be implemented when the ordering is figured out

		void splitLongEdge(const HyperedgePtr& long_edge, const std::vector<NodePtr>& sources, const std::vector<NodePtr>& long_targets);

		void dissolveSegments(const std::unordered_set<Hyperedge*>& long_edges);

		/// Remove specified sources from a hyperedge and all its segments, cleaning up unused dummies
		void removeSourcesFromHyperedge(const HyperedgePtr& edge, const std::unordered_set<Node*>& sources_to_remove);

		void removeTransitiveConnections(const NodePtr& parent, const NodePtr& child);

		/// Remove specified targets from a hyperedge and all its segments, cleaning up unused dummies
		//void removeTargetsFromHyperedge(const HyperedgePtr& edge, const std::unordered_set<NodePtr>& targets_to_remove);

		/// Check if a node is in the ancestors of this node
		bool parentIsInAncestors(const NodePtr& child, const NodePtr& parent);

		/// Check for cycles in the graph starting from a given node.
		bool checkCycles(const NodePtr& node);
	};

} // namespace hypergraph_logic
