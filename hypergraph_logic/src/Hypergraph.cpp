#include "Hypergraph.h"
#include <algorithm>
#include <stdexcept>
#include <queue>
#include <unordered_map>

namespace hypergraph_logic {
	// ============================================================================
	// Constructor
	// ============================================================================
	Hypergraph::Hypergraph(std::string name) : name_(std::move(name)) {};

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
		layer_outgoing_edges.erase(
			std::remove_if(layer_outgoing_edges.begin(), layer_outgoing_edges.end(),
				[&edges](const HyperedgePtr& edge) {
					return edges.count(edge.get()) > 0;
				}),
			layer_outgoing_edges.end()
		);

		for (const auto& edge : edges) {
			edge->setLayer(-1); // Unset layer
		}
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
	NodePtr Hypergraph::createNode(const std::string& label, int layer_position, const NodePtr& parent){
		NodePtr node = std::make_shared<Node>(label);
		all_nodes_.push_back(node);

		if(!parent){
			addNodeToLayer(0, layer_position, node);
		}
		else{
			createHyperedge({ parent }, { node }, parent->getLayer());
			addNodeToLayer(parent->getLayer() + 1, layer_position, node);
		}

		return node;
	}

	void Hypergraph::addConnection(const NodePtr& parent, const NodePtr& child) {
		if (!child || !parent) return;
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
		removeTransitiveConnections({ parent }, { child });

		int parent_layer = parent->getLayer();
		int child_layer = child->getLayer();
		if (parent_layer == child_layer - 1) {
			// Easiest case: just add the new hyperedge and update the layer data
			createHyperedge({ parent }, { child }, parent_layer);
		}
		else if (parent_layer < child_layer) {
			// The parent is in a layer above the child, so the child's layer does not need to be updated, but we need 
			// to add the new hyperedge, split it and add the necessary dummy nodes in the intermediate layers.
			HyperedgePtr edge = createHyperedge({ parent }, { child }, -1);

			// Rewire the connection from parent to child with the new dummy nodes and split the hyperedge.
			splitLongEdge(edge);
		}
		else {
			// Worst case: the child layer needs to be updated. This automatically implies that the new layer
			// number is the parent_layer + 1 and this should propagate down to all the descendants of the child.
			HyperedgePtr edge = createHyperedge({ parent }, { child }, parent_layer);

			applyRelocationAndPropagate({ {child, parent_layer + 1} });
		}
	}

