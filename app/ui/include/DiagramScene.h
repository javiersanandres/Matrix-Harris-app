#pragma once

#include "HypergraphEditor.h"
#include "JointHypergraphEditor.h"
#include "HypergraphRenderer.h"
#include "NodeItem.h"
#include "HyperedgeItem.h"

#include <QColor>
#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QList>

#include <functional>
#include <unordered_map>
#include <unordered_set>

class QTimer;

namespace ui {

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
    // Background click: if state == Idle, shows "Nueva caja" (regular) or the
    // AddHypergraphDialog (joint). If state != Idle, cancels the operation.
    // ============================================================================
    class DiagramScene : public QGraphicsScene {
        Q_OBJECT
		friend class NodeItem;  // Allow NodeItem to open the context menu and the properties dialog.
		friend class HyperedgeItem;  // Allow HyperedgeItem to call showEdgeContextMenu.
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

        // ── Signals emitted to MainWindow ────────────────────────────────────────
    signals:
        // Emitted after any mutation so MainWindow can update undo/redo actions
        // and refresh the tab bar thumbnail.
        void graphChanged();

        // Emitted by NodeItem after a horizontal drag is released.
        // DiagramScene connects this to call relocateNodeInLayer on the editor.
        void nodeRelocated(hypergraph_logic::Node* node, double new_scene_x, double new_scene_y);

        // Emitted when the user clicks the background of the joint diagram.
        // MainWindow handles this because it has access to the full diagram list.
        void addHypergraphRequested(double click_x);

        // Tells the user what the pending operation expects next; an empty hint
        // means no operation is pending any more.
        void interactionHintChanged(const QString& hint);

    protected:
        void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;

    private:
        // ── Editor access (exactly one is non-null) ───────────────────────────────
        app_logic::HypergraphEditor* regular_editor_ = nullptr;
        app_logic::JointHypergraphEditor* joint_editor_ = nullptr;
        bool is_joint_ = false;

        // ── Item maps (rebuilt each time) ─────────────────────────────────────────
        std::unordered_map<hypergraph_logic::Node*, NodeItem*>      node_items_;
        std::unordered_map<hypergraph_logic::Hyperedge*, HyperedgeItem*> edge_items_;

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
        void showBackgroundContextMenu(const QPointF& scene_pos);
        void showJointBackgroundMenu(const QPointF& scene_pos);

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

        // Background / joint operations
        void onCreateRootNode(const QPointF& scene_pos);
        void onAddHypergraph(const QPointF& scene_pos);

        // Node attribute dialogs. createNodeWithDialog asks for the new node's
        // attributes (pre-filled with default_name) and, if accepted, runs create
        // with them; showNodeProperties edits an existing node ("Propiedades").
        void createNodeWithDialog(const QString& default_name,
            const std::function<void(const hypergraph_logic::NodeAttributes&)>& create);
        void showNodeProperties(NodeItem* item);
        QList<QColor> recentColours() const;
        void rememberColours(const QList<QColor>& colours);
        QWidget* dialogParent() const;

        // Convenience: get the graph from whichever editor is active.
        const hypergraph_logic::GraphicalHypergraph& currentGraph() const;
    };

} // namespace ui
