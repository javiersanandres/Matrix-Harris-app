#pragma once
#include "GraphicalHypergraph.h"
#include "ILPCancellationToken.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>


using json = nlohmann::json;

namespace app_logic {
	using namespace hypergraph_logic;

	// ============================================================================
	// History entries
	//
	// Each step of the undo/redo history is one of two kinds:
	//
	//   - A snapshot: a full copy of the graph. Used by the operations that
	//     rebuild the structure (adding, removing or fusing boxes and connections,
	//     moving boxes, minimising crossings...), which are too intricate to
	//     invert by hand. The entry always holds "the other" state: on the undo
	//     stack, the graph before the operation; once undone, the graph after it.
	//
	//   - A LocalChange: a pair of small functions that undo and redo one cosmetic
	//     change (a name, a box's attributes, a connection's line style). No copy
	//     of the graph is made.
	//
	// Snapshots replace the graph's objects on undo/redo, so a LocalChange never
	// holds pointers: it finds its box (or connection) again by position (see
	// NodeLocator / EdgeLocator), which is the same in every copy of the same
	// state.
	//
	// before / after identify the editor's state on each side of the step (see
	// HypergraphEditorBase::isAtSavedState).
	// ============================================================================
	// ============================================================================
	// InitialLayout
	//
	// What an editor does with the layout of the graph it is given:
	//   Compute  lays the graph out (the graph was built in code, or changed
	//            since it was last laid out).
	//   Keep     uses the layout the graph already carries, e.g. one just read
	//            by fromJSON, which restores it in full. Laying it out again
	//            would only repeat the work -- and could re-order the bars
	//            differently from what was saved.
	// ============================================================================
	enum class InitialLayout { Compute, Keep };

	struct LocalChange {
		std::function<void(GraphicalHypergraph&)> undo;
		std::function<void(GraphicalHypergraph&)> redo;
	};

	template <typename Snapshot>
	struct HistoryEntry {
		std::variant<Snapshot, LocalChange> change;
		std::uint64_t before = 0;
		std::uint64_t after = 0;
	};

	// ============================================================================
	// HypergraphEditorBase<Derived>
	//
	// CRTP base class that implements all shared undo/redo machinery and the full
	// set of forwarding methods that are common to both HypergraphEditor and
	// JointHypergraphEditor.
	//
	// The undo and redo stacks live in the derived classes because the snapshot
	// type for JointGraphicalHypergraph is different from the one for
	// GraphicalHypergraph; the derived class provides:
	//   graph()                  the live graph
	//   takeSnapshot()           a full copy of it
	//   installSnapshot(s)       makes s the live graph
	//   past_, future_           std::deque<HistoryEntry<Snapshot>>
	//
	// Transactional snapshot model (structural operations):
	//   1. Clone the live graph via derived().takeSnapshot() into a local variable.
	//   2. Attempt the mutation (and computeLayout if needed) inside a try block.
	//   3. On SUCCESS  -> commitSnapshot(std::move(saved)).
	//      The snapshot lands on past_ and future_ is cleared only now.
	//   4. On FAILURE  -> the catch block rethrows. The local clone is destroyed
	//      automatically and nothing reaches the history.
	//
	// Local model (cosmetic operations): the change is applied, and on success
	// commitLocal() records how to undo and redo it. A change that would leave
	// everything as it was records nothing.
	//
	// Read-only queries and toJSON() never touch the history.
	// ============================================================================
	template <typename Derived>
	class HypergraphEditorBase {
	public:

		// This is the maximum number of steps that the undo and redo stacks can
		// hold. When the cap is exceeded, the oldest step is discarded. It is
		// unlikely that the user will want to undo more than 25 steps, and keeping
		// more snapshots in memory would be wasteful.
		static constexpr int MAX_HISTORY = 25;

		// ── Undo / Redo ───────────────────────────────────────────────────────────

		// ── canUndo ───────────────────────────────────────────────────────────────
		//
		// Returns true if there is at least one state in the undo stack.
		// The graphical engine can use this to enable or disable the undo
		// button in the UI.
		bool canUndo() const { return derived().canUndo(); }

		// ── canRedo ───────────────────────────────────────────────────────────────
		//
		// Returns true if there is at least one state in the redo stack.
		// The graphical engine can use this to enable or disable the redo
		// button in the UI.
		bool canRedo() const { return derived().canRedo(); }

		// ── undo ──────────────────────────────────────────────────────────────────
		//
		// Reverts the graph to the state before the last mutating operation.
		// The current state is pushed onto the redo stack so it can be recovered.
		// Throws std::logic_error if the undo stack is empty.
		void undo() {
			step(derived().past_, derived().future_, true);
		}

