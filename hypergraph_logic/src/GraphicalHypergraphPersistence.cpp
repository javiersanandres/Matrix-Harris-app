#include "GraphicalHypergraph.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

namespace hypergraph_logic {

	// ============================================================================
	// Node attribute (de)serialization helpers
	//
	// Enums are stored by name and colours as "#RRGGBBAA" strings so saved files
	// stay human-readable and independent of the enums' underlying values.
	// ============================================================================
	namespace {

		const char* shapeToString(NodeShape shape) {
			switch (shape) {
			case NodeShape::Circle:  return "circle";
			case NodeShape::Rhombus: return "rhombus";
			default:                 return "rectangle";
			}
		}

		NodeShape shapeFromString(const std::string& s) {
			if (s == "rectangle") return NodeShape::Rectangle;
			if (s == "circle")    return NodeShape::Circle;
			if (s == "rhombus")   return NodeShape::Rhombus;
			throw std::runtime_error("GraphicalHypergraph::fromJSON: unknown node shape: " + s);
		}

		const char* fireToString(FireState fire) {
			switch (fire) {
			case FireState::Fire:          return "fire";
			case FireState::FireWithAshes: return "fire_with_ashes";
			default:                       return "none";
			}
		}

		FireState fireFromString(const std::string& s) {
			if (s == "none")            return FireState::None;
			if (s == "fire")            return FireState::Fire;
			if (s == "fire_with_ashes") return FireState::FireWithAshes;
			throw std::runtime_error("GraphicalHypergraph::fromJSON: unknown fire state: " + s);
		}

		Color colorFromString(const std::string& s) {
			try {
				return colorFromHex(s);
			}
			catch (const std::invalid_argument&) {
				throw std::runtime_error("GraphicalHypergraph::fromJSON: invalid colour: " + s);
			}
		}

		void attributesToJSON(const NodeAttributes& a, json& entry) {
			entry["name"] = a.name;
			entry["shape"] = shapeToString(a.shape);
			entry["colour"] = colorToHex(a.colour);
			entry["font_colour"] = colorToHex(a.font_colour);
			entry["font_size"] = a.font_size;
			entry["fire"] = fireToString(a.fire);
		}

		// Every field but the name is optional, so files saved before node attributes
		// existed still load, with the missing fields taking their default values.
		NodeAttributes attributesFromJSON(const json& entry) {
			NodeAttributes a(entry.at("name").get<std::string>());
			if (entry.contains("shape"))
				a.shape = shapeFromString(entry.at("shape").get<std::string>());
			if (entry.contains("colour"))
				a.colour = colorFromString(entry.at("colour").get<std::string>());
			if (entry.contains("font_colour"))
				a.font_colour = colorFromString(entry.at("font_colour").get<std::string>());
			if (entry.contains("font_size"))
				a.font_size = entry.at("font_size").get<int>();
			if (entry.contains("fire"))
				a.fire = fireFromString(entry.at("fire").get<std::string>());
			return a;
		}

	} // namespace

	// ============================================================================
	// ID generation
	// ============================================================================

	std::string GraphicalHypergraph::generateId() {
		static std::atomic<uint64_t> counter{ 1 };
		return "gh-" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
	}

