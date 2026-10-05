#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"

#include <algorithm>
#include <climits>
#include <unordered_set>

namespace hypergraph_logic {

	GraphicalHypergraph::GraphicalHypergraph(const std::string& name)
		: Hypergraph(name)
		, id_(generateId())
	{
	}

	struct EdgeSpan {
		double xmin = std::numeric_limits<double>::max();
		double xmax = std::numeric_limits<double>::lowest();
	};

	struct YLevel {
		double y;
		std::vector<EdgeSpan> bars; // bars that occupy this y level.
	};

	double GraphicalHypergraph::getLayerHeight(int layer) const {
		auto it = layers_.find(layer);
		if (it == layers_.end()) return 0.0;
		double height = 0.0;
		for (const auto& n : it->second.nodes)
			height = std::max(height, n->getHeight());
		return height;
	}

	void GraphicalHypergraph::assignPortYCoordinates() {
		for (const auto& [layer_idx, layer_data] : layers_) {
			auto layer_it = layer_layout_.find(layer_idx);
			if (layer_it == layer_layout_.end()) continue;
			const double layer_y = layer_it->second;

			for (const auto& node : layer_data.nodes) {
				auto it = node_layout_.find(node.get());
				if (it == node_layout_.end()) continue;
				NodeLayout& nl = it->second;

				// Source ports leave through the bottom half of the shape (more negative y),
				// target ports arrive through the top half (less negative y).
				for (auto& port : nl.source_ports)
					port.y = layer_y - node->getBoundaryHalfHeight(port.x - nl.x);
				for (auto& port : nl.target_ports)
					port.y = layer_y + node->getBoundaryHalfHeight(port.x - nl.x);
			}
		}
	}