		// ── redo ──────────────────────────────────────────────────────────────────
		//
		// Re-applies the most recently undone mutating operation.
		// The current state is pushed onto the undo stack.
		// Throws std::logic_error if the redo stack is empty.
		void redo() {
			step(derived().future_, derived().past_, false);
		}

		// ── Saved state ───────────────────────────────────────────────────────────
		//
		// Every change gives the editor a new state id; undo and redo go back to
		// the id the graph had on that side of the step. markSaved() remembers the
		// current id, and isAtSavedState() tells whether the graph is back in
		// exactly that state (e.g. after undoing every change made since saving),
		// however many steps were done and undone in between.
		// A new editor starts at its saved state.
		bool isAtSavedState() const { return state_ == saved_state_; }
		void markSaved() { saved_state_ = state_; }

		// ── Read-only queries (no snapshot needed) ────────────────────────────────

		const std::string& getName() const {
			return derived().graph().getName();
		}
		const std::string& getId() const {
			return derived().graph().getId();
		}
		int getLayerCount() const {
			return derived().graph().getLayerCount();
		}
		const std::map<int, LayerData>& getLayers() const {
			return derived().graph().getLayers();
		}
		const LayerData& getLayerData(int layer) const {
			return derived().graph().getLayerData(layer);
		}
		std::vector<NodePtr> getNodesAt(int layer) const {
			return derived().graph().getNodesAt(layer);
		}
		std::vector<NodePtr> getAllNodes() const {
			return derived().graph().getAllNodes();
		}
		std::vector<HyperedgePtr> getAllHyperedges() const {
			return derived().graph().getAllHyperedges();
		}
		double getX(const NodePtr& node) const {
			return derived().graph().getX(node);
		}

		// ── toJSON ────────────────────────────────────────────────────────────────
		//
		// Serializes the current graph state to a JSON file.
		// Read-only — does not snapshot, does not affect the undo/redo stacks.
		void toJSON(json& j) const {
			derived().graph().toJSON(j);
		}

		// ── getGraph ──────────────────────────────────────────────────────────────
		//
		// Returns a const reference to the current graph so that the graphical
		// engine can read all layout and topology data for rendering without being
		// able to mutate the graph directly. The reference is valid until the next
		// mutating call on this editor, so the graphical engine should call
		// getGraph() every time.
		const auto& getGraph() const {
			return derived().graph();
		}

		// ── Mutating API shared by both editors ───────────────────────────────────

		// ── setName ───────────────────────────────────────────────────────────────
		//
		// Renames the diagram. Local change: undone without copying the graph.
		void setName(const std::string& name) {
			const std::string old_name = derived().graph().getName();
			if (old_name == name) return;
			derived().graph().setName(name);
			commitLocal({
				[old_name](GraphicalHypergraph& g) { g.setName(old_name); },
				[name](GraphicalHypergraph& g) { g.setName(name); } });
		}

		// ── renameNode ────────────────────────────────────────────────────────────
		//
		// Renames the node. Local change: undone without copying the graph.
		void renameNode(const NodePtr& node, const std::string& new_name) {
			if (!node) throw std::invalid_argument("La caja no puede ser nula.");
			const std::string old_name = node->getName();
			if (old_name == new_name) return;
			const NodeLocator at = locate(derived().graph(), node);
			node->setName(new_name);
			commitLocal({
				[at, old_name](GraphicalHypergraph& g) { resolve(g, at)->setName(old_name); },
				[at, new_name](GraphicalHypergraph& g) { resolve(g, at)->setName(new_name); } });
		}