	// ============================================================================
	// Cloning and merging functionality
	// ============================================================================
	GraphicalHypergraph GraphicalHypergraph::clone() const {
		GraphicalHypergraph copy(name_);
		copy.id_ = id_;  // preserve identity across copies

		// 1. Build a mapping from old Node* -> new NodePtr (fresh allocation).
		std::unordered_map<Node*, NodePtr> node_map;
		for (const auto& n : all_nodes_) {
			NodePtr new_node = n->isDummy()
				? std::make_shared<Node>()
				: std::make_shared<Node>(n->getAttributes());
			new_node->setLayer(n->getLayer());
			new_node->setDesiredLayer(n->getDesiredLayer());
			node_map[n.get()] = new_node;
			copy.all_nodes_.push_back(new_node);
		}

		// 2. Re-wire parent/child links using the map.
		for (const auto& n : all_nodes_) {
			NodePtr& new_node = node_map[n.get()];
			for (const auto& p : n->getParents())
				new_node->addParent(node_map.at(p.get()));
			for (const auto& c : n->getChildren())
				new_node->addChild(node_map.at(c.get()));
		}

		// 3. Build a mapping from old Hyperedge* -> new HyperedgePtr.
		//    Originals first so that origin weak_ptrs can be set correctly for segments.
		std::unordered_map<Hyperedge*, HyperedgePtr> edge_map;
		for (const auto& [orig, segs] : all_hyperedges_) {

			// Translate sources and targets of the original edge inline.
			std::vector<NodePtr> new_sources;
			for (const auto& s : orig->getSources())
				new_sources.push_back(node_map.at(s.get()));
			std::vector<NodePtr> new_targets;
			for (const auto& t : orig->getTargets())
				new_targets.push_back(node_map.at(t.get()));

			auto new_orig = std::make_shared<Hyperedge>(new_sources, new_targets);
			new_orig->setLayer(orig->getLayer());
			std::vector<NodePtr> uncertain_sources, uncertain_targets;
			for (const auto& s : orig->getSources())
				if (orig->isSourceUncertain(s.get())) uncertain_sources.push_back(node_map.at(s.get()));
			for (const auto& t : orig->getTargets())
				if (orig->isTargetUncertain(t.get())) uncertain_targets.push_back(node_map.at(t.get()));
			new_orig->setUncertainEnds(uncertain_sources, uncertain_targets);
			edge_map[orig.get()] = new_orig;
			copy.all_hyperedges_[new_orig] = {};

			for (const auto& seg : segs) {

				std::vector<NodePtr> seg_sources;
				for (const auto& s : seg->getSources())
					seg_sources.push_back(node_map.at(s.get()));
				std::vector<NodePtr> seg_targets;
				for (const auto& t : seg->getTargets())
					seg_targets.push_back(node_map.at(t.get()));

				auto new_seg = std::make_shared<Hyperedge>(
					WeakHyperedgePtr(new_orig), seg_sources, seg_targets);
				new_seg->setLayer(seg->getLayer());
				edge_map[seg.get()] = new_seg;
				copy.all_hyperedges_[new_orig].push_back(new_seg);
			}
		}

		// 4. Reconstruct layers_ preserving node order and edge order.
		for (const auto& [idx, data] : layers_) {
			LayerData& ld = copy.layers_[idx];
			for (const auto& n : data.nodes)
				ld.nodes.push_back(node_map.at(n.get()));
			for (const auto& e : data.outgoing_edges)
				ld.outgoing_edges.push_back(edge_map.at(e.get()));
		}

		// 5. Copy layout data, translating raw pointers via the maps.
		copy.layer_layout_ = layer_layout_;

		for (const auto& [raw, layout] : node_layout_) {
			NodeLayout new_layout;
			new_layout.x = layout.x;
			for (const auto& port : layout.source_ports)
				new_layout.source_ports.push_back({ edge_map.at(port.edge).get(), port.x, port.y, port.uncertain });
			for (const auto& port : layout.target_ports)
				new_layout.target_ports.push_back({ edge_map.at(port.edge).get(), port.x, port.y, port.uncertain });
			copy.node_layout_[node_map.at(raw).get()] = new_layout;
		}
		for (const auto& [raw, y] : edge_layout_)
			copy.edge_layout_[edge_map.at(raw).get()] = y;

		return copy;
	}

