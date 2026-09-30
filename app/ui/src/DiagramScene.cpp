#include "DiagramScene.h"
#include "NodeDialogs.h"
#include "NodeEditorWidgets.h"
#include "AddHypergraphDialog.h"
#include "LayoutTypes.h"
#include "UiStyle.h"

#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include "AppDialogs.h"
#include <QApplication>
#include <QGraphicsView>
#include <QTimer>

#include <cmath>
#include <numbers>

using namespace hypergraph_logic;
using namespace app_logic;

namespace ui {

    namespace {
        // One full breath of the candidates' glow, in milliseconds.
        constexpr double PULSE_PERIOD_MS = 1400.0;
    }

    // ============================================================================
    // Construction
    // ============================================================================

    DiagramScene::DiagramScene(HypergraphEditor* editor, QObject* parent)
        : QGraphicsScene(parent)
        , regular_editor_(editor)
        , is_joint_(false)
    {
        connect(this, &DiagramScene::nodeRelocated,
            this, [this](Node* node, double new_x, double new_y) {
                try {
                    regular_editor_->relocateNode(node->shared_from_this(), new_x, -new_y);
                }
                catch (const std::exception&) {
                    rebuild();
                    return;
                }
                rebuild();
                emit graphChanged();
            });
        initialise();
    }

    DiagramScene::DiagramScene(JointHypergraphEditor* editor, QObject* parent)
        : QGraphicsScene(parent)
        , joint_editor_(editor)
        , is_joint_(true)
    {
        connect(this, &DiagramScene::nodeRelocated,
            this, [this](Node* node, double new_x, double new_y) {
                try {
                    joint_editor_->relocateNode(node->shared_from_this(), new_x, -new_y);
                }
                catch (const std::exception&) {
                    rebuild();
                    return;
                }
                rebuild();
                emit graphChanged();
            });
        initialise();
    }

    void DiagramScene::initialise() {
        pulse_timer_ = new QTimer(this);
        pulse_timer_->setInterval(33);
        connect(pulse_timer_, &QTimer::timeout, this, [this] {
            const double t = static_cast<double>(pulse_clock_.elapsed()) / PULSE_PERIOD_MS;
            pulse_ = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * t);
            for (Node* n : candidates_)
                if (auto it = node_items_.find(n); it != node_items_.end()) it->second->update();
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
            [](Hyperedge* edge, const QPainterPath& solid, const QPainterPath& dashed) {
                return new HyperedgeItem(edge, solid, dashed);
            });

        // Normalize: set sceneRect to the actual items bounding box,
        // with a small margin. This makes the scene origin consistent
        // regardless of the sign convention in the layout algorithm.
        QRectF bounds = itemsBoundingRect();
        if (!bounds.isEmpty())
            setSceneRect(bounds.adjusted(-20, -20, 20, 20));
    }

    // ============================================================================
    // Two-step interactions
    // ============================================================================

    void DiagramScene::cancelInteraction() {
        const bool was_pending = state_ != InteractionState::Idle;
        clearSelectionMarks();
        state_ = InteractionState::Idle;
        pending_node_ = nullptr;
        pending_edge_ = nullptr;
        candidates_.clear();
        if (pulse_timer_) pulse_timer_->stop();
        if (was_pending) emit interactionHintChanged(QString());
    }

    std::unordered_set<Node*> DiagramScene::candidatesFor(InteractionState state,
        Node* first, Hyperedge* edge) const
    {
        const GraphicalHypergraph& g = currentGraph();
        const NodePtr f = first ? first->shared_from_this() : nullptr;
        const HyperedgePtr e = edge ? edge->shared_from_this() : nullptr;

        std::unordered_set<Node*> out;
        for (const auto& [raw, item] : node_items_) {
            const NodePtr n = raw->shared_from_this();
            bool ok = false;
            switch (state) {
            case InteractionState::WaitingForSecondNode_AddConnectionParent:
                ok = g.canAddConnection(n, f); break;          // n ends up above first
            case InteractionState::WaitingForSecondNode_AddConnectionChild:
                ok = g.canAddConnection(f, n); break;          // n ends up below first
            case InteractionState::WaitingForSecondNode_RemoveConnection:
                ok = g.canRemoveConnection(f, n) || g.canRemoveConnection(n, f); break;
            case InteractionState::WaitingForSecondNode_FuseNodes:
                ok = g.canFuseNodes(f, n); break;
            case InteractionState::WaitingForSecondNode_AddSource:
                ok = g.canAddSourceToEdge(e, n); break;
            case InteractionState::WaitingForSecondNode_AddTarget:
                ok = g.canAddTargetToEdge(e, n); break;
            case InteractionState::WaitingForSecondNode_SimplifyConnection:
                ok = e && (e->containsSource(n) || e->containsTarget(n)); break;
            case InteractionState::Idle:
                break;
            }
            if (ok) out.insert(raw);
        }
        return out;
    }