		// ── setNodeAttributes ─────────────────────────────────────────────────────
		//
		// Sets the node's attributes (name, shape, colours, font size and fire
		// state all at once). Local change: undone without copying the graph.
		//
		// A new shape changes the node's width and height, so the layout is
		// recomputed in that case, which may re-order the bars around the node's
		// layer. The entry remembers that re-ordering, so undo and redo bring back
		// exactly the drawing there was, not just an equivalent one.
		void setNodeAttributes(const NodePtr& node, const NodeAttributes& attributes) {
			if (!node) throw std::invalid_argument("La caja no puede ser nula.");
			auto& graph = derived().graph();
			const NodeAttributes old_attributes = node->getAttributes();
			if (old_attributes == attributes) return;
			const NodeLocator at = locate(graph, node);

			if (node->getShape() == attributes.shape) {
				node->setAttributes(attributes);
				commitLocal({
					[at, old_attributes](GraphicalHypergraph& g) { resolve(g, at)->setAttributes(old_attributes); },
					[at, attributes](GraphicalHypergraph& g) { resolve(g, at)->setAttributes(attributes); } });
				return;
			}

			const int layer = node->getLayer();
			std::set<int> mip_layers{ layer };
			if (layer > 0) mip_layers.insert(layer - 1);
			std::map<int, std::vector<HyperedgePtr>> old_orders;
			for (int l : mip_layers) old_orders[l] = graph.getLayerData(l).outgoing_edges;

			node->setAttributes(attributes);
			try {
				graph.computeLayout(mip_layers);
			}
			catch (...) {
				// Leave the graph as it was: the old attributes, bars and drawing.
				node->setAttributes(old_attributes);
				for (const auto& [l, order] : old_orders) graph.setHyperedgeOrder(l, order);
				graph.computeLayout({});
				throw;
			}

			// moved_from[l][i]: where the bar now at position i of layer l was before.
			std::map<int, std::vector<int>> moved_from;
			for (const auto& [l, old_order] : old_orders) {
				auto& indices = moved_from[l];
				for (const auto& e : graph.getLayerData(l).outgoing_edges)
					indices.push_back(static_cast<int>(
						std::find(old_order.begin(), old_order.end(), e) - old_order.begin()));
			}

			commitLocal({
				[at, old_attributes, moved_from](GraphicalHypergraph& g) {
					resolve(g, at)->setAttributes(old_attributes);
					for (const auto& [l, from] : moved_from) {
						const auto& now = g.getLayerData(l).outgoing_edges;
						std::vector<HyperedgePtr> before(now.size());
						for (size_t i = 0; i < now.size(); ++i) before.at(from.at(i)) = now[i];
						g.setHyperedgeOrder(l, before);
					}
					g.computeLayout({});
				},
				[at, attributes, moved_from](GraphicalHypergraph& g) {
					resolve(g, at)->setAttributes(attributes);
					for (const auto& [l, from] : moved_from) {
						const auto& now = g.getLayerData(l).outgoing_edges;
						std::vector<HyperedgePtr> after;
						for (int i : from) after.push_back(now.at(i));
						g.setHyperedgeOrder(l, after);
					}
					g.computeLayout({});
				} });
		}

		// ── setHyperedgeContinuous ────────────────────────────────────────────────
		//
		// Draws the whole connection with a continuous (true) or discontinuous
		// (false) line, overriding any uncertain ends. Purely cosmetic, so the
		// layout is untouched. Local change: undone without copying the graph.
		// edge must be an original hyperedge (a segment throws).
		void setHyperedgeContinuous(const HyperedgePtr& edge, bool continuous) {
			if (!edge) throw std::invalid_argument("La conexión no puede ser nula.");
			if (edge->isSegment()) throw std::logic_error("Solo se puede cambiar la línea de la conexión completa.");
			if (continuous ? edge->isContinuous() : edge->allEndsUncertain()) return;

			const EdgeLocator at = locate(derived().graph(), edge);
			const LineStyle before = lineStyleOf(derived().graph(), *edge);
			edge->setContinuous(continuous);
			derived().graph().refreshUncertainPorts();
			commitLocal(lineStyleChange(at, before, lineStyleOf(derived().graph(), *edge)));
		}

		// ── isConnectionEndUncertain ──────────────────────────────────────────────
		//
		// Whether the connection is doubted at node. (Dashed as a whole is every
		// end doubted, so there is nothing else to look at.)
		static bool isConnectionEndUncertain(const HyperedgePtr& edge, const NodePtr& node) {
			if (!edge || !node) return false;
			return edge->containsSource(node) ? edge->isSourceUncertain(node.get())
			                                  : edge->isTargetUncertain(node.get());
		}

		// ── setConnectionEndUncertain ─────────────────────────────────────────────
		//
		// Marks (or unmarks) the connection as uncertain at node, one of its sources
		// or targets. A connection drawn discontinuous as a whole counts as doubted
		// at every end, so unmarking one end of it keeps the others marked; and
		// marking every end is stored as "discontinuous as a whole". Undoable.
		void setConnectionEndUncertain(const HyperedgePtr& edge, const NodePtr& node, bool uncertain) {
			if (!edge || !node) throw std::invalid_argument("La conexión y la caja no pueden ser nulas.");
			if (edge->isSegment()) throw std::logic_error("Solo se puede marcar como dudosa la conexión completa.");
			const bool as_source = edge->containsSource(node);
			if (!as_source && !edge->containsTarget(node))
				throw std::invalid_argument("La caja no forma parte de esta conexión.");
			if (isConnectionEndUncertain(edge, node) == uncertain) return;

			auto& graph = derived().graph();
			const EdgeLocator at = locate(graph, edge);
			const LineStyle before = lineStyleOf(graph, *edge);
			if (as_source) edge->setSourceUncertain(node, uncertain);
			else           edge->setTargetUncertain(node, uncertain);

			graph.refreshUncertainPorts();
			commitLocal(lineStyleChange(at, before, lineStyleOf(graph, *edge)));
		}