	void Hypergraph::addSourceToEdge(const HyperedgePtr& edge, const NodePtr& source) {
		if (!edge || edge->isSegment() || !source) return;

		const auto& targets = edge->getTargets();
		for (const auto& t : targets) {
			if (t == source) {
				throw std::logic_error("A node cannot be connected to itself.");
			}
		}

		if (parentIsInAncestors(targets, source)) {
			throw std::logic_error("This connection already exists in the diagram.");
		}
	
		// Temporarily add the source and check for cycles
		for (const auto& t : targets) {
			source->addChild(t);
			t->addParent(source);
		}

		if (checkCycles(source)) {
			// Rollback
			for (const auto& t : targets) {
				source->removeChild(t);
				t->removeParent(source);
			}
			throw std::logic_error("Adding this connection would create a cycle in the diagram.");
		}

		// Now we know that no cycles are added, we can safely add the connection.
		// As before, we need to remove any pre-existing connections which are now redundant.
		removeTransitiveConnections({ source }, targets);
		
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
			// No targets need to be relocated. But the updated edge might be long, so we
			// need to resplit or split it if necessary.

			if (edgeIsShort(edge) < 0) {
				// The edge needs to be split or resplitted. 
				dissolveSegments({ edge.get() });
				splitLongEdge(edge);
			}
		}
		else {
			// Inside the function call, if adding the new source has made the edge long,
			// it will be split and the necessary dummy nodes will be added.
			applyRelocationAndPropagate(relocations);
		}
	}

	void Hypergraph::addTargetToEdge(const HyperedgePtr& edge, const NodePtr& target) {
		if (!edge || edge->isSegment() || !target) return;

		const auto& sources = edge->getSources();
		for (const auto& s : sources) {
			if (s == target) {
				throw std::logic_error("A node cannot be connected to itself.");
			}
		}

		if (childIsInDescendants(sources, target)) {
			throw std::logic_error("This connection already exists in the diagram.");
		}

		// Temporarily add the target and check for cycles
		int parents_layer = 0;
		for (const auto& s : sources) {
			target->addParent(s);
			s->addChild(target);
			if (s->getLayer() > parents_layer) parents_layer = s->getLayer();
		}

		// If adding these connections has created a cycle, then it must be the case
		// that target is part of the cycle, so it suffices to check for cycles starting
		// there rather than checking the sources.
		if (checkCycles(target)) {
			// Rollback
			for (const auto& s : sources) {
				target->removeParent(s);
				s->removeChild(target);
			}
			throw std::logic_error("Adding this connection would create a cycle in the diagram.");
		}

		// Now we know that no cycles are added, we can safely add the connection.
		// As before, we need to remove any pre-existing connections which are now redundant.
		removeTransitiveConnections(sources, { target });

		edge->addTarget(target);

		if (parents_layer + 1 > target->getLayer()) {
			// The child layer needs to be updated. This automatically implies that the new layer
			// number is the parents_layer + 1 and this should propagate down to all the descendants of the child.
			applyRelocationAndPropagate({ {target, parents_layer + 1} });
		}
		else {
			// The target layer does not need to be updated, but it may be necessary to split the edge.
			if (edgeIsShort(edge) < 0) {
				// The edge needs to be split or resplitted. 
				dissolveSegments({ edge.get() });
				splitLongEdge(edge);
			}
		}
	}

	// ============================================================================
	// Removal management
	// ============================================================================
	void Hypergraph::removeNode(const NodePtr& node) {
		if (!node) return;

		const auto& parents = node->getParents();
		const auto& children = node->getChildren();

		if (children.empty()) {
			// If it has no children, just remove all connections
			// in which this node participates, which must be as
			// a target since it has no children.

			for (const auto& [edge, _] : all_hyperedges_) {
				if (edge->containsTarget(node)) {
					removeTargetsFromHyperedge(edge, { node.get() }, false); // No relocation will be needed
				}
			}
		}
		else {
			if (parents.empty()) {
				// If it has no parents but has children, just remove it from source in all
				// conections in which it participates as so and relocate the targets accordingly.
				for (const auto& [edge, _] : all_hyperedges_) {
					if (edge->containsSource(node)) {
						removeSourcesFromHyperedge(edge, { node.get() }, true);
					}
				}
			}
			else {
				// If it has both parents and children, the parents need to assume the connections 
				// to the children to aovid losing information.

				// First, remove it from targets and sources. In both cases, we avoid early relocation
				// since more updates will come after.
				std::vector<HyperedgePtr> target_edges;
				std::vector<HyperedgePtr> source_edges;
				for (const auto& [edge, _] : all_hyperedges_) {
					if (edge->containsSource(node)) {
						removeSourcesFromHyperedge(edge, { node.get() }, false);
						source_edges.push_back(edge);
					}

					if (edge->containsTarget(node)) {
						removeTargetsFromHyperedge(edge, { node.get() }, false);
						target_edges.push_back(edge);
					}
				}

				// Now we rewire the parents to the children and relocate them if necessary. If there was
				// only one edge connecting the parents to the node, we just add the targets in that edge.
				// If not, create a new edge.
				HyperedgePtr edge;

				if (target_edges.size() == 1) {
					edge = target_edges[0];

					for (const auto& child : children) {
						edge->addTarget(child);
						for (const auto& parent : parents) {
							child->addParent(parent);
							parent->addChild(child);
						}
					}
				}
				else {
					edge = createHyperedge(source_edges[0], parents, children, -1);
				}

				// Now we need to apply relocation and splitting if necessary.
				if (!relocateTargets(edge)) {
					// No relocations where needed, the edge is for sure long, so we split it.
					splitLongEdge(edge);
				}
			}
		}

		// Finally, remove the node from the graph and from its layer.
		all_nodes_.erase(std::remove(all_nodes_.begin(), all_nodes_.end(), node), all_nodes_.end());
		removeNodeFromLayer(node->getLayer(), node);
	}

	void Hypergraph::removeConnection(const NodePtr& parent, const NodePtr& child) {
		if (!parent || !child) return;

		
		for (const auto& [edge, _] : all_hyperedges_) {
			if (edge->containsSource(parent) && edge->containsTarget(child)) {
				removeTargetsFromHyperedge(edge, { child.get() }, false);

				std::vector<NodePtr> remaining_sources;
				for (const auto& s : edge->getSources()) {
					if (s != parent) {
						remaining_sources.push_back(s);
					}
				}

				if (remaining_sources.empty()) return;

				const auto& new_edge = createHyperedge(edge, remaining_sources, { child }, -1);
				int k = edgeIsShort(new_edge);
				if (k >= 0) {
					// The new edge is short, just add it to the layer data.
					addHyperedgeToLayer(k, new_edge);
				}
				else {
					// The new edge is long, we need to split it and add the necessary dummy nodes.
					splitLongEdge(new_edge);
				}
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
			if (relocation) relocateTargets(original_edge);
			return;
		}

		if (all_hyperedges_[original_edge].empty()) return; // The edge was short, so no further action needed.
		int k = edgeIsShort(original_edge);
		if (k >= 0) {
			// The edge is now short, dissolve the edges. The targets don't update their
			// layer, since the remaining sources are in the inmediate shallower layer.
			dissolveSegments({ original_edge.get() });
			addHyperedgeToLayer(k, original_edge);
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
		if (relocation) relocateTargets(original_edge);
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
			if (relocation) relocateTargets(original_edge);
			return;
		}

		if (all_hyperedges_[original_edge].empty()) return; // The edge was short, so no further action needed.
		int k = edgeIsShort(original_edge);
		if (k >= 0) {
			// The edge is now short, so just dissolve the segments and add it to the new layer.
			dissolveSegments({ original_edge.get() });
			addHyperedgeToLayer(k, original_edge);
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
		if (relocation) relocateTargets(original_edge);
	}


	// ============================================================================
	// Protected helper methods for connection management
	// ============================================================================
	void Hypergraph::splitLongEdge(const HyperedgePtr& long_edge) {
		if (long_edge->isSegment()) return;

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
			// Create the segment hyperedge for this L → L+1 transition.
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

	void Hypergraph::removeTransitiveConnections(const std::vector<NodePtr>& parents, const std::vector<NodePtr>& children) {
		if (children.empty()) return;

		// Collect all ancestors of every parent (including the parents themselves).
		std::unordered_set<Node*> parents_and_ancestors = getAllAncestors(parents);
		for (const auto& p : parents)   parents_and_ancestors.insert(p.get());

		// Collect all descendants of every child (including the children themselves).
		std::unordered_set<Node*> children_and_descendants = getAllDescendants(children);
		for (const auto& c : children)  children_and_descendants.insert(c.get());

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
					}
					else {
						addHyperedgeToLayer(k, new_edge);
					}
				}
			}
		}
	}

	bool Hypergraph::relocateTargets(const HyperedgePtr& original_edge) {
		std::vector<std::pair<NodePtr, int>> relocations;
		for (const auto& t : original_edge->getTargets()) {
			auto parents = t->getParents();

			int correct_layer = parents.empty() ? 0
				: (*std::max_element(parents.begin(), parents.end(),
					[](const NodePtr& a, const NodePtr& b) {
						return a->getLayer() < b->getLayer();
					}))->getLayer() + 1;

			if (correct_layer != t->getLayer())
				relocations.push_back({ t, correct_layer });
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
					affected_nodes.erase(*it);
					nodes.erase(it);
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

		dissolveSegments(affected_edges);
		for (Hyperedge* edge : affected_edges) {
			int k = edgeIsShort(edge->shared_from_this());
			if (k >= 0) {
				if (k != edge->getLayer()) {
					// This edge is now short, so it just needs to be relocated
					// to the correct layer if it is not already there.
					removeHyperedgeFromLayer(edge->getLayer(), edge->shared_from_this());
					addHyperedgeToLayer(k, edge->shared_from_this());
				}
			}
			else {
				splitLongEdge(edge->shared_from_this());
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
