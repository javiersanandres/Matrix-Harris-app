#pragma once

// ==================================================================================
// GlobalSifting.h
//
// Internal data structures and helper functions for the Global Sifting algorithm:
//
// Bachmaier, C., Brandenburg, F. J., Brunner, W., & Hübner, F. (2011).
// "A Global k-Level Crossing Reduction Algorithm."
// In: Graph Drawing (GD 2010), LNCS 6502, pp. 70-81. Springer.
// DOI: 10.1007/978-3-642-18469-7_7
//
// ==================================================================================

#include "GraphicalHypergraph.h"
#include <algorithm>
#include <climits>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sifting_internal {

	using namespace hypergraph_logic;

	// ── G1 node ──────────────────────────────────────────────────────────────────
	struct G1Node {
		Node* original = nullptr;   // non-null for real/dummy nodes from the base class
		int   g1_layer = -1;        // virtual G1 layer: 2*L for nodes, 2*L+1 for hubs
		int   block_id = -1;        // which block owns this node

		G1Node(Node* original, int g1_layer)
			: original(original)
			, g1_layer(g1_layer)
			, block_id(-1)
		{
		}
	};

	// ── Block ─────────────────────────────────────────────────────────────────────
	//
	// The blocks we are considering are:
	//   • A dummy chain (dummy -> hub -> dummy -> ...)
	//   • A singleton real/dummy node or hub.
	//
	// This is pretty similar to what the paper describes, except that we allow blocks
	// to be composed by dummy nodes. This is because we are dealing with a hypergraph.
	// To illustrate this, consider the following example:
	// {Source1}->{Dummy1}
	// {Dummy1, Source2}->{Dummy2}
	// {Dummy2, Source3}->{Target1}
	// In this case, Dummy1 and Dummy2 would not form a dummy chain because they are
	// not directly linked together, there are some other non-dummy nodes which cannot
	// be avoided.
	//
	// However, in a normal graph, a long edge splits in a chain of dummies by definition.
	//
	// g1_nodes is ordered top-to-bottom by g1_layer number.
	// N_minus denotes the parent blocks of the block, sorted by their position in B.
	// N_plus denotes the child blocks of the block, sorted by their position in B.
	struct Block {
		std::vector<int> g1_nodes; // Indices of G1 nodes in this block.

		int upper() const { return g1_nodes.front(); }
		int lower() const { return g1_nodes.back(); }

		std::vector<int> N_minus; // lower of parents of block (sorted by pi)
		std::vector<int> N_plus;  // upper of children of block (sorted by pi)
		std::vector<int> I_minus; // cross-ref: I_minus[i] = position of upper() in the vector N_plus(N_minus[i])
		std::vector<int> I_plus;  // cross-ref: I_plus[i]  = position of lower() in the vector N_minus(N_plus[i])

		Block(std::vector<int> nodes) : g1_nodes(std::move(nodes)) {}
	};

	using BlockList = std::vector<int>; // alias for ensuring clarity when we are talking about block orderings (pi)

	// ── Algorithm state ───────────────────────────────────────────────────────────

	struct SiftState {
		std::vector<G1Node> g1_nodes;
		std::vector<std::vector<int>> g1_in;        // g1_in[i] = parents of G1 node i (for quick lookup)
		std::vector<std::vector<int>> g1_out;       // g1_out[i] = children of G1 node i (for quick lookup)
		std::map<int, std::vector<int>> g1_layers;
		std::unordered_map<Node*, int> node_to_g1;  // original Node* -> G1 index
		std::vector<Block> blocks;
		std::vector<int> pi;                        // pi[block_id] = position in B
		int fixed_position_count = 0;               // number of blocks with fixed position (anchors)
	};

	// ── GlobalSifter ──────────────────────────────────────────────────────────────
	//
	// Encapsulates all state and algorithms required to run a full global-sifting
	// crossing-reduction pass over a range of layers [start_layer, end_layer].
	struct GlobalSifter {
		GlobalSifter(int start_layer, int end_layer, std::map<int, LayerData>& layers, bool order=true);

#ifdef GS_TEST
		// No-op constructor for unit tests. Skips the full pipeline so tests can
		// set S_ and B_ directly. layers_ is bound to a local dummy that is never
		// read by any of the sifting methods.
		GlobalSifter() : start_layer_(0), end_layer_(0), layers_(dummyLayers_()) {}
	private:
		static std::map<int, LayerData>& dummyLayers_() {
			static std::map<int, LayerData> d;
			return d;
		}
	public:
#endif

		SiftState S_;
		BlockList B_;

		// ── buildBlockOrder ──────────────────────────────────────────────────────────────────────────────
		//
		// As they suggest in the paper, to aim for better results in the sifting phase, it is
		// important to have some good initial order. They also suggest a topological sort of
		// blocks based on the lexicographical order determined by the layer number and the
		// position of the block in that layer. My implementation takes this into account.
		//
		// The key to understand the initialization policy developed here is to understand
		// how we locate nodes in layers when we have to relocate them. This happens in many
		// situations, all which involve relocation of nodes or splitting of edges. In that
		// case, what we do is adding the new nodes at the very end of the layer they belong to.
		// This means that many crossings will appear because the targets are located far away
		// from their sources. In order to mitigate this, we want to have an initial order where
		// sources and targets are as close as possible.
		// In this sense, we process the first layer with the seed order given by the original
		// order of the nodes in LayerData, and then we sort each subsequent layer based on the
		// order of the upper layer, with the function sortLayer. The hubs are located between
		// the upper and lower layer and they are sorted with the function sortHubs based on the
		// order of the upper and lower layers.
		//
		// This initialization policy ensures, for example, that whenever the client wants to change
		// the order between two nodes in the same layer, the minimizeCrossings function will not
		// oppose to this change by taking the nodes back to their original order (where they get less
		// crossings), but it will try to find a good order where the subgraph of the first node goes
		// before the subgraph of the second node. This is the intended behaviour.
		void buildBlockOrder(bool no_restriction = true);

		// Run up to sifting_rounds rounds of the global sifting sweep.
		void runSifting(int sifting_rounds);

		// Commit the block ordering back to LayerData::nodes for every layer
		// in [start_layer_, end_layer_].
		void writeBack();

		// Count and return the total number of crossings in the current ordering.
		int countCrossings();

		// Sift only the blocks associated with the given set of nodes to find the
		// best position for each of them, without moving any other block.
		// The sifting step for each targeted block still moves it across all
		// non-targeted blocks to find its global optimum within the current ordering.
		// This is useful for edge splitting operations, where we want to find the best position
		// for dummy nodes while preserving the user's mental map as much as possible.
		void siftNodes(const std::vector<Node*>& nodes);

	protected:
		int start_layer_;
		int end_layer_;
		std::map<int, LayerData>& layers_;

		// ── G1 construction ───────────────────────────────────────────────────────
		//
		// Registers all hypergraph nodes at layers in [anchor_layer, end_layer_+1]
		// into G1. Any node is sent to the virtual layer 2*L, where L is the
		// original hypergraph layer. Furthermore, for each short hyperedge, it
		// inserts a hub node with odd virtual layer 2*L+1, with L being the original
		// layer of the hyperedge in the hypergraph; and inserts binary edges from
		// each source to the hub and from the hub to each target in the form of an
		// adjacency cache.
		//
		// Nodes at anchor_layer (= start_layer_ - 1, if any) are included as fixed
		// anchors: they appear in G1 but are never moved by the sifter.
		// Nodes at end_layer_ + 1 (if any) are also included because the edges
		// between end_layer_ and end_layer_ + 1 carry crossing information that is
		// needed to evaluate swaps on end_layer_. Since we will only make use of the
		// end_layer_ parameter for siftNodes function, it is not needed to declared 
		// them as fixed anchors, they will surely not be moved by the sifter.
		void buildG1();

		// ── Chain detection ───────────────────────────────────────────────────────
		//
		// A dummy chain starts at a dummy node with exactly one child that is also a
		// dummy and whose only parent is this node. The hubs in between the dummies
		// in the chain are also considered part of the block and they are guarenteed
		// to satisfy the same condition.
		static bool isDummyChainStart(Node* n);

		std::vector<int> collectChainG1Nodes(int start_g1, std::unordered_set<int>& visited) const;

		// ── Block construction ────────────────────────────────────────────────────
		//
		// 1) Identify dummy chains -> one block each.
		// 2) Every remaining G1 node -> singleton block.
		void buildBlocks();

		// ── Layer and hub sorting ─────────────────────────────────────────────────
		//
		// We sort the original nodes in each layer based on the position of their
		// parents in the upper layer, with ties broken by the original position in
		// the LayerData. In order to do this, given some node A in the lower layer,
		// we compute left(A) = minimum position of the parents of A in the upper
		// layer. Then, we sort the nodes in the lower layer based on left(A) values
		// in ascending order.
		//
		// We sort the hubs lexicographically based on the minimum position of their
		// parents in the upper layer and in case of ties, based on the minimum
		// position of their children in the lower layer. This order is well defined
		// due to imposed restrictions on the hypergraph structure: if two hyperedges
		// share a source, then they cannot share a target, and vice versa. Therefore,
		// if two hubs share a parent, they cannot share a child, so there cannot be
		// ties in both the upper and lower layer positions.
		static std::unordered_map<Node*, int> sortLayer(
			const std::unordered_map<Node*, int>& upper_pos,
			std::vector<NodePtr>& lower,
			std::vector<HyperedgePtr>& edges);

		void sortHubs(
			const std::unordered_map<Node*, int>& upper_pos,
			const std::unordered_map<Node*, int>& lower_pos,
			int layer);

		// ── Adjacency sorting ─────────────────────────────────────────────────────
		//
		// Builds N±/I± for every block from the G1 adjacency cache.
		// N_minus(A) = blocks connected to top(A) from above, sorted by pi.
		// N_plus(A)  = blocks connected to bottom(A) from below, sorted by pi.
		// I_minus/I_plus are cross-reference arrays for O(1) post-swap updates.
		void sortAdjacencies();

		// ── Swap primitives ───────────────────────────────────────────────────────
		//
		// uswap: net crossing delta when block A (with neighbours Na) swaps right
		//   past block B (with neighbours Nb). Both lists are sorted by ascending pi.
		//   Positive = more crossings after swap; negative = fewer.
		//
		// updateAdjacency: after swapping A and B, repair the adjacency lists of
		//   their common neighbours in direction d so N± lists remain sorted by pi.
		//
		// siftingSwap: swaps adjacent blocks A (left) and B (right), updates pi
		//   and adjacency lists, returns the change in crossing count.
		static int uswap(const SiftState& S,
			const std::vector<int>& Na,
			const std::vector<int>& Nb,
			const std::vector<int>& pi);

		static void updateAdjacency(SiftState& S, Block& A, Block& B,
			int a, int b, bool minus_direction);

		static int getNodeAtLevel(const SiftState& S, Block& block, int level);

		int siftingSwap(int a_id, int b_id);

		// ── Sifting step ──────────────────────────────────────────────────────────
		//
		// Places A at the front of B', sweeps it right one swap at a time, records
		// the position p* with the minimum cumulative crossing delta, then rotates
		// A to p*.
		int siftingStep(int a_id);

		// ── Crossing count ────────────────────────────────────────────────────────
		//
		// Counts edge crossings between two layers of a bipartite graph.
		// Implements the BJM algorithm from Barth, Mutzel & Jünger (2004).
		// Complexity: O(|E| log |V_small|)
		static int countBilayerCrossings(
			const std::vector<int>& layer1,
			const std::vector<int>& layer2,
			const std::vector<std::vector<int>>& connections_out);

		void orderLayersByBlockOrder();

		// Recursive helper for siftNodes — see minimizeCrossings.cpp for details.
		void siftNodesSearch(
			int movable_start, int nb, int last_block,
			const std::unordered_set<int>& movable_set,
			int chi, int& best_chi, BlockList& best_B);
	};

} // namespace sifting_internal