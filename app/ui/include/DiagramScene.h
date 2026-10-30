#pragma once

#include "HypergraphEditor.h"
#include "JointHypergraphEditor.h"
#include "HypergraphRenderer.h"
#include "NodeItem.h"
#include "HyperedgeItem.h"
#include "EquivalenceItem.h"

#include <QColor>
#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QList>
#include <QPointer>
#include <QVariantAnimation>

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class QGraphicsEllipseItem;
class QGraphicsItemGroup;
class QGraphicsPathItem;
class QMenu;
class QTimer;

namespace ui {

    class NodeDialog;

    // ============================================================================
    // InteractionState
    //
    // Tracks the current multi-step interaction. When state != Idle, the next
    // click on a candidate node completes the pending operation instead of
    // showing a context menu. Esc, a click on the background or a right click
    // cancels and returns to Idle.
    // ============================================================================
    enum class InteractionState {
        Idle,
        WaitingForSecondNode_AddConnectionParent,   // "Caja actual por debajo de"
        WaitingForSecondNode_AddConnectionChild,    // "Caja actual por encima de"
        WaitingForSecondNode_RemoveConnection,      // "Eliminar conexión parcial"
        WaitingForSecondNode_FuseNodes,             // "Fusionar"
        WaitingForSecondNode_AddSource,             // "Bifurcar arriba con caja existente"
        WaitingForSecondNode_AddTarget,             // "Bifurcar abajo con caja existente"
        WaitingForSecondNode_SimplifyConnection,    // "Simplificar conexión"
        WaitingForSecondNode_AddEquivalence,        // "Hacer equivalente a"
    };

    // ============================================================================
    // DiagramScene
    //
    // Central QGraphicsScene for one diagram (regular or joint). Owns all
    // NodeItems and HyperedgeItems. Rebuilt from scratch after every editor
    // mutation via rebuild().
    //
    // Interaction model
    // ─────────────────
    // Single-step operations (removeNode, removeHyperedge) execute immediately
    // on the first click. Two-step operations (addConnection, fuseNodes, etc.)
    // set the state to one of the WaitingFor… values. The first node clicked is
    // stored in pending_node_ and the connection an operation works on in
    // pending_edge_.
    //
    // While waiting, only the nodes the operation can actually take (as told by
    // the graph's can* queries) are candidates: they glow and pulse, every other
    // node fades out, the origin node gets an amber ring, and edges not involved
    // are dimmed. Clicks on anything that is not a candidate are ignored. A menu
    // entry whose operation has no candidate at all is disabled.
    //
    // Equivalences: "Hacer equivalente a…" in a box's menu is one more two-step
    // operation (its candidates are the boxes canAddEquivalence accepts); the
    // line between two equivalent boxes has a menu of its own (its style and
    // removing it), and the box menu offers the same for its own lines. While a
    // box of a chain is dragged, the chain shows what the drop will do to it
    // (see BoxDrag).
    //
    // Background click: if state == Idle, shows "Nueva caja" (regular) or
    // "Administrar esquemas" (joint: every diagram of the project, to add it
    // where the user clicked or take it out). If state != Idle, cancels the
    // operation.
    // ============================================================================
    class DiagramScene : public QGraphicsScene {
        Q_OBJECT
		friend class NodeItem;  // Allow NodeItem to open the context menu and the properties dialog.
		friend class HyperedgeItem;  // Allow HyperedgeItem to call showEdgeContextMenu.
		friend class EquivalenceItem;  // Allow EquivalenceItem to call showEquivalenceContextMenu.
    public:
        // Construct for a regular diagram editor.
        explicit DiagramScene(app_logic::HypergraphEditor* editor,
            QObject* parent = nullptr);

        // Construct for the joint diagram editor.
        explicit DiagramScene(app_logic::JointHypergraphEditor* editor,
            QObject* parent = nullptr);

        // Rebuild all items from the current graph layout. Called after every
        // editor mutation and on first construction.
        void rebuild();

        // Cancel any pending two-step interaction and return to Idle.
        void cancelInteraction();

        bool isInteractionPending() const { return state_ != InteractionState::Idle; }

        // Current value, in [0, 1], of the breathing animation of candidate nodes.
        double selectionPulse() const { return pulse_; }

