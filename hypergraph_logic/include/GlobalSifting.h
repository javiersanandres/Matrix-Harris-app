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

#include "Hypergraph.h"
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

		// False for blocks that reach the layers around the range (the anchor above,
		// end_layer+1 below): they are never sifted themselves, so their relative order
		// stays as it is, but any other block may still be swapped past them.
		bool movable = true;

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
	};

	// ── GlobalSifter ──────────────────────────────────────────────────────────────
	//
	// Encapsulates all state and algorithms required to run a full global-sifting
	// crossing-reduction pass over a range of layers [start_layer, end_layer].
	struct GlobalSifter {
		// order: run the Efficient Barycenter pass after building the block order. Only the
		// global minimization (minimizeCrossingsILP) asks for it; every other call keeps the
		// current orders as its starting point.
		// moved: the nodes the user just moved sideways, if any (see buildBlockOrder).
		// group: the only nodes sifted, empty for all of them. It must be closed (whole
		// connected components, see Hypergraph::crossingGroup): G1 holds only these nodes
		// and their connections, and writeBack puts them back into the places they had in
		// each layer, so every other node stays exactly where it was.
		// equivalences: the chains whose rule every order keeps (see "Equivalence chains"
		// below). Those not wholly in one row of G1 are left out.
		GlobalSifter(int start_layer, int end_layer, std::map<int, LayerData>& layers, bool order = true,
			const std::vector<Node*>& moved = {}, std::unordered_set<Node*> group = {},
			const std::vector<EquivalenceChain>& equivalences = {});
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
		// important to have some good initial order: a topological sort of the blocks based on
		// the lexicographical order determined by the layer number and the position of the block
		// in that layer.
		//
		// The block list B fixes every layer's order at once (a layer reads its nodes in the order
		// their blocks appear in B), so B has to be built so that it reproduces all of them. Simply
		// appending blocks layer by layer does not: a dummy chain would be appended at its top
		// layer and therefore land to the left of every block starting deeper, whatever that
		// layer's order said. Instead, each G1 row (node rows and hub rows alike) contributes the
		// rules "block of its i-th element before block of its (i+1)-th element", and B is a
		// topological sort of those rules, ties broken by (top row, position in it). Rules can
		// only contradict each other around guessed positions (two chains swapping sides between
		// two layers); the stuck block with the smallest key is then taken first anyway, and
		// sifting sorts out the rest.
		//
		// The rows contain every layer as it currently is. Nodes that never had a position have
		// already been placed by Hypergraph::placeUnpositionedNodes, so the current orders are
		// the user's mental map and sifting only fixes what is actually wrong.
		// When the user has just moved nodes sideways in their layer (moved, in the anchor layer:
		// a node, or a whole equivalence chain), each layer in [start_layer, end_layer] first
		// re-places their descendants so that they follow them (followMovedNode), without
		// touching the relative order of the rest:
		// minimizeCrossings does not take the subgraph back to where it was, nor reshuffles
		// parts of the drawing the move has nothing to do with.
		// Hub rows are always sorted with sortHubs from the rows around them.
		// A chain stays together through all of it (followMovedNode moves it as one, and
		// gatherChains repairs what contradicting rows may break).
		void buildBlockOrder(const std::vector<Node*>& moved = {});

		// ── buildBlockListFromRows ───────────────────────────────────────────────────────────────────
		//
		// The second half of buildBlockOrder: B as a topological sort of the current G1 row orders
		// (see above). Also used by runEfficientBarycenter, which sorts the rows themselves.
		void buildBlockListFromRows();

		// ── runEfficientBarycenter ─────────────────────────────────────────────────────────────────────────
		//
		// Runs Efficient Barycenter Algorithm as described in Dynamic Hierarchical
		// Graph Drawing by A. A. K. Ismaeel. This way, the block order produced by
		// buildBlockOrder() is re-seeded and improved, which could potentially result
		// in a better performance of Global Sifting.
		// It works on the real G1 rows: every node present in a row (chain dummies
		// passing through included) is sorted by the average position of its
		// neighbours, positions being normalized by row size so rows of different
		// sizes are comparable. Nodes of blocks that are not movable keep their
		// place. An equivalence chain is sorted as one unit, by the average position of
		// all its members' neighbours, and its members then by their own barycenters
		// within the places it takes. B is then rebuilt from the rows
		// (buildBlockListFromRows, gatherChains), and the result is only kept if it has
		// fewer crossings than the order it started from.
		void runEfficientBarycenter(int max_iterations = 100);

		// Run up to sifting_rounds rounds of the global sifting sweep (movable blocks only).
		// A chain is sifted as one unit (chainStep), its members reordered among
		// themselves only with reorder_chains: their order is something the user sets by
		// hand, so only the global minimization asks for it.
		void runSifting(int sifting_rounds, bool stability = true, bool reorder_chains = false);

		// Solve an ILP to minimize crossings.
		int runCrossingILP(double time_budget_seconds);

		// Commit the block ordering back to LayerData::nodes for every layer
		// in [start_layer_, end_layer_].
		void writeBack();

		// Counterpart to writeBack() for the ILP path in runCrossingILP():
		// commits the node order left in S_.g1_layers back to LayerData::nodes
		// for every real (even-keyed) row.
		void writeBackFromG1Order();

		// Count and return the total number of crossings in the current ordering, the
		// crossings of the equivalence lines with the dummies between their boxes included.
		int countCrossings();

		// Sift only the blocks associated with the given set of nodes to find the
		// best position for each of them, without moving any other block.
		// The sifting step for each targeted block still moves it across all
		// non-targeted blocks to find its global optimum within the current ordering.
		// This is useful for edge splitting operations, where we want to find the best position
		// for dummy nodes while preserving the user's mental map as much as possible.
		// A node in a chain brings the whole chain, which moves as one unit in its own order.
		void siftNodes(const std::vector<Node*>& nodes);

	protected:
		int start_layer_;
		int end_layer_;
		std::map<int, LayerData>& layers_;
		std::unordered_set<Node*> group_;   // empty: every node
		std::vector<int> initial_pi_;       // each block's position when sifting started

		bool inGroup(Node* n) const { return group_.empty() || group_.count(n) > 0; }

		// ── Equivalence chains ────────────────────────────────────────────────────
		//
		// The members of a chain are real nodes, so each is a block of its own, and all lie
		// in one row. The rule (only dummies between them) only concerns the blocks with a
		// node in that row: a block with a real node there is never left inside a chain
		// (between its first and last member), and one with a dummy there may be, at one
		// crossing (with the chain's line) for every chain it is inside.
		//
		// Since a real block only shows in its own row, moving it in B changes nothing but
		// that row's order: chains are kept together, and repaired, at no other cost.
		struct Chain {
			int row = -1;
			std::vector<int> blocks; // its members' blocks
		};
		std::vector<Chain> chains_;
		std::vector<int> chain_of_;                        // block -> chain, -1 for none
		std::unordered_map<Node*, int> chain_of_node_;     // member -> chain
		std::map<int, std::vector<int>> chains_at_row_;    // row -> chains

		int chainOf(int bid) const { return bid < static_cast<int>(chain_of_.size()) ? chain_of_[bid] : -1; }
		void buildChains(const std::vector<EquivalenceChain>& equivalences);

		// A chain's members, left to right in B.
		std::vector<int> orderedMembers(int c) const;

		// The crossings of every chain's line with the dummies between its members.
		// The rows must be in B's order (orderLayersByBlockOrder).
		int equivalenceCrossings() const;

		// Puts back together the chains that are not (only after B is rebuilt from rows
		// that contradict each other): a real node inside a chain goes to its nearer end,
		// as Hypergraph::keepEquivalencesTogether does in a layer.
		void gatherChains();

		// The chains that some blocks may end up inside while they move past their members
		// (the chains of the rows those blocks have nodes in, not their own), with how many
		// of those blocks have a dummy or a real node in its row and how many of its members
		// are to their left. passMember updates a chain when a member passes the moving
		// blocks (direction +1: to their left, -1: to their right), returning the change in
		// crossings and adding the real nodes it leaves inside, or takes out, to blocked.
		struct ChainWatch {
			int chain;
			int dummies = 0;
			int reals = 0;
			int left = 0;
		};
		std::vector<ChainWatch> watchChains(const std::vector<int>& moving, bool members_to_the_left) const;
		int passMember(std::vector<ChainWatch>& watch, int member, int direction, int& blocked) const;

		// Whether x, passing a chain's members, may stop between left and right: they are
		// members of one chain and x has a dummy in its row (which then crosses its line).
		bool dummyFitsBetween(int left, int right, int x);

		// ── chainStep ─────────────────────────────────────────────────────────────
		//
		// The sifting step of a chain as a whole: its members, side by side and in their
		// order, go to the front of B and sweep right as one block, never stopping inside
		// another chain of their row. A dummy of their row may also stop between two of
		// them as it passes, at one crossing with their line. The best position is kept as
		// in siftingStep. With reorder, reorderMembers follows. The members leave behind
		// the dummies between them, and take at most one back, so the order sifting started
		// from may not be reachable: if the result is worse (or, with stability, no
		// better), B goes back to what it was.
		void chainStep(int c, bool stability, bool reorder);

		// Each member in turn takes the place among the chain's places (the same set of
		// positions in B, so the dummies between them stay) with the fewest crossings.
		void reorderMembers(int c);

		// The crossings between row and the rows right above and below it, in B's order.
		int crossingsAround(int row);

		// The nodes of the group in data, in their order, and the places they occupy there.
		std::vector<NodePtr> groupRow(const LayerData& data, std::vector<int>* slots = nullptr) const;

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
		// needed to evaluate swaps on end_layer_. They are fixed too, since that
		// layer is never written back.
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
		// 3) Blocks reaching the anchor layer or end_layer_+1 are marked as not movable.
		//    A chain crossing the range border is kept whole: its dummies inside the
		//    range cannot be sifted, but other blocks can still move around them.
		void buildBlocks();

		// ── Layer and hub sorting ─────────────────────────────────────────────────
		//
		// followMovedNode (only after nodes were moved sideways) re-places, in one layer,
		// the descendants of the moved nodes: the nodes with a neighbour right above (through
		// the hubs, so a long connection's dummies count too) among `followers`, which then
		// takes them in. Every node gets a key, the weighted average of the positions of its
		// neighbours above, the followers weighing FOLLOW_WEIGHT and the rest 1; a root takes
		// the key of its left-hand neighbour, so it stays right after it. The descendants are
		// sorted by key and merged, by key, into the other nodes, which keep their relative
		// order (ties: the order the layer had).
		//

		// We sort the hubs by their barycenter: the average position of all their
		// endpoints (parents in the upper layer and children in the lower one), each
		// position normalized by its row size so both rows weigh the same, and every
		// endpoint counting once, since a side with more edges has more to cross.
		// Ties are broken lexicographically by the minimum position of their parents
		// and then by the minimum position of their children. This tie-break is well
		// defined due to imposed restrictions on the hypergraph structure: if two
		// hyperedges share a source, then they cannot share a target, and vice versa.
		// Therefore, if two hubs share a parent, they cannot share a child, so there
		// cannot be ties in both the upper and lower layer positions.
		static constexpr double FOLLOW_WEIGHT = 3.0;
		void followMovedNode(
			const std::unordered_map<Node*, int>& upper_pos,
			std::unordered_set<Node*>& followers,
			std::vector<NodePtr>& lower) const;

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
		// Only called for movable blocks.
		// Places A at the front of B', sweeps it right one swap at a time, records
		// the position p* with the minimum cumulative crossing delta, then rotates
		// A to p*.
		// Among positions with the same crossings, A takes the one that leaves it out
		// of order with the fewest blocks, compared with the order sifting started from
		// (initial_pi_), counting only blocks that share a row with A (the others' order
		// relative to A never shows). So a block stays where it is, or goes back to where
		// it was, unless moving it actually removes crossings: the user's mental map.
		// A never stops inside a chain of a row where it has a real node; inside one where it
		// has a dummy, it crosses the chain's line (see ChainWatch).
		int siftingStep(int a_id, bool stability = true);

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
			const std::vector<std::vector<int>>& units, const std::vector<int>& starts, size_t u,
			const std::unordered_set<int>& movable_set,
			int chi, int blocked, int& best_chi, BlockList& best_B);
	};

} // namespace sifting_internal