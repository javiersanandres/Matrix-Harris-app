#include "Hypergraph.h"
#include <algorithm>
#include <stdexcept>
#include <set>

namespace hypergraph_logic {
	// ============================================================================
	// Constructor
	// ============================================================================
	Hypergraph::Hypergraph(const std::string& name) : name_(name) {};

	// ============================================================================
	// Node management
	// ============================================================================
	void Hypergraph::addNodeToLayer(int layer, int position, const NodePtr& node, int* out_min_new_layer) {
		if (!node) return;

		// Create layer if it doesn't exist
		if (layers_.find(layer) == layers_.end()) {
			layers_[layer] = LayerData{};
		}

		auto& layer_data = layers_[layer];

		if (std::find(layer_data.nodes.begin(), layer_data.nodes.end(), node) != layer_data.nodes.end()) {
			return; // Node already in this layer, do nothing
		}

		if (position < 0 || position > static_cast<int>(layer_data.nodes.size())) {
			position = static_cast<int>(layer_data.nodes.size()); // Append to end if out of bounds
		}

		layer_data.nodes.insert(layer_data.nodes.begin() + position, node);

		node->setLayer(layer);

		if (out_min_new_layer && layer < *out_min_new_layer) {
			*out_min_new_layer = layer;
		}
	}

	void Hypergraph::removeNodeFromLayer(int layer, const NodePtr& node) {
		if (!node || layers_.find(layer) == layers_.end()) return;
		auto& layer_nodes = layers_[layer].nodes;
		layer_nodes.erase(std::remove(layer_nodes.begin(), layer_nodes.end(), node), layer_nodes.end());
	}

	void Hypergraph::removeNodeFromLayer(int layer, const std::unordered_set<Node*>& nodes) {
		if (nodes.empty() || layers_.find(layer) == layers_.end()) return;
		auto& layer_nodes = layers_[layer].nodes;
		layer_nodes.erase(
			std::remove_if(layer_nodes.begin(), layer_nodes.end(),
				[&nodes](const NodePtr& node) {
					return nodes.count(node.get()) > 0;
				}),
			layer_nodes.end()
		);
	}

	std::vector<NodePtr> Hypergraph::getNodesAt(int layer) const {
		auto it = layers_.find(layer);
		if (it != layers_.end()) {
			return it->second.nodes;
		}
		return std::vector<NodePtr>();  // Empty vector if layer doesn't exist
	}

	std::vector<NodePtr> Hypergraph::getAllNodes() const {
		return all_nodes_;
	}

	// ============================================================================
	// Hyperedge management
	// ============================================================================
	HyperedgePtr Hypergraph::createHyperedge(const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets, int layer, std::set<int>* out_altered_layers) {
		auto edge = std::make_shared<Hyperedge>(sources, targets);
		all_hyperedges_[edge] = {};

		if (layer >= 0) {
			edge->setLayer(layer);
			layers_[layer].outgoing_edges.push_back(edge);
			if (out_altered_layers) out_altered_layers->insert(layer);
		}

		// Register parent-child relationships between sources and targets. 
		// Since the hyperedge is not a segment, all sources and targets are real nodes, 
		// so we can directly connect them without worrying about dummy nodes.
		for (const auto& s : sources)
			for (const auto& t : targets)
				if (!s->isDummy() && !t->isDummy()) {
					s->addChild(t);
					t->addParent(s);
				}

		return edge;
	}

	HyperedgePtr Hypergraph::createHyperedge(const WeakHyperedgePtr& origin, const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets, int layer, std::set<int>* out_altered_layers) {
		auto edge = std::make_shared<Hyperedge>(origin, sources, targets);
		if (auto orig = origin.lock()) {
			all_hyperedges_[orig].push_back(edge);
		}

		if (layer >= 0) {
			edge->setLayer(layer);
			layers_[layer].outgoing_edges.push_back(edge);
			if (out_altered_layers) out_altered_layers->insert(layer);
		}

		// Establish parent-child relationships between sources and targets,
		// taking account the real nodes should not have any notion of the dummies.
		for (const auto& src : sources) {
			for (const auto& tgt : targets) {
				bool src_dummy = src->isDummy();
				bool tgt_dummy = tgt->isDummy();

				if (src_dummy && tgt_dummy) {
					src->addChild(tgt);
					tgt->addParent(src);
				}
				else if (src_dummy && !tgt_dummy) {
					src->addChild(tgt);
				}
				else if (!src_dummy && tgt_dummy) {
					tgt->addParent(src);
				}
			}
		}

		return edge;
	}

	void Hypergraph::addHyperedgeToLayer(int layer, const HyperedgePtr& edge, std::set<int>* out_altered_layers) {
		if (!edge) return;
		if (layers_.find(layer) == layers_.end()) {
			layers_[layer] = LayerData{};
		}

		auto& layer_data = layers_[layer];

		if (std::find(layer_data.outgoing_edges.begin(), layer_data.outgoing_edges.end(), edge) != layer_data.outgoing_edges.end()) {
			return; // Edge already in this layer, do nothing
		}

		layer_data.outgoing_edges.push_back(edge);

		edge->setLayer(layer);

		if (out_altered_layers) out_altered_layers->insert(layer);
	}

	void Hypergraph::removeHyperedgeFromLayer(int layer, const HyperedgePtr& edge) {
		if (!edge || layers_.find(layer) == layers_.end()) return;
		auto& layer_outgoing_edges = layers_[layer].outgoing_edges;
		layer_outgoing_edges.erase(std::remove(layer_outgoing_edges.begin(), layer_outgoing_edges.end(), edge), layer_outgoing_edges.end());
		edge->setLayer(-1); // Unset layer
	}

	void Hypergraph::removeHyperedgeFromLayer(int layer, const std::unordered_set<Hyperedge*>& edges) {
		if (edges.empty() || layers_.find(layer) == layers_.end()) return;
		auto& layer_outgoing_edges = layers_[layer].outgoing_edges;
		for (const auto& edge : edges) {
			edge->setLayer(-1); // Unset layer
		}
		layer_outgoing_edges.erase(
			std::remove_if(layer_outgoing_edges.begin(), layer_outgoing_edges.end(),
				[&edges](const HyperedgePtr& edge) {
					return edges.count(edge.get()) > 0;
				}),
			layer_outgoing_edges.end()
		);
	}

	std::vector<HyperedgePtr> Hypergraph::getAllHyperedges() const {
		std::vector<HyperedgePtr> result;
		for (const auto& [orig, segments] : all_hyperedges_) {
			result.push_back(orig);
			result.insert(result.end(), segments.begin(), segments.end());
		}
		return result;
	}

	int Hypergraph::edgeIsShort(const HyperedgePtr& edge) {
		if (!edge || edge->getSources().empty() || edge->getTargets().empty()) return -1;


		for (const auto& s : edge->getSources())
			for (const auto& t : edge->getTargets())
				if (std::abs(s->getLayer() - t->getLayer()) != 1)
					return -1;

		return edge->getSources()[0]->getLayer();
	}

	int Hypergraph::settleEdgePlacement(const HyperedgePtr& edge, int* out_min_new_layer, std::set<int>* out_altered_layers) {
		int k = edgeIsShort(edge);
		if (k >= 0) {
			addHyperedgeToLayer(k, edge, out_altered_layers);
		}
		else {
			splitLongEdge(edge, out_min_new_layer, out_altered_layers);
		}
		return k;
	}

	void Hypergraph::collectSegmentDummies(const HyperedgePtr& edge, std::vector<Node*>& out_nodes, int& min_layer, int& max_layer, bool include_real_sources) {
		for (const auto& segment : all_hyperedges_[edge]) {
			for (const auto& s : segment->getSources()) {
				if (s->isDummy()) {
					out_nodes.push_back(s.get());
				}
				else if (!include_real_sources) {
					continue; // Real sources excluded from the bounds in this mode; skip updating min/max.
				}
				if (s->getLayer() < min_layer) {
					min_layer = s->getLayer();
				}
				if (s->getLayer() > max_layer) {
					max_layer = s->getLayer();
				}
			}
		}
	}
	
	int Hypergraph::settleEdgePlacementAndCollectDummies(const HyperedgePtr& edge, std::vector<Node*>& seed_nodes, int& min_layer, int& max_layer, bool include_real_sources, int* out_min_new_layer, std::set<int>* out_altered_layers) {
		int k = settleEdgePlacement(edge, out_min_new_layer, out_altered_layers);
		if (k < 0) {
			collectSegmentDummies(edge, seed_nodes, min_layer, max_layer, include_real_sources);
		}
		return k;
	}

	void Hypergraph::settleAndMinimizeIfSplit(const HyperedgePtr& edge, int* out_min_new_layer, std::set<int>* out_altered_layers) {
		std::vector<Node*> nodes_to_minimize;
		int min_layer = INT_MAX, max_layer = 0;
		if (settleEdgePlacementAndCollectDummies(edge, nodes_to_minimize, min_layer, max_layer, true, out_min_new_layer, out_altered_layers) < 0) {
			minimizeCrossingsForNodes(nodes_to_minimize, min_layer, max_layer);
		}
	}

	void Hypergraph::collapseToShortLayer(const HyperedgePtr& edge, int k, std::set<int>* out_altered_layers) {
		dissolveSegments({ edge.get() });
		addHyperedgeToLayer(k, edge, out_altered_layers);
	}

	void Hypergraph::resettleEdge(const HyperedgePtr& edge, int* out_min_new_layer, std::set<int>* out_altered_layers) {
		int k = edgeIsShort(edge);
		if (k >= 0) {
			dissolveSegments({ edge.get() });
			if (k != edge->getLayer()) {
				// This edge is now short, so it just needs to be relocated to the
				// correct layer if it is not already there.
				removeHyperedgeFromLayer(edge->getLayer(), edge);
				addHyperedgeToLayer(k, edge, out_altered_layers);
			}
		}
		else {
			splitLongEdge(edge, out_min_new_layer, out_altered_layers); // Already handles dissolving any stale segments before rebuilding.
		}
	}

	void Hypergraph::minimizeCrossingsAfterRelocation(const std::vector<NodePtr>& reference_nodes, int start_layer) {
		for (const auto& n : reference_nodes) {
			if (n->getLayer() + 1 < start_layer) {
				start_layer = n->getLayer() + 1;
			}
		}
		minimizeCrossings(10, start_layer);
	}

	void Hypergraph::minimizeCrossingsForRelocatedTargets(const HyperedgePtr& original_edge) {
		int start_layer = INT_MAX;
		for (const auto& tgt : original_edge->getTargets()) {
			for (const auto& p : tgt->getParents()) {
				if (p->getLayer() + 1 < start_layer) {
					start_layer = p->getLayer() + 1;
				}
			}
		}
		if (start_layer < INT_MAX) {
			minimizeCrossings(10, start_layer);
		}
	}

	// ============================================================================
	// Layer queries
	// ============================================================================
	int Hypergraph::getLayerCount() const {
		return static_cast<int>(layers_.size());
	}

	const std::map<int, LayerData>& Hypergraph::getLayers() const {
		return layers_;
	}