    void DiagramScene::beginSelection(InteractionState state, Node* first, Hyperedge* edge) {
        cancelInteraction();
        candidates_ = candidatesFor(state, first, edge);
        if (candidates_.empty()) return; // The menu entry was disabled; nothing to pick.

        state_ = state;
        pending_node_ = first;
        pending_edge_ = edge;
        applySelectionMarks();

        pulse_clock_.start();
        pulse_ = 0.0;
        pulse_timer_->start();
        emit interactionHintChanged(hintFor(state));
    }

    void DiagramScene::applySelectionMarks() {
        for (auto& [raw, item] : node_items_) {
            if (raw == pending_node_)          item->setSelectionMark(NodeItem::SelectionMark::Origin);
            else if (candidates_.count(raw))   item->setSelectionMark(NodeItem::SelectionMark::Candidate);
            else                               item->setSelectionMark(NodeItem::SelectionMark::Unavailable);
        }
        for (auto& [raw, item] : edge_items_) {
            // Keep the connection being worked on, and those of the origin node,
            // in full view; everything else steps back.
            if (raw == pending_edge_) {
                item->setSelectionRole(HyperedgeItem::SelectionRole::Focus);
                continue;
            }
            bool touches_origin = false;
            if (pending_node_) {
                for (const auto& s : raw->getSources()) touches_origin |= s.get() == pending_node_;
                for (const auto& t : raw->getTargets()) touches_origin |= t.get() == pending_node_;
            }
            item->setSelectionRole(touches_origin ? HyperedgeItem::SelectionRole::None
                                                  : HyperedgeItem::SelectionRole::Dimmed);
        }
    }

    void DiagramScene::clearSelectionMarks() {
        for (auto& [raw, item] : node_items_) item->setSelectionMark(NodeItem::SelectionMark::None);
        for (auto& [raw, item] : edge_items_) item->setSelectionRole(HyperedgeItem::SelectionRole::None);
    }

    QString DiagramScene::hintFor(InteractionState state) const {
        const QString name = pending_node_
            ? QString::fromStdString(pending_node_->getName()) : QString();
        switch (state) {
        case InteractionState::WaitingForSecondNode_AddConnectionParent:
            return QStringLiteral("Elige la caja que quedará por encima de «%1»").arg(name);
        case InteractionState::WaitingForSecondNode_AddConnectionChild:
            return QStringLiteral("Elige la caja que quedará por debajo de «%1»").arg(name);
        case InteractionState::WaitingForSecondNode_RemoveConnection:
            return QStringLiteral("Elige la caja cuya conexión con «%1» quieres eliminar").arg(name);
        case InteractionState::WaitingForSecondNode_FuseNodes:
            return QStringLiteral("Elige la caja que se fusionará con «%1»").arg(name);
        case InteractionState::WaitingForSecondNode_AddSource:
            return QStringLiteral("Elige la caja que se unirá a la conexión por arriba");
        case InteractionState::WaitingForSecondNode_AddTarget:
            return QStringLiteral("Elige la caja que se unirá a la conexión por abajo");
        case InteractionState::WaitingForSecondNode_SimplifyConnection:
            return QStringLiteral("Elige la caja que quieres quitar de la conexión");
        case InteractionState::Idle:
            break;
        }
        return QString();
    }

    // ============================================================================
    // Mouse and keyboard
    // ============================================================================

