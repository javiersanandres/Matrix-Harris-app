#include "BrandesKopf.h"

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_map>

// ====================================================================================
// Implementation of the Brandes–Köpf horizontal coordinate assignment as
// described in:
//  Brandes, U., & Köpf, B. (2002).
//  "Fast and Simple Horizontal Coordinate Assignment."
//  In: Graph Drawing (GD 2001), LNCS 2265, pp. 31–44. Springer.
//  DOI: 10.1007/978-3-540-45848-7_3
//
// Since we are dealing with hypergraphs, we first have to transform the
// original hypergraph into a directed graph G2 by replacing each short 
// hyperedge with binary edges between all source-target pairs. This way, 
// we can apply the algorithm to G2 and then translate the resulting horizontal 
// coordinates back. Our inner segments are somewhat different from the paper's 
// definition, as explained below, but the overall intent is the same: to identify
// the "spines" of long edges and preferentially keep them vertical.
// 
// IMPORTANT: The original paper had some mistakes which dealt into wrong layouts.
// I have fixed these mistakes as stated in: 
//   Brandes, U., Walter, J., & Zink, J. (2021). "Erratum: Fast and
//   Simple Horizontal Coordinate Assignment."
//   In: Graph Drawing and Network Visualization (GD 2020),
//   LNCS 12590, pp. 433–435. Springer.
//   DOI: 10.1007/978-3-030-68766-3_32
// 
// ====================================================================================

namespace bk_internal {

	using namespace hypergraph_logic;

	// ── BrandesKopf: construction ─────────────────────────────────────────────────

	BrandesKopf::BrandesKopf(const std::map<int, LayerData>& layers) {
		std::map<Node*, int> node_to_g2;

		g_.num_layers = static_cast<int>(layers.size());
		g_.layers.resize(g_.num_layers);
		int id_counter = 0;
		for (const auto& [layer, data] : layers) {
			for (int i = 0; i < static_cast<int>(data.nodes.size()); ++i) {
				auto node = data.nodes[i].get();
				g_.nodes.push_back(node);
				g_.layers[layer].push_back(id_counter);
				g_.pos.push_back(i);
				node_to_g2[node] = id_counter;
				id_counter++;
			}
		}

		int n = static_cast<int>(g_.nodes.size());
		g_.upper.assign(n, {});
		g_.lower.assign(n, {});

		for (const auto& [layer, data] : layers) {
			if (layer == static_cast<int>(layers.size()) - 1) continue;
			for (const auto& edge : data.outgoing_edges) {
				for (const auto& src : edge->getSources()) {
					int src_id = node_to_g2.at(src.get());
					for (const auto& tgt : edge->getTargets()) {
						int tgt_id = node_to_g2.at(tgt.get());
						g_.upper[tgt_id].push_back(src_id);
						g_.lower[src_id].push_back(tgt_id);
					}
				}
			}
		}

		for (int node_id = 0; node_id < n; ++node_id) {
			std::sort(g_.upper[node_id].begin(), g_.upper[node_id].end(),
				[&](int a, int b) { return g_.pos[a] < g_.pos[b]; });
			std::sort(g_.lower[node_id].begin(), g_.lower[node_id].end(),
				[&](int a, int b) { return g_.pos[a] < g_.pos[b]; });
		}
	}

	// ── isInnerSegment ────────────────────────────────────────────────────────────

	bool BrandesKopf::isInnerSegment(const G2& g, int u, int v) {
		Node* nu = g.nodes[u];
		Node* nv = g.nodes[v];
		if (!nu->isDummy() || !nv->isDummy()) return false;

		auto u_children = nu->getChildren();
		if (u_children.size() != 1 || u_children[0].get() != nv) return false;

		auto v_parents = nv->getParents();
		if (v_parents.size() != 1 || v_parents[0].get() != nu) return false;

		return true;
	}

	// ── Algorithm 1: markType1Conflicts ───────────────────────────────────────────