	const LayerData& Hypergraph::getLayerData(int layer) const {
		auto it = layers_.find(layer);
		if (it != layers_.end()) {
			return it->second;
		}
		static const LayerData empty_layer{};
		return empty_layer;
	}

	// ============================================================================
	// Connection addition management
	// ============================================================================
	NodePtr Hypergraph::createNode(const std::string& label, int layer_position, const NodePtr& parent, std::set<int>* out_altered_layers) {
		NodePtr node = std::make_shared<Node>(label);
		all_nodes_.push_back(node);

		if (!parent) {
			addNodeToLayer(0, layer_position, node);
		}
		else {
			createHyperedge({ parent }, { node }, parent->getLayer(), out_altered_layers);
			addNodeToLayer(parent->getLayer() + 1, layer_position, node);
		}

		if (layer_position == -1) {
			// No specific position requested, so we will insert it at the rightmost and less
			// disruptive position in the layer.
			minimizeCrossingsForNodes({ node.get() }, node->getLayer(), node->getLayer());
		}

		return node;
	}

	NodePtr Hypergraph::createParent(const std::string& label, const NodePtr& child, std::set<int>* out_altered_layers) {
		if (!child) {
			throw std::invalid_argument("Child node cannot be null when creating a parent.");
		}
		NodePtr node = std::make_shared<Node>(label);
		all_nodes_.push_back(node);
		addNodeToLayer(std::max(child->getLayer()-1, 0), -1, node);
		HyperedgePtr edge = createHyperedge({ node }, { child }, -1); // layer -1: not placed yet, nothing to report here.
		if (child->getLayer() == 0) {
			// The child will for sure need to be relocated and the parent will be placed in layer 0.
			relocateNodes({ child }, nullptr, out_altered_layers);
			// We needed to relocate, so we will apply crossing minimization globally from
			// layer 0 to the deepest layer, since the disruption is global. Also, the relocation
			// will have assigned the new edge to a layer, so we don't need to do it here.
			minimizeCrossings(10, 0);
		}
		else {
			// No relocation will be needed. The parent is located in the inmediate upper layer.
			// The edge is guaranteed to be short.
			node->setDesiredLayer(child->getLayer() > 1 ? child->getLayer() - 1 : -1);
			settleEdgePlacement(edge, nullptr, out_altered_layers);
			minimizeCrossingsForNodes({ node.get() }, node->getLayer(), node->getLayer());
		}
		return node;
	}

	NodePtr Hypergraph::createNodeInEdge(const std::string& label, const HyperedgePtr& edge, std::set<int>* out_altered_layers) {
		if (!edge) return nullptr;
		NodePtr node = std::make_shared<Node>(label);
		all_nodes_.push_back(node);

		// Snapshot sources and targets before modifying.
		auto sources = edge->getSources();
		auto targets = edge->getTargets();
		int edge_layer = edge->getLayer();

		// Removing all sources triggers edge removal and also handles the connection rewiring
		std::unordered_set<Node*> sources_set;
		int node_layer = 0;
		int min_layer = std::numeric_limits<int>::max();
		for (const auto& s : sources) {
			sources_set.insert(s.get());

			if (s->getLayer() + 1 > node_layer)
				node_layer = s->getLayer() + 1;

			if (s->getLayer() + 1 < min_layer)
				min_layer = s->getLayer() + 1;
		}
		// This removes every source of 'edge', so it dissolves the whole edge — exempt from
		// reporting on its own; the two brand-new replacement edges below report for themselves.
		removeSourcesFromHyperedge(edge, sources_set, false);
		addNodeToLayer(node_layer, -1, node);

		if (edge_layer >= 0) {
			// If it was short, the two new edges will also be short
			createHyperedge(sources, { node }, edge_layer, out_altered_layers);
			const auto& new_edge = createHyperedge({ node }, targets, edge_layer + 1, out_altered_layers);
			relocateNodes(new_edge->getTargets(), &min_layer, out_altered_layers); // Relocate the targets to one layer down
		}
		else {
			const auto& new_edge_1 = createHyperedge(sources, { node }, -1);
			settleEdgePlacement(new_edge_1, &min_layer, out_altered_layers);

			const auto& new_edge_2 = createHyperedge({ node }, targets, -1);
			if (!relocateNodes(new_edge_2->getTargets(), &min_layer, out_altered_layers)) {
				settleEdgePlacement(new_edge_2, &min_layer, out_altered_layers);
			}
		}

		// Apply crossing minimization to the new node and all possible new dummy nodes created by splitting the edge
		// or as a consequence of relocating the targets. We will decrease the number of sifting rounds by the purpose
		// of preserving the mental map as much as possible, while obviously minimizing crossings as well.
		minimizeCrossings(3, min_layer);
		return node;
	}

	NodePtr Hypergraph::createNodeNextTo(const std::string& label, const NodePtr& node, bool left) {
		if (!node) return nullptr;
		NodePtr new_node = std::make_shared<Node>(label);
		all_nodes_.push_back(new_node);

		int layer = node->getLayer();
		const auto& layer_nodes = layers_.at(layer).nodes;

		int position = -1;
		for (size_t i = 0; i < layer_nodes.size(); i++) {
			if (layer_nodes[i] == node) {
				position = left ? static_cast<int>(i) : static_cast<int>(i) + 1;
				break;
			}
		}
		if (position < 0) {
			throw std::logic_error("Node is not registered in its own layer.");
		}

		addNodeToLayer(layer, position, new_node);

		if (layer != 0) new_node->setDesiredLayer(layer);

		return new_node;
	}

	NodePtr Hypergraph::createSource(const std::string& label, int layer_position, const HyperedgePtr& edge, std::set<int>* out_altered_layers) {
		if (!edge) return nullptr;
		if (edge->getSources().empty() || edge->getTargets().empty()) {
			// Every edge reachable through the public API always has both, by construction (any edge
			// that loses its last source or target is dissolved immediately) — this should be
			// unreachable. Throwing rather than silently handling it turns a violated invariant
			// elsewhere into an immediate, debuggable failure instead of a corrupted layer placement.
			throw std::logic_error("Edge must already have at least one source and one target.");
		}

		NodePtr node = std::make_shared<Node>(label);
		all_nodes_.push_back(node);

		int layer_before = edge->getLayer();
		edge->addSource(node);
		int node_layer = INT_MAX;
		for (const auto& t : edge->getTargets()) {
			if (t->getLayer() - 1 < node_layer) node_layer = t->getLayer() - 1;
			t->addParent(node);
			node->addChild(t);
		}

		// node_layer is guaranteed >= 0 here: every target already has at least one parent (an
		// existing source of edge), so every target's layer is already >= 1 by the depth rule.
		addNodeToLayer(node_layer, layer_position, node);
		if (node_layer != 0) node->setDesiredLayer(node_layer);

		settleEdgePlacement(edge, nullptr, out_altered_layers);

		if (out_altered_layers && layer_before >= 0 && edge->getLayer() == layer_before) {
			out_altered_layers->insert(layer_before);
		}
		minimizeCrossingsForNodes({ node.get() }, node->getLayer(), node->getLayer());
		return node;
	}

	NodePtr Hypergraph::createTarget(const std::string& label, int layer_position, const HyperedgePtr& edge, std::set<int>* out_altered_layers) {
		if (!edge) return nullptr;
		NodePtr node = std::make_shared<Node>(label);
		all_nodes_.push_back(node);

		int layer_before = edge->getLayer();
		edge->addTarget(node);
		int node_layer = 0;
		for (const auto& s : edge->getSources()) {
			if (s->getLayer() + 1 > node_layer) node_layer = s->getLayer() + 1;
			s->addChild(node);
			node->addParent(s);
		}

		// Place the target in the shallowest possible layer
		addNodeToLayer(node_layer, layer_position, node);

		// Analogously to the createSource case, if the edge was short it will
		// continue being short and if it was long, the resplitting won't create
		// any new dummies or segments.
		settleEdgePlacement(edge, nullptr, out_altered_layers);

		// Same as createSource: adding a new target very commonly leaves the edge short at exactly
		// the same layer (the new target lands right where the existing ones already are), which
		// addHyperedgeToLayer's dedup check would otherwise silently swallow.
		if (out_altered_layers && layer_before >= 0 && edge->getLayer() == layer_before) {
			out_altered_layers->insert(layer_before);
		}
		minimizeCrossingsForNodes({ node.get() }, node->getLayer(), node->getLayer());
		return node;
	}