		// ── addConnection ─────────────────────────────────────────────────────────
		//
		// Clones the graph, attempts addConnection + computeLayout(), and commits
		// the snapshot only if both succeed.
		HyperedgePtr addConnection(const NodePtr& parent, const NodePtr& child) {
			auto saved = derived().takeSnapshot();
			try {
				std::set<int> mip_layers;
				HyperedgePtr result = derived().graph().addConnection(parent, child, &mip_layers);
				derived().graph().computeLayout(mip_layers);
				commitSnapshot(std::move(saved));
				return result;
			}
			catch (...) {
				throw;
			}
		}

		// ── addSourceToEdge ───────────────────────────────────────────────────────
		//
		// Clones the graph, attempts addSourceToEdge (resolving segments to their
		// origin) + computeLayout(), and commits the snapshot only if both succeed.
		void addSourceToEdge(const HyperedgePtr& edge, const NodePtr& source) {
			auto saved = derived().takeSnapshot();
			try {
				std::set<int> mip_layers;
				if (edge->isSegment()) {
					HyperedgePtr origin = edge->getOrigin().lock();
					derived().graph().addSourceToEdge(origin, source, &mip_layers);
				}
				else {
					derived().graph().addSourceToEdge(edge, source, &mip_layers);
				}
				derived().graph().computeLayout(mip_layers);
			}
			catch (...) {
				throw;
			}
			commitSnapshot(std::move(saved));
		}

		// ── addTargetToEdge ───────────────────────────────────────────────────────
		//
		// Clones the graph, attempts addTargetToEdge (resolving segments to their
		// origin) + computeLayout(), and commits the snapshot only if both succeed.
		void addTargetToEdge(const HyperedgePtr& edge, const NodePtr& target) {
			auto saved = derived().takeSnapshot();
			try {
				std::set<int> mip_layers;
				if (edge->isSegment()) {
					HyperedgePtr origin = edge->getOrigin().lock();
					derived().graph().addTargetToEdge(origin, target, &mip_layers);
				}
				else {
					derived().graph().addTargetToEdge(edge, target, &mip_layers);
				}
				derived().graph().computeLayout(mip_layers);
			}
			catch (...) {
				throw;
			}
			commitSnapshot(std::move(saved));
		}

		// ── removeNode ────────────────────────────────────────────────────────────
		//
		// Clones the graph, attempts removeNode + computeLayout(), and commits the
		// snapshot only if both succeed.
		void removeNode(const NodePtr& node) {
			auto saved = derived().takeSnapshot();
			try {
				std::set<int> mip_layers;
				derived().graph().removeNode(node, &mip_layers);
				derived().graph().computeLayout(mip_layers);
			}
			catch (...) {
				throw;
			}
			commitSnapshot(std::move(saved));
		}

		// ── removeConnection ──────────────────────────────────────────────────────
		//
		// Clones the graph, attempts removeConnection + computeLayout(), and commits
		// the snapshot only if both succeed.
		void removeConnection(const NodePtr& parent, const NodePtr& child) {
			auto saved = derived().takeSnapshot();
			try {
				std::set<int> mip_layers;
				derived().graph().removeConnection(parent, child, &mip_layers);
				derived().graph().computeLayout(mip_layers);
			}
			catch (...) {
				throw;
			}
			commitSnapshot(std::move(saved));
		}

		// ── removeSourceFromHyperedge ────────────────────────────────────────────
		//
		// Clones the graph, attempts removeSourceFromHyperedge (resolving segments)
		// + computeLayout(), and commits the snapshot only if both succeed.
		void removeSourceFromHyperedge(const HyperedgePtr& edge, const NodePtr& source) {
			auto saved = derived().takeSnapshot();
			try {
				std::set<int> mip_layers;
				if (edge->isSegment()) {
					HyperedgePtr origin = edge->getOrigin().lock();
					derived().graph().removeSourcesFromHyperedge(origin, { source.get() }, true, &mip_layers);
				}
				else {
					derived().graph().removeSourcesFromHyperedge(edge, { source.get() }, true, &mip_layers);
				}
				derived().graph().computeLayout(mip_layers);
			}
			catch (...) {
				throw;
			}
			commitSnapshot(std::move(saved));
		}