	void GraphicalHypergraph::assignYCoordinates() {
		// incoming_edges: the outgoing_edges of the previous layer, i.e. the hyperedges
		// that cross the gap above the current layer being processed.
		// nodes_in_prev_layer: nodes of the previous layer, used to read source_ports.
		// Both start empty; the top layer (layer 0) has no gap above it.
		std::vector<HyperedgePtr> incoming_edges;
		std::vector<NodePtr> nodes_in_prev_layer;

		for (const auto& [layer_idx, layer_data] : layers_) {
			// Every node's centre sits on its layer's y, so the tallest node of a layer
			// decides how far that layer reaches up and down.
			const double half_height = getLayerHeight(layer_idx) / 2.0;
			const double prev_half_height = getLayerHeight(layer_idx - 1) / 2.0;

			if (incoming_edges.empty() || nodes_in_prev_layer.empty()) {
				if (layer_idx == 0) {
					// No incoming edges for the top layer, so just place it at y=0.
					layer_layout_[layer_idx] = 0.0;
				}
				else {
					layer_layout_[layer_idx] = layer_layout_[layer_idx - 1] - prev_half_height - LAYER_GAP - half_height;
				}
				// Carry forward for the next gap.
				incoming_edges = layer_data.outgoing_edges;
				nodes_in_prev_layer = layer_data.nodes;
				continue;
			}

			// Step 1: compute the horizontal span of each edge in incoming_edges.
			// We only insert entries for edges that actually appear in incoming_edges,
			// so the map doubles as the authoritative set.
			std::unordered_map<Hyperedge*, EdgeSpan> spans;
			for (const auto& e : incoming_edges)
				spans.emplace(e.get(), EdgeSpan{});

			for (const auto& node_ptr : nodes_in_prev_layer) {
				if (auto it = node_layout_.find(node_ptr.get()); it != node_layout_.end()) {
					for (const auto& port : it->second.source_ports) {
						if (auto sit = spans.find(port.edge); sit != spans.end()) {
							sit->second.xmin = std::min(sit->second.xmin, port.x);
							sit->second.xmax = std::max(sit->second.xmax, port.x);
						}
					}
				}
			}
			for (const auto& node_ptr : layer_data.nodes) {
				if (auto it = node_layout_.find(node_ptr.get()); it != node_layout_.end()) {
					for (const auto& port : it->second.target_ports) {
						if (auto sit = spans.find(port.edge); sit != spans.end()) {
							sit->second.xmin = std::min(sit->second.xmin, port.x);
							sit->second.xmax = std::max(sit->second.xmax, port.x);
						}
					}
				}
			}

			// Step 2: promote trivial bars (zero-width span) to the front while
			// preserving the relative order within each group.
			std::stable_partition(incoming_edges.begin(), incoming_edges.end(),
				[&spans](const HyperedgePtr& e) {
					const auto& s = spans.at(e.get());
					return s.xmin == s.xmax;
				});

			// Step 3: assign y-coordinates.
			//
			// Coordinate convention: layer 0 is at y=0, layers below have negative y.
			//
			// y_levels is ordered from index 0 (least negative, closest to the upper
			// node row) to back (most negative, furthest from the upper node row and
			// closest to the lower node row).
			// New conflict levels are appended at the back (more negative).
			//
			// The first available slot is just below the upper layer's tallest node:
			//   first_y = layer_layout_[layer_idx-1] - H(layer_idx-1)/2 - LAYER_GAP
			//
			// Edges earlier in incoming_edges were placed higher (less negative y) by
			// the ordering step.  For each non-trivial edge we walk y_levels from the
			// back (most negative) toward index 0 (least negative) and take the first
			// (least negative) slot that has no x-overlap.  If every slot conflicts we
			// push a new level one HORIZONTAL_SEP further negative.
			const double first_y = layer_layout_[layer_idx - 1] - prev_half_height - LAYER_GAP;
			std::vector<YLevel> y_levels{ { first_y, {} } };

			for (const auto& edge : incoming_edges) {
				const EdgeSpan& s = spans.at(edge.get());

				if (s.xmin == s.xmax) {
					// Trivial: no horizontal bar needed. Place it flush against the
					// bottom of the upper layer's tallest node (least negative possible).
					// The bar is never drawn: the edge is a single vertical segment
					// between its source and target ports.
					edge_layout_[edge.get()] = layer_layout_[layer_idx - 1] - prev_half_height;
					continue;
				}

				// Walk from the back (most negative) toward front (least negative),
				// recording the least-negative conflict-free slot found so far.
				int best = -1; // -1 means no free slot found yet

				for (int i = static_cast<int>(y_levels.size()) - 1; i >= 0; --i) {
					bool conflict = false;
					// Check for x-overlap with any bar already in this slot but respecting
					// a MIN_VERTICAL_SEP distance, i.e., the span for s becomes:
					//      [s.xmin - MIN_VERTICAL_SEP/2, s.xmax + MIN_VERTICAL_SEP/2]
					// Note: [a1, a2] and [b1, b2] overlap iff a1 <= b2 and b1 <= a2.
					for (const auto& bar : y_levels[i].bars) {
						if ((s.xmin - MIN_VERTICAL_SEP/2.0 <= bar.xmax) && 
							(bar.xmin <= s.xmax + MIN_VERTICAL_SEP/2.0)) {
							conflict = true;
							break;
						}
					}
					if (!conflict) {
						best = i; // Free — keep going toward less negative.
					}
					else {
						break; // Conflict here; everything above is locked. Stop.
					}
				}

				if (best == -1) {
					// Every existing slot conflicted: push a new level further negative.
					double new_y = y_levels.back().y - HORIZONTAL_SEP;
					y_levels.push_back({ new_y, { s } });
					edge_layout_[edge.get()] = new_y;
				}
				else {
					y_levels[best].bars.push_back(s);
					edge_layout_[edge.get()] = y_levels[best].y;
				}
			}

			if (y_levels[0].bars.empty()){
				// No bars at the first level, which means that all hyperedges are trivial
				// and therfore, the next layer_layout does not need to be pushed down by
				// the default gap.
				layer_layout_[layer_idx] = layer_layout_[layer_idx - 1] - prev_half_height - LAYER_GAP - half_height;
			}
			else {
				// The current layer's tallest node top sits one LAYER_GAP below the bottom-most bar.
				layer_layout_[layer_idx] = y_levels.back().y - LAYER_GAP - half_height;
			}

			incoming_edges = layer_data.outgoing_edges;
			nodes_in_prev_layer = layer_data.nodes;
		}

		assignPortYCoordinates();
	}

	void GraphicalHypergraph::computeLayout(const std::set<int>& mip_layers) {
		node_layout_.clear();
		edge_layout_.clear();
		layer_layout_.clear();

		assignXCoordinates();
		for (const auto& layer : mip_layers) {
			orderHyperedges(layer, static_cast<int>(mip_layers.size()));
		}
		assignPorts();
		assignYCoordinates();
		refreshUncertainPorts();
	}

	void GraphicalHypergraph::computeLayout() {
		node_layout_.clear();
		edge_layout_.clear();
		layer_layout_.clear();

		assignXCoordinates();
		for (const auto& [layer_idx, _] : layers_) {
			orderHyperedges(layer_idx, static_cast<int>(layers_.size()));
		}
		assignPorts();
		assignYCoordinates();
		refreshUncertainPorts();
	}

