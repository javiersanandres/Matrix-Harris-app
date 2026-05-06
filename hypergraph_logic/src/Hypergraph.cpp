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
	void Hypergraph::addNodeToLayer(int layer, int position, const NodePtr& node) {
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
	HyperedgePtr Hypergraph::createHyperedge(const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets, int layer) {
		auto edge = std::make_shared<Hyperedge>(sources, targets);
		all_hyperedges_[edge] = {};

		if (layer >= 0) {
			edge->setLayer(layer);
			layers_[layer].outgoing_edges.push_back(edge);
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

	HyperedgePtr Hypergraph::createHyperedge(const WeakHyperedgePtr& origin, const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets, int layer) {
		auto edge = std::make_shared<Hyperedge>(origin, sources, targets);
		if (auto orig = origin.lock()) {
			all_hyperedges_[orig].push_back(edge);
		}

		if (layer >= 0) {
			edge->setLayer(layer);
			layers_[layer].outgoing_edges.push_back(edge);
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

	void Hypergraph::addHyperedgeToLayer(int layer, const HyperedgePtr& edge) {
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
	NodePtr Hypergraph::createNode(const std::string& label, int layer_position, const NodePtr& parent) {
		NodePtr node = std::make_shared<Node>(label);
		all_nodes_.push_back(node);

		if (!parent) {
			addNodeToLayer(0, layer_position, node);
		}
		else {
			createHyperedge({ parent }, { node }, parent->getLayer());
			addNodeToLayer(parent->getLayer() + 1, layer_position, node);
		}

		if (layer_position == -1) {
			// No specific position requested, so we will insert it at the rightmost and less
			// disruptive position in the layer.
			minimizeCrossingsForNodes({ node.get() }, node->getLayer(), node->getLayer());
		}

		return node;
	}

	NodePtr Hypergraph::createNode(const std::string& label, const HyperedgePtr& edge) {
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
		removeSourcesFromHyperedge(edge, sources_set, false);
		addNodeToLayer(node_layer, -1, node);

		if (edge_layer >= 0) {
			// If it was short, the two new edges will also be short
			createHyperedge(sources, { node }, edge_layer);
			const auto& new_edge = createHyperedge({ node }, targets, edge_layer + 1);
			relocateNodes(new_edge->getTargets()); // Relocate the targets to one layer down
		}
		else {
			const auto& new_edge_1 = createHyperedge(sources, { node }, -1);
			int k = edgeIsShort(new_edge_1);
			if (k >= 0) {
				addHyperedgeToLayer(k, new_edge_1);
			}
			else {
				splitLongEdge(new_edge_1);
			}

			const auto& new_edge_2 = createHyperedge({ node }, targets, -1);
			if (!relocateNodes(new_edge_2->getTargets())) {
				k = edgeIsShort(new_edge_2);
				if (k >= 0) {
					addHyperedgeToLayer(k, new_edge_2);
				}
				else {
					splitLongEdge(new_edge_2);
				}
			}
		}

		// Apply crossing minimization to the new node and all possible new dummy nodes created by splitting the edge
		// or as a consequence of relocating the targets. We will decrease the number of sifting rounds by the purpose
		// of preserving the mental map as much as possible, while obviously minimizing crossings as well.
		minimizeCrossings(3, min_layer);
		return node;
	}

	NodePtr Hypergraph::createSource(const std::string& label, int layer_position, const HyperedgePtr& edge) {
		if (!edge) return nullptr;
		NodePtr node = std::make_shared<Node>(label);
		all_nodes_.push_back(node);
		addNodeToLayer(0, layer_position, node);

		edge->addSource(node);
		for (const auto& t : edge->getTargets()) {
			t->addParent(node);
			node->addChild(t);
		}

		if (edgeIsShort(edge) < 0) { // The edge needs to be split or resplitted.
			if (edge->getLayer() >= 0) { // It wasn't long before, so we need to remove it from the layer data.
				removeHyperedgeFromLayer(edge->getLayer(), edge);
			}
			splitLongEdge(edge);

			// Now, we should find the best position for the new source in layer 0, as well as for any new
			// dummy nodes created by the splitting.
			std::vector<Node*> nodes_to_minimize = { node.get() };
			int max_layer = 0;
			for (const auto& segment : all_hyperedges_[edge]) {
				for (const auto& s : segment->getSources()) {
					if (s->isDummy()) {
						nodes_to_minimize.push_back(s.get());
					}
					if (s->getLayer() > max_layer) {
						max_layer = s->getLayer();
					}
				}
			}
			minimizeCrossingsForNodes(nodes_to_minimize, 0, max_layer);
		}
		else { 
			// The edge was short before, and it keeps on being short now, but we have to 
			// find a good place for the new source in layer 0.
			minimizeCrossingsForNodes({ node.get() }, 0, 0);
		}
		// No relocation is needed since the new source is in the first layer and no cycles
		// can be created by adding a new source to an existing edge.
		return node;
	}

	NodePtr Hypergraph::createTarget(const std::string& label, int layer_position, const HyperedgePtr& edge) {
		if (!edge) return nullptr;
		NodePtr node = std::make_shared<Node>(label);
		all_nodes_.push_back(node);

		edge->addTarget(node);
		int node_layer = 0;
		for (const auto& s : edge->getSources()) {
			if (s->getLayer() + 1 > node_layer) {
				node_layer = s->getLayer() + 1;
			}
			s->addChild(node);
			node->addParent(s);
		}

		addNodeToLayer(node_layer, layer_position, node);
		if (edgeIsShort(edge) < 0) { // The edge needs to be resplitted (it was long before).
			splitLongEdge(edge);

			// Now, we should find the best position for the new target in the layer, as well as for any new
			// dummy nodes created by the splitting.
			std::vector<Node*> nodes_to_minimize = { node.get() };
			int max_layer = node_layer;
			int min_layer = node_layer;
			for (const auto& segment : all_hyperedges_[edge]) {
				for (const auto& s : segment->getSources()) {
					if (s->isDummy()) {
						nodes_to_minimize.push_back(s.get());
					}
					if (s->getLayer() > max_layer) {
						max_layer = s->getLayer();
					}
					if (s->getLayer() < min_layer) {
						min_layer = s->getLayer();
					}
				}
			}
			minimizeCrossingsForNodes(nodes_to_minimize, min_layer, max_layer);
		}
		else {
			// Just find the best position for the new target in its layer.
			minimizeCrossingsForNodes({ node.get() }, node_layer, node_layer);
		}
		// Again it is fairly simple to see that no relocation is needed and 
		// also no cycles can be created.
		return node;
	}

	HyperedgePtr Hypergraph::addConnection(const NodePtr& parent, const NodePtr& child) {
		if (!child || !parent) return nullptr;
		if (child == parent) {
			throw std::invalid_argument("A node cannot be connected to itself.");
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
		int start_layer = removeTransitiveConnections({ parent }, { child });

		int parent_layer = parent->getLayer();
		int child_layer = child->getLayer();
		HyperedgePtr edge;
		if (parent_layer == child_layer - 1) {
			// Easiest case: just add the new hyperedge and update the layer data
			edge = createHyperedge({ parent }, { child }, parent_layer);
		}
		else if (parent_layer < child_layer) {
			// The parent is in a layer above the child, so the child's layer does not need to be updated, but we need 
			// to add the new hyperedge, split it and add the necessary dummy nodes in the intermediate layers.
			edge = createHyperedge({ parent }, { child }, -1);

			// Rewire the connection from parent to child with the new dummy nodes and split the hyperedge.
			splitLongEdge(edge);

			if (start_layer < INT_MAX) {
				// We needed to remove some redundant connections, which lead to the creation of new dummy nodes
				// in between the layers. Therefore, at this point, we run global sifting from the start_layer.
				// This case is less disruptive than the worst case, since fewer nodes are affected by the change,
				// so we will allow fewer rounds of sifting.
				minimizeCrossings(3, start_layer);
			}
			else {
				// If no new dummy nodes were created when removing redudant connections, then we only need to minimize crossings for the
				// new dummy nodes created by splitting the edge, which are a subset of the sources of the edge segments.
				std::vector<Node*> nodes_to_minimize;
				for (const auto& segment : all_hyperedges_[edge]) {
					for (const auto& s : segment->getSources()) {
						if (s->isDummy()) {
							nodes_to_minimize.push_back(s.get());
						}
					}
				}
				minimizeCrossingsForNodes(nodes_to_minimize, parent_layer + 1, child_layer - 1);
			}
		}
		else {
			// Worst case: the child layer needs to be updated. This automatically implies that the new layer
			// number is the parent_layer + 1 and this should propagate down to all the descendants of the child.
			edge = createHyperedge({ parent }, { child }, parent_layer);

			applyRelocationAndPropagate({ {child, parent_layer + 1} });

			// We run global sifting from the min_layer of all parents to the affected child. Since this operation is
			// quite disruptive, we will allow more rounds of sifting to try to minimize crossings as much as possible.
			for (const auto& child_parent : child->getParents()) {
				if (child_parent->getLayer() + 1 < start_layer) {
					start_layer = child_parent->getLayer() + 1;
				}
			}
			minimizeCrossings(10, start_layer);
		}
		return edge;
	}

	void Hypergraph::addSourceToEdge(const HyperedgePtr& edge, const NodePtr& source) {
		if (!edge || edge->isSegment() || !source) return;

		const auto targets = edge->getTargets();
		for (const auto& t : targets) {
			if (t == source) {
				throw std::logic_error("A node cannot be connected to itself.");
			}
		}

		// Even though some connections might be redundant, they can also encode an intention of grouping the sources and targets
		// in a different manner. The policy applied to allow these regroupings is the following:
		// If a pre-existing connection from the new source to (at least) one of the targets of the edge already exists,
		// then allow the regrouping if and only if the sources in that connection are a subset of the edge sources. But, since
		// the targets are already in the edge, it is not possible to have another connecting edge to that target with any of the
		// edge sources. Therefore, it suffices to check that the new source is the only parent of that connection.
		std::unordered_set<Node*> affected_targets;
		std::vector<HyperedgePtr> affected_edges;
		for (const auto& [hyperedge, _] : all_hyperedges_) {
			if (!hyperedge->containsSource(source)) continue;
			bool update_grouping = false;
			for (const auto& t : targets) {
				if (hyperedge->containsTarget(t)) {
					affected_targets.insert(t.get());
					affected_edges.push_back(hyperedge);
					update_grouping = true;
				}
			}
			if (update_grouping && hyperedge->getSources().size() != 1) {
				throw std::logic_error("This connection already exists in the diagram.");
			}
		}

		if (affected_targets.empty() && parentIsInAncestors(targets, source)) {
			throw std::logic_error("This connection already exists in the diagram.");
		}

		// Temporarily add the source and check for cycles
		for (const auto& t : targets) {
			if (affected_targets.count(t.get()) == 0) {
				source->addChild(t);
				t->addParent(source);
			}
		}

		if (checkCycles(source)) {
			// Rollback
			for (const auto& t : targets) {
				if (affected_targets.count(t.get()) == 0) {
					source->removeChild(t);
					t->removeParent(source);
				}
			}
			throw std::logic_error("Adding this connection would create a cycle in the diagram.");
		}

		// Now we know that no cycles are added, we can safely add the connection.
		if (!affected_targets.empty()) {
			// Remove the targets from the affected hyperedges in which they participate as so.
			for (const auto& hyperedge : affected_edges) {
				removeTargetsFromHyperedge(hyperedge, affected_targets, false); // We will relocate later, so we avoid it here.
			}

			// Now, reinstate the connections from the source to the affected targets since they were removed in the previous step.
			for (const auto& t : affected_targets) {
				source->addChild(t->shared_from_this());
				t->addParent(source);
			}
		}

		// Remove any pre-existing connections which are now redundant.
		int start_layer = removeTransitiveConnections({ source }, targets);

		// Special care, the previous call could have removed the edge from the hypergraph
		// if all sources where ancestors of source. So we may have to readd it.
		all_hyperedges_[edge]; // This does nothing or reinstates the edge in the hypergraph.

		edge->addSource(source);
		int parent_layer = source->getLayer();
		std::vector<std::pair<NodePtr, int>> relocations;
		for (const auto& t : targets) {
			if (parent_layer + 1 > t->getLayer()) {
				// The child layer needs to be updated. This automatically implies that the new layer
				// number is the parent_layer + 1 and this should propagate down to all the descendants of the child.
				relocations.push_back({ t, parent_layer + 1 });
			}
		}

		if (relocations.empty()) {
			// No targets need to be relocated. But this edge could have some ancestors of the new source
			// as sources, which have been removed in the removeTransitiveConnections call, so the new edge
			// could be long or short depending on the case.

			int k = edgeIsShort(edge);
			if (k >= 0) {
				addHyperedgeToLayer(k, edge);
			}
			else {
				// The edge needs to be split or resplitted. 
				splitLongEdge(edge);

				if (start_layer < INT_MAX) {
					// Update the start_layer to be the minimum layer of the new dummy nodes created by splitting the edge.
					for (const auto& src : edge->getSources()) {
						if (src->getLayer() + 1 < start_layer) {
							start_layer = src->getLayer() + 1;
						}
					}
				}
				else {
					// No new dummy nodes were created when removing redundant connections, so it is just a matter of placing
					// the new dummy nodes created by splitting the edge in good positions to minimize crossings.
					std::vector<Node*> nodes_to_minimize;
					int max_layer = 0;
					int min_layer = INT_MAX;
					for (const auto& segment : all_hyperedges_[edge]) {
						for (const auto& s : segment->getSources()) {
							if (s->isDummy()) {
								nodes_to_minimize.push_back(s.get());
							}
							if (s->getLayer() > max_layer) {
								max_layer = s->getLayer();
							}
							if (s->getLayer() < min_layer) {
								min_layer = s->getLayer();
							}
						}
					}
					minimizeCrossingsForNodes(nodes_to_minimize, min_layer, max_layer);
				}
			}

			if (start_layer < INT_MAX) {
				// We needed to remove some redundant connections, which lead to the creation of new dummy nodes
				// in between the layers. Therefore, at this point, we run global sifting from the start_layer.
				minimizeCrossings(3, start_layer);	
			}

		}
		else {
			applyRelocationAndPropagate(relocations);

			// After relocations are applied, we need to minimize crossings for all the affected nodes, starting from
			// the shallowest dummy node (which will be located in the sources top layer + 1). Since this is a very
			// disruptive operation, we will allow more rounds of sifting to try to minimize crossings as much as possible.
			for (const auto& src : edge->getSources()) {
				if (src->getLayer() + 1 < start_layer) {
					start_layer = src->getLayer() + 1;
				}
			}
			minimizeCrossings(10, start_layer);
		}
	}

	void Hypergraph::addTargetToEdge(const HyperedgePtr& edge, const NodePtr& target) {
		if (!edge || edge->isSegment() || !target) return;

		const auto& sources = edge->getSources();
		int parents_layer = 0;
		std::unordered_set<Node*> sources_set; // for quick lookup later on.
		for (const auto& s : sources) {
			if (s == target) {
				throw std::logic_error("A node cannot be connected to itself.");
			}
			if (s->getLayer() > parents_layer) {
				parents_layer = s->getLayer();
			}
			sources_set.insert(s.get());
		}


		// Even though some connections might be redundant, they can also encode an intention of grouping the sources and targets
		// in a different manner. The policy applied to allow these regroupings is the following:
		// If a pre-existing connection from (at least) one of the sources to the target already exists, then we allo regrouping
		// if and only if the sources of that connection are a subset of the edge sources.
		std::vector<HyperedgePtr> affected_edges;
		for (const auto& [hyperedge, _] : all_hyperedges_) {
			if (!hyperedge->containsTarget(target)) continue;
			bool update_grouping = false;
			for (const auto& s : sources) {
				if (hyperedge->containsSource(s)) {
					affected_edges.push_back(hyperedge);
					update_grouping = true;
					break;
				}
			}
			if (update_grouping) {
				for (const auto& s : hyperedge->getSources()) {
					if (!sources_set.count(s.get())) {
						throw std::logic_error("This connection already exists in the diagram.");
					}
				}
			}
		}

		if (affected_edges.empty() && childIsInDescendants(sources, target)) {
			throw std::logic_error("This connection already exists in the diagram.");
		}


		// Temporarily add the target and check for cycles
		std::unordered_set<Node*> target_parents_set; // for quick lookup later on.
		for (const auto& p : target->getParents()) {
			target_parents_set.insert(p.get());
		}

		std::unordered_set<Node*> already_existing_sources;
		for (const auto& s : sources) {
			if (target_parents_set.count(s.get()) == 0) {
				// This connection does not exist yet, so we need to add it temporarily.
				s->addChild(target);
				target->addParent(s);
			}
			else {
				// This connection already exists, so we keep track of it to avoid rolling it back later on.
				already_existing_sources.insert(s.get());
			}
		}

		// If adding these connections has created a cycle, then it must be the case
		// that target is part of the cycle, so it suffices to check for cycles starting
		// there rather than checking the sources.
		if (checkCycles(target)) {
			// Rollback
			for (const auto& s : sources) {
				if (already_existing_sources.count(s.get()) == 0) {
					// This connection was not pre-existing, so we need to rollback.
					s->removeChild(target);
					target->removeParent(s);
				}
			}
			throw std::logic_error("Adding this connection would create a cycle in the diagram.");
		}

		// Now that we know that no cycles are added, we can safely add the connection.
		if (!affected_edges.empty()) {
			// Remove the target from all the hyperedges in which it participates as so.
			for (const auto& hyperedge : affected_edges) {
				removeTargetsFromHyperedge(hyperedge, { target.get() }, false); // We will relocate later, so we avoid it here.
			}
			// Now, reinstate the connections from the target parents to the target since they were removed in the previous step.
			for (const auto& s : already_existing_sources) {
				s->addChild(target);
				target->addParent(s->shared_from_this());
			}
		}

		// As before, we need to remove any pre-existing connections which are now redundant.
		int start_layer = removeTransitiveConnections(sources, { target });

		// Special care, the previous call could have removed the edge from the hypergraph
		// if all targets where descendants of target. So we may have to readd it.
		all_hyperedges_[edge]; // This does nothing or reinstates the edge in the hypergraph.

		edge->addTarget(target);
		if (parents_layer + 1 > target->getLayer()) {
			// The child layer needs to be updated. This automatically implies that the new layer
			// number is the parents_layer + 1 and this should propagate down to all the descendants of the child.
			applyRelocationAndPropagate({ {target, parents_layer + 1} });

			// No, after relocations are applied, we need to minimize crossings for all the affected nodes, starting from
			// the shallowest dummy node (which will be located in the sources top layer + 1). Since this is a very disruptive
			// operation, we will allow more rounds of sifting to try to minimize crossings as much as possible.
			for (const auto& src : edge->getSources()) {
				if (src->getLayer() + 1 < start_layer) {
					start_layer = src->getLayer() + 1;
				}
			}
			minimizeCrossings(10, start_layer);
		}
		else {
			// Same as with adding a new source, the new edge could be long or short depending on the case.
			int k = edgeIsShort(edge);
			if (k >= 0) {
				addHyperedgeToLayer(k, edge);
			}
			else {
				// The edge needs to be split or resplitted. 
				splitLongEdge(edge);

				if (start_layer < INT_MAX) {
					// Update the start_layer to be the minimum layer of the new dummy nodes created by splitting the edge.
					for (const auto& src : edge->getSources()) {
						if (src->getLayer() + 1 < start_layer) {
							start_layer = src->getLayer() + 1;
						}
					}
				}
				else {
					// No new dummy nodes were created when removing redundant connections, so it is just a matter of placing
					// the new dummy nodes created by splitting the edge in good positions to minimize crossings.
					std::vector<Node*> nodes_to_minimize;
					int max_layer = 0;
					int min_layer = INT_MAX;
					for (const auto& segment : all_hyperedges_[edge]) {
						for (const auto& s : segment->getSources()) {
							if (s->isDummy()) {
								nodes_to_minimize.push_back(s.get());
							}
							if (s->getLayer() > max_layer) {
								max_layer = s->getLayer();
							}
							if (s->getLayer() < min_layer) {
								min_layer = s->getLayer();
							}
						}
					}
					minimizeCrossingsForNodes(nodes_to_minimize, min_layer, max_layer);
				}
			}

			if (start_layer < INT_MAX) {
				// We needed to remove some redundant connections, which lead to the creation of new dummy nodes
				// in between the layers. Therefore, at this point, we run global sifting from the start_layer.
				minimizeCrossings(3, start_layer);
			}

		}
	}

	// ============================================================================
	// Removal management
	// ============================================================================
	void Hypergraph::removeNode(const NodePtr& node) {
		if (!node) return;

		const auto parents = node->getParents();
		const auto children = node->getChildren();

		// Snapshot the hyperedges before modifying them.
		std::unordered_map<HyperedgePtr, std::vector<HyperedgePtr>, HyperedgePtrHash> snapshot = all_hyperedges_;

		if (children.empty()) {
			// If it has no children, just remove all connections
			// in which this node participates, which must be as
			// a target since it has no children.

			for (const auto& [edge, _] : snapshot) {
				if (edge->containsTarget(node)) {
					removeTargetsFromHyperedge(edge, { node.get() }, false); // No relocation will be needed
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
						removeSourcesFromHyperedge(edge, { node.get() }, false); // We will relocate at the very end.
						for (const auto& t : targets) {
							if (relocations.insert(t.get()).second) {
								relocations_vec.push_back(t);
							}
						}
					}
				}

				relocateNodes(relocations_vec);
			}
			else {
				// If it has both parents and children, the parents need to assume the connections 
				// to the children to aovid losing information.

				// First, remove it from targets and sources. In both cases, we avoid early relocation
				// since more updates will come after.
				std::vector<HyperedgePtr> target_edges;
				std::vector<HyperedgePtr> source_edges;
				for (const auto& [edge, _] : snapshot) {
					if (edge->containsSource(node)) {
						removeSourcesFromHyperedge(edge, { node.get() }, false);
						source_edges.push_back(edge);
					}

					if (edge->containsTarget(node)) {
						removeTargetsFromHyperedge(edge, { node.get() }, false);
						target_edges.push_back(edge);
					}
				}

				// Now we rewire the parents to the children and relocate them if necessary.
				HyperedgePtr edge = createHyperedge(parents, children, -1);

				// Now we need to apply relocation and splitting if necessary.
				if (!relocateNodes(edge->getTargets())) {
					int k = edgeIsShort(edge);
					if (k >= 0) addHyperedgeToLayer(k, edge);
					else splitLongEdge(edge);
				}
			}
		}

		// Finally, remove the node from the graph and from its layer.
		all_nodes_.erase(std::remove(all_nodes_.begin(), all_nodes_.end(), node), all_nodes_.end());
		removeNodeFromLayer(node->getLayer(), node);
		cleanUp();
	}

	void Hypergraph::removeConnection(const NodePtr& parent, const NodePtr& child) {
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
				removeTargetsFromHyperedge(edge, { child.get() }, false);
				if (remaining_sources.empty()) {
					if (relocateNodes({ child })) {
						int start_layer = child->getLayer();
						for (const auto& child_parent : child->getParents()) {
							if (child_parent->getLayer() + 1 < start_layer) {
								start_layer = child_parent->getLayer() + 1;
							}
						}
						minimizeCrossings(10, start_layer);
					}
					return;
				}

				const auto& new_edge = createHyperedge(remaining_sources, { child }, -1);
				if (relocateNodes({ child })) {
					if (child->getChildren().empty()) {
						// The child has no children, so we just have to take care of the possibly
						// created dummy nodes in the new edge (if relocating has caused splitting).
						std::vector<Node*> nodes_to_minimize;
						int min_layer = INT_MAX;
						int max_layer = 0;
						for (const auto& segment : all_hyperedges_[new_edge]) {
							for (const auto& s : segment->getSources()) {
								if (s->isDummy()) {
									nodes_to_minimize.push_back(s.get());
									if (s->getLayer() < min_layer) {
										min_layer = s->getLayer();
									}
									if (s->getLayer() > max_layer) {
										max_layer = s->getLayer();
									}
								}
							}
						}

						minimizeCrossingsForNodes(nodes_to_minimize, min_layer, max_layer);
					}
					else {
						int start_layer = child->getLayer();
						for (const auto& child_parent : child->getParents()) {
							if (child_parent->getLayer() + 1 < start_layer) {
								start_layer = child_parent->getLayer() + 1;
							}
						}

						minimizeCrossings(10, start_layer);
					}
				}
				else {
					int k = edgeIsShort(new_edge);
					if (k >= 0) {
						// The new edge is short, just add it to the layer data.
						addHyperedgeToLayer(k, new_edge);
					}
					else {
						// The new edge is long, we need to split it and add the necessary dummy nodes.
						splitLongEdge(new_edge);

						// Locate the new dummy nodes created by splitting the edge in good positions to minimize crossings.
						std::vector<Node*> nodes_to_minimize;
						int min_layer = INT_MAX;
						int max_layer = 0;
						for (const auto& segment : all_hyperedges_[new_edge]) {
							for (const auto& s : segment->getSources()) {
								if (s->isDummy()) {
									nodes_to_minimize.push_back(s.get());
									if (s->getLayer() < min_layer) {
										min_layer = s->getLayer();
									}
									if (s->getLayer() > max_layer) {
										max_layer = s->getLayer();
									}
								}
							}
						}
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

	void Hypergraph::removeSourcesFromHyperedge(const HyperedgePtr& original_edge, const std::unordered_set<Node*>& sources_to_remove, bool relocation)
	{
		if (sources_to_remove.empty() || original_edge->isSegment()) return;

		// This is a check only for user interaction. The user will only introduce this one at a time.
		if (sources_to_remove.size() == 1) {
			for (const auto& s : sources_to_remove) {
				if (!original_edge->containsSource(s->shared_from_this())) {
					throw std::logic_error("The specified connection does not exist in the diagram.");
				}
			}
		}


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
			// Edge has no sources left — dissolve everything.
			dissolveSegments({ original_edge.get() });
			all_hyperedges_.erase(original_edge);
			if (original_edge->getLayer() >= 0) {
				removeHyperedgeFromLayer(original_edge->getLayer(), original_edge);
			}
			if (relocation) relocateNodes(original_edge->getTargets());
			return;
		}

		if (all_hyperedges_[original_edge].empty()) return; // The edge was short, so no further action needed.
		int k = edgeIsShort(original_edge);
		if (k >= 0) {
			// The edge is now short, dissolve the edges. The targets don't update their
			// layer, since the remaining sources are in the inmediate shallower layer.
			dissolveSegments({ original_edge.get() });
			addHyperedgeToLayer(k, original_edge);
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
			for (Node* s : sources_to_remove)
				seg->removeSource(s->shared_from_this());
			for (Node* d : dead_dummies)
				seg->removeSource(d->shared_from_this());

			if (allSourcesDead(seg, dead_dummies, sources_to_remove)) {
				// This whole segment is dead.
				dead_segments.insert(seg.get());

				// Any dummy targets of this segment are now dead too —
				// they were only reachable via this segment's sources.
				for (const auto& t : seg->getTargets())
					if (t->isDummy()) dead_dummies.insert(t.get());
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
			if (relocateNodes(original_edge->getTargets())) {
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
		}
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

	void Hypergraph::removeTargetsFromHyperedge(const HyperedgePtr& original_edge, const std::unordered_set<Node*>& targets_to_remove, bool relocation)
	{
		if (targets_to_remove.empty() || original_edge->isSegment()) return;

		// This is just a check for the user interaction. It introduces removals one at a time.
		if (targets_to_remove.size() == 1) {
			for (Node* t : targets_to_remove) {
				if (!original_edge->containsTarget(t->shared_from_this())) {
					throw std::logic_error("The specified connection does not exist in the diagram.");
				}
			}
		}

		// Remove targets from the original edge.
		for (Node* t : targets_to_remove)
			original_edge->removeTarget(t->shared_from_this());

		// Remove real parent/child links on real nodes.
		for (const auto& s : original_edge->getSources()) {
			for (Node* t : targets_to_remove) {
				s->removeChild(t->shared_from_this());
				t->removeParent(s);
			}
		}

		if (original_edge->getTargets().empty()) {
			// Edge has no targets left — dissolve everything.
			dissolveSegments({ original_edge.get() });
			all_hyperedges_.erase(original_edge);
			if (original_edge->getLayer() >= 0) {
				removeHyperedgeFromLayer(original_edge->getLayer(), original_edge);
			}
			if (relocation) relocateNodes(original_edge->getTargets());
			return;
		}

		if (all_hyperedges_[original_edge].empty()) return; // The edge was short, so no further action needed.
		int k = edgeIsShort(original_edge);
		if (k >= 0) {
			// The edge is now short, so just dissolve the segments and add it to the new layer.
			dissolveSegments({ original_edge.get() });
			addHyperedgeToLayer(k, original_edge);
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

			// Remove the real targets that are being dropped from this segment.
			for (Node* t : targets_to_remove)
				seg->removeTarget(t->shared_from_this());

			// Remove dummies that died in the segment below (they were targets here).
			for (Node* d : dead_dummies)
				seg->removeTarget(d->shared_from_this());

			if (allTargetsDead(seg, dead_dummies, targets_to_remove)) {
				dead_segments.insert(seg.get());

				// Any dummy sources of this segment are now dead too —
				// they only existed to feed targets that are all gone.
				for (const auto& s : seg->getSources())
					if (s->isDummy()) dead_dummies.insert(s.get());
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
			if (relocateNodes(original_edge->getTargets())) {
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
		}
	}

	// ============================================================================
	// Node fusion management
	// ============================================================================
	void Hypergraph::fuseNodes(const NodePtr& node1, const NodePtr& node2, const std::string& new_name) {
		if (!node1 || !node2) return;
		if (node1 == node2) {
			throw std::invalid_argument("Cannot fuse a node with itself.");
		}

		// Store the previous configuration of the nodes to be able to rollback in case of cycle creation.
		const auto parents2 = node2->getParents();
		const auto children2 = node2->getChildren();
		std::unordered_set<Node*> parents1_set; // for quick lookup later on.
		std::unordered_set<Node*> children1_set; // for quick lookup later on.
		for (const auto& p : node1->getParents()) parents1_set.insert(p.get());
		for (const auto& c : node1->getChildren()) children1_set.insert(c.get());

		// Temporarily fuse the nodes moving all parents and children to one while disconnecting the other
		for (const auto& p : parents2) {
			if (!parents1_set.count(p.get())) {
				p->replaceChild(node2, node1);
				node1->addParent(p);
			}
			else {
				p->removeChild(node2);
			}
		}

		for (const auto& c : children2) {
			if (!children1_set.count(c.get())) {
				c->replaceParent(node2, node1);
				node1->addChild(c);
			}
			else {
				c->removeParent(node2);
			}
		}

		if (checkCycles(node1)) {
			// Rollback
			for (const auto& p : parents2) {
				if (!parents1_set.count(p.get())) {
					p->replaceChild(node1, node2);
					node1->removeParent(p);
				}
				else {
					p->addChild(node2);
				}
			}

			for (const auto& c : children2) {
				if (!children1_set.count(c.get())) {
					c->replaceParent(node1, node2);
					node1->removeChild(c);
				}
				else {
					c->addParent(node2);
				}
			}
			throw std::logic_error("Fusing these nodes would create a cycle in the diagram.");
		}

		// Now we know that no cycles are added, we can safely fuse the nodes.
		node1->setName(new_name);

		// Modify all hyperedges in which node2 participated to replace it with node1.
		std::vector<HyperedgePtr> modified_edges;
		for (const auto& edge : getAllHyperedges()) {
			if (edge->containsSource(node2)) {
				if (edge->containsSource(node1)) {
					// If node1 is already a source of this edge, we just need to remove node2 from the sources without replacement.
					edge->removeSource(node2);
				}
				else {
					edge->replaceSource(node2, node1);
				}
				if (!edge->isSegment()) modified_edges.push_back(edge);
				continue;
			}
			if (edge->containsTarget(node2)) {
				if (edge->containsTarget(node1)) {
					// If node1 is already a target of this edge, we just need to remove node2 from the targets without replacement.
					edge->removeTarget(node2);
				}
				else {
					edge->replaceTarget(node2, node1);
				}
				if (!edge->isSegment()) modified_edges.push_back(edge);
				continue;
			}
			if (!edge->isSegment() && (edge->containsSource(node1) || edge->containsTarget(node1))) {
				modified_edges.push_back(edge);
			}
		}

		// Remove the node from the graph and from its layer.
		all_nodes_.erase(std::remove(all_nodes_.begin(), all_nodes_.end(), node2), all_nodes_.end());
		removeNodeFromLayer(node2->getLayer(), node2);

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

		// Relocate the new node. If no relocation is needed for this node, that 
		// implicitly means that node1 and node2 were both in the same layer and
		// the fusion doesn't change any other layer number, so no relocation is
		// needed for any other node.
		if (relocateNodes({ node1 })) {
			// Run global sifting for all the affected nodes.
			int start_layer = node1->getLayer();
			for (const auto& parent : node1->getParents()) {
				if (parent->getLayer() + 1 < start_layer) {
					start_layer = parent->getLayer() + 1;
				}
			}
			minimizeCrossings(10, start_layer);
		}
	}

	// ============================================================================
	// Helper methods for connection management
	// ============================================================================
	void Hypergraph::splitLongEdge(const HyperedgePtr& long_edge) {
		if (long_edge->isSegment()) return;
		if (!all_hyperedges_[long_edge].empty()) dissolveSegments({ long_edge.get() }); // If it was already split, dissolve the previous segments before splitting again.

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

		if (min_src_layer >= max_tgt_layer - 1) return; // No splitting needed, the edge is not long.

		// If it needs to be split, the edge should me removed from its current layer (if any).
		if (long_edge->getLayer() >= 0) {
			removeHyperedgeFromLayer(long_edge->getLayer(), long_edge);
		}

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
				carry_dummy = std::make_shared<Node>();
				all_nodes_.push_back(carry_dummy);
				addNodeToLayer(L + 1, -1, carry_dummy);
				seg_targets.push_back(carry_dummy);
			}
			else {
				carry_dummy = nullptr;
			}

			// ------------------------------------------------------------
			// Create the segment hyperedge for this L -> L+1 transition.
			// It automatically deals with the parent/child wiring.
			// ------------------------------------------------------------
			createHyperedge(long_edge, seg_sources, seg_targets, L);
		}
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

	int Hypergraph::removeTransitiveConnections(const std::vector<NodePtr>& parents, const std::vector<NodePtr>& children) {
		int min_affected_layer = INT_MAX;
		if (children.empty()) return min_affected_layer; // No connections to remove, so no affected layer.

		// Collect all ancestors of every parent (including the parents themselves).
		std::unordered_set<Node*> parents_and_ancestors = getAllAncestors(parents);
		for (const auto& p : parents) parents_and_ancestors.insert(p.get());

		// Collect all descendants of every child (including the children themselves).
		std::unordered_set<Node*> children_and_descendants = getAllDescendants(children);
		for (const auto& c : children) children_and_descendants.insert(c.get());

		// Snapshot all_hyperedges_ since it may be modified during the iteration.
		std::unordered_map<HyperedgePtr, std::vector<HyperedgePtr>, HyperedgePtrHash> snapshot = all_hyperedges_;
		for (const auto& [edge, _] : snapshot) {
			// Collect sources which are ancestors of the parent.
			std::unordered_set<Node*> ancestor_sources;
			for (const auto& s : edge->getSources())
				if (parents_and_ancestors.count(s.get()))
					ancestor_sources.insert(s.get());

			if (ancestor_sources.empty()) continue;

			// Split targets into redundant (now covered transitively) and surviving.
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

			// Remove the ancestor sources from this edge (and its segments).
			// No relocation is needed here since the addConnection, addSourceToEdge 
			// and addTargetToEdge functions will take care of relocating if necessary.
			removeSourcesFromHyperedge(edge, ancestor_sources, false);

			// If those sources still had non-redundant targets, create a 
			// new edge from the ancestor sources to the surviving targets
			// since that connection is still needed.
			if (!surviving_targets.empty()) {
				std::vector<NodePtr> ancestor_sources_vec(ancestor_sources.begin(), ancestor_sources.end());
				if (edge->getLayer() >= 0) {
					// If the original edge was short, this new edge will also be short and can
					// be directly added to the same layer without any lookup.
					createHyperedge(ancestor_sources_vec, surviving_targets, edge->getLayer());
				}
				else {
					const auto& new_edge = createHyperedge(ancestor_sources_vec, surviving_targets, -1);
					int k = edgeIsShort(new_edge);
					if (k < 0) {
						splitLongEdge(new_edge);

						// There will be newly created dummies which will need to be located in a convenient
						// position in the layer. So, min_affected_layer should be at most the layer of the 
						// ancestors + 1, since the new dummies will be children of those ancestors.
						for (const auto& anc : ancestor_sources_vec) {
							if (anc->getLayer() + 1 < min_affected_layer)
								min_affected_layer = anc->getLayer() + 1;
						}
					}
					else {
						addHyperedgeToLayer(k, new_edge);
					}
				}
			}
		}
		
		return min_affected_layer;
	}

	bool Hypergraph::relocateNodes(const std::vector<NodePtr>& nodes) {
		std::vector<std::pair<NodePtr, int>> relocations;
		for (const auto& node : nodes) {
			auto parents = node->getParents();

			int correct_layer = parents.empty() ? 0
				: (*std::max_element(parents.begin(), parents.end(),
					[](const NodePtr& a, const NodePtr& b) {
						return a->getLayer() < b->getLayer();
					}))->getLayer() + 1;

			if (correct_layer != node->getLayer())
				relocations.push_back({ node, correct_layer });
		}

		if (!relocations.empty()) {
			applyRelocationAndPropagate(relocations);
			return true;
		}
		return false;
	}

	void Hypergraph::applyRelocationAndPropagate(const std::vector<std::pair<NodePtr, int>>& relocations) {
		if (relocations.empty()) return;

		// ====================================================================
		// Phase 1: Move all nodes in LayerData and update incoming edges.
		// ====================================================================
		std::unordered_set<Hyperedge*> incoming_set;
		std::unordered_set<Node*> relocated_nodes; // Improve efficiency when checking for incoming edges.
		std::vector<NodePtr> vec_relocated_nodes; // For getAllDescendants input.

		for (const auto& [node, new_layer] : relocations) {
			removeNodeFromLayer(node->getLayer(), node);
			addNodeToLayer(new_layer, -1, node);
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

		dissolveSegments(incoming_set);
		for (Hyperedge* edge : incoming_set) {
			int k = edgeIsShort(edge->shared_from_this());
			if (k >= 0) {
				if (k != edge->getLayer()) {
					// This edge is now short, so it just needs to be relocated to the 
					// correct layer if it is not already there.
					removeHyperedgeFromLayer(edge->getLayer(), edge->shared_from_this());
					addHyperedgeToLayer(k, edge->shared_from_this());
				}
			}
			else {
				splitLongEdge(edge->shared_from_this());
			}
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
				auto parents = (*it)->getParents();
				int new_depth = parents.empty() ? 0
					: (*std::max_element(parents.begin(), parents.end(),
						[](const NodePtr& a, const NodePtr& b) {
							return a->getLayer() < b->getLayer();
						}))->getLayer() + 1;

				if (new_depth == layer) {
					it = nodes.erase(it); // Prevent removing from layer if the node does not need to relocate.
				}
				else {
					addNodeToLayer(new_depth, -1, (*it)->shared_from_this());
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
			int k = edgeIsShort(edge->shared_from_this());
			if (k >= 0) {
				dissolveSegments({ edge }); // Dissolve any existing segments since the edge may now be short when it wasn't before.
				if (k != edge->getLayer()) {
					// This edge is now short, so it just needs to be relocated
					// to the correct layer if it is not already there.
					removeHyperedgeFromLayer(edge->getLayer(), edge->shared_from_this());
					addHyperedgeToLayer(k, edge->shared_from_this());
				}
			}
			else {
				splitLongEdge(edge->shared_from_this()); // Already handles edge dissolution.
			}
		}

		cleanUp();
	}

	void Hypergraph::cleanUp() {
		std::vector<int> empty;
		for (const auto& [l, data] : layers_)
			if (data.nodes.empty() && data.outgoing_edges.empty())
				empty.push_back(l);
		for (int l : empty)
			layers_.erase(l);
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

	bool Hypergraph::parentIsInAncestors(const std::vector<NodePtr>& children, const NodePtr& parent) {
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

	bool Hypergraph::childIsInDescendants(const std::vector<NodePtr>& parents, const NodePtr& child) {
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