	void GraphicalHypergraph::mergeFrom(GraphicalHypergraph&& other, bool left) {
		for (const auto& node : other.all_nodes_)
			all_nodes_.push_back(node);

		for (auto& [orig, segs] : other.all_hyperedges_)
			all_hyperedges_[orig] = std::move(segs);

		for (auto& [layer_idx, src_data] : other.layers_) {
			LayerData& dst = layers_[layer_idx];
			if (left) {
				dst.nodes.insert(dst.nodes.begin(),
					src_data.nodes.begin(), src_data.nodes.end());
				dst.outgoing_edges.insert(dst.outgoing_edges.begin(),
					src_data.outgoing_edges.begin(), src_data.outgoing_edges.end());
			}
			else {
				dst.nodes.insert(dst.nodes.end(),
					src_data.nodes.begin(), src_data.nodes.end());
				dst.outgoing_edges.insert(dst.outgoing_edges.end(),
					src_data.outgoing_edges.begin(), src_data.outgoing_edges.end());
			}
		}

		for (auto& [raw, layout] : other.node_layout_)
			node_layout_[raw] = std::move(layout);
		for (auto& [raw, y] : other.edge_layout_)
			edge_layout_[raw] = y;
		for (const auto& [idx, y] : other.layer_layout_)
			layer_layout_.emplace(idx, y);
	}

	void GraphicalHypergraph::mergeFrom(GraphicalHypergraph&& other,
		const std::function<size_t(int layer)>& insert_at)
	{
		for (const auto& node : other.all_nodes_)
			all_nodes_.push_back(node);

		for (auto& [orig, segs] : other.all_hyperedges_)
			all_hyperedges_[orig] = std::move(segs);

		for (auto& [layer_idx, src_data] : other.layers_) {
			LayerData& dst = layers_[layer_idx];
			const size_t at = std::min(insert_at(layer_idx), dst.nodes.size());
			dst.nodes.insert(dst.nodes.begin() + static_cast<std::ptrdiff_t>(at),
				src_data.nodes.begin(), src_data.nodes.end());
			dst.outgoing_edges.insert(dst.outgoing_edges.end(),
				src_data.outgoing_edges.begin(), src_data.outgoing_edges.end());
		}

		for (auto& [raw, layout] : other.node_layout_)
			node_layout_[raw] = std::move(layout);
		for (auto& [raw, y] : other.edge_layout_)
			edge_layout_[raw] = y;
		for (const auto& [idx, y] : other.layer_layout_)
			layer_layout_.emplace(idx, y);
	}