        // Where the node dialogs read the project's recently used colours from,
        // and where they report every colour the user picks. Set by MainWindow.
        void setColourStore(std::function<QList<QColor>()> recent,
            std::function<void(const QColor&)> remember);

        // Where new boxes take the project's defaults from, and where the node
        // dialog sets a new default background, text colour or font size. Set by
        // MainWindow; without it new boxes take the usual ones.
        void setProjectDefaultsStore(
            std::function<hypergraph_logic::NodeAttributes(const std::string&)> new_attributes,
            std::function<void(const QColor&)> set_colour,
            std::function<void(const QColor&)> set_font_colour,
            std::function<void(int)> set_font_size);

        // The project's diagrams, for the joint diagram's "Administrar esquemas"
        // menu. Set by MainWindow.
        struct DiagramInfo {
            std::string id;
            QString name;
        };
        void setDiagramCatalog(std::function<std::vector<DiagramInfo>()> catalog);

        // ── Layout transitions ────────────────────────────────────────────────────
        //
        // Where every box is drawn right now (centre, scene coordinates).
        std::unordered_map<hypergraph_logic::Node*, QPointF> nodeCenters() const;

        // rebuild(), then every box glides from where it was drawn in `from` to
        // its new place, boxes that did not exist fade in, and the connections
        // of anything that moved fade back in once the boxes are nearly there.
        void rebuildAnimated(const std::unordered_map<hypergraph_logic::Node*, QPointF>& from);

        // ── Moving a whole piece of the diagram ───────────────────────────────────
        //
        // A block (connected component) of any diagram, or in the joint diagram a
        // whole diagram that is not mixed with others, chosen from a box or
        // connection menu: it is picked up by a grip at its top middle, which hops
        // under the mouse and follows it, with a guide showing where it would land
        // (see GraphicalHypergraph's "Blocks"); a click (or releasing a drag)
        // drops it there, Esc or a right click puts it back.
        bool isMovingPiece() const { return piece_move_.has_value(); }

        // ── Copying and pasting (see PieceClipboard) ──────────────────────────────
        //
        // Box and connection menus copy the block, or the whole diagram (in the
        // joint, the box's diagram while it is not mixed); the background menu
        // of a regular diagram pastes, right below "Nueva caja", when there is
        // something to paste. The copy goes on the system clipboard, so another
        // running instance can paste it too. The joint diagram cannot paste.
        //
        // copyAt (Ctrl+C): the block of the box or connection at scene_pos, or
        // the whole diagram over the background. Returns what was copied, or an
        // empty string when there was nothing to copy.
        QString copyAt(const QPointF& scene_pos);

        // pasteAt (Ctrl+V): the copy becomes a new block whose first row is the
        // layer at scene_pos, placed among the other blocks at its x, like a
        // moved block. Undoable. False when nothing was pasted.
        bool pasteAt(const QPointF& scene_pos);
        bool canPaste() const;

        // ── Signals emitted to MainWindow ────────────────────────────────────────
    signals:
        // Emitted after any mutation so MainWindow can update undo/redo actions
        // and refresh the tab bar thumbnail.
        void graphChanged();

        // Emitted by NodeItem after a horizontal drag is released.
        // DiagramScene connects this to call relocateNodeInLayer on the editor.
        void nodeRelocated(hypergraph_logic::Node* node, double new_scene_x, double new_scene_y);

        // Emitted when the user asks to add a diagram to the joint, from the
        // "Administrar esquemas" menu opened at click_x. MainWindow handles it,
        // since it owns the diagrams.
        void addHypergraphRequested(const QString& diagram_id, double click_x);

        // Tells the user what the pending operation expects next; an empty hint
        // means no operation is pending any more.
        void interactionHintChanged(const QString& hint);

        // A short message for the status bar (e.g. what was copied).
        void notice(const QString& message);

    protected:
        void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
        void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;

    private:
        // ── Editor access (exactly one is non-null) ───────────────────────────────
        app_logic::HypergraphEditor* regular_editor_ = nullptr;
        app_logic::JointHypergraphEditor* joint_editor_ = nullptr;
        bool is_joint_ = false;

