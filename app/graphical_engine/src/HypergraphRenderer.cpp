#include "HypergraphRenderer.h"

#include <QGraphicsSimpleTextItem>
#include <QPen>
#include <QBrush>

#include <algorithm>
#include <cmath>
#include <limits>

namespace graphical_engine {

	// ============================================================================
	// render
	// ============================================================================
	void HypergraphRenderer::render(
		const GraphicalHypergraph& graph,
		QGraphicsScene* scene,
		std::unordered_map<Node*, QGraphicsRectItem*>& node_items,
		std::unordered_map<Hyperedge*, QGraphicsPathItem*>& edge_items)
	{
		scene->clear();
		node_items.clear();
		edge_items.clear();

		if (graph.getLayers().empty()) return;

		const auto& node_layout = graph.getNodeLayout();
		const auto& edge_layout = graph.getEdgeLayout();
		const auto& layer_layout = graph.getLayerLayout();

		// Accumulate one QPainterPath per original edge across all its segments.
		std::unordered_map<Hyperedge*, QPainterPath> edge_paths;
		for (const auto& e : graph.getAllHyperedges())
			if (!e->isSegment())
				edge_paths[e.get()] = QPainterPath();

		// Helper: resolve a segment to its original edge pointer.
		auto get_original = [&](const HyperedgePtr& e) -> Hyperedge* {
			if (!e->isSegment()) return e.get();
			auto locked = e->getOrigin().lock();
			return locked ? locked.get() : nullptr;
			};

		std::vector<HyperedgePtr> incoming_edges;
		std::vector<NodePtr>      nodes_in_prev_layer;

		for (const auto& [layer_idx, layer_data] : graph.getLayers()) {

			double layer_y = layer_layout.at(layer_idx);

			// ── Step 1: draw real node boxes for this layer ───────────────────────
			for (const auto& node : layer_data.nodes) {
				if (node->isDummy()) continue;
				const NodeLayout& nl = node_layout.at(node.get());
				drawNodeBox(node.get(), nl, layer_y, scene, node_items);
			}

			// ── Step 2: initialise on first layer, then process the gap above ─────
			if (incoming_edges.empty() || nodes_in_prev_layer.empty()) {
				incoming_edges = layer_data.outgoing_edges;
				nodes_in_prev_layer = layer_data.nodes;
				continue;
			}

			double layer_y_prev = layer_layout.at(layer_idx - 1);
			const double trivial_y = layer_y_prev - NODE_HEIGHT / 2.0;

			// Stable-partition: trivial edges (no horizontal bar, bar flush against
			// upper node boxes) move to the front.
			std::stable_partition(incoming_edges.begin(), incoming_edges.end(),
				[&](const HyperedgePtr& e) {
					Hyperedge* orig = get_original(e);
					if (!orig) return false;
					auto it = edge_layout.find(orig);
					return it != edge_layout.end() &&
						std::abs(it->second - trivial_y) < 1e-9;
				});

			// Vertical occupancy map — reset for each gap.
			std::map<double, std::vector<VerticalRange>> vertical_occupancy;

			// ── Step 3: process each segment edge in order ────────────────────────
			for (const auto& seg : incoming_edges) {
				Hyperedge* orig = get_original(seg);
				if (!orig) continue;

				auto it_bar = edge_layout.find(orig);
				double bar_y = (it_bar != edge_layout.end()) ? it_bar->second : trivial_y;
				bool is_trivial = (std::abs(bar_y - trivial_y) < 1e-9);

				std::vector<PortInfo> src_ports, tgt_ports;
				double x_min = std::numeric_limits<double>::max();
				double x_max = std::numeric_limits<double>::lowest();

				buildPortMaps(seg, node_layout, src_ports, tgt_ports, x_min, x_max);

				QPainterPath& path = edge_paths[orig];

				drawVerticalSegments(
					src_ports, tgt_ports,
					bar_y, layer_y_prev, layer_y,
					is_trivial,
					node_layout, vertical_occupancy, path);

				if (!is_trivial && x_min < x_max)
					drawHorizontalBar(x_min, x_max, bar_y, vertical_occupancy, path);
			}

			incoming_edges = layer_data.outgoing_edges;
			nodes_in_prev_layer = layer_data.nodes;
		}

		// ── Step 4: commit all edge paths to the scene ────────────────────────────
		QPen edge_pen(Qt::black, 1.5);
		for (auto& [raw, path] : edge_paths) {
			auto* item = scene->addPath(path, edge_pen);
			edge_items[raw] = item;
		}
	}