	// ============================================================================
	// toJSON — core (json& overload)
	//
	// Serializes the complete state of the GraphicalHypergraph into the supplied
	// json object. The caller decides whether to write it to a file or embed it
	// directly in a larger document (e.g. a Project save file).
	//
	// Schema — see toJSON(const std::string& path) for the full description.
	// ============================================================================
	void GraphicalHypergraph::toJSON(json& j) const {
		j["name"] = name_;
		j["id"] = id_;

		// ── 1. Assign stable integer IDs to every node ────────────────────────────
		std::unordered_map<Node*, int> node_id;
		json nodes_arr = json::array();
		int nid = 0;
		for (const auto& n : all_nodes_) {
			node_id[n.get()] = nid++;
			json entry;
			entry["id"] = node_id[n.get()];
			entry["dummy"] = n->isDummy();
			if (n->isDummy())
				entry["name"] = n->getName();
			else
				attributesToJSON(n->getAttributes(), entry);
			entry["layer"] = n->getLayer();
			entry["desired_layer"] = n->getDesiredLayer();
			nodes_arr.push_back(std::move(entry));
		}
		j["nodes"] = std::move(nodes_arr);

		// ── 2. Assign stable integer IDs to every edge (originals then segments) ──
		std::unordered_map<Hyperedge*, int> edge_id;
		json edges_arr = json::array();
		int eid = 0;

		auto serialize_edge = [&](const HyperedgePtr& e, bool is_segment, int origin_id) {
			edge_id[e.get()] = eid++;
			json entry;
			entry["id"] = edge_id[e.get()];
			entry["segment"] = is_segment;
			entry["origin"] = is_segment ? json(origin_id) : json(nullptr);
			entry["layer"] = e->getLayer();
			if (!is_segment) {
				entry["continuous"] = !e->allEndsUncertain(); // false: dashed as a whole
				json uncertain_sources = json::array(), uncertain_targets = json::array();
				for (const auto& s : e->getSources())
					if (e->isSourceUncertain(s.get())) uncertain_sources.push_back(node_id.at(s.get()));
				for (const auto& t : e->getTargets())
					if (e->isTargetUncertain(t.get())) uncertain_targets.push_back(node_id.at(t.get()));
				if (!uncertain_sources.empty()) entry["uncertain_sources"] = std::move(uncertain_sources);
				if (!uncertain_targets.empty()) entry["uncertain_targets"] = std::move(uncertain_targets);
			}

			json srcs = json::array();
			for (const auto& s : e->getSources())
				srcs.push_back(node_id.at(s.get()));
			entry["sources"] = std::move(srcs);

			json tgts = json::array();
			for (const auto& t : e->getTargets())
				tgts.push_back(node_id.at(t.get()));
			entry["targets"] = std::move(tgts);

			edges_arr.push_back(std::move(entry));
		};

		for (const auto& [orig, segs] : all_hyperedges_) {
			serialize_edge(orig, false, -1);
			int orig_id = edge_id[orig.get()];
			for (const auto& seg : segs)
				serialize_edge(seg, true, orig_id);
		}
		j["edges"] = std::move(edges_arr);

		// ── 3. Layers (preserving node order and edge order) ──────────────────────
		json layers_arr = json::array();
		for (const auto& [idx, data] : layers_) {
			json layer_entry;
			layer_entry["index"] = idx;

			json layer_nodes = json::array();
			for (const auto& n : data.nodes)
				layer_nodes.push_back(node_id.at(n.get()));
			layer_entry["nodes"] = std::move(layer_nodes);

			json layer_edges = json::array();
			for (const auto& e : data.outgoing_edges)
				layer_edges.push_back(edge_id.at(e.get()));
			layer_entry["outgoing_edges"] = std::move(layer_edges);

			layers_arr.push_back(std::move(layer_entry));
		}
		j["layers"] = std::move(layers_arr);

		// ── 4. Layout data ────────────────────────────────────────────────────────
		json layout;

		json ll = json::object();
		for (const auto& [idx, y] : layer_layout_)
			ll[std::to_string(idx)] = y;
		layout["layer_layout"] = std::move(ll);

		json nl = json::array();
		for (const auto& [raw, data] : node_layout_) {
			json ne;
			ne["node_id"] = node_id.at(raw);
			ne["x"] = data.x;

			json sp = json::array();
			for (const auto& port : data.source_ports)
				sp.push_back({ {"edge_id", edge_id.at(port.edge)}, {"x", port.x} });
			ne["source_ports"] = std::move(sp);

			json tp = json::array();
			for (const auto& port : data.target_ports)
				tp.push_back({ {"edge_id", edge_id.at(port.edge)}, {"x", port.x} });
			ne["target_ports"] = std::move(tp);

			nl.push_back(std::move(ne));
		}
		layout["node_layout"] = std::move(nl);

		json el = json::array();
		for (const auto& [raw, y] : edge_layout_)
			el.push_back({ {"edge_id", edge_id.at(raw)}, {"y", y} });
		layout["edge_layout"] = std::move(el);

		j["layout"] = std::move(layout);
	}

	// ============================================================================
	// toJSON — file-path overload (thin wrapper)
	// ============================================================================
	void GraphicalHypergraph::toJSON(const std::string& path) const {
		json j;
		toJSON(j);
		std::ofstream file(path);
		if (!file.is_open())
			throw std::runtime_error("GraphicalHypergraph::toJSON: cannot open file: " + path);
		file << j.dump(2);
	}