        // ── Item maps (rebuilt each time) ─────────────────────────────────────────
        std::unordered_map<hypergraph_logic::Node*, NodeItem*>      node_items_;
        std::unordered_map<hypergraph_logic::Hyperedge*, HyperedgeItem*> edge_items_;
        std::vector<EquivalenceItem*> equivalence_items_; // one per line between equivalent neighbours

        // ── Interaction state ─────────────────────────────────────────────────────
        InteractionState state_ = InteractionState::Idle;

        // First node selected in a two-step operation.
        hypergraph_logic::Node* pending_node_ = nullptr;

        // Edge selected in addSource/addTarget/simplifyConnection.
        hypergraph_logic::Hyperedge* pending_edge_ = nullptr;

        // Nodes the pending operation can take as its second node.
        std::unordered_set<hypergraph_logic::Node*> candidates_;

        // Breathing animation of the candidates (running only while pending).
        QTimer* pulse_timer_ = nullptr;
        QElapsedTimer pulse_clock_;
        double pulse_ = 0.0;

        // ── Project colour store (see setColourStore) ─────────────────────────────
        std::function<QList<QColor>()> recent_colours_;
        std::function<void(const QColor&)> remember_colour_;
        std::function<hypergraph_logic::NodeAttributes(const std::string&)> new_attributes_;
        std::function<void(const QColor&)> set_default_colour_;
        std::function<void(const QColor&)> set_default_font_colour_;
        std::function<void(int)> set_default_font_size_;

        // ── Project diagrams (see setDiagramCatalog) ──────────────────────────────
        std::function<std::vector<DiagramInfo>()> diagram_catalog_;

        // ── Dragging one box (see NodeItem) ───────────────────────────────────────
        //
        // A box in an equivalence chain shows what its drop will do to the chain
        // (see GraphicalHypergraph::relocateNodeInLayer):
        //   - Reorder: dropped in its own layer within the chain's reorder span (up
        //     to the nearest box of no part of it on each side), it goes where its x
        //     puts it, between other members or past lines (dummies) too. The other
        //     members stay; the landing marker shows its place among boxes and lines.
        //   - Whole: anywhere else (past such a box, or in another layer), the whole
        //     chain moves along: the other members and their lines follow the box.
        // Switching between the two glides (FOLLOW_MS). A box of no chain dropped
        // inside a chain goes to its nearer end, which the landing marker shows.
        enum class ChainMode { None, Reorder, Whole };
        struct BoxDrag {
            NodeItem* item = nullptr;
            std::vector<QGraphicsItem*> edges; // its connections (and its chain's), faded while it moves

            // Its chain, if any: the members in order, where it can be reordered, and its lines.
            std::vector<hypergraph_logic::Node*> chain;
            std::pair<double, double> reorder_span{ 0.0, 0.0 }; // see chainReorderSpan
            std::vector<EquivalenceItem*> chain_lines;
            ChainMode mode = ChainMode::None;
            // Offsets of the other members: shown now, at the last mode switch, wanted.
            std::unordered_map<hypergraph_logic::Node*, QPointF> shown, from, target;
            double blend = 1.0; // 0 -> 1 after a mode switch
        };
        std::optional<BoxDrag> box_drag_;

        void beginBoxDrag(NodeItem* item);   // lifts the box
        void updateBoxDrag(NodeItem* item);  // landing guide in the target layer
        void endBoxDrag();

        // The chain preview of the drag (see BoxDrag): updates the mode and the
        // other members' wanted offsets for the box's centre c, then shows them.
        void updateChainPreview(const QPointF& c, int target_layer);
        void showChainPreview();

        // Where a box dropped at x in layer lands when x falls inside a chain of that
        // layer (other than its own): next to the chain's nearer end. Returns the
        // landing marker's x and whether it is the left end; nullopt otherwise.
        std::optional<std::pair<double, bool>> chainEndLanding(hypergraph_logic::Node* box, int layer, double x) const;