	void BrandesKopf::markType1Conflicts() {
		for (int i = 1; i < g_.num_layers - 2; ++i) {
			const auto& lower_layer = g_.layers[i + 1];
			int L1 = static_cast<int>(lower_layer.size());

			int k0 = 0;
			int l = 0;
			for (int l1 = 0; l1 < L1; ++l1) {
				int v = lower_layer[l1];
				bool incident_to_inner = (g_.upper[v].size() == 1 &&
					isInnerSegment(g_, g_.upper[v][0], v));

				if (l1 == L1 - 1 || incident_to_inner) {
					int k1 = static_cast<int>(g_.layers[i].size());
					if (incident_to_inner)
						k1 = g_.pos[g_.upper[v][0]];

					while (l <= l1) {
						int w = lower_layer[l];
						for (int parent : g_.upper[w])
							if (g_.pos[parent] < k0 || g_.pos[parent] > k1)
								g_.marked.insert({ parent, w });
						++l;
					}
					k0 = k1;
				}
			}
		}
	}

	// ── computeBlockWidth ─────────────────────────────────────────────────────────

	double BrandesKopf::computeBlockWidth(const G2& g, const BlockList& B, int v) {
#ifdef BK_TEST
		if (g.nodes[v] == nullptr) return NODE_WIDTH;
#endif
		if (!g.nodes[v]->isDummy()) return NODE_WIDTH;
		int cur_id = B.align[v];
		while (cur_id != v) {
			if (!g.nodes[cur_id]->isDummy()) return NODE_WIDTH;
			cur_id = B.align[cur_id];
		}
		return DUMMY_NODE_WIDTH;
	}

	// ── Algorithm 2: verticalAlignment ───────────────────────────────────────────

	BlockList BrandesKopf::verticalAlignment(int vdir, int hdir) const {
		int n = static_cast<int>(g_.nodes.size());
		BlockList B;
		B.root.resize(n);
		B.align.resize(n);
		B.block_widths.resize(n);
		for (int v = 0; v < n; v++) {
			B.root[v] = v;
			B.align[v] = v;
		}

		for (int i = (vdir == 1) ? 0 : g_.num_layers - 1;
			i != ((vdir == 1) ? g_.num_layers : -1); i += vdir) {
			int r = (hdir == 1) ? -1 : INT_MAX;
			const auto& cur_layer = g_.layers[i];
			int L = static_cast<int>(cur_layer.size());

			for (int k = (hdir == 1) ? 0 : L - 1;
				k != ((hdir == 1) ? L : -1); k += hdir) {
				int v = cur_layer[k];
				if (B.align[v] != v) continue;

				const std::vector<int>& neighbours = (vdir == 1) ? g_.upper[v] : g_.lower[v];
				if (neighbours.empty()) continue;

				int d = static_cast<int>(neighbours.size());
				int median_left = (d - 1) / 2;
				int median_right = d / 2;

				std::vector<int> medians;
				if (median_left == median_right) {
					medians = { median_left };
				}
				else {
					if (hdir == 1) medians = { median_left, median_right };
					else           medians = { median_right, median_left };
				}

				for (int m_idx : medians) {
					int um = neighbours[m_idx];
					int um_pos = g_.pos[um];

					bool not_marked = (vdir == 1) ? !g_.isMarked(um, v) : !g_.isMarked(v, um);
					bool position_ok = (hdir == 1) ? (r < um_pos) : (r > um_pos);

					if (not_marked && position_ok) {
						B.align[um] = v;
						B.root[v] = B.root[um];
						B.align[v] = B.root[v];
						r = um_pos;
						break;
					}
				}
			}
		}

		std::unordered_set<int> visited;
		for (int v = 0; v < n; v++) {
			if (!visited.insert(B.root[v]).second) continue;
			double width = computeBlockWidth(g_, B, v);
			int    root_id = B.root[v];
			for (int cur_id = root_id; ; cur_id = B.align[cur_id]) {
				B.block_widths[cur_id] = width;
				if (B.align[cur_id] == root_id) break;
			}
		}

		return B;
	}