		// ── removeTargetFromHyperedge ────────────────────────────────────────────
		//
		// Clones the graph, attempts removeTargetFromHyperedge (resolving segments)
		// + computeLayout(), and commits the snapshot only if both succeed.
		void removeTargetFromHyperedge(const HyperedgePtr& edge, const NodePtr& target) {
			auto saved = derived().takeSnapshot();
			try {
				std::set<int> mip_layers;
				if (edge->isSegment()) {
					HyperedgePtr origin = edge->getOrigin().lock();
					derived().graph().removeTargetsFromHyperedge(origin, { target.get() }, true, &mip_layers);
				}
				else {
					derived().graph().removeTargetsFromHyperedge(edge, { target.get() }, true, &mip_layers);
				}
				derived().graph().computeLayout(mip_layers);
			}
			catch (...) {
				throw;
			}
			commitSnapshot(std::move(saved));
		}

		// ── removeHyperedge ───────────────────────────────────────────────────────
		//
		// Clones the graph, collects all targets of the origin edge, removes them
		// all (destroying the entire hyperedge) + computeLayout(), and commits the
		// snapshot only if both succeed.
		void removeHyperedge(const HyperedgePtr& edge) {
			auto saved = derived().takeSnapshot();
			try {
				HyperedgePtr origin = edge->isSegment() ? edge->getOrigin().lock() : edge;
				auto targets = origin->getTargets();
				std::unordered_set<Node*> targets_set;
				for (const auto& t : targets)
					targets_set.insert(t.get());
				std::set<int> mip_layers;
				derived().graph().removeTargetsFromHyperedge(origin, targets_set, true, &mip_layers);
				derived().graph().computeLayout(mip_layers);
			}
			catch (...) {
				throw;
			}
			commitSnapshot(std::move(saved));
		}

		// ── fuseNodes ─────────────────────────────────────────────────────────────
		//
		// Clones the graph, attempts fuseNodes + computeLayout(), and commits the
		// snapshot only if both succeed. The fused node takes new_attributes in full.
		void fuseNodes(const NodePtr& node1, const NodePtr& node2,
			const NodeAttributes& new_attributes)
		{
			auto saved = derived().takeSnapshot();
			try {
				std::set<int> mip_layers;
				derived().graph().fuseNodes(node1, node2, new_attributes, &mip_layers);
				derived().graph().computeLayout(mip_layers);
			}
			catch (...) {
				throw;
			}
			commitSnapshot(std::move(saved));
		}

		// ── Blocks: moving and removing one ───────────────────────────────────────
		//
		// The block (connected component) that contains box, moved sideways to
		// click_x, up or down to top_layer, or both, or removed with all its boxes
		// and connections (see GraphicalHypergraph's "Blocks"; they lay the graph
		// out themselves). Undoable.
		void moveComponent(const NodePtr& box, double click_x) {
			auto saved = derived().takeSnapshot();
			derived().graph().moveComponent(box.get(), click_x);
			commitSnapshot(std::move(saved));
		}

		void moveComponentToLayer(const NodePtr& box, int top_layer) {
			auto saved = derived().takeSnapshot();
			derived().graph().moveComponentToLayer(box.get(), top_layer);
			commitSnapshot(std::move(saved));
		}

		void moveComponent(const NodePtr& box, double click_x, int top_layer) {
			auto saved = derived().takeSnapshot();
			derived().graph().moveComponent(box.get(), click_x, top_layer);
			commitSnapshot(std::move(saved));
		}

		void removeComponent(const NodePtr& box) {
			auto saved = derived().takeSnapshot();
			derived().graph().removeComponent(box.get());
			commitSnapshot(std::move(saved));
		}

		// ── minimizeCrossings ("Minimize (fast)") ───────────────────────────────────────
		//
		// Clones the graph, runs GraphicalHypergraph::minimizeCrossings + computeLayout(), 
		// and commits the snapshot only if both succeed. Although minimizeCrossings does
		// not change the topology it does change the visible ordering of nodes, which
		// the user may want to undo. minimmizeCrossings call will last 5 seconds at most.
		int minimizeCrossings() {
			auto saved = derived().takeSnapshot();
			try {
				int crossings = derived().graph().minimizeCrossings();
				derived().graph().computeLayout();
				commitSnapshot(std::move(saved));
				return crossings;
			}
			catch (...) {
				throw;
			}
		}

