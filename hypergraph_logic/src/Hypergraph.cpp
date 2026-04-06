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
	static void rewireConnection(const NodePtr& parent, const NodePtr& oldChild, const NodePtr& newChild) {
		parent->replaceChild(oldChild, newChild); // this deletes the connection from parent to oldChild and creates a new one to newChild
		oldChild->replaceParent(parent, newChild); // this deletes the connection from oldChild to parent and creates a new one to newChild
		newChild->addParent(parent);
		newChild->addChild(oldChild);
	}
	
	void Hypergraph::addNodeToLayer(int layer, int position, const NodePtr& node) {
		if (!node) return;

		// Create layer if it doesn't exist
		if (layers_.find(layer) == layers_.end()) {
			layers_[layer] = LayerData{};
		}

		auto& layer_data = layers_[layer];

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

	void Hypergraph::addHyperedgeToLayer(int layer, const HyperedgePtr& edge) {
		if (!edge || layers_.find(layer) == layers_.end()) return;
		auto& layer_data = layers_[layer];

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

	// We add a new node to the graph, and we also add it to the appropriate layer at the specified position.
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
		if (parentIsInAncestors(child, parent)) {
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
		removeTransitiveConnections(parent, child);

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
			splitLongEdge(edge, { parent }, { child });
		}
		else {
			// Worst case: the child layer needs to be updated. This automatically implies that the new layer
			// number is the parent_layer + 1 and this should propagate down to all the descendants of the child.
			HyperedgePtr edge = createHyperedge({ parent }, { child }, parent_layer);
			applyRelocationAndPropagate(child, parent_layer+1);
		}
	}

	void Hypergraph::splitLongEdge(const HyperedgePtr& long_edge, const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets) {
		// ----------------------------------------------------------------
		// Group sources and targets by layer
		// ----------------------------------------------------------------
		std::map<int, std::vector<NodePtr>> sources_by_layer;
		for (const auto& source : sources)
			sources_by_layer[source->getLayer()].push_back(source);

		std::map<int, std::vector<NodePtr>> targets_by_layer;
		for (const auto& target : targets)
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

			if (L+1 >= min_tgt_layer && targets_by_layer.count(L + 1))
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

		// ================================================================
		// 1. Collect segments that belong to any of the long edges and
		//	 the dummy nodes which also need to be removed
		// ================================================================
		std::map<int, std::unordered_set<Node*>> dummy_removes; // for quick lookup when removing from LayerData
		std::map<int, std::unordered_set<Hyperedge*>> segment_removes; // for quick lookup when removing from LayerData
		std::unordered_set<Node*> dummy_set; // for quick lookup when removing from all_nodes_
		std::unordered_set<Hyperedge*> remove_set; // for quick lookup when removing from all_hyperedges_

		for (const auto& edge : all_hyperedges_) {
			if (!edge->isSegment()) continue;

			auto origin = edge->getOrigin();
			if (!origin.lock() || !long_edges.count(origin.lock().get())) continue;

			int layer = edge->getLayer();
			for (const auto& s : edge->getSources()) {
				if (s->isDummy()) {
					dummy_removes[layer].insert(s.get());
					dummy_set.insert(s.get());
				}
			}
			for (const auto& t : edge->getTargets()) {
				if (t->isDummy()) {
					dummy_removes[layer + 1].insert(t.get());
					dummy_set.insert(t.get());
				}
			}
			segment_removes[layer].insert(edge.get());
			remove_set.insert(edge.get());
		}

		if (remove_set.empty()) return;

		// ================================================================
		// 2. Remove from all_hyperedges_ and all_nodes_.
		// ================================================================
		all_hyperedges_.erase(
			std::remove_if(all_hyperedges_.begin(), all_hyperedges_.end(),
				[&](const HyperedgePtr& e) {
					return remove_set.count(e.get()) > 0;
				}),
			all_hyperedges_.end());

		all_nodes_.erase(
			std::remove_if(all_nodes_.begin(), all_nodes_.end(),
				[&](const NodePtr& n) {
					return dummy_set.count(n.get()) > 0;
				}),
			all_nodes_.end());

		// ================================================================
		// 3. Remove from LayerData.
		// ================================================================
		for (const auto& [layer, edges] : segment_removes) {
			removeHyperedgeFromLayer(layer, edges);
		}
		for (const auto& [layer, nodes] : dummy_removes) {
			removeNodeFromLayer(layer, nodes);
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
	
	void Hypergraph::removeSourcesFromHyperedge(const HyperedgePtr& original_edge, const std::unordered_set<Node*>& sources_to_remove)
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
			all_hyperedges_.erase(
				std::remove(all_hyperedges_.begin(), all_hyperedges_.end(), original_edge),
				all_hyperedges_.end());

			if (original_edge->getLayer() >= 0) {
				removeHyperedgeFromLayer(original_edge->getLayer(), original_edge);
			}
			return;
		}
		// Group segments by layer for higher efficiency in the next steps.
		std::map<int, HyperedgePtr> segs_by_layer;
		for (const auto& edge : all_hyperedges_) {
			if (!edge->isSegment()) continue;
			auto origin = edge->getOrigin();
			if (!origin.lock() || origin.lock() != edge) continue;
			segs_by_layer[edge->getLayer()] = edge;
		}

		// Track dead dummies and segments in a up-bottom manner.
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
		all_hyperedges_.erase(
			std::remove_if(all_hyperedges_.begin(), all_hyperedges_.end(),
				[&](const HyperedgePtr& e) {
					return dead_segments.count(e.get()) > 0;
				}),
			all_hyperedges_.end());

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
	}

	void Hypergraph::removeTransitiveConnections(const NodePtr& parent, const NodePtr& child) {
		// Collect child and all its descendants.
		std::unordered_set<Node*> child_and_descendants = child->getAllDescendants();
		child_and_descendants.insert(child.get());

		// Collect parent and all its ancestors.
		std::unordered_set<Node*> parent_and_ancestors = parent->getAllAncestors();
		parent_and_ancestors.insert(parent.get());

		// Snapshot all_hyperedges_ since it may be modified during the iteration.
		std::vector<HyperedgePtr> snapshot = all_hyperedges_;

		for (const auto& edge : snapshot) {
			if (edge->isSegment()) continue;

			// Check if any source of this edge is an ancestor of child
			// (including parent itself).
			bool source_is_ancestor = false;
			std::unordered_set<Node*> ancestor_sources = {};
			for (const auto& s : edge->getSources())
				if (parent_and_ancestors.count(s.get())) {
					source_is_ancestor = true;
					ancestor_sources.insert(s.get());
				}
			if (!source_is_ancestor) continue;

			// Find targets that are now redundant.
			std::vector<NodePtr> non_descendants_targets = {};
			bool redundant = false;
			for (const auto& t : edge->getTargets()) {
				if (child_and_descendants.count(t.get())) {
					redundant = true;
				}
				else {
					non_descendants_targets.push_back(t);
				}
			}
			
			if (!redundant) continue;

			// We have to eliminate the sources from the hyperedge and we also have to 
			// create a new hyperedge with the same sources that targets the non redundant 
			// connections that existed before.
			removeSourcesFromHyperedge(edge, ancestor_sources);
			if (!non_descendants_targets.empty()) {
				std::vector<NodePtr> ancestor_sources_vec(ancestor_sources.begin(), ancestor_sources.end());
				if (edge->getLayer() >= 0) {
					// The original edge is short and still valid after source removal, so we can just create a new short edge.
					 createHyperedge(ancestor_sources_vec, non_descendants_targets, edge->getLayer());
				}
				else {
					const auto& new_edge = createHyperedge(ancestor_sources_vec, non_descendants_targets, -1);
					// It may be short or long depending on the layers of the sources and targets.
					int k = edgeIsShort(new_edge);
					if (k < 0) {
						splitLongEdge(new_edge, ancestor_sources_vec, non_descendants_targets);
					}
					else {
						addHyperedgeToLayer(k, new_edge);
					}

				}
			}
		}
	}


	void Hypergraph::applyRelocationAndPropagate(const NodePtr& node, int new_layer) {

		// ====================================================================
		// Phase 1: Move the node in LayerData and fix all real edges that
		//          target it, since they may now be long.
		// ====================================================================
		removeNodeFromLayer(node->getLayer(), node);
		addNodeToLayer(new_layer, -1, node);

		std::unordered_set<Hyperedge*> incoming_set;
		for (const auto& edge : all_hyperedges_) {
			if (edge->isSegment()) continue;
			if (edge->containsTarget(node)) incoming_set.insert(edge.get());
		}

		dissolveSegments(incoming_set);
		for (const auto& edge : incoming_set) {
			splitLongEdge(edge->shared_from_this(), edge->getSources(), edge->getTargets());
		}

		// ====================================================================
		// Phase 2: Collect the affected nodes and recalculate their layers.
		//			This is done in a bottom-up manner, layer by layer, to 
		//			ensure that when we recalculate the depth of a node,
		//			all its parents have already been recalculated and are in 
		//			their correct layers. 
		// ====================================================================
		std::unordered_set<Node*> affected_nodes;
		std::map<int, std::unordered_set<Node*>> affected_nodes_by_layer;
		affected_nodes = node->getAllDescendants();
		if (affected_nodes.empty()) return;

		for (Node* n : affected_nodes)
			affected_nodes_by_layer[n->getLayer()].insert(n);

		for (auto& [layer, nodes] : affected_nodes_by_layer) {			
			for (auto it = nodes.begin(); it != nodes.end();) {
				// Recompute its new depth and check if it varies
				auto parents = (*it)->getParents();
				int new_depth = parents.empty() ? 0 : (*std::max_element(parents.begin(), parents.end(),
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

			// It does not matter if some nodes do not belong to this layer, they will be ignored by the function.
			// In addition, the performance is not affected since the lookup is O(1) independently of the number of nodes.
			removeNodeFromLayer(layer, nodes);
		}

		// After Phase 2, affected_nodes contains only nodes whose layer
		// actually changed. Nodes that were descendants but did not need
		// to move were erased and their edges need not be rebuilt.

		// ====================================================================
		// Phase 3: Collect the affected hyperedges, clean up segments
		//			and split them again if necessary.
		// ====================================================================
		std::unordered_set<Hyperedge*> affected_edges;
		for (const auto& edge : all_hyperedges_) {
			if (edge->isSegment()) continue;
			bool sourced_here = false;
			bool targeted_here = false;
			for (const auto& s : edge->getSources())
				if (affected_nodes.count(s.get())) { sourced_here = true; break; }
			for (const auto& t : edge->getTargets())
				if (affected_nodes.count(t.get())) { targeted_here = true; break; }
			if (sourced_here || targeted_here)
				affected_edges.insert(edge.get());
		}

		dissolveSegments(affected_edges);
		for (const auto& edge : affected_edges) {
			int k = edgeIsShort(edge->shared_from_this());
			if (k >= 0) {
				// This edge is now short, so it just needs to be relocated
				// to the correct layer if it is not already there.
				if (k != edge->getLayer()) {
					removeHyperedgeFromLayer(edge->getLayer(), edge->shared_from_this());
					addHyperedgeToLayer(k, edge->shared_from_this());
				}
			}
			else {
				// This edge is a long edge, so it needs to be resplitted.
				splitLongEdge(edge->shared_from_this(), edge->getSources(), edge->getTargets());
			}
		}

		// ====================================================================
		// Cleanup: remove empty layers left behind after relocation.
		// ====================================================================
		std::vector<int> empty;
		for (const auto& [l, data] : layers_)
			if (data.nodes.empty() && data.outgoing_edges.empty())
				empty.push_back(l);
		for (int l : empty)
			layers_.erase(l);
	}

	static bool isParentInAncestorsHelper(const NodePtr& node, const NodePtr& parent, std::unordered_set<Node*>& visited) {
		// Helper function to check if parent is in the ancestors of a node.
		// Uses memoization (visited set) to avoid re-exploring the same nodes multiple times.
		// Also uses layer information to prune impossible branches.

		int parent_layer = parent->getLayer();

		for (const auto& ancestor : node->getParents()) {
			// Skip if already visited (avoids redundant exploration)
			if (visited.count(ancestor.get()) > 0) {
				continue;
			}

			int ancestor_layer = ancestor->getLayer();

			// If ancestor layer < parent layer, the parent cannot be in this branch.
			// If both layers are the same, it must be the case that both nodes are the same.
			if (ancestor_layer < parent_layer) {
				continue;  // Skip this branch
			}
			else if (ancestor_layer == parent_layer) {
				// Same layer - can only be ancestor if it's the exact same node
				if (ancestor == parent) {
					return true;
				}
				continue;
			}

			// Mark as visited to avoid repetition in future calls
			visited.insert(ancestor.get());

			if (isParentInAncestorsHelper(ancestor, parent, visited)) {
				return true;
			}
		}
		return false;
	}

	bool Hypergraph::parentIsInAncestors(const NodePtr& child, const NodePtr& parent) {
		// Wrapper function that initializes the visited set and calls the helper
		if (!child || !parent) return false;
		std::unordered_set<Node*> visited;
		return isParentInAncestorsHelper(child, parent, visited);
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
	// Hyperedge management
	// ============================================================================
	HyperedgePtr Hypergraph::createHyperedge(const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets, int layer) {
		auto edge = std::make_shared<Hyperedge>(sources, targets);
		all_hyperedges_.push_back(edge);

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
		all_hyperedges_.push_back(edge);

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

	std::vector<HyperedgePtr> Hypergraph::getAllHyperedges() const {
		return all_hyperedges_;
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
	// Private helpers
	// ============================================================================


} // namespace hypergraph_logic