	HyperedgePtr Hypergraph::addConnection(const NodePtr& parent, const NodePtr& child, std::set<int>* out_altered_layers) {
		if (!child || !parent) return nullptr;
		if (child == parent) {
			throw std::invalid_argument("A node cannot be connected to itself.");
		}

		// Check for possible regroupings, that is, the parent and child are 
		// already linked through some hyperedge and this function call is a
		// way for them to be linked by a binary hyperedge. The hyperedge that
		// links them will suffer modifications and (at most) two other hyperedges
		// will be created: the one that links parent and child directly and possibly
		// another one from the already existing hyperedge sources (without the parent)
		// to the child. Note that this scenario has nothing to do with adding a 
		// connection but regrouping the ones already established. It is readily seen
		// that neither cycles nor relocations can occur. 
		bool already_connected = false;
		for (const auto& p : child->getParents()) {
			if (p == parent) {
				already_connected = true;
				break;
			}
		}

		if (already_connected) {
			// Find the specific hyperedge that links them (guaranteed to exist and be unique).
			for (const auto& [edge, _] : all_hyperedges_) {
				if (!edge->containsSource(parent) || !edge->containsTarget(child)) continue;

				std::vector<NodePtr> remaining_sources;
				std::vector<NodePtr> remaining_targets;
				for (const auto& source : edge->getSources()) {
					if (source != parent) remaining_sources.push_back(source);
				}
				for (const auto& target : edge->getTargets()) {
					if (target != child) remaining_targets.push_back(target);
				}

				if (remaining_sources.empty() && remaining_targets.empty()) {
					// Nothing to do, the connection already exists in the diagram
					throw std::logic_error("This connection already exists in the diagram.");
				}
				else if (remaining_sources.empty()) {
					// Remove target from edge and create a new one that links parent and child
					removeTargetsFromHyperedge(edge, { child.get() }, false, out_altered_layers);
				}
				else {
					// Remove source from edge and create a new one that links parent and child
					removeSourcesFromHyperedge(edge, { parent.get() }, false, out_altered_layers);
				}

				auto new_edge = createHyperedge({ parent }, { child }, -1);
				settleAndMinimizeIfSplit(new_edge, nullptr, out_altered_layers);

				if (!remaining_sources.empty() && !remaining_targets.empty()) {
					// If there are both remaining sources and targets, the previous removeSources call
					// removed the connection between parent and remaining targets, so we reinstate it.
					auto new_edge2 = createHyperedge({ parent }, remaining_targets, -1);
					settleAndMinimizeIfSplit(new_edge2, nullptr, out_altered_layers);
				}

				return new_edge;
			}
		}

		//  Check for redundancy connections (the child already has this parent in its ancestry)
		if (parentIsInAncestors({ child }, parent)) {
			throw std::logic_error("This connection already exists in the diagram.");
		}
		// Temporarily add the connection and check for cycles
		child->addParent(parent);
		parent->addChild(child);

		if (checkCycles(child)) {
			// Rollback
			child->removeParent(parent);
			parent->removeChild(child);

			throw std::logic_error("Adding this connection would create a cycle in the diagram.");
		}

		// Now, after we know that no cycles are added, we can safely add the connection.
		// First, remove possible pre-existing connections which are now redundant due to
		// the transitive property of any order relation.
		// Explanation: if we have a set X with a partial order <, 
		// and we add a new relation a < b, then we automatically obtain:
		//   - For any x in X, if x < a then we also have x < b (transitivity).
		//   - For any y in X, if b < y then we also have a < y (transitivity).
		// Therefore, any preexisting connection x < y where x < a and b < y would now be redundant 
		// and can be removed without losing any information about the partial order.
		//
		// Tracks the shallowest layer touched by any node placement across this whole operation —
		// the single accumulator throughout, never a separate return value or snapshot.
		int min_start_layer = INT_MAX;
		removeTransitiveConnections({ parent }, { child }, nullptr, &min_start_layer, out_altered_layers);

		int parent_layer = parent->getLayer();
		int child_layer = child->getLayer();
		HyperedgePtr edge;
		if (parent_layer == child_layer - 1) {
			// Easiest case: just add the new hyperedge and update the layer data
			edge = createHyperedge({ parent }, { child }, parent_layer, out_altered_layers);
		}
		else if (parent_layer < child_layer) {
			// The parent is in a layer above the child, so the child's layer does not need to be updated, but we need 
			// to add the new hyperedge, split it and add the necessary dummy nodes in the intermediate layers.
			edge = createHyperedge({ parent }, { child }, -1);

			bool outside_disruption = (min_start_layer < INT_MAX);

			// This edge is guaranteed to be long here, so settleEdgePlacementAndCollectDummies will always
			// split it and gather the new dummy nodes for us.
			std::vector<Node*> nodes_to_minimize;
			int min_layer = INT_MAX, max_layer = 0;
			settleEdgePlacementAndCollectDummies(edge, nodes_to_minimize, min_layer, max_layer, true, &min_start_layer, out_altered_layers);

			if (outside_disruption) {
				// We needed to remove some redundant connections, which lead to the creation of new dummy nodes
				// in between the layers. Therefore, at this point, we run global sifting from min_start_layer.
				// This case is less disruptive than the worst case, since fewer nodes are affected by the change,
				// so we will allow fewer rounds of sifting.
				minimizeCrossings(3, min_start_layer);
			}
			else {
				// If no new dummy nodes were created when removing redudant connections, then we only need to minimize 
				// crossings for the new dummy nodes created by splitting the edge.
				minimizeCrossingsForNodes(nodes_to_minimize, parent_layer + 1, child_layer - 1);
			}
		}
		else {
			// Worst case: the child layer needs to be updated. This automatically implies that the new layer
			// number is the parent_layer + 1 and this should propagate down to all the descendants of the child.
			edge = createHyperedge({ parent }, { child }, parent_layer, out_altered_layers);

			applyRelocationAndPropagate({ {child, resolveTargetLayer(child)} }, &min_start_layer, out_altered_layers);

			// We run global sifting from the min_layer of all parents to the affected child. Since this operation is
			// quite disruptive, we will allow more rounds of sifting to try to minimize crossings as much as possible.
			minimizeCrossingsAfterRelocation(child->getParents(), min_start_layer);
		}
		return edge;
	}

	void Hypergraph::addSourceToEdge(const HyperedgePtr& edge, const NodePtr& source, std::set<int>* out_altered_layers) {
		if (!edge || edge->isSegment() || !source) return;

		const auto targets = edge->getTargets();
		for (const auto& t : targets) {
			if (t == source) {
				throw std::logic_error("A node cannot be connected to itself.");
			}
		}
		if (edge->containsSource(source)) {
			throw std::logic_error("Source is already part of the hyperedge");
		}

		// Even though some connections might be redundant, they can also encode an intention
		// of grouping the sources and targets in a different manner. 
		std::unordered_set<Node*> already_linked_targets;
		std::vector<HyperedgePtr> affected_edges;
		for (const auto& [hyperedge, _] : all_hyperedges_) {
			if (!hyperedge->containsSource(source)) continue;
			bool already_added = false;
			for (const auto& t : targets) {
				if (hyperedge->containsTarget(t)) {
					already_linked_targets.insert(t.get());
					if (!already_added) {
						affected_edges.push_back(hyperedge);
						already_added = true;
					}
				}
			}
		}

		if (affected_edges.empty() && parentIsInAncestors(targets, source)) {
			throw std::logic_error("This connection already exists in the diagram.");
		}

		// Temporarily add the source and check for cycles
		for (const auto& t : targets) {
			if (already_linked_targets.count(t.get()) == 0) {
				source->addChild(t);
				t->addParent(source);
			}
		}

		if (checkCycles(source)) {
			// Rollback
			for (const auto& t : targets) {
				if (already_linked_targets.count(t.get()) == 0) {
					source->removeChild(t);
					t->removeParent(source);
				}
			}
			throw std::logic_error("Adding this connection would create a cycle in the diagram.");
		}

		// Tracks the shallowest layer touched by any node placement (new dummies, relocations)
		// across this whole operation so that minimize crossings can be performed wisely.
		int min_start_layer = INT_MAX;

		// Now we know that no cycles are added, we can safely add the connection.
		if (!affected_edges.empty()) {
			// Remove the source from the affected hyperedges.
			for (const auto& hyperedge : affected_edges) {
				// We will relocate later, so we avoid it here. Since there is no relocation.
				removeSourcesFromHyperedge(hyperedge, { source.get() }, false, out_altered_layers);
			}

			// Now, reinstate the connections from the source to the affected targets since they were removed in the previous step.
			for (const auto& t : already_linked_targets) {
				source->addChild(t->shared_from_this());
				t->addParent(source);
			}

			// For each affected hyperedge, create a new hyperedge with those targets which
			// are not present in the edge to which the source is added
			for (const auto& hyperedge : affected_edges) {
				std::vector<NodePtr> remaining_targets;
				for (const auto& target : hyperedge->getTargets()) {
					if (!already_linked_targets.count(target.get())) {
						remaining_targets.push_back(target);
					}
				}

				if (!remaining_targets.empty()) {
					// Add a new hyperedge with the remaining targets from affected hyperedges from the source.
					// This avoids data loss since the source was removed from those hyperedges before.
					HyperedgePtr new_edge = createHyperedge({ source }, remaining_targets, -1);

					// Since all dummy nodes created will be located minimizing crossings,
					// their layers should not affect min_start_layer computation.
					settleAndMinimizeIfSplit(new_edge, nullptr, out_altered_layers);
				}
			}
		}

		// Remove any pre-existing connections which are now redundant.
		removeTransitiveConnections({ source }, targets, nullptr, &min_start_layer, out_altered_layers);

		// Special care, the previous call could have removed the edge from the hypergraph
		// if all sources where ancestors of source. So we may have to readd it.
		all_hyperedges_[edge]; // This does nothing or reinstates the edge in the hypergraph.

		int edge_layer_before = edge->getLayer();
		edge->addSource(source);

		if (!relocateNodes(targets, &min_start_layer, out_altered_layers)) {
			// No targets need to be relocated. But this edge could have some ancestors of the new source
			// as sources, which have been removed in the removeTransitiveConnections call, so the new edge
			// could be long or short depending on the case.
			bool outside_disruption = (min_start_layer < INT_MAX);

			std::vector<Node*> nodes_to_minimize;
			int min_layer = INT_MAX, max_layer = 0;
			int k = settleEdgePlacementAndCollectDummies(edge, nodes_to_minimize, min_layer, max_layer, true, &min_start_layer, out_altered_layers);

			// If the edge stays short at exactly the layer it was already registered at,
			// addHyperedgeToLayer's dedup check silently no-ops and never reports it — but the
			// edge just gained a source, so its span changed regardless of registration.
			if (out_altered_layers && edge_layer_before >= 0 && edge->getLayer() == edge_layer_before) {
				out_altered_layers->insert(edge_layer_before);
			}

			if (k < 0 && outside_disruption) {
				if (min_layer + 1 < min_start_layer) min_start_layer = min_layer + 1;
			}
			else if (k < 0) {
				// No outside disruption: this edge's own new dummies are the only thing that needs attention.
				minimizeCrossingsForNodes(nodes_to_minimize, min_layer, max_layer);
			}

			if (outside_disruption) {
				// min_start_layer already reflects everything merged in above: outside disruption plus
				// (if k < 0) this edge's own footprint, so it's used directly as the final start point.
				minimizeCrossings(3, min_start_layer);
			}
		}
		else {
			if (out_altered_layers && edge_layer_before >= 0 && edge->getLayer() == edge_layer_before) {
				out_altered_layers->insert(edge_layer_before);
			}
			minimizeCrossingsAfterRelocation(edge->getSources(), min_start_layer);
		}
	}
	