	void GraphicalHypergraph::refreshUncertainPorts() {
		for (auto& [node, layout] : node_layout_) {
			const bool real = !node->isDummy();
			for (auto& port : layout.source_ports)
				port.uncertain = real && port.edge && port.edge->isSourceUncertain(node);
			for (auto& port : layout.target_ports)
				port.uncertain = real && port.edge && port.edge->isTargetUncertain(node);
		}
	}

	void GraphicalHypergraph::setHyperedgeOrder(int layer, const std::vector<HyperedgePtr>& order) {
		auto it = layers_.find(layer);
		if (it == layers_.end())
			throw std::invalid_argument("setHyperedgeOrder: la capa no existe.");
		auto& current = it->second.outgoing_edges;
		std::unordered_set<Hyperedge*> remaining;
		for (const auto& e : current) remaining.insert(e.get());
		if (order.size() != current.size())
			throw std::invalid_argument("setHyperedgeOrder: el orden no contiene las mismas conexiones.");
		for (const auto& e : order)
			if (!e || remaining.erase(e.get()) == 0)
				throw std::invalid_argument("setHyperedgeOrder: el orden no contiene las mismas conexiones.");
		current = order;
	}

	void GraphicalHypergraph::relocateNodeInLayer(const NodePtr& node, double new_x_coordinate, std::set<int>* out_altered_layers) {
		LayerData& layer_data = layers_[node->getLayer()];
		bool minimize_crossings = false;
		bool pos_changed = false;
		bool no_children = node->getChildren().empty();
		double current_x = node_layout_[node.get()].x;

		// Find the iterator to the node being moved once, used in both branches.
		auto node_it = std::find(layer_data.nodes.begin(), layer_data.nodes.end(), node);

		if (new_x_coordinate < current_x) {
			// Moving left: find the rightmost neighbour to the left that the node has crossed.
			for (auto it = layer_data.nodes.rbegin(); it != layer_data.nodes.rend(); ++it) {
				if (node_layout_[it->get()].x >= current_x) {
					// Skip nodes that are currently to the right of the node being moved.
					continue;
				}
				if (node_layout_[it->get()].x > new_x_coordinate) {
					if (!no_children && !(*it)->getChildren().empty()) {
						// Both have children, so we will need to run the crossing minimisation
						// algorithm after the swap, to ensure the new layout is as good as possible.
						minimize_crossings = true;
					}
					auto neighbour_it = it.base() - 1;
					std::iter_swap(neighbour_it, node_it);
					node_it = neighbour_it;
					pos_changed = true;
				}
			}
		}
		else if (new_x_coordinate > current_x) {
			// Moving right: find the leftmost neighbour to the right that the node has crossed.
			for (auto it = layer_data.nodes.begin(); it != layer_data.nodes.end(); ++it) {
				if (node_layout_[it->get()].x <= current_x) {
					// Skip nodes that are currently to the left of the node being moved.
					continue;
				}
				if (node_layout_[it->get()].x < new_x_coordinate) {
					if (!no_children && !(*it)->getChildren().empty()) {
						// Both have children, so we will need to run the crossing minimisation
						// algorithm after the swap, to ensure the new layout is as good as possible.
						minimize_crossings = true;
					}
					std::iter_swap(it, node_it);
					node_it = it;
					pos_changed = true;
				}
			}
		}

		if (pos_changed) {
			if (minimize_crossings) {
				// Avoid being too aggressive with crossing minimisation, since the user
				// is making a manual adjustment and may not want the layout to change too much.
				// The node's descendants are re-placed in the layers below first, so that its
				// subgraph follows the swap instead of pulling the nodes back.
				Hypergraph::minimizeCrossings(3, node->getLayer() + 1, -1, {}, node.get());
				if (out_altered_layers) {
					// We need to add those layers where the span of hyperedges could have changed.
					// Since the node moved and many others all the way down, we need to collect all
					// layers starting from the previous to the node's.
					for (const auto& [layer_idx, _] : layers_) {
						if (layer_idx >= node->getLayer() - 1) {
							out_altered_layers->insert(layer_idx);
						}
					}
				}
			}
			else if (out_altered_layers) {
				// No need to minimmize crossings means that either current node or those it
				// swapped with did not have any children, which means no hyperedges colliding
				// in the node's layer and therefore no MIP should be solved. The previous layer
				// could have suffered some changes and a MIP in that one should be solved.
				if (node->getLayer() > 0) out_altered_layers->insert(node->getLayer()-1);
			}
		}
		else {
			throw std::invalid_argument("La nueva posición no cambia el orden de la caja en su nivel.");
		}
	}