		// ── minimizeCrossingsInterruptible ("Minimize (slow)") ───────────────────
		//
		// Same as minimizeCrossings function above, except the exact solve runs 
		// with NO time limit, stoppable instead via `token`.
		//
		// IMPORTANT (threading): this method itself still blocks synchronously
		// until the exact solve stops, one way or another -- it does not spawn a
		// thread. The caller MUST invoke this from a worker thread, not the UI
		// thread, or the UI will be unable to process the Pause click (or
		// anything else) while this runs. Do not call this again, on any editor,
		// before a previously-armed call has actually started and returned.
		int minimizeCrossingsInterruptible(ILPCancellationToken& token) {
			auto saved = derived().takeSnapshot();
			try {
				beginInterruptibleILP(token);
				int crossings = derived().graph().minimizeCrossings();
				derived().graph().computeLayout();
				commitSnapshot(std::move(saved));
				return crossings;
			}
			catch (...) {
				throw;
			}
		}

		// ── computeBracketedX ─────────────────────────────────────────────────────
		//
		// Translates the user's raw target x (chosen against the destination layer
		// as it looked BEFORE the vertical move) into an x that reproduces their
		// actual intent: "place node between these two neighbors." Brackets against
		// old_nodes (the pre-move, x-sorted roster of whichever old layer node's
		// destination corresponds to), using new_layer_nodes/new_layout (the live,
		// post-move roster and coordinates) to compute the final target.
		double computeBracketedX(const std::vector<NodePtr>& old_nodes,
			const std::unordered_map<Node*, NodeLayout>& old_layout,
			const std::vector<NodePtr>& new_layer_nodes,
			const std::unordered_map<Node*, NodeLayout>& new_layout,
			const NodePtr& node,
			double new_x_coordinate) const {
			std::unordered_set<Node*> new_layer_members;
			for (const auto& n : new_layer_nodes) new_layer_members.insert(n.get());

			std::vector<NodePtr> candidates;
			candidates.reserve(old_nodes.size());
			for (const auto& n : old_nodes) {
				if (n.get() == node.get()) continue;
				if (new_layer_members.count(n.get())) candidates.push_back(n);
			}

			NodePtr left, right;
			for (const auto& n : candidates) {
				if (old_layout.at(n.get()).x <= new_x_coordinate) left = n;
				else { right = n; break; }
			}

			auto index_excluding_node = [&](const NodePtr& target) -> int {
				int idx = 0;
				for (const auto& n : new_layer_nodes) {
					if (n.get() == node.get()) continue;
					if (n.get() == target.get()) return idx;
					++idx;
				}
				return -1;
				};
			auto min_max_excluding_node = [&]() {
				double mn = std::numeric_limits<double>::max();
				double mx = std::numeric_limits<double>::lowest();
				for (const auto& n : new_layer_nodes) {
					if (n.get() == node.get()) continue;
					double x = new_layout.at(n.get()).x;
					mn = std::min(mn, x);
					mx = std::max(mx, x);
				}
				return std::make_pair(mn, mx);
				};

			if (!left && right) {
				return min_max_excluding_node().first - 1.0;
			}
			if (left && !right) {
				return min_max_excluding_node().second + 1.0;
			}
			if (left && right) {
				int li = index_excluding_node(left);
				int ri = index_excluding_node(right);
				if (li >= 0 && ri >= 0) {
					return (new_layout.at(left.get()).x + new_layout.at(right.get()).x) / 2.0;
				}
				double old_min = old_layout.at(old_nodes.front().get()).x;
				double old_max = old_layout.at(old_nodes.back().get()).x;
				double old_width = old_max - old_min;
				if (old_width > 0.0) {
					auto [new_min, new_max] = min_max_excluding_node();
					double new_width = new_max - new_min;
					double relative = (new_x_coordinate - old_min) / old_width;
					return new_min + relative * new_width;
				}
			}
			return new_x_coordinate;
		}

