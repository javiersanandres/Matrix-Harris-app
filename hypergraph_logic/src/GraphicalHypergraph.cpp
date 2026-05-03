#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"

#include <algorithm>

namespace hypergraph_logic {
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
			const double first_y = ;
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

			// The current layer's node row sits one LAYER_GAP below the bottom-most bar.
			layer_layout_[layer_idx] = y_levels.back().y - LAYER_GAP - NODE_HEIGHT / 2.0;

			incoming_edges = layer_data.outgoing_edges;
			nodes_in_prev_layer = layer_data.nodes;
		}
	}

	void GraphicalHypergraph::computeLayout() {
		assignXCoordinates();
		for (const auto& [layer_idx, _] : layers_) {
			orderHyperedges(layer_idx);
		}
		assignPorts();
		assignYCoordinates();
	}



} // namespace hypergraph_logic