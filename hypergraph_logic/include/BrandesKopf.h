#pragma once

// ============================================================================
// BrandesKopf.h
//
// Internal data structures and helper functions for the Brandes–Köpf
// horizontal coordinate assignment algorithm:
//
//   Brandes, U., & Köpf, B. (2002).
//   "Fast and Simple Horizontal Coordinate Assignment."
//   In: Graph Drawing (GD 2001), LNCS 2265, pp. 31–44. Springer.
//   DOI: 10.1007/978-3-540-45848-7_3
//
// ============================================================================

#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"

#include <climits>
#include <map>
#include <unordered_set>
#include <vector>

namespace bk_internal {

    using namespace hypergraph_logic;

    // ── G2 ───────────────────────────────────────────────────────────────────────
    //
    // A binary-edge view of the hypergraph, built by expanding every short
    // hyperedge e = (S, T) into |S| * |T| directed binary edges (u, v).
    // Nodes are indexed by an integer id assigned in layer-then-position
    // order, matching the ordering stored in LayerData::nodes.
    //
    // upper[i] and lower[i] list the parent and child ids of node i in G2,
    // sorted by their position within their respective layer so that median
    // neighbours can be found in O(1) during alignment. These are the abstraction
    // for the "edges" of the BK algorithm.
    // 
    // layers[k] holds the ordered list of node ids in hypergraph layer k.
    // pos[i] is the 0-based index of node i within its layer.

    // Hash for std::pair<int,int> used by G2::marked.
    struct PairHash {
        size_t operator()(const std::pair<int, int>& p) const {
            return std::hash<long long>()(
                (static_cast<long long>(p.first) << 32) | static_cast<unsigned>(p.second));
        }
    };
    struct G2 {
        std::vector<Node*> nodes;              // nodes[id] = original Node*
        std::vector<std::vector<int>> upper;   // upper[id] = parent ids, sorted by pos
        std::vector<std::vector<int>> lower;   // lower[id] = child ids,  sorted by pos
        std::vector<std::vector<int>> layers;  // layers[layer_key] = ordered node ids
		int num_layers = 0;                    // number of layers in the original hypergraph
        std::vector<int> pos;                  // pos[id] = position within its layer

        // Segments marked as type-1 conflicted during preprocessing.
        // A pair (u, v) in this set means the segment u->v must not be used
        // as an alignment edge (it is a non-inner segment crossing an inner one).
        std::unordered_set<std::pair<int, int>, PairHash> marked;

        bool isMarked(int u, int v) const { return marked.count({ u, v }) > 0; }
    };

   
    // ── BlockList ─────────────────────────────────────────────────────────────────
    //
    // Output of verticalAlignment. A "block" in BK is a maximal set of nodes
    // that are aligned vertically. We represent the blocks implicitly through
    // two parallel arrays, following the paper:
    //
    //   root[v]  – id of the topmost node in the block that contains v.
    //   align[v] – id of the next node below v in the same block (cyclic:
    //              the lowest node points back to the root).
    //
    // block_width[v] is the visual width of the block that contains v
    // (= max node width among all block members). It is the same for every
    // member of the block and is computed once at the end of verticalAlignment.
    // Dummy nodes contribute width 0; real nodes contribute DefaultNodeWidth.
    struct BlockList {
        std::vector<int>    root;
        std::vector<int>    align;
        std::vector<double> block_widths;
    };

    // ── buildG2 ──────────────────────────────────────────────────────────────────
    //
    // Constructs G2 from the hypergraph layer map.
    // For each short hyperedge e = (S, T) in layer k, inserts a binary edge
    // (u, v) for every u in S, v in T. This "insertion" is purely conceptual:
    // the connection is reflected in the upper and lower adjacency lists.
    G2 buildG2(const std::map<int, LayerData>& layers);

    // ── isInnerSegment ────────────────────────────────────────────────────────────
    //
    // Returns true iff the G2 edge u->v qualifies as an inner segment.
    //
    // In the paper an inner segment is a dummy->dummy edge. Because of the way
    // our splitting works, a dummy node can have multiple real parents. We
    // therefore define an inner segment more precisely:
    //   (u -> v)  is an inner segment  iff
    //       u is dummy  AND  v is dummy  AND
    //       u has exactly one child (v)  AND
    //       v has exactly one parent (u).
    // This matches the paper's intent: such a chain forms the "spine" of a long
    // edge and should be kept vertical.
    bool isInnerSegment(const G2& g, int u, int v);

    // ── markType1Conflicts ────────────────────────────────────────────────────────
    //
    // Algorithm 1 from the paper.
    //
    // Scans interior layers left-to-right and marks every non-inner segment that
    // crosses an inner segment, inserting the pair (u, v) into g.marked.
    // Marked segments are skipped during vertical alignment so that inner
    // segments (the spines of long edges) are preferentially kept vertical.
    void markType1Conflicts(G2& g);

    // ── verticalAlignment ─────────────────────────────────────────────────────────
    //
    // Algorithm 2 from the paper.
    //
    // Computes a BlockList encoding the vertical alignment of nodes.
    // Each node is aligned with its median neighbour in the adjacent layer,
    // subject to the constraint that no two alignment edges cross and no marked
    // segment is used.
    //
    // vdir = +1 : align upward   (process layers top-down, look at upper neighbours)
    // vdir = -1 : align downward (process layers bottom-up, look at lower neighbours)
    // hdir = +1 : leftmost  preference (try left median first, advance r leftward)
    // hdir = -1 : rightmost preference (try right median first, advance r rightward)
    //
    // Also computes block_width for each node in the returned BlockList.
    BlockList verticalAlignment(const G2& g, int vdir, int hdir);

    // ── placeBlock ────────────────────────────────────────────────────────────────
    //
    // Recursive sub-routine of horizontalCompaction (Algorithm 3).
    //
    // Places the block rooted at v by walking its cyclic chain and, for each
    // member w, examining the predecessor of w in its layer.  It is intended
    // to impose separation contraints between blocks of different classes and also
	// direct constraints between blocks of the same class.
    // Here, our minimum separation constraint for two blocks a and b is:
	//   sep(a,b) = (blockWidth(a) + blockWidth(b)) / 2  +  MIN_BLOCK_SEP
    void placeBlock(const G2& g,
                    const BlockList& B,
                    int v,
                    std::vector<int>& sink,
                    std::vector<double>& shift,
                    std::vector<double>& x,
                    int hdir);

    // ── horizontalCompaction ──────────────────────────────────────────────────────
    //
    // Algorithm 3 from the paper.
    //
    // Given the vertical alignment encoded in B, assigns an absolute x-coordinate
    // to every node in G2.  Nodes in the same block receive the same coordinate.
    //
    // hdir = +1 : pack toward the left  (roots are placed at the smallest valid x)
    // hdir = -1 : pack toward the right (roots are placed at the largest valid x)
    //
    // The vdir parameter controls the traversal order over layers, which must
    // match the direction used in the preceding verticalAlignment call so that
    // roots are encountered before their block members.
    std::vector<double> horizontalCompaction(const G2& g, const BlockList& B, int vdir, int hdir);

    // ── assignHorizontalCoordinates ───────────────────────────────────────────────
    //
    // Algorithm 4 from the paper. 
    //
    // Runs markType1Conflicts once, then runs verticalAlignment +
    // horizontalCompaction for all four (vdir, hdir) combinations.  The four
    // resulting layouts are aligned to the one with the smallest total width and
    // the final coordinate of each node is the average of the two median values
    // among its four candidates (the "average median" of the paper).
    std::vector<double> assignHorizontalCoordinates(G2& g);

} // namespace bk_internal