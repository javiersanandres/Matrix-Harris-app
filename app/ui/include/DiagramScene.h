#pragma once

#include "HypergraphEditor.h"
#include "JointHypergraphEditor.h"
#include "HypergraphRenderer.h"
#include "NodeItem.h"
#include "HyperedgeItem.h"

#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <unordered_map>

namespace ui {

    // ============================================================================
    // InteractionState
    //
    // Tracks the current multi-step interaction. When state != Idle, the next
    // node click completes the pending operation instead of showing a context menu.
    // Clicking the background cancels and returns to Idle.
    // ============================================================================
    enum class InteractionState {
        Idle,
        WaitingForSecondNode_AddConnectionParent,   // "Añadir nodo arriba"
        WaitingForSecondNode_AddConnectionChild,    // "Añadir nodo debajo"
        WaitingForSecondNode_RemoveConnection,      // "Eliminar conexión"
        WaitingForSecondNode_FuseNodes,             // "Fusionar"
        WaitingForSecondNode_AddSource,             // "Añadir origen"
        WaitingForSecondNode_AddTarget,             // "Añadir destino"
        WaitingForSecondNode_RemoveSource,          // "Eliminar origen"
        WaitingForSecondNode_RemoveTarget,          // "Eliminar destino"
        DraggingNode,
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
    // set the state to one of the WaitingFor… values and highlight all valid
    // second-click targets. The first node clicked is stored in pending_node_.
    // The pending edge (for addSource/addTarget/removeSource/removeTarget) is
    // stored in pending_edge_.
    //
    // Background click: if state == Idle, shows "Nuevo nodo" (regular) or the
    // AddHypergraphDialog (joint). If state != Idle, cancels the operation.
    //
    // Pan: holding left button on the background and moving pans the view.
    // ============================================================================
    class DiagramScene : public QGraphicsScene {
        Q_OBJECT
		friend class NodeItem;  // Allow NodeItem to use StartInlineRename and CommitInlineRename.
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

        // ── Signals emitted to MainWindow ────────────────────────────────────────
    signals:
        // Emitted after any mutation so MainWindow can update undo/redo actions
        // and refresh the tab bar thumbnail.
        void graphChanged();

        // Emitted when a rename is committed so the tab bar label updates.
        void diagramRenamed(const QString& new_name);

        // Emitted by NodeItem after a horizontal drag is released.
        // DiagramScene connects this to call relocateNodeInLayer on the editor.
        void nodeRelocated(hypergraph_logic::Node* node, double new_scene_x);

        // Emitted when the user clicks the background of the joint diagram.
        // MainWindow handles this because it has access to the full diagram list.
        void addHypergraphRequested(double click_x);

    protected:
        void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

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

        // Edge selected in addSource/addTarget/removeSource/removeTarget.
        hypergraph_logic::Hyperedge* pending_edge_ = nullptr;

        // ── Pan state ─────────────────────────────────────────────────────────────
        bool    panning_ = false;
        QPointF pan_start_;

        // ── Helpers ───────────────────────────────────────────────────────────────

        // Show error message from a caught exception.
        void showError(const std::exception& e);

        // Highlight / un-highlight all node items as valid second-click targets.
        void setAllNodesHighlighted(bool on);

        // Complete a two-step operation given the second node.
        void completeSecondNodeClick(hypergraph_logic::Node* second);

        // ── Context menu builders ─────────────────────────────────────────────────
        void showNodeContextMenu(NodeItem* item, const QPointF& scene_pos);
        void showEdgeContextMenu(HyperedgeItem* item, const QPointF& scene_pos);
        void showBackgroundContextMenu(const QPointF& scene_pos);
        void showJointBackgroundMenu(const QPointF& scene_pos);

        // ── Slot-like private methods called from context menus ───────────────────

        // Node operations
        void onCreateNodeBelow(hypergraph_logic::Node* parent);
        void onCreateNodeIntoEdge(hypergraph_logic::Hyperedge* edge);
        void onCreateSource(hypergraph_logic::Hyperedge* edge);
        void onCreateTarget(hypergraph_logic::Hyperedge* edge);
        void onRemoveNode(hypergraph_logic::Node* node);
        void onRemoveHyperedge(hypergraph_logic::Hyperedge* edge);
        void onRenameNode(hypergraph_logic::Node* node, NodeItem* item);
        void onBeginAddConnectionParent(hypergraph_logic::Node* first);
        void onBeginAddConnectionChild(hypergraph_logic::Node* first);
        void onBeginRemoveConnection(hypergraph_logic::Node* first);
        void onBeginFuseNodes(hypergraph_logic::Node* first);
        void onBeginAddSource(hypergraph_logic::Hyperedge* edge);
        void onBeginAddTarget(hypergraph_logic::Hyperedge* edge);
        void onBeginRemoveSource(hypergraph_logic::Hyperedge* edge);
        void onBeginRemoveTarget(hypergraph_logic::Hyperedge* edge);

        // Background / joint operations
        void onCreateRootNode(const QPointF& scene_pos);
        void onAddHypergraph(const QPointF& scene_pos);

        // Inline rename helpers
        void startInlineRename(NodeItem* item);
        void commitInlineRename(NodeItem* item, const QString& new_name);

        // Convenience: get the graph from whichever editor is active.
        const hypergraph_logic::GraphicalHypergraph& currentGraph() const;
    };

} // namespace ui