	void Hypergraph::addTargetToEdge(const HyperedgePtr& edge, const NodePtr& target, std::set<int>* out_altered_layers) {
		if (!edge || edge->isSegment() || !target) return;

		const auto& sources = edge->getSources();
		int parents_layer = 0;
		for (const auto& s : sources) {
			if (s == target) {
				throw std::logic_error("A node cannot be connected to itself.");
			}
			if (s->getLayer() > parents_layer) {
				parents_layer = s->getLayer();
			}
		}
		if (edge->containsTarget(target)) {
			throw std::logic_error("The target is already part of the hyperedge.");
		}

		// Even though some connections might be redundant, they can also encode an
		// intention of grouping the sources and targets in a different manner. 
		std::unordered_set<Node*> already_linked_sources;
		std::vector<HyperedgePtr> affected_edges;
		for (const auto& [hyperedge, _] : all_hyperedges_) {
			if (!hyperedge->containsTarget(target)) continue;
			bool already_added = false;
			for (const auto& s : sources) {
				if (hyperedge->containsSource(s)) {
					already_linked_sources.insert(s.get());
					if (!already_added) {
						affected_edges.push_back(hyperedge);
						already_added = true;
					}
				}
			}
		}

		if (affected_edges.empty() && childIsInDescendants(sources, target)) {
			throw std::logic_error("This connection already exists in the diagram.");
		}

		// Temporarily add the target and check for cycles
		for (const auto& s : sources) {
			if (already_linked_sources.count(s.get()) == 0) {
				// This connection does not exist yet, so we need to add it temporarily.
				s->addChild(target);
				target->addParent(s);
			}
		}

		// If adding these connections has created a cycle, then it must be the case
		// that target is part of the cycle, so it suffices to check for cycles starting
		// there rather than checking the sources.
		if (checkCycles(target)) {
			// Rollback
			for (const auto& s : sources) {
				if (already_linked_sources.count(s.get()) == 0) {
					// This connection was not pre-existing, so we need to rollback.
					s->removeChild(target);
					target->removeParent(s);
				}
			}
			throw std::logic_error("Adding this connection would create a cycle in the diagram.");
		}

		// Tracks the shallowest layer touched by any node placement across this whole operation.
		// This way we will start minimizing crossings right where they need to be.
		int min_start_layer = INT_MAX;

		// Now that we know that no cycles are added, we can safely add the connection.
		if (!affected_edges.empty()) {
			// Remove the target from all the hyperedges in which it participates as so.
			for (const auto& hyperedge : affected_edges) {
				// We will relocate later, so we avoid it here. Since there is no relocation.
				removeTargetsFromHyperedge(hyperedge, { target.get() }, false, out_altered_layers);
			}
			// Now, reinstate the connections from the target parents to the target since they were removed in the previous step.
			for (const auto& s : already_linked_sources) {
				s->addChild(target);
				target->addParent(s->shared_from_this());
			}

			// For each affected hyperedge, create a new hyperedge with those sources which
			// are not present in the edge to which the target is added.
			for (const auto& hyperedge : affected_edges) {
				std::vector<NodePtr> remaining_sources;
				for (const auto& source : hyperedge->getSources()) {
					if (!already_linked_sources.count(source.get())) {
						remaining_sources.push_back(source);
					}
				}

				if (!remaining_sources.empty()) {
					// Add a new hyperedge with the remaining sources from affected hyperedges to the target.
					// This avoids data loss since the target was removed from those hyperedges before.
					HyperedgePtr new_edge = createHyperedge(remaining_sources, { target }, -1);

					// Since all dummy nodes created will be located minimizing crossings,
					// their layers should not affect min_start_layer computation.
					settleAndMinimizeIfSplit(new_edge, nullptr, out_altered_layers);
				}
			}
		}

		HyperedgePtr replacement_edge = resolveOwnRedundantTargets(edge, target, &min_start_layer, out_altered_layers);
		bool edge_was_dissolved = (replacement_edge != nullptr);

		// As before, we need to remove any other, unrelated pre-existing connections which are now
		// redundant. We must exclude `replacement_edge`, if one was created above, from this scan: it
		// exactly represents the connection currently being added, and would otherwise be found and
		// destroyed by this generic check as a trivial self-match.
		removeTransitiveConnections(sources, { target }, replacement_edge, &min_start_layer, out_altered_layers);

		int edge_layer_before = edge->getLayer();
		if (!edge_was_dissolved) {
			edge->addTarget(target);
		}

		if (parents_layer + 1 > target->getLayer()) {
			applyRelocationAndPropagate({ {target, resolveTargetLayer(target)} }, &min_start_layer, out_altered_layers);
			if (out_altered_layers && !edge_was_dissolved && edge_layer_before >= 0 && edge->getLayer() == edge_layer_before) {
				out_altered_layers->insert(edge_layer_before);
			}
			minimizeCrossingsAfterRelocation(sources, min_start_layer);
		}
		else {
			bool outside_disruption = (min_start_layer < INT_MAX);

			if (!edge_was_dissolved) {
				std::vector<Node*> nodes_to_minimize;
				int min_layer = INT_MAX, max_layer = 0;
				int k = settleEdgePlacementAndCollectDummies(edge, nodes_to_minimize, min_layer, max_layer, true, &min_start_layer, out_altered_layers);

				if (out_altered_layers && edge_layer_before >= 0 && edge->getLayer() == edge_layer_before) {
					out_altered_layers->insert(edge_layer_before);
				}

				if (k < 0 && outside_disruption) {
					if (min_layer + 1 < min_start_layer) min_start_layer = min_layer + 1;
				}
				else if (k < 0) {
					// No outside disruption: this edge's own new dummies are the only thing that needs attention.
					minimizeCrossingsForNodes(nodes_to_minimize, min_layer, max_layer);
				}
			}

			if (outside_disruption) {
				// min_start_layer already reflects everything merged in above: outside disruption plus
				// (if k < 0) this edge's own footprint, so it's used directly as the final start point.
				minimizeCrossings(3, min_start_layer);
			}
		}
	}

	// ============================================================================
	// Removal management
	// ============================================================================
	void Hypergraph::removeNode(const NodePtr& node, std::set<int>* out_altered_layers) {
		if (!node) return;

		const auto parents = node->getParents();
		const auto children = node->getChildren();

		// Snapshot the hyperedges before modifying them.
		std::unordered_map<HyperedgePtr, std::vector<HyperedgePtr>, HyperedgePtrHash> snapshot = all_hyperedges_;

		if (children.empty()) {
			// If it has no children, just remove all connections in which this node 
			// participates, which must be as a target since it has no children.
			for (const auto& [edge, _] : snapshot) {
				if (edge->containsTarget(node)) {
					removeTargetsFromHyperedge(edge, { node.get() }, false, out_altered_layers); // No relocation will be needed
				}
			}
		}
		else {
			if (parents.empty()) {
				// If it has no parents but has children, just remove it from source in all
				// conections in which it participates as so and relocate the targets accordingly.
				std::unordered_set<Node*> relocations; // for quick lookup
				std::vector<NodePtr> relocations_vec; // to avoid copying the nodes when we need to relocate them later on.
				for (const auto& [edge, _] : snapshot) {
					if (edge->containsSource(node)) {
						auto targets = edge->getTargets(); // Snapshot the targets before modifying the edge.
						removeSourcesFromHyperedge(edge, { node.get() }, false, out_altered_layers); // We will relocate at the very end.
						for (const auto& t : targets) {
							if (relocations.insert(t.get()).second) {
								relocations_vec.push_back(t);
							}
						}
					}
				}

				int min_start_layer = INT_MAX;
				if (relocateNodes(relocations_vec, &min_start_layer, out_altered_layers)) {
					minimizeCrossings(10, min_start_layer);
				}
			}
			else {
				// If it has both parents and children, the parents need to assume the connections 
				// to the children to avoid losing information.

				// First, remove it from targets and sources. In both cases, we avoid early relocation
				// since more updates will come after.
				for (const auto& [edge, _] : snapshot) {
					if (edge->containsSource(node)) {
						removeSourcesFromHyperedge(edge, { node.get() }, false, out_altered_layers);
					}

					if (edge->containsTarget(node)) {
						removeTargetsFromHyperedge(edge, { node.get() }, false, out_altered_layers);
					}
				}

				HyperedgePtr edge = createHyperedge(parents, children, -1);

				int min_start_layer = INT_MAX;
				if (relocateNodes(edge->getTargets(), &min_start_layer, out_altered_layers)) {
					minimizeCrossingsAfterRelocation(parents, min_start_layer);
				}
				else {
					settleAndMinimizeIfSplit(edge, nullptr, out_altered_layers);
				}
			}
		}

		// Finally, remove the node from the graph and from its layer.
		all_nodes_.erase(std::remove(all_nodes_.begin(), all_nodes_.end(), node), all_nodes_.end());
		removeNodeFromLayer(node->getLayer(), node);
		cleanUp();
	}

	void Hypergraph::removeConnection(const NodePtr& parent, const NodePtr& child, std::set<int>* out_altered_layers) {
		if (!parent || !child) return;

		// Check that the connection actually exists before trying to remove it.
		bool is_parent = false;
		for (const auto& p : child->getParents()) {
			if (p == parent) {
				is_parent = true;
				break;
			}
		}

		if (!is_parent) {
			throw std::logic_error("The specified connection does not exist in the diagram.");
		}

		// Snapshot before modifying
		std::unordered_map<HyperedgePtr, std::vector<HyperedgePtr>, HyperedgePtrHash> snapshot = all_hyperedges_;

		for (const auto& [edge, _] : snapshot) {
			if (edge->containsSource(parent) && edge->containsTarget(child)) {
				auto remaining_sources = edge->getSources();
				remaining_sources.erase(std::remove(remaining_sources.begin(), remaining_sources.end(), parent), remaining_sources.end());
				removeTargetsFromHyperedge(edge, { child.get() }, false, out_altered_layers);
				if (remaining_sources.empty()) {
					int min_new_layer = INT_MAX;
					if (relocateNodes({ child }, &min_new_layer, out_altered_layers)) {
						minimizeCrossingsAfterRelocation(child->getParents(), min_new_layer);
					}
					return;
				}

				const auto& new_edge = createHyperedge(remaining_sources, { child }, -1);
				int min_start_layer = INT_MAX;
				if (relocateNodes({ child }, &min_start_layer, out_altered_layers)) {
					if (child->getChildren().empty()) {
						// The child has no children, so we just have to take care of the possibly
						// created dummy nodes in the new edge (if relocating has caused splitting).
						std::vector<Node*> nodes_to_minimize;
						int min_layer = INT_MAX, max_layer = 0;
						collectSegmentDummies(new_edge, nodes_to_minimize, min_layer, max_layer, /*include_real_sources=*/false);
						minimizeCrossingsForNodes(nodes_to_minimize, min_layer, max_layer);
					}
					else {
						minimizeCrossingsAfterRelocation(child->getParents(), min_start_layer);
					}
				}
				else {
					std::vector<Node*> nodes_to_minimize;
					int min_layer = INT_MAX, max_layer = 0;
					int k = settleEdgePlacementAndCollectDummies(new_edge, nodes_to_minimize, min_layer, max_layer, /*include_real_sources=*/false, nullptr, out_altered_layers);
					if (k < 0) {
						// The new edge is long: it has already been split above; place the new dummy nodes.
						minimizeCrossingsForNodes(nodes_to_minimize, min_layer, max_layer);
					}
				}
				return; // There cannot be any other edge connecting the same parent and child, so we may stop after the first one.
			}
		}
	}

	static bool allSourcesDead(const HyperedgePtr& seg, const std::unordered_set<Node*>& dead_dummies, const std::unordered_set<Node*>& removed_sources)
	{
		for (const auto& s : seg->getSources()) {
			if (dead_dummies.count(s.get()))  continue;
			if (removed_sources.count(s.get())) continue;
			return false;
		}
		return true;
	}

