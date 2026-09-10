#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"

#include <algorithm>

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

	void GraphicalHypergraph::assignYCoordinates() {
		// incoming_edges: the outgoing_edges of the previous layer, i.e. the hyperedges
		// that cross the gap above the current layer being processed.
		// nodes_in_prev_layer: nodes of the previous layer, used to read source_ports.
		// Both start empty; the top layer (layer 0) has no gap above it.
		std::vector<HyperedgePtr> incoming_edges;
		std::vector<NodePtr> nodes_in_prev_layer;

		for (const auto& [layer_idx, layer_data] : layers_) {
			if (incoming_edges.empty() || nodes_in_prev_layer.empty()) {
				if (layer_idx == 0) {
					// No incoming edges for the top layer, so just place it at y=0.
					layer_layout_[layer_idx] = 0.0;
				}
				else {
					// This should never happen for layer_idx > 0, but just in case,
					// we place it below the previous layer with a gap.
					layer_layout_[layer_idx] = layer_layout_[layer_idx - 1] - LAYER_GAP - NODE_HEIGHT;
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
			// The first available slot is just below the upper layer's node boxes:
			//   first_y = layer_layout_[layer_idx-1] - NODE_HEIGHT/2 - LAYER_GAP
			//
			// Edges earlier in incoming_edges were placed higher (less negative y) by
			// the ordering step.  For each non-trivial edge we walk y_levels from the
			// back (most negative) toward index 0 (least negative) and take the first
			// (least negative) slot that has no x-overlap.  If every slot conflicts we
			// push a new level one HORIZONTAL_SEP further negative.
			const double first_y = layer_layout_[layer_idx - 1] - NODE_HEIGHT / 2 - LAYER_GAP;
			std::vector<YLevel> y_levels{ { first_y, {} } };

			for (const auto& edge : incoming_edges) {
				const EdgeSpan& s = spans.at(edge.get());

				if (s.xmin == s.xmax) {
					// Trivial: no horizontal bar needed. Place it flush against the
					// bottom of the upper node boxes (least negative possible).
					edge_layout_[edge.get()] = layer_layout_[layer_idx - 1] - NODE_HEIGHT / 2.0;
					continue;
				}

				// Walk from the back (most negative) toward front (least negative),
				// recording the least-negative conflict-free slot found so far.
				int best = -1; // -1 means no free slot found yet

				for (int i = static_cast<int>(y_levels.size()) - 1; i >= 0; --i) {
					bool conflict = false;
					// Check for x-overlap with any bar already in this slot. 
					// Note: [a1, a2] and [b1, b2] overlap iff a1 <= b2 and b1 <= a2.
					for (const auto& bar : y_levels[i].bars) {
						if ((s.xmin <= bar.xmax) && (bar.xmin <= s.xmax)) {
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
				layer_layout_[layer_idx] = layer_layout_[layer_idx - 1] - LAYER_GAP - NODE_HEIGHT;
			}
			else {
				// The current layer's node row sits one LAYER_GAP below the bottom-most bar.
				layer_layout_[layer_idx] = y_levels.back().y - LAYER_GAP - NODE_HEIGHT / 2.0;
			}

			incoming_edges = layer_data.outgoing_edges;
			nodes_in_prev_layer = layer_data.nodes;
		}
	}

	void GraphicalHypergraph::computeLayout(const std::set<int>& mip_layers) {
		node_layout_.clear();
		edge_layout_.clear();
		layer_layout_.clear();

		assignXCoordinates();
		for (const auto& layer : mip_layers) {
			orderHyperedges(layer);
		}
		assignPorts();
		assignYCoordinates();
	}

	void GraphicalHypergraph::computeLayout() {
		node_layout_.clear();
		edge_layout_.clear();
		layer_layout_.clear();

		assignXCoordinates();
		for (const auto& [layer_idx, _] : layers_) {
			orderHyperedges(layer_idx);
		}
		assignPorts();
		assignYCoordinates();
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
				Hypergraph::minimizeCrossings(3, node->getLayer() + 1);
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
			throw std::invalid_argument("New x coordinate does not change the node's position in the layer.");
		}
	}

	void GraphicalHypergraph::relocateNodeToLayer(const NodePtr& node, double new_y_coordinate, std::set<int>* out_altered_layers) {
		if (layer_layout_.empty()) {
			throw std::runtime_error(
				"relocateNodeToLayer(node, y) requires a computed layout; call computeLayout() first.");
		}

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
				lower_bound = hL - (NODE_HEIGHT + LAYER_GAP) / 2.0;
			}

			// Upper bound involves the *previous* (shallower) layer.
			double upper_bound;
			if (i > 0) {
				const double h_prev = layer_layout_.at(layer_indices[i - 1]);
				upper_bound = (h_prev + hL) / 2.0;
			}
			else {
				upper_bound = hL + (NODE_HEIGHT + LAYER_GAP) / 2.0;
			}

			if (new_y_coordinate >= lower_bound && new_y_coordinate <= upper_bound) {
				desired_layer = L;
				found = true;
				break;
			}
		}

		if (!found) {
			// Coordinate lies outside every existing layer's span: either above the
			// shallowest layer (new shallowest layer) or below the deepest layer
			// (new deepest layer).
			const double h_shallowest = layer_layout_.at(shallowest);
			const double shallowest_upper = h_shallowest + NODE_HEIGHT / 2.0 + LAYER_GAP;

			if (new_y_coordinate > shallowest_upper) {
				desired_layer = -1;          // brand-new shallowest layer
			}
			else {
				desired_layer = deepest + 1; // brand-new deepest layer
			}
		}

		Hypergraph::relocateNodeToLayer(node, desired_layer, out_altered_layers);
	}

	int GraphicalHypergraph::choosePositionForRelocatedNode(int new_layer, const NodePtr& node) const {
		if (!node) return -1;

		auto node_it = node_layout_.find(node.get());
		if (node_it == node_layout_.end()) return -1;   // never laid out before
		const double x = node_it->second.x;

		auto layer_it = layers_.find(new_layer);
		if (layer_it == layers_.end()) return -1;       // brand new layer

		const auto& existing_nodes = layer_it->second.nodes;
		for (size_t i = 0; i < existing_nodes.size(); ++i) {
			auto sibling_it = node_layout_.find(existing_nodes[i].get());
			if (sibling_it == node_layout_.end()) continue;
			if (sibling_it->second.x > x) return static_cast<int>(i);
		}
		return -1;
	}


} // namespace hypergraph_logic