	// ============================================================================
	// drawNodeBox
	// ============================================================================
	QGraphicsRectItem* HypergraphRenderer::drawNodeBox(
		Node* node,
		const NodeLayout& layout,
		double                                          layer_y,
		QGraphicsScene* scene,
		std::unordered_map<Node*, QGraphicsRectItem*>& node_items)
	{
		// Negate layer_y to convert from layout convention (layer 0 at y=0,
		// deeper layers at negative y) to Qt convention (y increases downward).
		double cx = layout.x;
		double cy = -layer_y;
		double left = cx - NODE_WIDTH / 2.0;
		double top = cy - NODE_HEIGHT / 2.0;

		auto* rect = scene->addRect(
			left, top, NODE_WIDTH, NODE_HEIGHT,
			QPen(Qt::black, 1.5),
			QBrush(Qt::white));

		// Label centred inside the box.
		auto* label = new QGraphicsSimpleTextItem(
			QString::fromStdString(node->getName()), rect);
		QRectF lb = label->boundingRect();
		label->setPos(
			left + (NODE_WIDTH - lb.width()) / 2.0,
			top + (NODE_HEIGHT - lb.height()) / 2.0);

		node_items[node] = rect;
		return rect;
	}

	// ============================================================================
	// buildPortMaps
	// ============================================================================
	void HypergraphRenderer::buildPortMaps(
		const HyperedgePtr& segment,
		const std::unordered_map<Node*, NodeLayout>& node_layout,
		std::vector<PortInfo>& src_ports,
		std::vector<PortInfo>& tgt_ports,
		double& x_min,
		double& x_max)
	{
		// Source ports: for each source node of this segment, find the port entry
		// whose edge pointer matches this segment.
		for (const auto& src_node : segment->getSources()) {
			auto it = node_layout.find(src_node.get());
			if (it == node_layout.end()) continue;
			for (const auto& port : it->second.source_ports) {
				if (port.edge == segment.get()) {
					src_ports.push_back({ port.x, src_node.get() });
					x_min = std::min(x_min, port.x);
					x_max = std::max(x_max, port.x);
					break;
				}
			}
		}

		// Target ports: same logic on the target side.
		for (const auto& tgt_node : segment->getTargets()) {
			auto it = node_layout.find(tgt_node.get());
			if (it == node_layout.end()) continue;
			for (const auto& port : it->second.target_ports) {
				if (port.edge == segment.get()) {
					tgt_ports.push_back({ port.x, tgt_node.get() });
					x_min = std::min(x_min, port.x);
					x_max = std::max(x_max, port.x);
					break;
				}
			}
		}
	}