		// ── relocateNode ───────────────────────────────────────────────────────────────
		//
		// Clones the graph, attempts to relocate both vertically and horizontally at the
		// same time, and commits the snapshot only on success.
		void relocateNode(const NodePtr& node, double new_x_coordinate, double new_y_coordinate) {
			auto saved = derived().takeSnapshot();
			std::set<int> mip_layers;
			try {
				auto& graph = derived().graph();

				std::map<int, std::vector<NodePtr>> old_layers_nodes;
				std::unordered_map<Node*, int> old_layer_of_node;
				for (const auto& [layer_idx, layer_data] : graph.getLayers()) {
					old_layers_nodes[layer_idx] = layer_data.nodes;
					for (const auto& n : layer_data.nodes) old_layer_of_node[n.get()] = layer_idx;
				}
				const auto old_layout = graph.getNodeLayout();

				graph.relocateNodeToLayer(node, new_y_coordinate, &mip_layers);
				graph.assignXCoordinates();

				double transformed_x = new_x_coordinate;
				const auto& new_layer_nodes = graph.getLayerData(node->getLayer()).nodes;

				auto old_layer_lookup = old_layer_of_node.end();
				for (const auto& n : new_layer_nodes) {
					if (n.get() == node.get()) continue;
					auto it = old_layer_of_node.find(n.get());
					if (it != old_layer_of_node.end()) { old_layer_lookup = it; break; }
				}

				if (old_layer_lookup != old_layer_of_node.end()) {
					transformed_x = computeBracketedX(
						old_layers_nodes.at(old_layer_lookup->second), old_layout,
						new_layer_nodes, graph.getNodeLayout(),
						node, new_x_coordinate);
				}

				try {
					graph.relocateNodeInLayer(node, transformed_x, &mip_layers);
					graph.computeLayout(mip_layers);
				}
				catch (...) {
					graph.computeLayout(mip_layers);
				}
			}
			catch (const std::invalid_argument&) {
				try {
					derived().graph().relocateNodeInLayer(node, new_x_coordinate, &mip_layers);
					derived().graph().computeLayout(mip_layers);
				}
				catch (...) { throw; }
			}
			catch (...) { throw; }

			commitSnapshot(std::move(saved));
		}

		// -─ onMutated callback ───────────────────────────────────────────────────────
		//
		// Callback function for when the graph changes: after every committed
		// change, undo and redo. The graphical engine uses it to refresh whether the
		// project has unsaved changes (see isAtSavedState).
		void setOnMutated(std::function<void()> callback) {
			on_mutated_ = std::move(callback);
		}

		void notifyMutated() {
			if (on_mutated_) on_mutated_();
		}

	protected:
		// ── commitSnapshot ────────────────────────────────────────────────────────
		//
		// Records a structural change: snapshot is the graph as it was before it.
		// Only called after the change has fully succeeded — never on failure.
		template <typename Snapshot>
		void commitSnapshot(Snapshot&& snapshot) {
			using Entry = typename std::decay_t<decltype(derived().past_)>::value_type;
			push(Entry{ std::forward<Snapshot>(snapshot) });
		}

		// ── commitLocal ───────────────────────────────────────────────────────────
		//
		// Records a local change that has already been applied to the live graph.
		void commitLocal(LocalChange change) {
			using Entry = typename std::decay_t<decltype(derived().past_)>::value_type;
			push(Entry{ std::move(change) });
		}

	private:
		// ── Locators ──────────────────────────────────────────────────────────────
		//
		// How a LocalChange finds its box or connection again in whichever copy of
		// the graph is live when it is undone or redone. A box is found by its
		// layer and position in it; a connection, as the original hyperedge with
		// exactly those sources and targets.
		struct NodeLocator {
			int layer = 0;
			int index = 0;
		};

		struct EdgeLocator {
			std::vector<NodeLocator> sources;
			std::vector<NodeLocator> targets;
		};

		static NodeLocator locate(const GraphicalHypergraph& g, const NodePtr& node) {
			const auto& nodes = g.getLayerData(node->getLayer()).nodes;
			const auto it = std::find(nodes.begin(), nodes.end(), node);
			if (it == nodes.end())
				throw std::invalid_argument("La caja no forma parte de este esquema.");
			return { node->getLayer(), static_cast<int>(it - nodes.begin()) };
		}

		static NodePtr resolve(const GraphicalHypergraph& g, const NodeLocator& at) {
			const auto& nodes = g.getLayerData(at.layer).nodes;
			if (at.index < 0 || at.index >= static_cast<int>(nodes.size()))
				throw std::logic_error("El historial de cambios no coincide con el esquema.");
			return nodes[at.index];
		}

		static EdgeLocator locate(const GraphicalHypergraph& g, const HyperedgePtr& edge) {
			EdgeLocator at;
			for (const auto& s : edge->getSources()) at.sources.push_back(locate(g, s));
			for (const auto& t : edge->getTargets()) at.targets.push_back(locate(g, t));
			return at;
		}