        // ── Moving a piece of the diagram ─────────────────────────────────────────
        struct PieceMove {
            std::string diagram_id;                   // empty: a block (joint only otherwise)
            hypergraph_logic::Node* anchor = nullptr; // a box of the block
            std::unordered_set<hypergraph_logic::Node*> nodes;
            std::vector<std::pair<double, double>> regions; // the others'
            QGraphicsItemGroup* piece = nullptr;      // its items, lifted
            QPointF grab;                             // its grip (top middle), before moving
            QPointF offset;                           // cursor - grab: how far it is moved
            double snap = 0.0;                        // 0 -> 1 while the grip hops under the cursor
            double center_x = 0.0;                    // of its span, before moving
            int top_layer = 0;
            int bottom_layer = 0;
            double top_row_y = 0.0;                   // scene y of its first row
            QRectF bounds;                            // its boxes, before moving
        };
        std::optional<PieceMove> piece_move_;
        bool swallow_context_menu_ = false; // the right click that put a piece back

        void startPieceMove(const std::string& diagram_id, hypergraph_logic::Node* anchor);
        void updatePiecePreview();
        void dropPiece();
        void cancelPieceMove();
        void endPieceMove(); // leaves the moving state without touching the graph

        // What the box and connection menus can offer for the piece around a box.
        struct PieceChoice {
            std::string diagram_id;  // joint only: the box's diagram, when it is separable
            QString diagram_name;
            bool move_diagram = false;
            bool move_block = false;
        };
        PieceChoice pieceChoiceFor(hypergraph_logic::Node* box) const;
        void addMoveEntries(QMenu* menu, const PieceChoice& choice, hypergraph_logic::Node* box);
        void addRemoveBlockEntry(QMenu* menu, hypergraph_logic::Node* box);
        void onRemoveDiagram(const std::string& diagram_id);
        void onRemoveBlock(hypergraph_logic::Node* box);

        // "Copiar bloque" / "Copiar esquema" for the piece around box.
        void addCopyEntries(QMenu* menu, hypergraph_logic::Node* box);
        void copyBlock(hypergraph_logic::Node* box);
        void copyDiagram(const std::string& diagram_id); // joint: that diagram; else the whole one
        void copyNodes(const std::unordered_set<hypergraph_logic::Node*>& nodes, const QString& description);
        // The doomed boxes and their connections shrink away, then apply()
        // changes the graph and the rest glides together (or back, if it throws).
        void takeOut(const std::unordered_set<hypergraph_logic::Node*>& doomed, std::function<void()> apply);

        // ── Guides drawn over the diagram while something moves ───────────────────
        //
        // Created once when a move starts and only reshaped while it goes on:
        // deleting items while the scene is delivering a mouse event to the
        // dragged box makes Qt drop that box's mouse grab (the drag would stop).
        struct Guides {
            QGraphicsPathItem* band = nullptr;      // target layer (box drag)
            QGraphicsPathItem* halo = nullptr;      // insertion bar: glow...
            QGraphicsPathItem* bar = nullptr;       // ...the bar...
            QGraphicsEllipseItem* dot_top = nullptr;    // ...and its ends
            QGraphicsEllipseItem* dot_bottom = nullptr;
            QGraphicsItem* chip = nullptr;          // label (ChipItem)
            std::vector<QGraphicsPathItem*> regions; // other pieces (piece move)
            QGraphicsPathItem* ghost = nullptr;     // where a moved piece lands
        };
        std::optional<Guides> guides_;
        void createGuides();  // hidden until placed
        void clearGuides();   // outside mouse-event delivery (see above)
        void placeBand(const QRectF& rect, bool dashed);
        void placeLandingMarker(double x, double top_y, double bottom_y);
        void placeChip(const QString& text, const QPointF& anchor); // empty text hides it

        // Scene y of a layer's row; beyond the existing layers, one layer gap
        // per layer above the first or below the last.
        double rowSceneY(int layer) const;
        // Scene y of the line past which a box or block opens a new layer above
        // (or below) everything: (H + layer gap) / 2 beyond the outermost row,
        // H being that row's tallest box (see GraphicalHypergraph::layerForY).
        // The new-layer guides are drawn on it, right where the cursor crosses.
        double newLayerEdgeY(bool above) const;

        // Where the mouse is, in scene coordinates (of the editing view).
        QPointF cursorScenePos() const;

        // Animations: the layout transition, the lift of what is being moved and
        // the fade-out of a diagram being taken out.
        QPointer<QVariantAnimation> transition_;
        QPointer<QVariantAnimation> lift_;
        QPointer<QVariantAnimation> fade_;
        QPointer<QVariantAnimation> follow_; // a dragged box's chain switching mode (BoxDrag)
        void stopAnimations();