	void GraphicalHypergraph::relocateNodeToLayer(const NodePtr& node, double new_y_coordinate, std::set<int>* out_altered_layers) {
		if (layer_layout_.empty()) {
			throw std::runtime_error(
				"relocateNodeToLayer(node, y) requires a computed layout; call computeLayout() first.");
		}
		Hypergraph::relocateNodeToLayer(node, layerForY(new_y_coordinate), out_altered_layers);
	}

	int GraphicalHypergraph::layerForY(double new_y_coordinate) const {
		if (layer_layout_.empty()) return 0;

		// Collect layer indices in ascending order. Layer 0 is shallowest (y = 0);
		// higher indices are deeper (more negative y).
		std::vector<int> layer_indices;
		layer_indices.reserve(layer_layout_.size());
		for (const auto& [idx, _] : layer_layout_) layer_indices.push_back(idx);
		std::sort(layer_indices.begin(), layer_indices.end());

		const int shallowest = layer_indices.front();
		const int deepest = layer_indices.back();

		int desired_layer = -2; // sentinel
		bool found = false;

		for (size_t i = 0; i < layer_indices.size(); ++i) {
			const int L = layer_indices[i];
			const double hL = layer_layout_.at(L);

			// Lower bound involves the *next* (deeper) layer.
			double lower_bound;
			if (i + 1 < layer_indices.size()) {
				const double h_next = layer_layout_.at(layer_indices[i + 1]);
				lower_bound = (h_next + hL) / 2.0;
			}
			else {
				lower_bound = hL - (getLayerHeight(L) + LAYER_GAP) / 2.0;
			}

			// Upper bound involves the *previous* (shallower) layer.
			double upper_bound;
			if (i > 0) {
				const double h_prev = layer_layout_.at(layer_indices[i - 1]);
				upper_bound = (h_prev + hL) / 2.0;
			}
			else {
				upper_bound = hL + (getLayerHeight(L) + LAYER_GAP) / 2.0;
			}

			if (new_y_coordinate >= lower_bound && new_y_coordinate <= upper_bound) {
				desired_layer = L;
				found = true;
				break;
			}
		}

		if (!found) {
			// Coordinate lies outside every existing layer's span, which reaches
			// (H + LAYER_GAP) / 2 past the shallowest and the deepest layer alike:
			// above it asks for a new shallowest layer, below it for a new deepest.
			desired_layer = new_y_coordinate > layer_layout_.at(shallowest) ? -1 : deepest + 1;
		}

		return desired_layer;
	}

	const NodeLayout* GraphicalHypergraph::validLayout(const Node* node) const {
		auto it = node_layout_.find(const_cast<Node*>(node));
		if (it == node_layout_.end()) return nullptr;
		const auto owner = it->second.node.lock();
		return owner.get() == node ? &it->second : nullptr;
	}

	std::optional<double> GraphicalHypergraph::guessX(
		const Node* node, const std::unordered_map<const Node*, double>& guessed) const
	{
		auto known = [&](const Node* n) -> std::optional<double> {
			if (const NodeLayout* layout = validLayout(n)) return layout->x;
			if (auto it = guessed.find(n); it != guessed.end()) return it->second;
			return std::nullopt;
		};
		// Average known x of a group of neighbours, and how many had one.
		auto average = [&](const std::vector<NodePtr>& nodes) -> std::optional<double> {
			double sum = 0.0;
			int count = 0;
			for (const auto& n : nodes)
				if (auto x = known(n.get())) { sum += *x; ++count; }
			if (count == 0) return std::nullopt;
			return sum / count;
		};

		if (!node->isDummy()) {
			auto parents = node->getParents();
			auto children = node->getChildren();
			auto up = average(parents), down = average(children);
			if (up && down) return (*up + *down) / 2.0;
			return up ? up : down;
		}

		// A dummy: find the nearest known ends of the chain it belongs to.
		std::optional<double> top_x, bottom_x;
		int top_layer = 0, bottom_layer = 0;
		for (const Node* up = node;;) {
			auto parents = up->getParents();
			if (parents.empty()) break; // It's impossible for a dummy to not have any parents but just in case.
			if (auto x = average(parents)) { top_x = x; top_layer = parents.front()->getLayer(); break; }
			if (parents.size() != 1 || !parents.front()->isDummy()) break;
			up = parents.front().get();
		}
		for (const Node* down = node;;) {
			auto children = down->getChildren();
			if (children.empty()) break;
			if (auto x = average(children)) { bottom_x = x; bottom_layer = children.front()->getLayer(); break; }
			if (children.size() != 1 || !children.front()->isDummy()) break;
			down = children.front().get();
		}

		if (top_x && bottom_x && bottom_layer != top_layer) {
			const double t = static_cast<double>(node->getLayer() - top_layer) / (bottom_layer - top_layer);
			return *top_x + t * (*bottom_x - *top_x);
		}
		return top_x ? top_x : bottom_x;
	}