	void Hypergraph::removeSourcesFromHyperedge(const HyperedgePtr& original_edge, const std::unordered_set<Node*>& sources_to_remove, bool relocation, std::set<int>* out_altered_layers)
	{
		if (sources_to_remove.empty() || original_edge->isSegment()) return;

		for (Node* s : sources_to_remove) {
			if (!original_edge->containsSource(s->shared_from_this())) {
				throw std::logic_error("The specified connection does not exist in the diagram.");
			}
		}

		for (Node* s : sources_to_remove)
			original_edge->removeSource(s->shared_from_this());

		// Also remove real parent/child links on the real nodes.
		for (const auto& t : original_edge->getTargets()) {
			for (Node* s : sources_to_remove) {
				s->removeChild(t);
				t->removeParent(s->shared_from_this());
			}
		}

		if (original_edge->getSources().empty()) {
			// Edge has no sources left — dissolve everything. Whole-edge removal never counts
			// as an altered layer, regardless of how many sources/targets it used to span.
			dissolveSegments({ original_edge.get() });
			all_hyperedges_.erase(original_edge);
			if (original_edge->getLayer() >= 0) {
				removeHyperedgeFromLayer(original_edge->getLayer(), original_edge);
			}

			if (relocation) {
				if (relocateNodes(original_edge->getTargets(), nullptr, out_altered_layers)) {
					minimizeCrossingsForRelocatedTargets(original_edge);
				}
			}
			cleanUp();
			return;
		}

		if (all_hyperedges_[original_edge].empty()) {
			// The edge was already short and stays short: no segment/layer restructuring needed,
			// but it just lost a source while remaining registered at its existing layer, which
			// is exactly the kind of change that can shift its span and affect that layer's MIP.
			if (out_altered_layers && original_edge->getLayer() >= 0) {
				out_altered_layers->insert(original_edge->getLayer());
			}
			return;
		}
		int k = edgeIsShort(original_edge);
		if (k >= 0) {
			// The edge is now short. The targets don't update their layer, since the remaining
			// sources are in the immediate shallower layer.
			collapseToShortLayer(original_edge, k, out_altered_layers);
			cleanUp();
			return;
		}

		// Group segments by layer for up-bottom traversal.
		// Dead targets propagate downwards: a dead real target kills the dummy below it, 
		// which may kill the segment below that and so on.
		std::map<int, HyperedgePtr> segs_by_layer;
		for (const auto& seg : all_hyperedges_[original_edge]) {
			segs_by_layer[seg->getLayer()] = seg;
		}

		// Track dead dummies and segments in an up-bottom manner.
		std::unordered_set<Node*> dead_dummies;
		std::unordered_set<Hyperedge*> dead_segments;
		for (auto& [layer, seg] : segs_by_layer) {
			size_t sources_before = seg->getSources().size();

			for (Node* s : sources_to_remove)
				seg->removeSource(s->shared_from_this());
			for (Node* d : dead_dummies)
				seg->removeSource(d->shared_from_this());

			bool lost_a_source = seg->getSources().size() != sources_before;

			if (allSourcesDead(seg, dead_dummies, sources_to_remove)) {
				// This whole segment is dead.
				dead_segments.insert(seg.get());

				// Any dummy targets of this segment are now dead too —
				// they were only reachable via this segment's sources.
				for (const auto& t : seg->getTargets())
					if (t->isDummy()) dead_dummies.insert(t.get());
			}
			else if (lost_a_source && out_altered_layers) {
				// Segment survives but its source set (and therefore its span) shrank.
				out_altered_layers->insert(layer);
			}
		}

		// Remove dead segments from LayerData and all_hyperedges_
		for (auto& [layer, seg] : segs_by_layer) {
			if (!dead_segments.count(seg.get())) continue;
			removeHyperedgeFromLayer(layer, seg);
		}
		auto& segments = all_hyperedges_[original_edge];
		segments.erase(
			std::remove_if(segments.begin(), segments.end(),
				[&](const HyperedgePtr& e) {
					return dead_segments.count(e.get()) > 0;
				}),
			segments.end());

		// Remove dead dummies from LayerData and all_nodes_. 
		std::map<int, std::unordered_set<Node*>> dead_by_layer;
		for (Node* d : dead_dummies)
			dead_by_layer[d->getLayer()].insert(d);

		for (const auto& [layer, nodes] : dead_by_layer)
			removeNodeFromLayer(layer, nodes);

		all_nodes_.erase(
			std::remove_if(all_nodes_.begin(), all_nodes_.end(),
				[&](const NodePtr& n) {
					return dead_dummies.count(n.get()) > 0;
				}),
			all_nodes_.end());

		// ----------------------------------------------------------------
		// Removed sources may have been the deepest parent of some 
		// targets, so those targets (and their descendants) may need
		// to relocate upward.
		// ----------------------------------------------------------------
		if (relocation) {
			if (relocateNodes(original_edge->getTargets(), nullptr, out_altered_layers)) {
				minimizeCrossingsForRelocatedTargets(original_edge);
			}
		}
		cleanUp();
	}

	static bool allTargetsDead(const HyperedgePtr& seg, const std::unordered_set<Node*>& dead_dummies, const std::unordered_set<Node*>& removed_targets)
	{
		for (const auto& t : seg->getTargets()) {
			if (dead_dummies.count(t.get()))    continue;
			if (removed_targets.count(t.get())) continue;
			return false;
		}
		return true;
	}

	void Hypergraph::removeTargetsFromHyperedge(const HyperedgePtr& original_edge, const std::unordered_set<Node*>& targets_to_remove, bool relocation, std::set<int>* out_altered_layers)
	{
		if (targets_to_remove.empty() || original_edge->isSegment()) return;
		std::vector<NodePtr> targets_to_relocate; // to batch relocate at the end if needed.

		for (Node* t : targets_to_remove) {
			if (!original_edge->containsTarget(t->shared_from_this())) {
				throw std::logic_error("The specified connection does not exist in the diagram.");
			}
		}

		// Remove targets from the original edge.
		for (Node* t : targets_to_remove) {
			original_edge->removeTarget(t->shared_from_this());
			targets_to_relocate.push_back(t->shared_from_this());
		}

		// Remove real parent/child links on real nodes.
		for (const auto& s : original_edge->getSources()) {
			for (Node* t : targets_to_remove) {
				s->removeChild(t->shared_from_this());
				t->removeParent(s);
			}
		}

		if (original_edge->getTargets().empty()) {
			// Edge has no targets left — dissolve everything. Whole-edge removal never counts
			// as an altered layer.
			dissolveSegments({ original_edge.get() });
			all_hyperedges_.erase(original_edge);
			if (original_edge->getLayer() >= 0) {
				removeHyperedgeFromLayer(original_edge->getLayer(), original_edge);
			}
			if (relocation) {
				int min_start_layer = INT_MAX;
				if (relocateNodes(targets_to_relocate, &min_start_layer, out_altered_layers)) {
					minimizeCrossings(10, min_start_layer);
				}
			}
			cleanUp();
			return;
		}

		if (all_hyperedges_[original_edge].empty()) {
			// The edge was already short and stays short: it just lost a target while remaining
			// registered at its existing layer, which can shift its span and affect that layer's MIP.
			if (out_altered_layers && original_edge->getLayer() >= 0) {
				out_altered_layers->insert(original_edge->getLayer());
			}
			return;
		}
		int k = edgeIsShort(original_edge);
		if (k >= 0) {
			// The edge is now short, so just collapse the segments and add it to the new layer.
			collapseToShortLayer(original_edge, k, out_altered_layers);
			cleanUp();
			return;
		}

		// Group segments by layer for bottom-up traversal.
		// Unlike source removal (top-down), dead targets propagate upward:
		// a dead real target kills the dummy above it, which may kill the segment above that.
		std::map<int, HyperedgePtr> segs_by_layer;
		for (const auto& seg : all_hyperedges_[original_edge])
			segs_by_layer[seg->getLayer()] = seg;

		std::unordered_set<Node*> dead_dummies;
		std::unordered_set<Hyperedge*> dead_segments;

		// Iterate bottom-to-top (reverse layer order).
		for (auto it = segs_by_layer.rbegin(); it != segs_by_layer.rend(); ++it) {
			auto& [layer, seg] = *it;

			size_t targets_before = seg->getTargets().size();

			// Remove the real targets that are being dropped from this segment.
			for (Node* t : targets_to_remove)
				seg->removeTarget(t->shared_from_this());

			// Remove dummies that died in the segment below (they were targets here).
			for (Node* d : dead_dummies)
				seg->removeTarget(d->shared_from_this());

			bool lost_a_target = seg->getTargets().size() != targets_before;

			if (allTargetsDead(seg, dead_dummies, targets_to_remove)) {
				dead_segments.insert(seg.get());

				// Any dummy sources of this segment are now dead too —
				// they only existed to feed targets that are all gone.
				for (const auto& s : seg->getSources())
					if (s->isDummy()) dead_dummies.insert(s.get());
			}
			else if (lost_a_target && out_altered_layers) {
				// Segment survives but its target set (and therefore its span) shrank.
				out_altered_layers->insert(layer);
			}
		}

		// Remove dead segments from LayerData.
		for (auto& [layer, seg] : segs_by_layer) {
			if (!dead_segments.count(seg.get())) continue;
			removeHyperedgeFromLayer(layer, seg);
		}

		// Remove dead segments from all_hyperedges_.
		auto& segments = all_hyperedges_[original_edge];
		segments.erase(
			std::remove_if(segments.begin(), segments.end(),
				[&](const HyperedgePtr& e) {
					return dead_segments.count(e.get()) > 0;
				}),
			segments.end());

		// Remove dead dummies from LayerData and all_nodes_.
		std::map<int, std::unordered_set<Node*>> dead_by_layer;
		for (Node* d : dead_dummies)
			dead_by_layer[d->getLayer()].insert(d);

		for (const auto& [layer, nodes] : dead_by_layer)
			removeNodeFromLayer(layer, nodes);

		all_nodes_.erase(
			std::remove_if(all_nodes_.begin(), all_nodes_.end(),
				[&](const NodePtr& n) {
					return dead_dummies.count(n.get()) > 0;
				}),
			all_nodes_.end());

		// ----------------------------------------------------------------
		// Removed targets may now have fewer parents, so their deepest 
		// parent may have changed and they (and descendants) must relocate 
		// upward. Collect all affected nodes and batch-relocate.
		// ----------------------------------------------------------------
		if (relocation) {
			if (relocateNodes(targets_to_relocate, nullptr, out_altered_layers)) {
				minimizeCrossingsForRelocatedTargets(original_edge);
			}
		}
		cleanUp();
	}