	// ── Algorithm 3: placeBlock ───────────────────────────────────────────────────

	void BrandesKopf::placeBlock(const G2& g, const BlockList& B, int v,
		std::vector<int>& sink, std::vector<double>& shift,
		std::vector<double>& x, int hdir)
	{
		if (x[v] != std::numeric_limits<double>::lowest()) return;
		x[v] = 0.0;
		int w = v;
		do {
#ifdef BK_TEST
			int layer_of_w = [&]() {
				for (int i = 0; i < g.num_layers; ++i)
					for (int id : g.layers[i])
						if (id == w) return i;
				return -1;
				}();
#else
			int layer_of_w = g.nodes[w]->getLayer();
#endif
			if ((hdir == 1 && g.pos[w] > 0) ||
				(hdir == -1 && g.pos[w] < static_cast<int>(g.layers[layer_of_w].size()) - 1)) {
				int pred_pos = (hdir == 1) ? g.pos[w] - 1 : g.pos[w] + 1;
				int u = B.root[g.layers[layer_of_w][pred_pos]];
				placeBlock(g, B, u, sink, shift, x, hdir);
				if (sink[v] == v) sink[v] = sink[u];
				double sep = (B.block_widths[v] + B.block_widths[u]) * 0.5 + MIN_BLOCK_SEP;
				if (sink[v] == sink[u]) {
					if (hdir == 1) x[v] = std::max(x[v], x[u] + sep);
					else            x[v] = std::min(x[v], x[u] - sep);
				}
			}
			w = B.align[w];
		} while (w != v);

		while (B.align[w] != v) {
			w = B.align[w];
			x[w] = x[v];
			sink[w] = sink[v];
		}
	}

	// ── Algorithm 3: horizontalCompaction ─────────────────────────────────────────

	std::vector<double> BrandesKopf::horizontalCompaction(const G2& g, const BlockList& B,
		int vdir, int hdir)
	{
		int n = static_cast<int>(g.nodes.size());

		std::vector<int> sink(n);
		for (int v = 0; v < n; v++) sink[v] = v;
		std::vector<double> shift(n, (hdir == 1) ?
			std::numeric_limits<double>::max() : std::numeric_limits<double>::lowest());
		std::vector<double> x(n, std::numeric_limits<double>::lowest());

		for (int i = (vdir == 1) ? 0 : g.num_layers - 1;
			i != ((vdir == 1) ? g.num_layers : -1); i += vdir) {
			const auto& cur_layer = g.layers[i];
			int L = static_cast<int>(cur_layer.size());
			for (int k = (hdir == 1) ? 0 : L - 1;
				k != ((hdir == 1) ? L : -1); k += hdir) {
				int v = cur_layer[k];
				if (B.root[v] == v)
					placeBlock(g, B, v, sink, shift, x, hdir);
			}
		}

		for (int i = (vdir == 1) ? 0 : g.num_layers - 1;
			i != ((vdir == 1) ? g.num_layers : -1); i += vdir) {
			const auto& cur_layer = g.layers[i];
			int first = cur_layer[(hdir == 1) ? 0 : static_cast<int>(cur_layer.size()) - 1];
			if (sink[first] == first) {
				if (shift[sink[first]] == ((hdir == 1) ?
					std::numeric_limits<double>::max() :
					std::numeric_limits<double>::lowest()))
					shift[sink[first]] = 0;

				int j = i;
				int k = (hdir == 1) ? 0 : static_cast<int>(cur_layer.size()) - 1;
				int v = g.layers[j][k];
				do {
					v = g.layers[j][k];
					while (B.align[v] != B.root[v]) {
						v = B.align[v];
						j += vdir;
						if ((hdir == 1 && g.pos[v] > 0) ||
							(hdir == -1 && g.pos[v] < static_cast<int>(g.layers[j].size()) - 1)) {
							int u = g.layers[j][(hdir == 1) ? g.pos[v] - 1 : g.pos[v] + 1];
							double sep = (B.block_widths[v] + B.block_widths[u]) * 0.5 + MIN_BLOCK_SEP;
							if (hdir == 1)
								shift[sink[u]] = std::min(shift[sink[u]], shift[sink[v]] + x[v] - x[u] - sep);
							else
								shift[sink[u]] = std::max(shift[sink[u]], shift[sink[v]] + x[v] - x[u] + sep);
						}
					}
					k = g.pos[v] + hdir;
				} while (((hdir == 1 && k < static_cast<int>(g.layers[j].size())) ||
					(hdir == -1 && k >= 0)) &&
					sink[v] == sink[g.layers[j][k]]);
			}
		}

		for (int v = 0; v < n; v++)
			x[v] += shift[sink[v]];

		return x;
	}

