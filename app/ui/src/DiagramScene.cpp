#include "DiagramScene.h"
#include "FuseNodesDialog.h"
#include "AddHypergraphDialog.h"
#include "LayoutTypes.h"

#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QInputDialog>
#include <QGraphicsProxyWidget>
#include <QLineEdit>
#include <QApplication>

using namespace hypergraph_logic;
using namespace app_logic;

namespace ui {

    // ============================================================================
    // Construction
    // ============================================================================

    DiagramScene::DiagramScene(HypergraphEditor* editor, QObject* parent)
        : QGraphicsScene(parent)
        , regular_editor_(editor)
        , is_joint_(false)
    {
        connect(this, &DiagramScene::nodeRelocated,
            this, [this](Node* node, double new_x) {
                try {
                    regular_editor_->relocateNodeInLayer(node->shared_from_this(), new_x);
                }
                catch (const std::exception& e) { showError(e); }
                rebuild();
                emit graphChanged();
            });
        rebuild();
    }

    DiagramScene::DiagramScene(JointHypergraphEditor* editor, QObject* parent)
        : QGraphicsScene(parent)
        , joint_editor_(editor)
        , is_joint_(true)
    {
        connect(this, &DiagramScene::nodeRelocated,
            this, [this](Node* node, double new_x) {
                try {
                    joint_editor_->relocateNodeInLayer(node->shared_from_this(), new_x);
                }
                catch (const std::exception& e) { showError(e); }
                rebuild();
                emit graphChanged();
            });
        rebuild();
    }

    // ============================================================================
    // rebuild
    // ============================================================================

    void DiagramScene::rebuild() {
        cancelInteraction();
        node_items_.clear();
        edge_items_.clear();

        HypergraphRenderer::render(
            currentGraph(), this, node_items_, edge_items_,
            [](Node* node, const QRectF& rect) {
                return new NodeItem(node, rect);
            },
            [](Hyperedge* edge, const QPainterPath& path) {
                return new HyperedgeItem(edge, path);
            });
    }

    // ============================================================================
    // cancelInteraction
    // ============================================================================

    void DiagramScene::cancelInteraction() {
        setAllNodesHighlighted(false);
        state_ = InteractionState::Idle;
        pending_node_ = nullptr;
        pending_edge_ = nullptr;
    }

    // ============================================================================
    // mousePressEvent — background clicks
    // ============================================================================

    void DiagramScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
        QGraphicsItem* item = itemAt(event->scenePos(), QTransform());

        if (item) {
            if (event->button() == Qt::LeftButton &&
                state_ != InteractionState::Idle) {
                if (auto* ni = qgraphicsitem_cast<NodeItem*>(item)) {
                    completeSecondNodeClick(ni->node());
                    return;
                }
            }
            QGraphicsScene::mousePressEvent(event);
            return;
        }