	// ============================================================================
	// drawVerticalSegments
	// ============================================================================
	void HypergraphRenderer::drawVerticalSegments(
		const std::vector<PortInfo>& src_ports,
		const std::vector<PortInfo>& tgt_ports,
		double                                        bar_y,
		double                                        layer_y_prev,
		double                                        layer_y,
		bool                                          is_trivial,
		const std::unordered_map<Node*, NodeLayout>& node_layout,
		std::map<double, std::vector<VerticalRange>>& vertical_occupancy,
		QPainterPath& path)
	{
		// For trivial edges the vertical range covers the full gap regardless of
		// node type, because any hyperedge bar in this gap could conflict with it.
		if (is_trivial) {
			for (const auto& pi : src_ports) {
				double y_shallow = layer_y_prev - NODE_HEIGHT / 2.0;
				double y_deep = layer_y + NODE_HEIGHT / 2.0;
				// In layout coords: y_shallow > y_deep (less negative is shallower).
				vertical_occupancy[pi.x].push_back({ y_deep, y_shallow });

				// Draw the straight vertical segment (source to target).
				// The actual endpoints depend on real vs dummy but for trivial
				// edges the bar_y == y_shallow so the full range is correct.
				path.moveTo(pi.x, -y_shallow);
				path.lineTo(pi.x, -(layer_y + NODE_HEIGHT / 2.0));
			}
			return;
		}

		// ── Source ports ──────────────────────────────────────────────────────────
		for (const auto& pi : src_ports) {
			double x = pi.x;
			bool   is_dummy = pi.generating_node->isDummy();

			// Top of vertical segment (at the source node).
			double y_top = is_dummy ? layer_y_prev
				: (layer_y_prev - NODE_HEIGHT / 2.0);
			// Bottom of vertical segment (at the horizontal bar).
			double y_bot = bar_y;

			// Draw segment (negate y for Qt).
			path.moveTo(x, -y_top);
			path.lineTo(x, -y_bot);

			// Record occupancy (y_bot < y_top in logical coords).
			vertical_occupancy[x].push_back({ y_bot, y_top });

			// Horizontal jog for dummy source nodes.
			// If the dummy's own target port x disagrees with this source port x,
			// draw a jog at y = layer_y_prev connecting the two x coordinates.
			if (is_dummy) {
				auto layout_it = node_layout.find(pi.generating_node);
				if (layout_it != node_layout.end()) {
					const NodeLayout& dl = layout_it->second;
					// A dummy in a chain has exactly one target port (pointing upward
					// to the previous segment's bar or source node).
					if (!dl.target_ports.empty()) {
						double tgt_x = dl.target_ports.front().x;
						if (std::abs(tgt_x - x) > 1e-9) {
							double jog_x_min = std::min(tgt_x, x);
							double jog_x_max = std::max(tgt_x, x);
							path.moveTo(jog_x_min, -layer_y_prev);
							path.lineTo(jog_x_max, -layer_y_prev);
						}
					}
				}
			}
		}

		// ── Target ports ──────────────────────────────────────────────────────────
		for (const auto& pi : tgt_ports) {
			double x = pi.x;
			bool   is_dummy = pi.generating_node->isDummy();

			// Top of vertical segment (at the horizontal bar).
			double y_top = bar_y;
			// Bottom of vertical segment (at the target node).
			double y_bot = is_dummy ? layer_y
				: (layer_y + NODE_HEIGHT / 2.0);

			// Draw segment.
			path.moveTo(x, -y_top);
			path.lineTo(x, -y_bot);

			// Record occupancy.
			vertical_occupancy[x].push_back({ y_bot, y_top });

			// No horizontal jog for dummy target ports (as specified).
		}
	}

	// ============================================================================
	// drawHorizontalBar
	// ============================================================================
	void HypergraphRenderer::drawHorizontalBar(
		double                                               x_min,
		double                                               x_max,
		double                                               bar_y,
		const std::map<double, std::vector<VerticalRange>>& vertical_occupancy,
		QPainterPath& path)
	{
		if (x_min >= x_max) return;

		// Collect all x coordinates strictly between x_min and x_max where
		// bar_y is strictly inside one of the recorded VerticalRanges.
		std::vector<double> hops;
		for (const auto& [x, ranges] : vertical_occupancy) {
			if (x <= x_min || x >= x_max) continue;
			for (const auto& r : ranges) {
				// r.y_min is the more negative (deeper) end.
				// r.y_max is the less negative (shallower) end.
				// bar_y is strictly inside if r.y_min < bar_y < r.y_max.
				if (bar_y > r.y_min && bar_y < r.y_max) {
					hops.push_back(x);
					break;
				}
			}
		}
		std::sort(hops.begin(), hops.end());

		// Draw the bar left-to-right, inserting an upward semicircular arc at
		// each hop x. In Qt coordinates (y negated) "upward in layout space"
		// means toward more negative Qt-y, i.e. visually upward on screen.
		double qt_y = -bar_y;
		double cur_x = x_min;

		path.moveTo(cur_x, qt_y);

		for (double hx : hops) {
			double hop_start = hx - HOP_RADIUS;
			double hop_end = hx + HOP_RADIUS;

			// Straight segment up to the hop start.
			if (hop_start > cur_x)
				path.lineTo(hop_start, qt_y);

			// Semicircular arc: bounding rect centred at (hx, qt_y - HOP_RADIUS)
			// so the arc passes through (hx - R, qt_y) on the left,
			// (hx, qt_y - 2R) at the top, and (hx + R, qt_y) on the right.
			// startAngle=180 (left point), sweepLength=-180 goes clockwise to the
			// right point, passing through the top — upward in Qt screen coords.
			QRectF arc_rect(
				hx - HOP_RADIUS,
				qt_y - 2.0 * HOP_RADIUS,
				2.0 * HOP_RADIUS,
				2.0 * HOP_RADIUS);
			path.arcTo(arc_rect, 180.0, -180.0);

			cur_x = hop_end;
		}

		// Remaining straight segment to the right end.
		if (cur_x < x_max)
			path.lineTo(x_max, qt_y);
	}

} // namespace graphical_engine