	// ============================================================================
	// Node fusion management
	// ============================================================================
	void Hypergraph::fuseNodes(const NodePtr& node1, const NodePtr& node2, const std::string& new_name, std::set<int>* out_altered_layers) {
		if (!node1 || !node2) return;
		if (node1 == node2) {
			throw std::invalid_argument("Cannot fuse a node with itself.");
		}

		// Always keep the shallower node as the surviving object, and absorb the deeper one.
		// This ensures a relocation is always needed in case nodes layers differ.
		const NodePtr& survivor = (node1->getLayer() <= node2->getLayer()) ? node1 : node2;
		const NodePtr& absorbed = (survivor == node1) ? node2 : node1;

		// Store the previous configuration of the nodes to be able to rollback in case of cycle creation.
		const auto parents_absorbed = absorbed->getParents();
		const auto children_absorbed = absorbed->getChildren();
		std::unordered_set<Node*> parents_survivor_set; // for quick lookup later on.
		std::unordered_set<Node*> children_survivor_set; // for quick lookup later on.
		for (const auto& p : survivor->getParents()) parents_survivor_set.insert(p.get());
		for (const auto& c : survivor->getChildren()) children_survivor_set.insert(c.get());

		// Temporarily fuse the nodes moving all parents and children to one while disconnecting the other
		for (const auto& p : parents_absorbed) {
			if (!parents_survivor_set.count(p.get())) {
				p->replaceChild(absorbed, survivor);
				survivor->addParent(p);
			}
			else {
				p->removeChild(absorbed);
			}
		}

		for (const auto& c : children_absorbed) {
			if (!children_survivor_set.count(c.get())) {
				c->replaceParent(absorbed, survivor);
				survivor->addChild(c);
			}
			else {
				c->removeParent(absorbed);
			}
		}

		if (checkCycles(survivor)) {
			// Rollback
			for (const auto& p : parents_absorbed) {
				if (!parents_survivor_set.count(p.get())) {
					p->replaceChild(survivor, absorbed);
					survivor->removeParent(p);
				}
				else {
					p->addChild(absorbed);
				}
			}

			for (const auto& c : children_absorbed) {
				if (!children_survivor_set.count(c.get())) {
					c->replaceParent(survivor, absorbed);
					survivor->removeChild(c);
				}
				else {
					c->addParent(absorbed);
				}
			}
			throw std::logic_error("Fusing these nodes would create a cycle in the diagram.");
		}

		// Now we know that no cycles are added, we can safely fuse the nodes.
		survivor->setName(new_name);
		if (survivor->getLayer() != absorbed->getLayer()) {
			survivor->setDesiredLayer(absorbed->getDesiredLayer());
		}
		else {
			survivor->setDesiredLayer(std::max(survivor->getDesiredLayer(), absorbed->getDesiredLayer()));
		}

		// Modify all hyperedges in which the absorbed node participated to replace it with the survivor.
		std::vector<HyperedgePtr> modified_edges;
		for (const auto& edge : getAllHyperedges()) {
			if (edge->containsSource(absorbed)) {
				if (edge->containsSource(survivor)) {
					// If survivor is already a source of this edge, we just need to remove absorbed from the sources without replacement.
					edge->removeSource(absorbed);
				}
				else {
					edge->replaceSource(absorbed, survivor);
				}
				// Either way, this edge's source set just changed and reports that change in altered layers.
				if (out_altered_layers && edge->getLayer() >= 0) {
					out_altered_layers->insert(edge->getLayer());
				}
				if (!edge->isSegment()) modified_edges.push_back(edge);
				continue;
			}
			if (edge->containsTarget(absorbed)) {
				if (edge->containsTarget(survivor)) {
					// If survivor is already a target of this edge, we just need to remove absorbed from the targets without replacement.
					edge->removeTarget(absorbed);
				}
				else {
					edge->replaceTarget(absorbed, survivor);
				}
				// Same here, this edge's target set has changed and reports it in altered layers.
				if (out_altered_layers && edge->getLayer() >= 0) {
					out_altered_layers->insert(edge->getLayer());
				}
				if (!edge->isSegment()) modified_edges.push_back(edge);
				continue;
			}
			if (!edge->isSegment() && (edge->containsSource(survivor) || edge->containsTarget(survivor))) {
				modified_edges.push_back(edge);
			}
		}

		// Remove the absorbed node from the graph and from its layer.
		all_nodes_.erase(std::remove(all_nodes_.begin(), all_nodes_.end(), absorbed), all_nodes_.end());
		removeNodeFromLayer(absorbed->getLayer(), absorbed);

		// Now it may be possible that some hyperedges have been "duplicated" in the sense
		// that they have the same sources and targets after the fusion, so we need to remove 
		// those redundancies.
		std::map<std::pair<std::set<Node*>, std::set<Node*>>, HyperedgePtr> seen;
		std::unordered_set<Hyperedge*> to_dissolve;

		for (const auto& edge : modified_edges) {
			auto sources = edge->getSources();
			auto targets = edge->getTargets();
			std::set<Node*> src_set, tgt_set;
			for (const auto& s : sources) src_set.insert(s.get());
			for (const auto& t : targets) tgt_set.insert(t.get());

			auto key = std::make_pair(src_set, tgt_set);
			auto it = seen.find(key);
			if (it == seen.end()) {
				seen[key] = edge;
			}
			else {
				// we have a duplicate, dissolve it.
				to_dissolve.insert(edge.get());
			}
		}

		for (auto* e : to_dissolve) {
			dissolveSegments({ e });
			auto ptr = e->shared_from_this();
			if (ptr->getLayer() >= 0)
				removeHyperedgeFromLayer(ptr->getLayer(), ptr);
			all_hyperedges_.erase(ptr);
		}

		// Relocate the surviving node. Since it was chosen as the shallower of the two, absorbing
		// the deeper node's parents can only ever push it deeper (never shallower).
		// If no relocation is needed, that implicitly means both nodes were already at the same layer 
		// and the fusion doesn't change any other layer number either, so nothing else needs relocating.
		int min_start_layer = INT_MAX;
		if (relocateNodes({ survivor }, &min_start_layer, out_altered_layers)) {
			// applyRelocationAndPropagate already re-settled every edge touching survivor (as
			// source or target) via its own Phase 1/Phase 3 sweep, which covers every surviving
			// entry in modified_edges, since each one touches survivor directly.
			minimizeCrossings(10, min_start_layer);
		}
		else {
			minimizeCrossingsForNodes({ survivor.get() }, survivor->getLayer(), survivor->getLayer());
		}
	}

	// ============================================================================
	// Node layer management
	// ============================================================================
	void Hypergraph::relabelLayerContents(const LayerData& data, int new_layer) {
		for (const auto& node : data.nodes) {
			node->setLayer(new_layer);
			// desired_layer_ is only ever -1 or exactly equal to the node's current layer, 
			// so if it's active it must currently equal the layer being relabelled.
			if (node->getDesiredLayer() != -1) {
				node->setDesiredLayer(new_layer);
			}
		}
		for (const auto& edge : data.outgoing_edges) {
			edge->setLayer(new_layer);
		}
	}

	void Hypergraph::renumberLayersFrom(int from_layer) {
		if (layers_.empty()) return;

		std::map<int, LayerData> shifted;
		for (auto& [old_layer, data] : layers_) {
			int new_layer = (old_layer >= from_layer) ? old_layer + 1 : old_layer;
			if (new_layer != old_layer) relabelLayerContents(data, new_layer);
			shifted[new_layer] = std::move(data);
		}

		layers_ = std::move(shifted);
	}

	void Hypergraph::relocateNodeToLayer(const NodePtr& node, int desired_layer, std::set<int>* out_altered_layers) {
		if (!node || layers_.empty()) return;

		int last_layer = prev(layers_.end())->first;

		if (desired_layer == -1) {
			// Special case, this means we want to create another layer (shallower than 0)
			// and place the node in it. All layer numbers need to be incremented by 1.

			if (!node->getParents().empty()) {
				throw std::logic_error("The desired layer breaks the layering invariant.");
			}

			if (node->getLayer() == 0 && layers_.at(0).nodes.size() == 1) {
				// Moving the node could potentially cause a gap, so we do not allow it.
				throw std::logic_error("Node is already placed at the shallowest layer.");
			}

			// Shift all layer numbers by 1.
			renumberLayersFrom(0);

			// Make sure the previous root nodes at layer 0 (now at layer 1) stay at layer 1.
			for (const auto& root : layers_.at(1).nodes) {
				root->setDesiredLayer(1);
			}
			node->setDesiredLayer(-1);

			if (relocateNodes({ node }, nullptr, out_altered_layers)) { // This will for sure be true.
				minimizeCrossings(3, 1);
			}
		}
		else if (desired_layer > last_layer) {
			// Another special case, we want to create another layer (deeper than the last)
			// and place the node in it.

			if (node->getParents().empty() &&
				node->getLayer() == last_layer &&
				layers_.at(last_layer).nodes.size() == 1) {
				// Moving the node could potentially originate a gap, so we do not allow it.
				throw std::logic_error("Node is already placed at the deepest layer.");
			}

			node->setDesiredLayer(last_layer + 1);
			int min_start_layer = INT_MAX;
			relocateNodes({ node }, &min_start_layer, out_altered_layers);
			if (!node->getParents().empty()) {
				minimizeCrossingsAfterRelocation(node->getParents(), min_start_layer);
			}
		}
		else {
			auto parents = node->getParents();
			int depth_rule_layer = parents.empty() ? 0
				: (*std::max_element(parents.begin(), parents.end(),
					[](const NodePtr& a, const NodePtr& b) { return a->getLayer() < b->getLayer(); }
				))->getLayer() + 1;

			if (desired_layer < depth_rule_layer) {
				throw std::logic_error("The desired layer breaks the layering invariant.");
			}

			node->setDesiredLayer(desired_layer == depth_rule_layer ? -1 : desired_layer);

			if (desired_layer == node->getLayer()) {
				throw std::invalid_argument("Node is already placed in that layer.");
			}

			int min_start_layer = std::min(node->getLayer(), desired_layer);
			applyRelocationAndPropagate({ {node, desired_layer} }, &min_start_layer, out_altered_layers);
			minimizeCrossingsAfterRelocation(parents, min_start_layer);
		}
	}

	// ============================================================================
	// Helper methods for connection management
	// ============================================================================
	 
	void Hypergraph::resyncSegmentEndpoints(const HyperedgePtr& segment, const std::vector<NodePtr>& new_sources, const std::vector<NodePtr>& new_targets, std::set<int>* out_altered_layers) {
		auto old_sources = segment->getSources();
		auto old_targets = segment->getTargets();

		std::unordered_set<Node*> old_src_set, new_src_set, old_tgt_set, new_tgt_set;
		for (const auto& s : old_sources) old_src_set.insert(s.get());
		for (const auto& s : new_sources) new_src_set.insert(s.get());
		for (const auto& t : old_targets) old_tgt_set.insert(t.get());
		for (const auto& t : new_targets) new_tgt_set.insert(t.get());

		std::vector<NodePtr> removed_sources, added_sources, kept_sources;
		for (const auto& s : old_sources)
			(new_src_set.count(s.get()) ? kept_sources : removed_sources).push_back(s);
		for (const auto& s : new_sources)
			if (!old_src_set.count(s.get())) added_sources.push_back(s);

		std::vector<NodePtr> removed_targets, added_targets, kept_targets;
		for (const auto& t : old_targets)
			(new_tgt_set.count(t.get()) ? kept_targets : removed_targets).push_back(t);
		for (const auto& t : new_targets)
			if (!old_tgt_set.count(t.get())) added_targets.push_back(t);

		if (removed_sources.empty() && added_sources.empty() &&
			removed_targets.empty() && added_targets.empty())
			return; // Nothing changed for this segment; leave it (and its links) untouched.

		auto link = [](const NodePtr& s, const NodePtr& t) {
			bool sd = s->isDummy(), td = t->isDummy();
			if (sd && td) { s->addChild(t); t->addParent(s); }
			else if (sd) { s->addChild(t); }
			else if (td) { t->addParent(s); }
			};
		auto unlink = [](const NodePtr& s, const NodePtr& t) {
			bool sd = s->isDummy(), td = t->isDummy();
			if (sd && td) { s->removeChild(t); t->removeParent(s); }
			else if (sd) { s->removeChild(t); }
			else if (td) { t->removeParent(s); }
			};

		// Break links involving anything that is leaving the segment.
		for (const auto& s : removed_sources)
			for (const auto& t : old_targets)
				unlink(s, t);
		for (const auto& t : removed_targets)
			for (const auto& s : kept_sources)
				unlink(s, t);

		// Apply the structural change to the segment itself.
		for (const auto& s : removed_sources) segment->removeSource(s);
		for (const auto& t : removed_targets) segment->removeTarget(t);
		for (const auto& s : added_sources) segment->addSource(s);
		for (const auto& t : added_targets) segment->addTarget(t);

		// Establish links involving anything that is newly joining the segment.
		for (const auto& s : added_sources)
			for (const auto& t : new_targets)
				link(s, t);
		for (const auto& t : added_targets)
			for (const auto& s : kept_sources)
				link(s, t);

		// Reaching this point already guarantees at least one of removed/added sources/targets
		// is non-empty which changes edge's span and therefore affects this layer's MIP.
		if (out_altered_layers) {
			out_altered_layers->insert(segment->getLayer());
		}
	}