    void DiagramScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
        QGraphicsItem* item = itemAt(event->scenePos(), QTransform());

        if (state_ != InteractionState::Idle) {
            if (event->button() == Qt::LeftButton) {
                auto* ni = qgraphicsitem_cast<NodeItem*>(item);
                if (ni && candidates_.count(ni->node())) completeSecondNodeClick(ni->node());
                else if (!item) cancelInteraction();
                // Anything else (a faded node, an edge) is not part of the operation.
            }
            else if (event->button() == Qt::RightButton) {
                cancelInteraction();
            }
            return;
        }

        if (item) {
            QGraphicsScene::mousePressEvent(event);
            return;
        }

        // Background click.
        if (event->button() == Qt::RightButton) {
            if (is_joint_)
                showJointBackgroundMenu(event->scenePos());
            else
                showBackgroundContextMenu(event->scenePos());
        }
    }

    void DiagramScene::keyPressEvent(QKeyEvent* event) {
        if (event->key() == Qt::Key_Escape && state_ != InteractionState::Idle) {
            cancelInteraction();
            event->accept();
            return;
        }
        QGraphicsScene::keyPressEvent(event);
    }

    // ============================================================================
    // Context menus
    // ============================================================================

    void DiagramScene::showNodeContextMenu(NodeItem* item, const QPointF&) {
        Node* node = item->node();
        // Right-click on a node while a two-step op is pending cancels it.
        if (state_ != InteractionState::Idle) {
            cancelInteraction();
            return;
        }

        QMenu* menu = style::createMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->setToolTipsVisible(true);

        const qreal dpr = dialogParent() ? dialogParent()->devicePixelRatioF() : qApp->devicePixelRatio();
        style::addMenuHeader(menu,
            renderNodeIcon(node->getAttributes(), QSize(44, 30), dpr),
            QString::fromStdString(node->getName()),
            QStringLiteral("%1 por encima · %2 por debajo")
                .arg(node->getParents().size()).arg(node->getChildren().size()));

        QAction* properties = menu->addAction(style::icon(style::Icon::Properties),
            QStringLiteral("Propiedades…"), [this, item] { showNodeProperties(item); });
        QFont bold = properties->font();
        bold.setBold(true);
        properties->setFont(bold);

        // Two-step entries are only enabled when some node can complete them.
        auto addPick = [&](style::Icon icon, const QString& text, InteractionState state,
            const QString& none_available) {
            QAction* a = menu->addAction(style::icon(icon), text,
                [this, state, node] { beginSelection(state, node, nullptr); });
            if (candidatesFor(state, node, nullptr).empty()) {
                a->setEnabled(false);
                a->setToolTip(none_available);
            }
        };

        if (!is_joint_) {
            style::addMenuSection(menu, QStringLiteral("Crear"));
            menu->addAction(style::icon(style::Icon::BoxAbove), QStringLiteral("Crear caja arriba"),
                [this, node] { onCreateNodeAbove(node); });
            menu->addAction(style::icon(style::Icon::BoxBelow), QStringLiteral("Crear caja debajo"),
                [this, node] { onCreateNodeBelow(node); });
            menu->addAction(style::icon(style::Icon::BoxLeft), QStringLiteral("Crear caja a la izquierda"),
                [this, node] { onCreateNodeLeft(node); });
            menu->addAction(style::icon(style::Icon::BoxRight), QStringLiteral("Crear caja a la derecha"),
                [this, node] { onCreateNodeRight(node); });
        }

        style::addMenuSection(menu, QStringLiteral("Conectar"));
        addPick(style::Icon::CurrentBelow, QStringLiteral("Caja actual por debajo de"),
            InteractionState::WaitingForSecondNode_AddConnectionParent,
            QStringLiteral("Ninguna caja puede quedar por encima de esta"));
        addPick(style::Icon::CurrentAbove, QStringLiteral("Caja actual por encima de"),
            InteractionState::WaitingForSecondNode_AddConnectionChild,
            QStringLiteral("Ninguna caja puede quedar por debajo de esta"));
        addPick(style::Icon::Fuse, QStringLiteral("Fusionar"),
            InteractionState::WaitingForSecondNode_FuseNodes,
            QStringLiteral("Ninguna caja puede fusionarse con esta"));

        // "Conexiones hipotéticas": one checkable entry per connection of this box,
        // named after the boxes at its other end. A doubtful connection is drawn
        // discontinuous from this box (see HypergraphRenderer).
        style::addMenuSection(menu, QStringLiteral("Estilo"));
        QMenu* doubtful = style::createMenu(menu);
        doubtful->setTitle(QStringLiteral("Conexiones hipotéticas"));
        doubtful->setIcon(style::icon(style::Icon::LineDashed));
        doubtful->setToolTipsVisible(true);
        menu->addMenu(doubtful);
        {
            const NodePtr node_ptr = node->shared_from_this();
            auto names = [](const std::vector<NodePtr>& nodes) {
                QStringList out;
                for (const auto& n : nodes) out << QStringLiteral("«%1»").arg(QString::fromStdString(n->getName()));
                return out.join(QStringLiteral(", "));
            };
            int entries = 0;
            for (const auto& e : currentGraph().getAllHyperedges()) {
                if (e->isSegment()) continue;
                const bool as_source = e->containsSource(node_ptr);
                if (!as_source && !e->containsTarget(node_ptr)) continue;
                const QString others = names(as_source ? e->getTargets() : e->getSources());
                const QString text = QStringLiteral("Con %1 (%2)")
                    .arg(QFontMetrics(doubtful->font()).elidedText(others, Qt::ElideRight, 260),
                         as_source ? QStringLiteral("abajo") : QStringLiteral("arriba"));
                QAction* a = doubtful->addAction(text);
                a->setCheckable(true);
                a->setChecked(HypergraphEditor::isConnectionEndUncertain(e, node_ptr));
                a->setToolTip(QStringLiteral("Marca esta conexión si no estás seguro de ella: "
                                             "se dibujará discontinua desde esta caja."));
                Hyperedge* raw = e.get();
                connect(a, &QAction::triggered, this, [this, raw, node] { onToggleUncertain(raw, node); });
                ++entries;
            }
            if (entries == 0) {
                doubtful->menuAction()->setEnabled(false);
                doubtful->menuAction()->setToolTip(QStringLiteral("Esta caja no tiene conexiones"));
            }
        }

        style::addMenuSection(menu, QStringLiteral("Eliminar"));
        addPick(style::Icon::RemovePartial, QStringLiteral("Eliminar conexión parcial"),
            InteractionState::WaitingForSecondNode_RemoveConnection,
            QStringLiteral("Esta caja no está conectada a ninguna otra"));
        menu->addAction(style::icon(style::Icon::RemoveBox), QStringLiteral("Eliminar caja"),
            [this, node] { onRemoveNode(node); });

        menu->popup(QCursor::pos());
    }

    void DiagramScene::showEdgeContextMenu(HyperedgeItem* item, const QPointF&) {
        if (state_ != InteractionState::Idle) {
            cancelInteraction();
            return;
        }
        Hyperedge* edge = item->edge();

        QMenu* menu = style::createMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->setToolTipsVisible(true);

        auto names = [](const std::vector<NodePtr>& nodes) {
            QStringList out;
            for (const auto& n : nodes) out << QString::fromStdString(n->getName());
            return out.join(QStringLiteral(", "));
        };
        const QString span = names(edge->getSources()) + QStringLiteral("  →  ") + names(edge->getTargets());
        const qreal dpr = dialogParent() ? dialogParent()->devicePixelRatioF() : qApp->devicePixelRatio();
        style::addMenuHeader(menu,
            style::icon(style::Icon::ForkDownExisting).pixmap(QSize(28, 28), dpr),
            QStringLiteral("Conexión"),
            QFontMetrics(menu->font()).elidedText(span, Qt::ElideRight, 260));

        auto addPick = [&](style::Icon icon, const QString& text, InteractionState state,
            const QString& none_available) {
            QAction* a = menu->addAction(style::icon(icon), text,
                [this, state, edge] { beginSelection(state, nullptr, edge); });
            if (candidatesFor(state, nullptr, edge).empty()) {
                a->setEnabled(false);
                a->setToolTip(none_available);
            }
        };

        if (!is_joint_) {
            style::addMenuSection(menu, QStringLiteral("Crear"));
            menu->addAction(style::icon(style::Icon::BoxBetween), QStringLiteral("Crear caja entre"),
                [this, edge] { onCreateNodeIntoEdge(edge); });
            menu->addAction(style::icon(style::Icon::ForkUpNew), QStringLiteral("Bifurcar arriba con caja nueva"),
                [this, edge] { onCreateSource(edge); });
            menu->addAction(style::icon(style::Icon::ForkDownNew), QStringLiteral("Bifurcar abajo con caja nueva"),
                [this, edge] { onCreateTarget(edge); });
        }

        style::addMenuSection(menu, QStringLiteral("Conectar"));
        addPick(style::Icon::ForkUpExisting, QStringLiteral("Bifurcar arriba con caja existente"),
            InteractionState::WaitingForSecondNode_AddSource,
            QStringLiteral("Ninguna caja puede unirse a esta conexión por arriba"));
        addPick(style::Icon::ForkDownExisting, QStringLiteral("Bifurcar abajo con caja existente"),
            InteractionState::WaitingForSecondNode_AddTarget,
            QStringLiteral("Ninguna caja puede unirse a esta conexión por abajo"));

        // Each entry names what it will do, with an icon of the result. A
        // connection that is partly dashed (doubtful at some boxes) offers both.
        style::addMenuSection(menu, QStringLiteral("Estilo"));
        if (!edge->isContinuous()) {
            QAction* solid = menu->addAction(style::icon(style::Icon::LineSolid), QStringLiteral("Usar línea continua"),
                [this, edge] { onSetLineStyle(edge, true); });
            if (!edge->allEndsUncertain())
                solid->setToolTip(QStringLiteral("Quita también las marcas de conexión dudosa"));
        }
        if (!edge->allEndsUncertain()) {
            menu->addAction(style::icon(style::Icon::LineDashed), QStringLiteral("Usar línea discontinua"),
                [this, edge] { onSetLineStyle(edge, false); });
        }

        style::addMenuSection(menu, QStringLiteral("Eliminar"));
        addPick(style::Icon::Simplify, QStringLiteral("Simplificar conexión"),
            InteractionState::WaitingForSecondNode_SimplifyConnection, QString());
        menu->addAction(style::icon(style::Icon::RemoveConnection), QStringLiteral("Eliminar conexión"),
            [this, edge] { onRemoveHyperedge(edge); });

        menu->popup(QCursor::pos());
    }

    void DiagramScene::showBackgroundContextMenu(const QPointF& scene_pos) {
        QMenu* menu = style::createMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->addAction(style::icon(style::Icon::NewBox), QStringLiteral("Nueva caja"),
            [this, scene_pos] { onCreateRootNode(scene_pos); });
        menu->popup(QCursor::pos());
    }

    void DiagramScene::showJointBackgroundMenu(const QPointF& scene_pos) {
        onAddHypergraph(scene_pos);
    }

    // ============================================================================
    // Completing a two-step operation
    // ============================================================================

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
                FuseNodesDialog dlg(first_ptr->getAttributes(), second_ptr->getAttributes(),
                    recentColours(), dialogParent());
                if (dlg.exec() != QDialog::Accepted) return;
                const NodeAttributes fused = dlg.attributes();
                if (is_joint_) joint_editor_->fuseNodes(first_ptr, second_ptr, fused);
                else           regular_editor_->fuseNodes(first_ptr, second_ptr, fused);
                rememberColours(dlg.pickedColours());
            }
            else if (st == InteractionState::WaitingForSecondNode_AddSource) {
                if (is_joint_) joint_editor_->addSourceToEdge(edge_ptr, second_ptr);
                else           regular_editor_->addSourceToEdge(edge_ptr, second_ptr);
            }
            else if (st == InteractionState::WaitingForSecondNode_AddTarget) {
                if (is_joint_) joint_editor_->addTargetToEdge(edge_ptr, second_ptr);
                else           regular_editor_->addTargetToEdge(edge_ptr, second_ptr);
            }
            else if (st == InteractionState::WaitingForSecondNode_SimplifyConnection) {
                // The clicked box is either a source or a target of the connection.
                if (edge_ptr->containsSource(second_ptr)) {
                    if (is_joint_) joint_editor_->removeSourceFromHyperedge(edge_ptr, second_ptr);
                    else           regular_editor_->removeSourceFromHyperedge(edge_ptr, second_ptr);
                }
                else {
                    if (is_joint_) joint_editor_->removeTargetFromHyperedge(edge_ptr, second_ptr);
                    else           regular_editor_->removeTargetFromHyperedge(edge_ptr, second_ptr);
                }
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

    void DiagramScene::onCreateNodeAbove(Node* child) {
        NodePtr child_ptr = child ? child->shared_from_this() : nullptr;
        createNodeWithDialog(QStringLiteral("Nueva caja"), [&](const NodeAttributes& a) {
            regular_editor_->createParent(a, child_ptr);
        });
    }

    void DiagramScene::onCreateNodeBelow(Node* parent) {
        NodePtr parent_ptr = parent ? parent->shared_from_this() : nullptr;
        createNodeWithDialog(QStringLiteral("Nueva caja"), [&](const NodeAttributes& a) {
            regular_editor_->createNode(a, parent_ptr->getLayer() + 1, -1, parent_ptr);
        });
    }

    void DiagramScene::onCreateNodeLeft(Node* node) {
        NodePtr neighbour_ptr = node ? node->shared_from_this() : nullptr;
        createNodeWithDialog(QStringLiteral("Nueva caja"), [&](const NodeAttributes& a) {
            regular_editor_->createNodeNextTo(a, neighbour_ptr, true);
        });
    }

    void DiagramScene::onCreateNodeRight(Node* node) {
        NodePtr neighbour_ptr = node ? node->shared_from_this() : nullptr;
        createNodeWithDialog(QStringLiteral("Nueva caja"), [&](const NodeAttributes& a) {
            regular_editor_->createNodeNextTo(a, neighbour_ptr, false);
        });
    }

    void DiagramScene::onCreateNodeIntoEdge(Hyperedge* edge) {
        HyperedgePtr edge_ptr = edge->shared_from_this();
        createNodeWithDialog(QStringLiteral("Nueva caja"), [&](const NodeAttributes& a) {
            regular_editor_->createNodeInEdge(a, edge_ptr);
        });
    }

    void DiagramScene::onCreateSource(Hyperedge* edge) {
        HyperedgePtr edge_ptr = edge->shared_from_this();
        createNodeWithDialog(QStringLiteral("Nueva caja"), [&](const NodeAttributes& a) {
            regular_editor_->createSource(a, -1, edge_ptr);
        });
    }

    void DiagramScene::onCreateTarget(Hyperedge* edge) {
        HyperedgePtr edge_ptr = edge->shared_from_this();
        createNodeWithDialog(QStringLiteral("Nueva caja"), [&](const NodeAttributes& a) {
            regular_editor_->createTarget(a, -1, edge_ptr);
        });
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

    void DiagramScene::onSetLineStyle(Hyperedge* edge, bool continuous) {
        try {
            HyperedgePtr ptr = edge->shared_from_this();
            if (is_joint_) joint_editor_->setHyperedgeContinuous(ptr, continuous);
            else           regular_editor_->setHyperedgeContinuous(ptr, continuous);
            rebuild();
            emit graphChanged();
        }
        catch (const std::exception& e) { showError(e); }
    }

    void DiagramScene::onToggleUncertain(Hyperedge* edge, Node* node) {
        try {
            HyperedgePtr edge_ptr = edge->shared_from_this();
            NodePtr node_ptr = node->shared_from_this();
            const bool uncertain = !HypergraphEditor::isConnectionEndUncertain(edge_ptr, node_ptr);
            if (is_joint_) joint_editor_->setConnectionEndUncertain(edge_ptr, node_ptr, uncertain);
            else           regular_editor_->setConnectionEndUncertain(edge_ptr, node_ptr, uncertain);
            rebuild();
            emit graphChanged();
        }
        catch (const std::exception& e) { showError(e); }
    }

    // ============================================================================
    // Background / root node creation
    // ============================================================================

    void DiagramScene::onCreateRootNode(const QPointF& scene_pos) {
        // The layer comes from the click's y, exactly as when a box is dragged to
        // another layer (scene y grows downwards, layout y upwards): -1 opens a new
        // top layer, a layer past the deepest a new bottom one.
        const GraphicalHypergraph& graph = currentGraph();
        const int layer = graph.layerForY(-scene_pos.y());

        // Within an existing layer, the box goes right after every node (dummies
        // included, since they hold positions too) whose centre is left of the click.
        int layer_position = -1;
        const auto layer_it = graph.getLayers().find(layer);
        if (layer_it != graph.getLayers().end()) {
            layer_position = 0;
            for (const auto& n : layer_it->second.nodes) {
                if (graph.getNodeLayout().at(n.get()).x < scene_pos.x()) ++layer_position;
                else break;
            }
        }

        createNodeWithDialog(QStringLiteral("Nueva caja"), [&](const NodeAttributes& a) {
            regular_editor_->createNode(a, layer, layer_position, nullptr);
        });
    }

    void DiagramScene::onAddHypergraph(const QPointF& scene_pos) {
        // Build the list of diagrams for the dialog.
        // We need access to the project, which we do not own directly.
        // MainWindow connects a lambda to a signal for this purpose;
        // here we emit a request signal and MainWindow supplies the dialog.
        emit addHypergraphRequested(scene_pos.x());
    }

    // ============================================================================
    // Node attribute dialogs
    // ============================================================================

    void DiagramScene::createNodeWithDialog(const QString& default_name,
        const std::function<void(const NodeAttributes&)>& create)
    {
        cancelInteraction();
        NodeDialog dlg(NodeDialog::Mode::Create, NodeAttributes(default_name.toStdString()),
            recentColours(), dialogParent());
        if (dlg.exec() != QDialog::Accepted) return;

        try {
            create(dlg.attributes());
        }
        catch (const std::exception& e) { showError(e); return; }

        rememberColours(dlg.pickedColours());
        rebuild();
        emit graphChanged();
    }

    void DiagramScene::showNodeProperties(NodeItem* item) {
        if (state_ != InteractionState::Idle) {
            cancelInteraction();
            return;
        }
        NodePtr node_ptr = item->node()->shared_from_this();
        NodeDialog dlg(NodeDialog::Mode::Edit, node_ptr->getAttributes(), recentColours(), dialogParent());
        if (dlg.exec() != QDialog::Accepted) return;

        const NodeAttributes updated = dlg.attributes();
        if (updated == node_ptr->getAttributes()) return;
        try {
            if (is_joint_) joint_editor_->setNodeAttributes(node_ptr, updated);
            else           regular_editor_->setNodeAttributes(node_ptr, updated);
        }
        catch (const std::exception& e) { showError(e); return; }

        rememberColours(dlg.pickedColours());
        rebuild();
        emit graphChanged();
    }

    void DiagramScene::setColourStore(std::function<QList<QColor>()> recent,
        std::function<void(const QColor&)> remember)
    {
        recent_colours_ = std::move(recent);
        remember_colour_ = std::move(remember);
    }

    QList<QColor> DiagramScene::recentColours() const {
        return recent_colours_ ? recent_colours_() : QList<QColor>{};
    }

    void DiagramScene::rememberColours(const QList<QColor>& colours) {
        if (!remember_colour_) return;
        // Oldest first, so the most recently picked colour ends up first.
        for (auto it = colours.rbegin(); it != colours.rend(); ++it)
            remember_colour_(*it);
    }

    QWidget* DiagramScene::dialogParent() const {
        const auto v = views();
        return v.isEmpty() ? nullptr : v.front()->window();
    }

    // ============================================================================
    // Helpers
    // ============================================================================

    void DiagramScene::showError(const std::exception& e) {
        dialogs::showError(dialogParent(), QStringLiteral("No se ha podido completar la operación"),
            QString::fromStdString(e.what()));
    }

    const GraphicalHypergraph& DiagramScene::currentGraph() const {
        if (is_joint_) return joint_editor_->getGraph();
        return regular_editor_->getGraph();
    }

} // namespace ui