		static HyperedgePtr resolve(const GraphicalHypergraph& g, const EdgeLocator& at) {
			std::vector<NodePtr> sources, targets;
			for (const auto& s : at.sources) sources.push_back(resolve(g, s));
			for (const auto& t : at.targets) targets.push_back(resolve(g, t));
			for (const auto& e : g.getAllHyperedges()) {
				if (e->isSegment()) continue;
				if (e->getSources().size() != sources.size() || e->getTargets().size() != targets.size()) continue;
				bool same = true;
				for (const auto& s : sources) same = same && e->containsSource(s);
				for (const auto& t : targets) same = same && e->containsTarget(t);
				if (same) return e;
			}
			throw std::logic_error("El historial de cambios no coincide con el esquema.");
		}

		// ── Line style of a connection ────────────────────────────────────────────
		//
		// Everything setHyperedgeContinuous and setConnectionEndUncertain can
		// change: the whole-line style and the doubted ends.
		struct LineStyle {
			std::vector<NodeLocator> uncertain_sources;
			std::vector<NodeLocator> uncertain_targets;
		};

		static LineStyle lineStyleOf(const GraphicalHypergraph& g, const Hyperedge& edge) {
			LineStyle style;
			for (const auto& s : edge.getSources())
				if (edge.isSourceUncertain(s.get())) style.uncertain_sources.push_back(locate(g, s));
			for (const auto& t : edge.getTargets())
				if (edge.isTargetUncertain(t.get())) style.uncertain_targets.push_back(locate(g, t));
			return style;
		}

		static void applyLineStyle(GraphicalHypergraph& g, const EdgeLocator& at, const LineStyle& style) {
			const HyperedgePtr edge = resolve(g, at);
			// Exactly the recorded marks: marking them one by one could let the
			// "only end on a side" rule add more than there were.
			std::vector<NodePtr> sources, targets;
			for (const auto& s : style.uncertain_sources) sources.push_back(resolve(g, s));
			for (const auto& t : style.uncertain_targets) targets.push_back(resolve(g, t));
			edge->setUncertainEnds(sources, targets);
			g.refreshUncertainPorts();
		}

		static LocalChange lineStyleChange(const EdgeLocator& at, const LineStyle& before, const LineStyle& after) {
			return {
				[at, before](GraphicalHypergraph& g) { applyLineStyle(g, at, before); },
				[at, after](GraphicalHypergraph& g) { applyLineStyle(g, at, after); } };
		}

		// ── History bookkeeping ───────────────────────────────────────────────────

		// Gives the editor a new state and puts entry on top of the undo stack.
		template <typename Entry>
		void push(Entry&& entry) {
			entry.before = state_;
			state_ = ++last_state_;
			entry.after = state_;
			auto& past = derived().past_;
			past.push_back(std::move(entry));
			if (static_cast<int>(past.size()) > MAX_HISTORY)
				past.pop_front();
			derived().future_.clear();
			notifyMutated();
		}

		// Undoes (or redoes) the step on top of from and moves it onto to. A
		// snapshot entry swaps places with the live graph, so it then holds the
		// state that redo (or undo) will bring back. If undoing a local change
		// fails, the step stays where it was.
		template <typename Stack>
		void step(Stack& from, Stack& to, bool undoing) {
			if (from.empty())
				throw std::logic_error(undoing ? "No hay nada que deshacer." : "No hay nada que rehacer.");
			auto& entry = from.back();
			if (auto* local = std::get_if<LocalChange>(&entry.change)) {
				GraphicalHypergraph& g = derived().graph();
				if (undoing) local->undo(g);
				else         local->redo(g);
			}
			else {
				auto& snapshot = std::get<0>(entry.change);
				auto current = derived().takeSnapshot();
				derived().installSnapshot(std::move(snapshot));
				snapshot = std::move(current);
			}
			state_ = undoing ? entry.before : entry.after;
			to.push_back(std::move(entry));
			from.pop_back();
			if (static_cast<int>(to.size()) > MAX_HISTORY)
				to.pop_front();
			notifyMutated();
		}
		// Safely downcast to the derived class. This is a common CRTP pattern that
		// allows the base class to call methods implemented in the derived class
		// without virtual dispatch.
		Derived& derived() { return static_cast<Derived&>(*this); }

		// Const overload — used by read-only methods (getId, getLayerCount, etc.).
		const Derived& derived() const { return static_cast<const Derived&>(*this); }

		// The callback function for notifying the project of unsaved mutations.
		std::function<void()> on_mutated_;

		// State ids (see isAtSavedState). 0 is the state the editor starts in.
		std::uint64_t state_ = 0;
		std::uint64_t saved_state_ = 0;
		std::uint64_t last_state_ = 0;
	};

} // namespace app_logic