	void Hypergraph::splitLongEdge(const HyperedgePtr& long_edge, int* out_min_new_layer, std::set<int>* out_altered_layers) {
		if (long_edge->isSegment()) return;

		std::unordered_map<int, HyperedgePtr> old_segments_by_layer;
		std::unordered_map<int, NodePtr> old_dummies_by_layer; // keyed by the layer the dummy sits at (L+1)
		for (const auto& seg : all_hyperedges_[long_edge]) {
			old_segments_by_layer[seg->getLayer()] = seg;
			for (const auto& t : seg->getTargets())
				if (t->isDummy()) old_dummies_by_layer[t->getLayer()] = t;
		}

		// ----------------------------------------------------------------
		// Group sources and targets by layer
		// ----------------------------------------------------------------
		std::map<int, std::vector<NodePtr>> sources_by_layer;
		for (const auto& source : long_edge->getSources())
			sources_by_layer[source->getLayer()].push_back(source);

		std::map<int, std::vector<NodePtr>> targets_by_layer;
		for (const auto& target : long_edge->getTargets())
			targets_by_layer[target->getLayer()].push_back(target);

		int min_src_layer = sources_by_layer.begin()->first;
		int min_tgt_layer = targets_by_layer.begin()->first;
		int max_src_layer = sources_by_layer.rbegin()->first;
		int max_tgt_layer = targets_by_layer.rbegin()->first;

		if (min_src_layer >= max_tgt_layer - 1) {
			// No splitting needed any more: dissolve whatever split existed before.
			if (!old_segments_by_layer.empty()) dissolveSegments({ long_edge.get() });
			return;
		}

		// If it needs to be split, the edge should me removed from its current layer (if any).
		if (long_edge->getLayer() >= 0) {
			removeHyperedgeFromLayer(long_edge->getLayer(), long_edge);
		}

		std::vector<HyperedgePtr> new_segments;
		std::unordered_set<Hyperedge*> reused_segments;
		std::unordered_set<Node*> reused_dummies;

		// carry_dummy: produced by the previous segment, feeds into the next.
		NodePtr carry_dummy = nullptr;

		for (int L = min_src_layer; L < max_tgt_layer; ++L) {

			// ------------------------------------------------------------
			// Segment sources: native real sources on L + carry dummy.
			// ------------------------------------------------------------
			std::vector<NodePtr> seg_sources;

			if (L <= max_src_layer && sources_by_layer.count(L))
				for (const auto& s : sources_by_layer[L])
					seg_sources.push_back(s);

			if (carry_dummy)
				seg_sources.push_back(carry_dummy);

			if (seg_sources.empty()) continue;

			// ------------------------------------------------------------
			// Segment targets: real targets on L+1 + new carry dummy if
			// there is still anything remaining beyond L+1.
			// ------------------------------------------------------------
			std::vector<NodePtr> seg_targets;

			if (L + 1 >= min_tgt_layer && targets_by_layer.count(L + 1))
				for (const auto& t : targets_by_layer[L + 1])
					seg_targets.push_back(t);

			if (L + 1 < max_tgt_layer) {
				// Reuse the previous split's dummy at this layer if there was one:
				// it keeps its identity  instead of being destroyed and replaced.
				auto dummy_it = old_dummies_by_layer.find(L + 1);
				if (dummy_it != old_dummies_by_layer.end()) {
					carry_dummy = dummy_it->second;
					reused_dummies.insert(carry_dummy.get());
				}
				else {
					carry_dummy = std::make_shared<Node>();
					all_nodes_.push_back(carry_dummy);
					addNodeToLayer(L + 1, -1, carry_dummy, out_min_new_layer);
				}
				seg_targets.push_back(carry_dummy);
			}
			else {
				carry_dummy = nullptr;
			}

			// ------------------------------------------------------------
			// Reuse the previous split's segment at this layer if there was
			// one, re-pointing its endpoints in place; otherwise create a
			// fresh segment hyperedge for this L -> L+1 transition. Either
			// way the parent/child wiring ends up identical.
			// ------------------------------------------------------------
			auto seg_it = old_segments_by_layer.find(L);
			HyperedgePtr seg;
			if (seg_it != old_segments_by_layer.end()) {
				seg = seg_it->second;
				reused_segments.insert(seg.get());
				resyncSegmentEndpoints(seg, seg_sources, seg_targets, out_altered_layers);
			}
			else {
				seg = createHyperedge(long_edge, seg_sources, seg_targets, L, out_altered_layers);
			}

			new_segments.push_back(seg);
		}

		// ----------------------------------------------------------------
		// Anything left over from the previous split that wasn't reused no
		// longer belongs to this edge's chain (the split shrank or shifted)
		// and must be torn down: its segment removed from its layer, and any
		// dummy that isn't feeding into the new split erased entirely.
		// ----------------------------------------------------------------
		std::map<int, std::unordered_set<Hyperedge*>> stale_segments_by_layer;
		for (const auto& [layer, seg] : old_segments_by_layer)
			if (!reused_segments.count(seg.get())) stale_segments_by_layer[layer].insert(seg.get());

		std::map<int, std::unordered_set<Node*>> stale_dummies_by_layer;
		std::unordered_set<Node*> stale_dummy_set;
		for (const auto& [layer, dummy] : old_dummies_by_layer) {
			if (reused_dummies.count(dummy.get())) continue;
			stale_dummies_by_layer[layer].insert(dummy.get());
			stale_dummy_set.insert(dummy.get());
		}

		for (const auto& [layer, edges] : stale_segments_by_layer)
			removeHyperedgeFromLayer(layer, edges);
		for (const auto& [layer, nodes] : stale_dummies_by_layer)
			removeNodeFromLayer(layer, nodes);

		if (!stale_dummy_set.empty()) {
			all_nodes_.erase(
				std::remove_if(all_nodes_.begin(), all_nodes_.end(),
					[&](const NodePtr& n) { return stale_dummy_set.count(n.get()) > 0; }),
				all_nodes_.end());
		}

		all_hyperedges_[long_edge] = new_segments;
	}

	void Hypergraph::dissolveSegments(const std::unordered_set<Hyperedge*>& long_edges) {
		if (long_edges.empty()) return;

		std::map<int, std::unordered_set<Node*>> dummy_removes; // for quick lookup when removing from LayerData
		std::map<int, std::unordered_set<Hyperedge*>> segment_removes; // for quick lookup when removing from LayerData
		std::unordered_set<Node*> dummy_set; // for quick lookup when removing from all_nodes_

		// Collect segments and dummy nodes to be removed.
		for (const auto& edge : long_edges) {
			if (!edge) continue;
			auto& segments = all_hyperedges_[edge->shared_from_this()];
			for (const auto& seg : segments) {
				for (const auto& s : seg->getSources()) {
					if (s->isDummy()) {
						dummy_removes[s->getLayer()].insert(s.get());
						dummy_set.insert(s.get());
					}
				}
				for (const auto& t : seg->getTargets()) {
					if (t->isDummy()) {
						dummy_removes[t->getLayer()].insert(t.get());
						dummy_set.insert(t.get());
					}
				}
				segment_removes[seg->getLayer()].insert(seg.get());
			}
			segments.clear(); // Remove segments from all_hyperedges_
		}

		if (segment_removes.empty()) return;

		// Remove from all_nodes and from LayerData.
		all_nodes_.erase(
			std::remove_if(all_nodes_.begin(), all_nodes_.end(),
				[&](const NodePtr& n) {
					return dummy_set.count(n.get()) > 0;
				}),
			all_nodes_.end());

		for (const auto& [layer, edges] : segment_removes) {
			removeHyperedgeFromLayer(layer, edges);
		}
		for (const auto& [layer, nodes] : dummy_removes) {
			removeNodeFromLayer(layer, nodes);
		}
	}

	void Hypergraph::removeTransitiveConnections(
		const std::vector<NodePtr>& parents,
		const std::vector<NodePtr>& children,
		const HyperedgePtr& edge_to_skip,
		int* out_min_new_layer,
		std::set<int>* out_altered_layers)
	{
		if (children.empty()) return;

		std::unordered_set<Node*> parents_and_ancestors = getAllAncestors(parents);
		for (const auto& p : parents) parents_and_ancestors.insert(p.get());
		std::unordered_set<Node*> children_and_descendants = getAllDescendants(children);
		for (const auto& c : children) children_and_descendants.insert(c.get());

		std::unordered_map<HyperedgePtr, std::vector<HyperedgePtr>, HyperedgePtrHash> snapshot = all_hyperedges_;
		for (const auto& [edge, _] : snapshot) {
			if (edge_to_skip && edge == edge_to_skip) continue; // Don't destroy the connection we're currently adding.

			std::unordered_set<Node*> ancestor_sources;
			for (const auto& s : edge->getSources())
				if (parents_and_ancestors.count(s.get()))
					ancestor_sources.insert(s.get());

			if (ancestor_sources.empty()) continue;

			std::vector<NodePtr> surviving_targets;
			bool any_redundant = false;
			for (const auto& t : edge->getTargets()) {
				if (children_and_descendants.count(t.get())) {
					any_redundant = true;
				}
				else {
					surviving_targets.push_back(t);
				}
			}

			if (!any_redundant) continue;

			removeSourcesFromHyperedge(edge, ancestor_sources, false, out_altered_layers);

			if (!surviving_targets.empty()) {
				std::vector<NodePtr> ancestor_sources_vec;
				ancestor_sources_vec.reserve(ancestor_sources.size());
				for (Node* n : ancestor_sources) {
					ancestor_sources_vec.push_back(n->shared_from_this());
				}

				const auto& new_edge = createHyperedge(ancestor_sources_vec, surviving_targets, -1);
				settleEdgePlacement(new_edge, out_min_new_layer, out_altered_layers);
			}
		}
	}