	// ============================================================================
	// fromJSON — core (const json& overload)
	//
	// Reconstructs a GraphicalHypergraph from a json object previously produced
	// by toJSON(json&). Uses a two-pass approach:
	//
	//   Pass 1 — allocate all nodes and all original edges, build id->ptr maps.
	//   Pass 2 — allocate all segment edges (origin ptr is now available),
	//            wire parent/child relationships on nodes,
	//            reconstruct layers_, layout maps.
	//
	// Parent/child wiring mirrors createHyperedge exactly:
	//   - real->real pairs are derived from original edges.
	//   - All pairs involving at least one dummy are derived from segment edges,
	//     with the asymmetric rule that only the dummy side records the link,
	//     keeping real nodes unaware of the dummy routing infrastructure.
	// ============================================================================
	GraphicalHypergraph GraphicalHypergraph::fromJSON(const json& j) {
		GraphicalHypergraph g(j.at("name").get<std::string>());
		// Restore the original identity so the JointGraphicalHypergraph can
		// recognise this graph even after a serialization round-trip.
		if (j.contains("id"))
			g.id_ = j.at("id").get<std::string>();

		// ── Pass 1a: allocate nodes ───────────────────────────────────────────────
		std::unordered_map<int, NodePtr> node_by_id;
		for (const auto& entry : j.at("nodes")) {
			int id = entry.at("id").get<int>();
			bool dummy = entry.at("dummy").get<bool>();
			int layer = entry.at("layer").get<int>();
			// Older files carry no desired layer: treat every node as un-overridden.
			int desired_layer = entry.value("desired_layer", -1);

			NodePtr n = dummy ? std::make_shared<Node>()
				: std::make_shared<Node>(attributesFromJSON(entry));
			n->setLayer(layer);
			n->setDesiredLayer(desired_layer);
			node_by_id[id] = n;
			g.all_nodes_.push_back(n);
		}

		// ── Pass 1b: allocate original edges ──────────────────────────────────────
		// Segments need their origin ptr, so originals must exist first.
		// toJSON guarantees originals always have lower IDs than their segments,
		// so a single forward pass over the edges array suffices here.
		std::unordered_map<int, HyperedgePtr> edge_by_id;

		const auto& edges_arr = j.at("edges");
		for (const auto& entry : edges_arr) {
			if (entry.at("segment").get<bool>()) continue;

			std::vector<NodePtr> sources, targets;
			for (int sid : entry.at("sources"))
				sources.push_back(node_by_id.at(sid));
			for (int tid : entry.at("targets"))
				targets.push_back(node_by_id.at(tid));

			auto e = std::make_shared<Hyperedge>(sources, targets);
			e->setLayer(entry.at("layer").get<int>());
			// Files saved before line styles existed have only continuous lines.
			if (!entry.value("continuous", true)) e->setContinuous(false);
			else {
				std::vector<NodePtr> uncertain_sources, uncertain_targets;
				if (entry.contains("uncertain_sources"))
					for (int sid : entry.at("uncertain_sources")) uncertain_sources.push_back(node_by_id.at(sid));
				if (entry.contains("uncertain_targets"))
					for (int tid : entry.at("uncertain_targets")) uncertain_targets.push_back(node_by_id.at(tid));
				e->setUncertainEnds(uncertain_sources, uncertain_targets);
			}

			int id = entry.at("id").get<int>();
			edge_by_id[id] = e;
			g.all_hyperedges_[e] = {};  // register with empty segment list
		}

		// ── Pass 2a: allocate segment edges ───────────────────────────────────────
		for (const auto& entry : edges_arr) {
			if (!entry.at("segment").get<bool>()) continue;

			int origin_id = entry.at("origin").get<int>();
			HyperedgePtr& orig = edge_by_id.at(origin_id);

			std::vector<NodePtr> sources, targets;
			for (int sid : entry.at("sources"))
				sources.push_back(node_by_id.at(sid));
			for (int tid : entry.at("targets"))
				targets.push_back(node_by_id.at(tid));

			auto seg = std::make_shared<Hyperedge>(WeakHyperedgePtr(orig), sources, targets);
			seg->setLayer(entry.at("layer").get<int>());

			int id = entry.at("id").get<int>();
			edge_by_id[id] = seg;
			g.all_hyperedges_[orig].push_back(seg);
		}

		// ── Pass 2b: wire parent/child links ──────────────────────────────────────
		//
		// Original edges encode real->real pairs only.
		// Segment edges encode all pairs involving at least one dummy, with the
		// same asymmetric rule as createHyperedge: only the dummy side records
		// the link so that real nodes remain unaware of the dummy infrastructure.
		for (const auto& [orig, segs] : g.all_hyperedges_) {

			// real->real from the original edge.
			for (const auto& src : orig->getSources()) {
				if (src->isDummy()) continue;
				for (const auto& tgt : orig->getTargets()) {
					if (tgt->isDummy()) continue;
					src->addChild(tgt);
					tgt->addParent(src);
				}
			}

			// dummy-involving pairs from each segment edge.
			for (const auto& seg : segs) {
				for (const auto& src : seg->getSources()) {
					for (const auto& tgt : seg->getTargets()) {
						bool src_dummy = src->isDummy();
						bool tgt_dummy = tgt->isDummy();

						if (src_dummy && tgt_dummy) {
							src->addChild(tgt);
							tgt->addParent(src);
						}
						else if (src_dummy && !tgt_dummy) {
							// Only the dummy knows the real target.
							src->addChild(tgt);
						}
						else if (!src_dummy && tgt_dummy) {
							// Only the dummy knows the real source.
							tgt->addParent(src);
						}
						// real->real: already handled above via the original edge.
					}
				}
			}
		}

		// ── Pass 2c: reconstruct layers_ ─────────────────────────────────────────
		for (const auto& layer_entry : j.at("layers")) {
			int idx = layer_entry.at("index").get<int>();
			LayerData& ld = g.layers_[idx];

			for (int nid : layer_entry.at("nodes"))
				ld.nodes.push_back(node_by_id.at(nid));
			for (int eid : layer_entry.at("outgoing_edges"))
				ld.outgoing_edges.push_back(edge_by_id.at(eid));
		}

		// ── Pass 2d: reconstruct layout maps ──────────────────────────────────────
		const auto& layout = j.at("layout");

		for (const auto& [key, val] : layout.at("layer_layout").items())
			g.layer_layout_[std::stoi(key)] = val.get<double>();

		for (const auto& ne : layout.at("node_layout")) {
			int nid = ne.at("node_id").get<int>();
			Node* raw = node_by_id.at(nid).get();
			NodeLayout& nl = g.node_layout_[raw];
			nl.x = ne.at("x").get<double>();

			for (const auto& port : ne.at("source_ports"))
				nl.source_ports.push_back({
					edge_by_id.at(port.at("edge_id").get<int>()).get(),
					port.at("x").get<double>()
					});
			for (const auto& port : ne.at("target_ports"))
				nl.target_ports.push_back({
					edge_by_id.at(port.at("edge_id").get<int>()).get(),
					port.at("x").get<double>()
					});
		}

		for (const auto& ee : layout.at("edge_layout")) {
			int eid = ee.at("edge_id").get<int>();
			double y = ee.at("y").get<double>();
			g.edge_layout_[edge_by_id.at(eid).get()] = y;
		}

		// Port y-coordinates are not persisted: they follow from the layer y's,
		// the port x's and each node's shape.
		g.assignPortYCoordinates();
		g.refreshUncertainPorts(); // Port::uncertain comes from the connections, not the file

		return g;
	}

	// ============================================================================
	// fromJSON — file-path overload (thin wrapper)
	// ============================================================================
	GraphicalHypergraph GraphicalHypergraph::fromJSON(const std::string& path) {
		std::ifstream file(path);
		if (!file.is_open())
			throw std::runtime_error("GraphicalHypergraph::fromJSON: cannot open file: " + path);

		json j;
		try {
			file >> j;
		}
		catch (const json::parse_error& e) {
			throw std::runtime_error(
				std::string("GraphicalHypergraph::fromJSON: JSON parse error: ") + e.what());
		}

		return fromJSON(j);
	}

} // namespace hypergraph_logic