	void GraphicalHypergraph::placeUnpositionedNodes(int first_layer, int last_layer) {
		std::unordered_map<const Node*, double> guessed;

		for (auto& [layer, data] : layers_) {
			if (layer < first_layer) continue;
			if (layer > last_layer) break;

			auto& nodes = data.nodes;
			if (std::all_of(nodes.begin(), nodes.end(), [&](const NodePtr& n) { return validLayout(n.get()); }))
				continue;

			// Leftmost parent position in the layer above, as it stands now (INT_MAX: root).
			std::unordered_map<const Node*, int> upper_pos;
			if (auto upper = layers_.find(layer - 1); upper != layers_.end())
				for (int i = 0; i < static_cast<int>(upper->second.nodes.size()); i++)
					upper_pos[upper->second.nodes[i].get()] = i;
			auto leftmostParent = [&](const Node* n) {
				int key = INT_MAX;
				for (const auto& parent : n->getParents())
					if (auto it = upper_pos.find(parent.get()); it != upper_pos.end())
						key = std::min(key, it->second);
				return key;
			};
			auto xOf = [&](const Node* n) -> std::optional<double> {
				if (const NodeLayout* layout = validLayout(n)) return layout->x;
				if (auto it = guessed.find(n); it != guessed.end()) return it->second;
				return std::nullopt;
			};

			// Placed nodes keep their relative order; the rest are inserted among them.
			std::vector<NodePtr> order;
			std::vector<NodePtr> by_x, by_parent;
			for (const auto& n : nodes) {
				if (validLayout(n.get())) { order.push_back(n); continue; }
				if (auto x = guessX(n.get(), guessed)) { guessed[n.get()] = *x; by_x.push_back(n); }
				else by_parent.push_back(n);
			}

			// 1. Guessed x: before the first node further right.
			for (const auto& n : by_x) {
				const double x = guessed.at(n.get());
				auto at = std::find_if(order.begin(), order.end(), [&](const NodePtr& other) {
					auto other_x = xOf(other.get());
					return other_x && *other_x > x;
				});
				order.insert(at, n);
			}

			// 2. Nothing to go on: under the leftmost parent, or after the left-hand
			//    neighbour for a root.
			for (const auto& n : by_parent) {
				const int key = leftmostParent(n.get());
				auto at = order.begin();
				if (key == INT_MAX) {
					// Right after the nearest node on its left that is already in place
					// (at the front when there is none).
					auto original = std::find(nodes.begin(), nodes.end(), n);
					while (original != nodes.begin()) {
						--original;
						auto neighbour = std::find(order.begin(), order.end(), *original);
						if (neighbour != order.end()) { at = neighbour + 1; break; }
					}
				}
				else {
					at = std::find_if(order.begin(), order.end(), [&](const NodePtr& other) {
						const int other_key = leftmostParent(other.get());
						return other_key != INT_MAX && other_key > key;
					});
				}
				order.insert(at, n);
			}

			nodes = std::move(order);
		}
	}

	int GraphicalHypergraph::choosePositionForRelocatedNode(int new_layer, const NodePtr& node) const {
		if (!node) return -1;

		std::optional<double> x;
		if (const NodeLayout* layout = validLayout(node.get())) x = layout->x;
		else x = guessX(node.get(), {});
		if (!x) return -1;                              // nothing to go on

		auto layer_it = layers_.find(new_layer);
		if (layer_it == layers_.end()) return -1;       // brand new layer

		const auto& existing_nodes = layer_it->second.nodes;
		for (size_t i = 0; i < existing_nodes.size(); ++i) {
			const NodeLayout* sibling = validLayout(existing_nodes[i].get());
			if (!sibling) continue;
			if (sibling->x > *x) return static_cast<int>(i);
		}
		return -1;
	}


} // namespace hypergraph_logic