        // ── Helpers ───────────────────────────────────────────────────────────────

        void initialise();

        // Show error message from a caught exception.
        void showError(const std::exception& e);

        // The nodes that operation `state` could take as its second node, given
        // its first node and/or connection.
        std::unordered_set<hypergraph_logic::Node*> candidatesFor(InteractionState state,
            hypergraph_logic::Node* first, hypergraph_logic::Hyperedge* edge) const;

        // Enters a two-step operation: marks candidates, starts the pulse and
        // publishes the hint.
        void beginSelection(InteractionState state, hypergraph_logic::Node* first,
            hypergraph_logic::Hyperedge* edge);

        // Applies (or clears) the selection look of every node and edge item.
        void applySelectionMarks();
        void clearSelectionMarks();

        QString hintFor(InteractionState state) const;

        // Complete a two-step operation given the second node.
        void completeSecondNodeClick(hypergraph_logic::Node* second);

        // ── Context menu builders ─────────────────────────────────────────────────
        void showNodeContextMenu(NodeItem* item, const QPointF& scene_pos);
        void showEdgeContextMenu(HyperedgeItem* item, const QPointF& scene_pos);
        // An equivalence line: its style and removing it.
        void showEquivalenceContextMenu(EquivalenceItem* item, const QPointF& scene_pos);
        // The box menu's "Equivalencias" section.
        void addEquivalenceEntries(QMenu* menu, hypergraph_logic::Node* node);
        void showBackgroundContextMenu(const QPointF& scene_pos);
        void showJointBackgroundMenu(const QPointF& scene_pos); // "Administrar esquemas"

        // ── Slot-like private methods called from context menus ───────────────────

        // Node operations
		void onCreateNodeAbove(hypergraph_logic::Node* parent);
        void onCreateNodeBelow(hypergraph_logic::Node* child);
        void onCreateNodeLeft(hypergraph_logic::Node* node);
        void onCreateNodeRight(hypergraph_logic::Node* node);
        void onCreateNodeIntoEdge(hypergraph_logic::Hyperedge* edge);
        void onCreateSource(hypergraph_logic::Hyperedge* edge);
        void onCreateTarget(hypergraph_logic::Hyperedge* edge);
        void onRemoveNode(hypergraph_logic::Node* node);
        void onRemoveHyperedge(hypergraph_logic::Hyperedge* edge);
        // Draws the whole connection with a continuous or a dashed line
        // (clearing any "conexión dudosa" marks on it).
        void onSetLineStyle(hypergraph_logic::Hyperedge* edge, bool continuous);
        // Marks or unmarks the connection as doubtful ("dudosa") at node.
        void onToggleUncertain(hypergraph_logic::Hyperedge* edge, hypergraph_logic::Node* node);
        // The line between neighbours a and b of a chain: its style, or removing it.
        void onSetEquivalenceStyle(hypergraph_logic::Node* a, hypergraph_logic::Node* b, bool continuous);
        void onRemoveEquivalence(hypergraph_logic::Node* a, hypergraph_logic::Node* b);

        // Background / joint operations
        void onCreateRootNode(const QPointF& scene_pos);

        // Node attribute dialogs. createNodeWithDialog asks for the new node's
        // attributes (pre-filled with default_name) and, if accepted, runs create
        // with them; showNodeProperties edits an existing node ("Propiedades").
        void createNodeWithDialog(const QString& default_name,
            const std::function<void(const hypergraph_logic::NodeAttributes&)>& create);
        void showNodeProperties(NodeItem* item);
        // A box dragged by its edges (see NodeItem) was dropped at `size`: the
        // dragged sides become its own size and the layout glides around it.
        // Boxes can only be resized while no other interaction is pending.
        void resizeNode(hypergraph_logic::Node* node, const QSizeF& size, Qt::Orientations sides);
        bool canResizeBoxes() const;
        QList<QColor> recentColours() const;
        void rememberColours(const QList<QColor>& colours);
        // Makes the dialog's colours and font size the project's defaults where it was asked to.
        void applyProjectDefaults(const NodeDialog& dialog);
        QWidget* dialogParent() const;

        // Convenience: get the graph from whichever editor is active.
        const hypergraph_logic::GraphicalHypergraph& currentGraph() const;
    };

} // namespace ui
