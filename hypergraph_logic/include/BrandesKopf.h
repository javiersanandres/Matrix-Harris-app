#pragma once

// ==================================================================================
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
// IMPORTANT: The original paper had some mistakes which dealt into wrong layouts.
// I have fixed these mistakes as stated in: 
//   Brandes, U., Walter, J., & Zink, J. (2021). "Erratum: Fast and
//   Simple Horizontal Coordinate Assignment."
//   In: Graph Drawing and Network Visualization (GD 2020),
//   LNCS 12590, pp. 433–435. Springer.
//   DOI: 10.1007/978-3-030-68766-3_32
// 
// ==================================================================================

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

    // ── BrandesKopf ──────────────────────────────────────────────────────────────
    //
    // Encapsulates all state and algorithms required to run a full Brandes–Köpf
    // horizontal coordinate assignment pass over a hypergraph layer map.
    struct BrandesKopf {
        explicit BrandesKopf(const std::map<int, LayerData>& layers);

        // Run the full BK pipeline (Algorithms 1–4) and return the final
        // x-coordinate for every node, indexed by G2 id.
        std::vector<double> run();

        // G2 is public so that the public entry point (assignCoordinates) can
        // read g_.nodes to map G2 ids back to original Node pointers.
        G2 g_;

    protected:
        // ── Algorithm 1 ──────────────────────────────────────────────────────────
        //
        // Scans interior layers left-to-right and marks every non-inner segment
        // that crosses an inner segment, inserting the pair (u, v) into g_.marked.
        void markType1Conflicts();

        // ── Inner-segment test ────────────────────────────────────────────────────
        //
        // Returns true iff the G2 edge u->v qualifies as an inner segment:
        //   u and v are both dummy, u has exactly one child (v), and v has exactly
        //   one parent (u). This matches the paper's intent for hypergraphs.
        static bool isInnerSegment(const G2& g, int u, int v);

        // ── Block-width helper ────────────────────────────────────────────────────
        //
        // Returns the visual width of the block containing v: NODE_WIDTH if any
        // member is a real node, DUMMY_NODE_WIDTH if all members are dummies.
        static double computeBlockWidth(const G2& g, const BlockList& B, int v);

        // ── Algorithm 2 ──────────────────────────────────────────────────────────
        //
        // Computes a BlockList encoding the vertical alignment of nodes.
        // vdir = +1 : top-down   hdir = +1 : leftmost preference
        // vdir = -1 : bottom-up  hdir = -1 : rightmost preference
        BlockList verticalAlignment(int vdir, int hdir) const;

        // ── Algorithm 3 helpers ───────────────────────────────────────────────────
        //
        // placeBlock: recursive sub-routine of horizontalCompaction that places the
        //   block rooted at v by walking its cyclic chain and imposing separation
        //   constraints with its predecessor block.
        //
        // horizontalCompaction: given the vertical alignment encoded in B, assigns
        //   an absolute x-coordinate to every node in G2.
        static void placeBlock(const G2& g,
            const BlockList& B,
            int v,
            std::vector<int>& sink,
            std::vector<double>& shift,
            std::vector<double>& x,
            int hdir);

        static std::vector<double> horizontalCompaction(const G2& g,
            const BlockList& B,
            int vdir,
            int hdir);
    };

} // namespace bk_internal