	// ── Algorithm 4: run ─────────────────────────────────────────────────────────

	std::vector<double> BrandesKopf::run() {
		markType1Conflicts();

		std::array<std::vector<double>, 4> layouts{};
		std::array<std::vector<double>, 4> block_widths{};
		int count = 0;
		for (int vdir : {1, -1}) {
			for (int hdir : {1, -1}) {
				BlockList B = verticalAlignment(vdir, hdir);
				layouts[count] = horizontalCompaction(g_, B, vdir, hdir);
				block_widths[count] = B.block_widths;
				count++;
			}
		}

		std::array<double, 4> min_x{}, max_x{}, layout_width{};
		int min_width_layout = 0;
		for (int i = 0; i < 4; i++) {
			min_x[i] = std::numeric_limits<double>::max();
			max_x[i] = std::numeric_limits<double>::lowest();
		}

		for (int k = 0; k < 4; ++k) {
			for (int v = 0; v < static_cast<int>(g_.nodes.size()); v++) {
				double bw = block_widths[k][v] * 0.5;
				double xp = layouts[k][v] - bw;
				if (min_x[k] > xp) min_x[k] = xp;
				xp = layouts[k][v] + bw;
				if (max_x[k] < xp) max_x[k] = xp;
			}
			layout_width[k] = max_x[k] - min_x[k];
			if (layout_width[k] < layout_width[min_width_layout])
				min_width_layout = k;
		}

		// Ensure min_width_layout is in valid range [0, 3]
		_ASSERTE(min_width_layout >= 0 && min_width_layout < 4);

		std::array<double, 4> shifts{};
		for (int k = 0; k < 4; ++k) {
			if (k % 2 == 0) {
				shifts[k] = min_x[min_width_layout] - min_x[k];
			}
			else {
				shifts[k] = max_x[min_width_layout] - max_x[k];
			}
		}

		std::vector<double> x;
		x.reserve(g_.nodes.size());
		for (int v = 0; v < static_cast<int>(g_.nodes.size()); v++) {
			for (int k = 0; k < 4; k++)
				layouts[k][v] += shifts[k];
			double c[4] = {
				layouts[0][v], layouts[1][v],
				layouts[2][v], layouts[3][v]
			};
			std::sort(c, c + 4);
			x.push_back((c[1] + c[2]) * 0.5);
		}

		return x;
	}

} // namespace bk_internal

// ============================================================================
// GraphicalHypergraph::assignCoordinates
// ============================================================================
namespace hypergraph_logic {

	void GraphicalHypergraph::assignCoordinates() {
		if (getLayers().empty()) return;

		using namespace bk_internal;

		BrandesKopf bk(layers_);
		if (bk.g_.nodes.empty()) return;

		std::vector<double> x = bk.run();

		node_layout_.clear();
		for (int id = 0; id < static_cast<int>(bk.g_.nodes.size()); id++) {
			NodeLayout layout;
			layout.x = x[id];
			node_layout_[bk.g_.nodes[id]] = layout;
		}
	}

	double GraphicalHypergraph::getX(const NodePtr& node) const {
		return node_layout_.at(node.get()).x;
	}

} // namespace hypergraph_logic