	HyperedgePtr Hypergraph::resolveOwnRedundantTargets(const HyperedgePtr& edge, const NodePtr& target, int* out_min_new_layer, std::set<int>* out_altered_layers) {
		std::unordered_set<Node*> target_and_descendants = getAllDescendants({ target });
		target_and_descendants.insert(target.get());

		std::unordered_set<Node*> own_redundant_targets;
		for (const auto& t : edge->getTargets()) {
			if (target_and_descendants.count(t.get())) {
				own_redundant_targets.insert(t.get());
			}
		}

		if (own_redundant_targets.empty()) return nullptr;

		bool edge_was_dissolved = (own_redundant_targets.size() == edge->getTargets().size());
		removeTargetsFromHyperedge(edge, own_redundant_targets, false, out_altered_layers);

		if (!edge_was_dissolved) return nullptr;

		// edge had nothing left to keep it alive and has just been dissolved by the call above;
		// build a fresh replacement to carry the pending connection to target. Sources are still
		// valid here because removeTargetsFromHyperedge never touches an edge's source list.
		const auto& sources = edge->getSources();
		const auto& new_edge = createHyperedge(sources, { target }, -1);
		settleEdgePlacement(new_edge, out_min_new_layer, out_altered_layers);
		return new_edge;
	}

	int Hypergraph::resolveTargetLayer(const NodePtr& node) {
		auto parents = node->getParents();
		int depth_rule_layer = parents.empty() ? 0
			: (*std::max_element(parents.begin(), parents.end(),
				[](const NodePtr& a, const NodePtr& b) { return a->getLayer() < b->getLayer(); }
			))->getLayer() + 1;

		int desired = node->getDesiredLayer();
		if (desired == -1) return depth_rule_layer;

		if (desired <= depth_rule_layer) {
			node->setDesiredLayer(-1);
			return depth_rule_layer;
		}
		return desired;                  
	}

	bool Hypergraph::relocateNodes(const std::vector<NodePtr>& nodes, int* out_min_new_layer, std::set<int>* out_altered_layers) {
		std::vector<std::pair<NodePtr, int>> relocations;
		for (const auto& node : nodes) {
			int target_layer = resolveTargetLayer(node);
			if (target_layer != node->getLayer())
				relocations.push_back({ node, target_layer });
		}
		if (!relocations.empty()) {
			applyRelocationAndPropagate(relocations, out_min_new_layer, out_altered_layers);
			return true;
		}
		return false;
	}

	void Hypergraph::applyRelocationAndPropagate(const std::vector<std::pair<NodePtr, int>>& relocations, int* out_min_new_layer, std::set<int>* out_altered_layers) {
		if (relocations.empty()) return;

		// ====================================================================
		// Phase 1: Move all nodes in LayerData and update incoming edges.
		// ====================================================================
		std::unordered_set<Hyperedge*> incoming_set;
		std::unordered_set<Node*> relocated_nodes; // Improve efficiency when checking for incoming edges.
		std::vector<NodePtr> vec_relocated_nodes; // For getAllDescendants input.

		for (const auto& [node, new_layer] : relocations) {
			removeNodeFromLayer(node->getLayer(), node);
			addNodeToLayer(new_layer, choosePositionForRelocatedNode(new_layer, node), node, out_min_new_layer);
			relocated_nodes.insert(node.get());
			vec_relocated_nodes.push_back(node);
		}

		for (const auto& [edge, _] : all_hyperedges_) {
			for (const auto& t : edge->getTargets()) {
				if (relocated_nodes.count(t.get()) > 0) {
					incoming_set.insert(edge.get());
					break;
				}
			}
		}

		// Every original edge with a relocated node as target may now be short or long differently
		// than before, so we re-settle each one (resettleEdge dissolves stale segments as needed).
		for (Hyperedge* edge : incoming_set) {
			resettleEdge(edge->shared_from_this(), out_min_new_layer, out_altered_layers);
		}

		// ====================================================================
		// Phase 2: Collect all descendants of every relocated node and
		//          recalculate their layers bottom-up to ensure that 
		//			when the depth of a node is recalculated, all its parents
		//			have previously updated their layers.
		// ====================================================================
		std::unordered_set<Node*> affected_nodes = getAllDescendants(vec_relocated_nodes);

		// Remove relocated nodes themselves from affected_nodes: they are
		// already in their correct layer and must not be moved again.
		for (const auto& node : relocated_nodes)
			affected_nodes.erase(node);

		if (affected_nodes.empty()) {
			cleanUp();
			return;
		}

		std::map<int, std::unordered_set<Node*>> affected_by_layer;
		for (Node* n : affected_nodes)
			affected_by_layer[n->getLayer()].insert(n);

		for (auto& [layer, nodes] : affected_by_layer) {
			for (auto it = nodes.begin(); it != nodes.end();) {
				NodePtr node_ptr = (*it)->shared_from_this();
				int new_depth = resolveTargetLayer(node_ptr);

				if (new_depth == layer) {
					it = nodes.erase(it);
				}
				else {
					addNodeToLayer(new_depth, choosePositionForRelocatedNode(new_depth, node_ptr), node_ptr, out_min_new_layer);
					++it;
				}
			}
			removeNodeFromLayer(layer, nodes);
		}

		// ====================================================================
		// Phase 3: Rebuild edges for all nodes whose layer actually changed.
		// ====================================================================
		std::unordered_set<Hyperedge*> affected_edges;
		for (const auto& [edge, _] : all_hyperedges_) {
			bool touches = false;
			for (const auto& s : edge->getSources())
				if (affected_nodes.count(s.get())) { touches = true; break; }
			if (!touches)
				for (const auto& t : edge->getTargets())
					if (affected_nodes.count(t.get())) { touches = true; break; }
			if (touches) affected_edges.insert(edge.get());
		}

		for (Hyperedge* edge : affected_edges) {
			resettleEdge(edge->shared_from_this(), out_min_new_layer, out_altered_layers);
		}

		cleanUp();
	}

	void Hypergraph::compactLayerNumbers() {
		if (layers_.empty()) return;

		// Cheap check first: if the keys are already 0, 1, 2, ... with no gaps, there's
		// nothing to do, and this is the common case after every operation that didn't
		// touch a layer boundary.
		int expected = 0;
		bool dense = true;
		for (const auto& [layer, data] : layers_) {
			if (layer != expected) { dense = false; break; }
			++expected;
		}
		if (dense) return;

		std::map<int, LayerData> compacted;
		int new_layer = 0;
		for (auto& [old_layer, data] : layers_) {
			if (new_layer != old_layer) relabelLayerContents(data, new_layer);
			compacted[new_layer] = std::move(data);
			++new_layer;
		}

		layers_ = std::move(compacted);
	}

	void Hypergraph::cleanUp() {
		std::vector<int> empty;
		for (const auto& [l, data] : layers_)
			if (data.nodes.empty() && data.outgoing_edges.empty())
				empty.push_back(l);
		for (int l : empty)
			layers_.erase(l);

		compactLayerNumbers();
	}

	static bool isNodeInNeighboursHelper(const NodePtr& node, const NodePtr& target, int target_layer, bool search_up, std::unordered_set<Node*>& visited) {
		const auto neighbours = search_up ? node->getParents() : node->getChildren();
		for (const auto& neighbour : neighbours) {
			if (visited.count(neighbour.get()) > 0) continue;

			int neighbour_layer = neighbour->getLayer();

			// Pruning: when searching up, skip branches shallower than target.
			//          when searching down, skip branches deeper than target.
			if (search_up && neighbour_layer < target_layer) continue;
			if (!search_up && neighbour_layer > target_layer) continue;

			if (neighbour_layer == target_layer) {
				if (neighbour == target) return true;
				continue;
			}

			visited.insert(neighbour.get());
			if (isNodeInNeighboursHelper(neighbour, target, target_layer, search_up, visited))
				return true;
		}
		return false;
	}

	bool Hypergraph::parentIsInAncestors(const std::vector<NodePtr>& children, const NodePtr& parent) const {
		if (!parent || children.empty()) return false;
		int target_layer = parent->getLayer();
		std::unordered_set<Node*> visited;
		for (const auto& child : children) {
			if (!child || child->getLayer() <= target_layer) continue;
			if (isNodeInNeighboursHelper(child, parent, target_layer, true, visited))
				return true;
		}
		return false;
	}

	bool Hypergraph::childIsInDescendants(const std::vector<NodePtr>& parents, const NodePtr& child) const {
		if (!child || parents.empty()) return false;
		int target_layer = child->getLayer();
		std::unordered_set<Node*> visited;
		for (const auto& parent : parents) {
			if (!parent || parent->getLayer() >= target_layer) continue;
			if (isNodeInNeighboursHelper(parent, child, target_layer, false, visited))
				return true;
		}
		return false;
	}

	static void getAllAncestorsHelper(const NodePtr& node, std::unordered_set<Node*>& ancestors) {
		for (const auto& parent : node->getParents()) {
			if (!parent) continue;
			if (!ancestors.insert(parent.get()).second) continue;
			getAllAncestorsHelper(parent, ancestors);
		}

	}

	std::unordered_set<Node*> Hypergraph::getAllAncestors(const std::vector<NodePtr>& nodes) {
		if (nodes.empty()) return {};
		std::unordered_set<Node*> ancestors = {};
		for (const auto& node : nodes) {
			if (!node || ancestors.count(node.get()) > 0) continue;
			getAllAncestorsHelper(node, ancestors);
		}
		for (const auto& node : nodes)
			ancestors.erase(node.get());
		return ancestors;
	}

	static void getAllDescendantsHelper(const NodePtr& node, std::unordered_set<Node*>& descendants) {
		for (const auto& child : node->getChildren()) {
			if (!child) continue;
			if (!descendants.insert(child.get()).second) continue;
			getAllDescendantsHelper(child, descendants);
		}
	}

	std::unordered_set<Node*> Hypergraph::getAllDescendants(const std::vector<NodePtr>& nodes) {
		if (nodes.empty()) return {};
		std::unordered_set<Node*> descendants = {};
		for (const auto& node : nodes) {
			if (!node || descendants.count(node.get()) > 0) continue;
			getAllDescendantsHelper(node, descendants);
		}
		for (const auto& node : nodes)
			descendants.erase(node.get());
		return descendants;
	}

	static bool checkCyclesUtil(Node* node, std::unordered_set<Node*>& visited, std::unordered_set<Node*>& path) {
		// The implemented algorithm for cycle detection uses DFS and keeps track of visited nodes and the current path.
		// For a more detailed explanation: https://takeuforward.org/data-structure/detect-cycle-in-a-directed-graph-using-dfs-g-19
		// I use unordered_set for path to allow O(1) lookup when checking for back edges, which is more efficient than using a vector.

		visited.insert(node);
		path.insert(node);

		const auto& adjacentNodes = node->getChildren();

		for (const auto& adjNodePtr : adjacentNodes) {
			if (!adjNodePtr) continue;
			Node* adjNode = adjNodePtr.get();

			if (visited.count(adjNode) == 0) { // the adjacent node has not been visited yet
				if (checkCyclesUtil(adjNode, visited, path)) {
					return true;
				}
			}
			else if (path.count(adjNode) > 0) {
				// Cycle detected, extract the cycle path
				return true;
			}
		}

		path.erase(node);
		return false;
	}

	bool Hypergraph::checkCycles(const NodePtr& node) {
		// Wrapper function for cycle detection
		std::unordered_set<Node*> visited;
		std::unordered_set<Node*> path;

		return checkCyclesUtil(node.get(), visited, path);
	}
} // namespace hypergraph_logic