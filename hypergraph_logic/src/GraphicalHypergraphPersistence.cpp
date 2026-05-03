#include "GraphicalHypergraph.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

namespace hypergraph_logic {

	GraphicalHypergraph GraphicalHypergraph::clone() const {
		GraphicalHypergraph copy(name_);

		// 1. Build a mapping from old Node* -> new NodePtr (fresh allocation).
		std::unordered_map<Node*, NodePtr> node_map;
		for (const auto& n : all_nodes_) {
			NodePtr new_node = n->isDummy()
				? std::make_shared<Node>()
				: std::make_shared<Node>(n->getName());
			new_node->setLayer(n->getLayer());
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
				new_layout.source_ports.push_back({ edge_map.at(port.edge).get(), port.x });
			for (const auto& port : layout.target_ports)
				new_layout.target_ports.push_back({ edge_map.at(port.edge).get(), port.x });
			copy.node_layout_[node_map.at(raw).get()] = new_layout;
		}
		for (const auto& [raw, y] : edge_layout_)
			copy.edge_layout_[edge_map.at(raw).get()] = y;

		return copy;
	}



	// ============================================================================
	// toJSON
	//
	// Serializes the full state of the GraphicalHypergraph to a JSON file at the
	// given path. The structure is as follows:
	//
	//   {
	//     "name": <string>,
	//     "nodes": [
	//       { "id": <int>, "dummy": <bool>, "name": <string>, "layer": <int> },
	//       ...
	//     ],
	//     "edges": [
	//       {
	//         "id":      <int>,       // unique ID across originals and segments
	//         "segment": <bool>,
	//         "origin":  <int|null>,  // ID of the original edge, null if not a segment
	//         "layer":   <int>,
	//         "sources": [<node_id>, ...],
	//         "targets": [<node_id>, ...]
	//       },
	//       ...
	//     ],
	//     "layers": [
	//       {
	//         "index": <int>,
	//         "nodes": [<node_id>, ...],           // ordered
	//         "outgoing_edges": [<edge_id>, ...]   // ordered
	//       },
	//       ...
	//     ],
	//     "layout": {
	//       "layer_layout": { "<layer_index>": <y>, ... },
	//       "node_layout": [
	//         {
	//           "node_id": <int>,
	//           "x": <double>,
	//           "source_ports": [ { "edge_id": <int>, "x": <double> }, ... ],
	//           "target_ports": [ { "edge_id": <int>, "x": <double> }, ... ]
	//         },
	//         ...
	//       ],
	//       "edge_layout": [ { "edge_id": <int>, "y": <double> }, ... ]
	//     }
	//   }
	//
	// Node IDs are indices into the "nodes" array.
	// Edge IDs are assigned sequentially: originals first (in all_hyperedges_
	// iteration order), then their segments in order. This guarantees that an
	// original edge always has a lower ID than any of its segments, which
	// simplifies the two-pass deserialization.
	// ============================================================================
	void GraphicalHypergraph::toJSON(const std::string& path) const {
		json j;
		j["name"] = name_;

		// ── 1. Assign stable integer IDs to every node ────────────────────────────
		std::unordered_map<Node*, int> node_id;
		json nodes_arr = json::array();
		int nid = 0;
		for (const auto& n : all_nodes_) {
			node_id[n.get()] = nid++;
			json entry;
			entry["id"] = node_id[n.get()];
			entry["dummy"] = n->isDummy();
			entry["name"] = n->getName();
			entry["layer"] = n->getLayer();
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

		// ── 5. Write to disk ──────────────────────────────────────────────────────
		std::ofstream file(path);
		if (!file.is_open())
			throw std::runtime_error("GraphicalHypergraph::toJSON: cannot open file: " + path);
		file << j.dump(2);
	}

	// ============================================================================
	// fromJSON  (static factory)
	//
	// Reconstructs a GraphicalHypergraph from a JSON file previously written by
	// toJSON(). Uses a two-pass approach:
	//
	//   Pass 1 — allocate all nodes and all original edges, build id->ptr maps.
	//   Pass 2 — allocate all segment edges (origin ptr is now available),
	//            wire parent/child relationships on nodes,
	//            reconstruct layers_, layout maps.
	//
	// Parent/child wiring mirrors createHyperedge exactly:
	//   - real→real pairs are derived from original edges.
	//   - All pairs involving at least one dummy are derived from segment edges,
	//     with the asymmetric rule that only the dummy side records the link,
	//     keeping real nodes unaware of the dummy routing infrastructure.
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

		GraphicalHypergraph g(j.at("name").get<std::string>());

		// ── Pass 1a: allocate nodes ───────────────────────────────────────────────
		std::unordered_map<int, NodePtr> node_by_id;
		for (const auto& entry : j.at("nodes")) {
			int id = entry.at("id").get<int>();
			bool dummy = entry.at("dummy").get<bool>();
			std::string nm = entry.at("name").get<std::string>();
			int layer = entry.at("layer").get<int>();

			NodePtr n = dummy ? std::make_shared<Node>()
				: std::make_shared<Node>(nm);
			n->setLayer(layer);
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
		// Original edges encode real→real pairs only.
		// Segment edges encode all pairs involving at least one dummy, with the
		// same asymmetric rule as createHyperedge: only the dummy side records
		// the link so that real nodes remain unaware of the dummy infrastructure.
		for (const auto& [orig, segs] : g.all_hyperedges_) {

			// real→real from the original edge.
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
						// real→real: already handled above via the original edge.
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

		return g;
	}

} // namespace hypergraph_logic