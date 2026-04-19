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

	// ── buildG1 ───────────────────────────────────────────────────────────────────
	//
	// Registers all hypergraph nodes at layers >= anchor_layer into G1.
	// Any node is sent to the virtual layer 2*L, where L is the original hypergraph layer.
	// Furthermore, for each short hyperedge, it inserts a hub node with odd virtual layer
	// 2*L+1, with L being the original layer of the hyperedge in the hypergraph; and inserts
	// binary edges from each source to the hub and from the hub to each target in the form of
	// an adjacency cache.
	void buildG1(SiftState& S, const std::map<int, LayerData>& layers, int start_layer);

	// ── Chain detection ───────────────────────────────────────────────────────────
	//
	// A dummy chain starts at a dummy node with exactly one child that is also a
	// dummy and whose only parent is this node.  Detection uses Node::getChildren /
	// Node::getParents on the original node graph.  In G1 the hub nodes inserted
	// between adjacent dummies are automatically absorbed because they satisfy
	// isChainContinuation (one G1-predecessor, one G1-successor, predecessor has
	// only one successor).
	bool isDummyChainStart(Node* n);

	std::vector<int> collectChainG1Nodes(int start_g1, const SiftState& S, std::unordered_set<int>& visited);

	// ── buildBlocks ───────────────────────────────────────────────────────────────
	//
	// 1) Identify dummy chains -> one block each.
	// 2) Every remaining G1 node -> singleton block.
	void buildBlocks(SiftState& S);

	// ── Layer and hub sorting ─────────────────────────────────────────────────────
	//
	// We sort the original nodes in each layer based on the position of their parents in the upper
	// layer, with ties broken by the original position in the LayerData. In order to do this, given some
	// node A in the lower layer, we compute left(A) = minimum position of the parents of A in the upper layer.
	// Then, we sort the nodes in the lower layer based on left(A) values in ascending order.
	//
	// We sort the hubs lexicographically based on the minimum position of their parents in the upper layer and
	// in case of ties, based on the minimum position of their children in the lower layer. This order is well defined
	// due to imposed restrictions on the hypergraph structure: if two hyperedges share a source, then they cannot share
	// a target, and vice versa. Therefore, if two hubs share a parent, they cannot share a child, so there cannot be
	// ties in both the upper and lower layer positions.
	std::unordered_map<Node*, int> sortLayer(
		const std::unordered_map<Node*, int>& upper_pos,
		std::vector<NodePtr>& lower,
		std::vector<HyperedgePtr>& edges);

	void sortHubs(
		SiftState& S,
		const std::unordered_map<Node*, int>& upper_pos,
		const std::unordered_map<Node*, int>& lower_pos,
		int layer);

	// ── buildBlockOrder ───────────────────────────────────────────────────────────
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
	void buildBlockOrder(BlockList& B, SiftState& S, std::map<int, LayerData>& layers, int start_layer);

	// ── sortAdjacencies ───────────────────────────────────────────────────────────
	//
	// Builds N±/I± for every block from the G1 adjacency cache.
	// N_minus(A) = blocks connected to top(A) from above, sorted by pi.
	// N_plus(A)  = blocks connected to bottom(A) from below, sorted by pi.
	// I_minus/I_plus are cross-reference arrays for O(1) post-swap updates.
	void sortAdjacencies(SiftState& S, const BlockList& B);

	// ── uswap ─────────────────────────────────────────────────────────────────────
	//
	// Net crossing delta when block A (with neighbours Na) swaps right past block B
	// (with neighbours Nb).  Both lists are sorted by ascending pi.
	// Positive = more crossings after swap; negative = fewer.
	int uswap(const SiftState& S, const std::vector<int>& Na, const std::vector<int>& Nb, const std::vector<int>& pi);

	// ── updateAdjacency ───────────────────────────────────────────────────────────
	//
	// After swapping A and B, repair the adjacency lists of their common neighbours
	// in direction d so N± lists remain sorted by pi.
	void updateAdjacency(SiftState& S, Block& A, Block& B, int a, int b, bool minus_direction);

	// ── siftingSwap ───────────────────────────────────────────────────────────────
	//
	// Swaps adjacent blocks A (left) and B (right), updates pi and adjacency lists,
	// returns the change in crossing count.
	int getNodeAtLevel(const SiftState& S, Block& block, int level);

	int siftingSwap(SiftState& S, int a_id, int b_id);

	// ── siftingStep ───────────────────────────────────────────────────────────────
	//
	// Places A at the front of B', sweeps it right one swap at a time, records the
	// position p* with the minimum cumulative crossing delta, then rotates A to p*.
	int siftingStep(SiftState& S, BlockList& B, int a_id);


	// ── crossing count ────────────────────────────────────────────────────────────
	//
	// Counts edge crossings between two layers of a bipartite graph.
	// Implements the BJM algorithm from Barth, Mutzel & Jünger (2004).
	// Complexity: O(|E| log |V_small|)
	int countBilayerCrossings(const std::vector<int>& layer1, const std::vector<int>& layer2, const std::vector<std::vector<int>>& connections_out);

	void orderLayersByBlockOrder(SiftState& S, const BlockList& B);

	// Counts total crossings in the current block order B.
	int countTotalCrossings(SiftState& S, const BlockList& B);

} // namespace sifting_internal