        // Background click.
        if (event->button() == Qt::LeftButton) {
            if (state_ != InteractionState::Idle)
                cancelInteraction();
            return;
        }
        if (event->button() == Qt::RightButton) {
            if (state_ != InteractionState::Idle) {
                cancelInteraction();
                return;
            }
            if (is_joint_)
                showJointBackgroundMenu(event->scenePos());
            else
                showBackgroundContextMenu(event->scenePos());
        }
    }

    void DiagramScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
        QGraphicsScene::mouseMoveEvent(event);
    }

    void DiagramScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
        QGraphicsScene::mouseReleaseEvent(event);
    }

    // ============================================================================
    // Context menus
    // ============================================================================

    void DiagramScene::showNodeContextMenu(NodeItem* item, const QPointF& scene_pos) {
        Node* node = item->node();
        // Right-click on a node while a two-step op is pending cancels it.
        if (state_ != InteractionState::Idle) {
            cancelInteraction();
            return;
        }

        QMenu menu;
        menu.addAction("Crear nodo debajo", [this, node] { onCreateNodeBelow(node); });
        menu.addSeparator();
        menu.addAction("Añadir conexión arriba", [this, node] { onBeginAddConnectionParent(node); });
        menu.addAction("Añadir conexión abajo", [this, node] { onBeginAddConnectionChild(node); });
        menu.addSeparator();
        menu.addAction("Eliminar conexión", [this, node] { onBeginRemoveConnection(node); });
        menu.addAction("Eliminar nodo", [this, node] { onRemoveNode(node); });
        menu.addSeparator();
        menu.addAction("Fusionar", [this, node] { onBeginFuseNodes(node); });
        menu.exec(QCursor::pos());
    }

    void DiagramScene::showEdgeContextMenu(HyperedgeItem* item, const QPointF&) {
        Hyperedge* edge = item->edge();

        QMenu menu;
        menu.addAction("Crear nodo en arista", [this, edge] { onCreateNodeIntoEdge(edge); });
        menu.addAction("Crear origen", [this, edge] { onCreateSource(edge); });
        menu.addAction("Crear destino", [this, edge] { onCreateTarget(edge); });
        menu.addSeparator();
        menu.addAction("Añadir origen", [this, edge] { onBeginAddSource(edge); });
        menu.addAction("Añadir destino", [this, edge] { onBeginAddTarget(edge); });
        menu.addSeparator();
        menu.addAction("Eliminar origen", [this, edge] { onBeginRemoveSource(edge); });
        menu.addAction("Eliminar destino", [this, edge] { onBeginRemoveTarget(edge); });
        menu.addSeparator();
        menu.addAction("Eliminar arista", [this, edge] { onRemoveHyperedge(edge); });
        menu.exec(QCursor::pos());
    }

    void DiagramScene::showBackgroundContextMenu(const QPointF& scene_pos) {
        QMenu menu;
        menu.addAction("Nuevo nodo", [this, scene_pos] { onCreateRootNode(scene_pos); });
        menu.exec(QCursor::pos());
    }

    void DiagramScene::showJointBackgroundMenu(const QPointF& scene_pos) {
        onAddHypergraph(scene_pos);
    }

    // ============================================================================
    // Two-step interaction helpers
    // ============================================================================

    void DiagramScene::setAllNodesHighlighted(bool on) {
        for (auto& [raw, item] : node_items_)
            item->setHighlighted(on);
    }

    void DiagramScene::completeSecondNodeClick(Node* second) {
        Node* first = pending_node_;
        Hyperedge* edge = pending_edge_;
        InteractionState st = state_;
        cancelInteraction();

        NodePtr first_ptr = first ? first->shared_from_this() : nullptr;
        NodePtr second_ptr = second ? second->shared_from_this() : nullptr;
        HyperedgePtr edge_ptr = edge ? edge->shared_from_this() : nullptr;

        try {
            if (st == InteractionState::WaitingForSecondNode_AddConnectionParent) {
                // first is child, second is parent
                if (is_joint_) joint_editor_->addConnection(second_ptr, first_ptr);
                else           regular_editor_->addConnection(second_ptr, first_ptr);
            }
            else if (st == InteractionState::WaitingForSecondNode_AddConnectionChild) {
                if (is_joint_) joint_editor_->addConnection(first_ptr, second_ptr);
                else           regular_editor_->addConnection(first_ptr, second_ptr);
            }
            else if (st == InteractionState::WaitingForSecondNode_RemoveConnection) {
                // Determine direction from parent/child relationship.
                bool first_is_parent = false;
                for (const auto& p : second_ptr->getParents())
                    if (p.get() == first) { first_is_parent = true; break; }

                if (first_is_parent) {
                    if (is_joint_) joint_editor_->removeConnection(first_ptr, second_ptr);
                    else           regular_editor_->removeConnection(first_ptr, second_ptr);
                }
                else {
                    if (is_joint_) joint_editor_->removeConnection(second_ptr, first_ptr);
                    else           regular_editor_->removeConnection(second_ptr, first_ptr);
                }
            }
            else if (st == InteractionState::WaitingForSecondNode_FuseNodes) {
                FuseNodesDialog dlg(QString::fromStdString(first_ptr->getName()));
                if (dlg.exec() == QDialog::Accepted && !dlg.chosenName().isEmpty()) {
                    std::string label = dlg.chosenName().toStdString();
                    if (is_joint_) joint_editor_->fuseNodes(first_ptr, second_ptr, label);
                    else           regular_editor_->fuseNodes(first_ptr, second_ptr, label);
                }
                else return;
            }
            else if (st == InteractionState::WaitingForSecondNode_AddSource) {
                if (is_joint_) joint_editor_->addSourceToEdge(edge_ptr, second_ptr);
                else           regular_editor_->addSourceToEdge(edge_ptr, second_ptr);
            }
            else if (st == InteractionState::WaitingForSecondNode_AddTarget) {
                if (is_joint_) joint_editor_->addTargetToEdge(edge_ptr, second_ptr);
                else           regular_editor_->addTargetToEdge(edge_ptr, second_ptr);
            }
            else if (st == InteractionState::WaitingForSecondNode_RemoveSource) {
                if (is_joint_) joint_editor_->removeSourceFromHyperedge(edge_ptr, second_ptr);
                else           regular_editor_->removeSourceFromHyperedge(edge_ptr, second_ptr);
            }
            else if (st == InteractionState::WaitingForSecondNode_RemoveTarget) {
                if (is_joint_) joint_editor_->removeTargetFromHyperedge(edge_ptr, second_ptr);
                else           regular_editor_->removeTargetFromHyperedge(edge_ptr, second_ptr);
            }
        }
        catch (const std::exception& e) {
            showError(e);
            return;
        }

        rebuild();
        emit graphChanged();
    }

    // ============================================================================
    // Node operations
    // ============================================================================

    void DiagramScene::onCreateNodeBelow(Node* parent) {
        NodePtr parent_ptr = parent ? parent->shared_from_this() : nullptr;
        try {
            NodePtr new_node = regular_editor_->createNode("Nuevo nodo", -1, parent_ptr);
            rebuild();
            emit graphChanged();
            // Start inline rename immediately.
            auto it = node_items_.find(new_node.get());
            if (it != node_items_.end()) startInlineRename(it->second);
        }
        catch (const std::exception& e) { showError(e); }
    }

    void DiagramScene::onCreateNodeIntoEdge(Hyperedge* edge) {
        HyperedgePtr edge_ptr = edge->shared_from_this();
        try {
            NodePtr new_node = regular_editor_->createNode("Nuevo nodo", edge_ptr);
            rebuild();
            emit graphChanged();
            auto it = node_items_.find(new_node.get());
            if (it != node_items_.end()) startInlineRename(it->second);
        }
        catch (const std::exception& e) { showError(e); }
    }

    void DiagramScene::onCreateSource(Hyperedge* edge) {
        HyperedgePtr edge_ptr = edge->shared_from_this();
        try {
            NodePtr new_node = regular_editor_->createSource("Nuevo origen", -1, edge_ptr);
            rebuild();
            emit graphChanged();
            auto it = node_items_.find(new_node.get());
            if (it != node_items_.end()) startInlineRename(it->second);
        }
        catch (const std::exception& e) { showError(e); }
    }

    void DiagramScene::onCreateTarget(Hyperedge* edge) {
        HyperedgePtr edge_ptr = edge->shared_from_this();
        try {
            NodePtr new_node = regular_editor_->createTarget("Nuevo destino", -1, edge_ptr);
            rebuild();
            emit graphChanged();
            auto it = node_items_.find(new_node.get());
            if (it != node_items_.end()) startInlineRename(it->second);
        }
        catch (const std::exception& e) { showError(e); }
    }

    void DiagramScene::onRemoveNode(Node* node) {
        try {
            NodePtr ptr = node->shared_from_this();
            if (is_joint_) joint_editor_->removeNode(ptr);
            else           regular_editor_->removeNode(ptr);
            rebuild();
            emit graphChanged();
        }
        catch (const std::exception& e) { showError(e); }
    }

    void DiagramScene::onRemoveHyperedge(Hyperedge* edge) {
        try {
            HyperedgePtr ptr = edge->shared_from_this();
            if (is_joint_) joint_editor_->removeHyperedge(ptr);
            else           regular_editor_->removeHyperedge(ptr);
            rebuild();
            emit graphChanged();
        }
        catch (const std::exception& e) { showError(e); }
    }

    void DiagramScene::onBeginAddConnectionParent(Node* first) {
        pending_node_ = first;
        state_ = InteractionState::WaitingForSecondNode_AddConnectionParent;
        setAllNodesHighlighted(true);
        node_items_[first]->setHighlighted(false); // first node not a valid second
    }

    void DiagramScene::onBeginAddConnectionChild(Node* first) {
        pending_node_ = first;
        state_ = InteractionState::WaitingForSecondNode_AddConnectionChild;
        setAllNodesHighlighted(true);
        node_items_[first]->setHighlighted(false);
    }

    void DiagramScene::onBeginRemoveConnection(Node* first) {
        pending_node_ = first;
        state_ = InteractionState::WaitingForSecondNode_RemoveConnection;
        setAllNodesHighlighted(true);
        node_items_[first]->setHighlighted(false);
    }

    void DiagramScene::onBeginFuseNodes(Node* first) {
        pending_node_ = first;
        state_ = InteractionState::WaitingForSecondNode_FuseNodes;
        setAllNodesHighlighted(true);
        node_items_[first]->setHighlighted(false);
    }

    void DiagramScene::onBeginAddSource(Hyperedge* edge) {
        pending_edge_ = edge;
        state_ = InteractionState::WaitingForSecondNode_AddSource;
        setAllNodesHighlighted(true);
    }

    void DiagramScene::onBeginAddTarget(Hyperedge* edge) {
        pending_edge_ = edge;
        state_ = InteractionState::WaitingForSecondNode_AddTarget;
        setAllNodesHighlighted(true);
    }

    void DiagramScene::onBeginRemoveSource(Hyperedge* edge) {
        pending_edge_ = edge;
        state_ = InteractionState::WaitingForSecondNode_RemoveSource;
        setAllNodesHighlighted(true);
    }

    void DiagramScene::onBeginRemoveTarget(Hyperedge* edge) {
        pending_edge_ = edge;
        state_ = InteractionState::WaitingForSecondNode_RemoveTarget;
        setAllNodesHighlighted(true);
    }

    // ============================================================================
    // Background / root node creation
    // ============================================================================

    void DiagramScene::onCreateRootNode(const QPointF& scene_pos) {
        // Determine layer_position from the click x coordinate.
        // If there are any layer-0 nodes and the click is to the left of the
        // leftmost one, insert at position 0; otherwise append (-1).
        int layer_position = -1;
        if (!currentGraph().getLayers().empty()) {
            auto nodes_at_0 = currentGraph().getNodesAt(0);
            if (!nodes_at_0.empty()) {
                double leftmost_x = currentGraph().getNodeLayout()
                    .at(nodes_at_0.front().get()).x;
                if (scene_pos.x() < leftmost_x)
                    layer_position = 0;
            }
        }

        try {
            NodePtr new_node = regular_editor_->createNode(
                "Nuevo nodo", layer_position, nullptr);
            rebuild();
            emit graphChanged();
            auto it = node_items_.find(new_node.get());
            if (it != node_items_.end()) startInlineRename(it->second);
        }
        catch (const std::exception& e) { showError(e); }
    }

    void DiagramScene::onAddHypergraph(const QPointF& scene_pos) {
        // Build the list of diagrams for the dialog.
        // We need access to the project, which we do not own directly.
        // MainWindow connects a lambda to a signal for this purpose;
        // here we emit a request signal and MainWindow supplies the dialog.
        emit addHypergraphRequested(scene_pos.x());
    }

    // ============================================================================
    // Inline rename
    // ============================================================================

    void DiagramScene::startInlineRename(NodeItem* item) {
        // Place a QLineEdit as a proxy widget on top of the node box.
        QLineEdit* edit = new QLineEdit(
            QString::fromStdString(item->node()->getName()));
        edit->setAlignment(Qt::AlignCenter);
        edit->selectAll();

        QGraphicsProxyWidget* proxy = addWidget(edit);
        proxy->setPos(item->rect().topLeft());
        proxy->resize(item->rect().size());
        proxy->setZValue(10.0);
        edit->setFocus();

        // Commit on Enter or focus loss.
        auto commit = [this, item, proxy, edit]() {
            QString new_name = edit->text().trimmed();
            if (new_name.isEmpty()) new_name = QString::fromStdString(
                item->node()->getName());
            removeItem(proxy);
            proxy->deleteLater();
            commitInlineRename(item, new_name);
            };

        connect(edit, &QLineEdit::editingFinished, this, commit);
    }

    void DiagramScene::commitInlineRename(NodeItem* item, const QString& new_name) {
        NodePtr node_ptr = item->node()->shared_from_this();
        try {
            if (is_joint_) joint_editor_->renameNode(node_ptr, new_name.toStdString());
            else           regular_editor_->renameNode(node_ptr, new_name.toStdString());
        }
        catch (const std::exception& e) { showError(e); return; }
        item->updateLabel(new_name);
        emit graphChanged();
    }

    // ============================================================================
    // Helpers
    // ============================================================================

    void DiagramScene::showError(const std::exception& e) {
        QMessageBox::warning(nullptr, "Error", QString::fromStdString(e.what()));
    }

    const GraphicalHypergraph& DiagramScene::currentGraph() const {
        if (is_joint_) return joint_editor_->getGraph();
        return regular_editor_->getGraph();
    }

    void DiagramScene::onRenameNode(Node*, NodeItem* item) {
        startInlineRename(item);
    }

} // namespace ui