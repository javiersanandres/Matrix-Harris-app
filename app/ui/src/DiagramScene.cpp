#include "DiagramScene.h"
#include "NodeDialogs.h"
#include "NodeEditorWidgets.h"
#include "NodeVisuals.h"
#include "LayoutTypes.h"
#include "UiStyle.h"
#include "OptionsHelp.h"
#include "PieceClipboard.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsEllipseItem>
#include <QGraphicsItemGroup>
#include <QGraphicsPathItem>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include "AppDialogs.h"
#include <QApplication>
#include <QCursor>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <set>

using namespace hypergraph_logic;
using namespace app_logic;

namespace ui {

    namespace {
        // One full breath of the candidates' glow, in milliseconds.
        constexpr double PULSE_PERIOD_MS = 1400.0;

        // Layout transition and lift timings, in milliseconds.
        constexpr int GLIDE_MS = 340;
        constexpr int LIFT_MS = 170;
        constexpr int PICK_UP_MS = 260;  // a piece hopping under the cursor
        constexpr int TAKE_OUT_MS = 220;
        constexpr int FOLLOW_MS = 180;   // a dragged box's chain switching between reorder and follow

        // A small label in the brand gradient; its position is the middle of its
        // bottom edge. The scene scales it to look the same size at any zoom of
        // the editing view (see placeChip) -- not ItemIgnoresTransformations, which
        // would draw it just as big over the tab miniatures.
        class ChipItem : public QGraphicsItem {
        public:
            ChipItem() {
                setZValue(60);
                font_.setPointSizeF(9.0);
                font_.setBold(true);
            }
            void setText(const QString& text) {
                if (text == text_) return;
                prepareGeometryChange();
                text_ = text;
                const QFontMetricsF fm(font_);
                const QSizeF size(fm.horizontalAdvance(text_) + 22.0, fm.height() + 10.0);
                rect_ = QRectF(-size.width() / 2.0, -size.height() - 6.0, size.width(), size.height());
                update();
            }
            QRectF boundingRect() const override { return rect_.adjusted(-6, -6, 6, 12); }
            void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
                p->setRenderHint(QPainter::Antialiasing);
                const double r = rect_.height() / 2.0;
                // Soft shadow, then the pill and a small pointer down to the anchor.
                p->setPen(Qt::NoPen);
                p->setBrush(QColor(31, 35, 48, 40));
                p->drawRoundedRect(rect_.translated(0, 3), r, r);
                QLinearGradient g(rect_.topLeft(), rect_.topRight());
                g.setColorAt(0, style::palette::accent);
                g.setColorAt(1, style::palette::violet);
                p->setBrush(g);
                QPainterPath pill;
                pill.addRoundedRect(rect_, r, r);
                QPolygonF tip{ QPointF(-5, rect_.bottom() - 0.5), QPointF(5, rect_.bottom() - 0.5), QPointF(0, rect_.bottom() + 5) };
                pill.addPolygon(tip);
                p->drawPath(pill.simplified());
                p->setPen(Qt::white);
                p->setFont(font_);
                p->drawText(rect_, Qt::AlignCenter, text_);
            }
        private:
            QString text_;
            QFont font_;
            QRectF rect_;
        };

        double easeOutCubic(double t) {
            t = std::clamp(t, 0.0, 1.0);
            return 1.0 - std::pow(1.0 - t, 3.0);
        }

        // Keeps every click it receives. A click a menu's widget does not take
        // goes on to the menu, which treats it as choosing that entry and closes;
        // a list of rows with their own buttons wants clicks outside the buttons
        // to do nothing.
        class ClickSink : public QWidget {
        public:
            using QWidget::QWidget;
        protected:
            void mousePressEvent(QMouseEvent* e) override { e->accept(); }
            void mouseReleaseEvent(QMouseEvent* e) override { e->accept(); }
            void mouseDoubleClickEvent(QMouseEvent* e) override { e->accept(); }
        };

        // The dot before a diagram's name: filled with the brand gradient when the
        // diagram is in the joint, a hollow ring otherwise. Painted with room for
        // its stroke all around, so no style can clip it.
        class StatusDot : public QWidget {
        public:
            StatusDot(bool filled, QWidget* parent) : QWidget(parent), filled_(filled) {
                setFixedSize(12, 12);
                setAttribute(Qt::WA_TransparentForMouseEvents);
            }
        protected:
            void paintEvent(QPaintEvent*) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                const QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
                if (filled_) {
                    QLinearGradient g(r.topLeft(), r.bottomRight());
                    g.setColorAt(0, style::palette::accent);
                    g.setColorAt(1, style::palette::violet);
                    p.setPen(Qt::NoPen);
                    p.setBrush(g);
                }
                else {
                    p.setPen(QPen(QColor(0xC5, 0xC9, 0xD6), 1.5));
                    p.setBrush(Qt::NoBrush);
                }
                p.drawEllipse(r);
            }
        private:
            bool filled_;
        };
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
                // Every box glides from where it is drawn now (the dragged one
                // from where it was dropped); a refused move glides it back.
                const auto from = nodeCenters();
                try {
                    regular_editor_->relocateNode(node->shared_from_this(), new_x, -new_y);
                }
                catch (const std::exception&) {
                    rebuildAnimated(from);
                    return;
                }
                rebuildAnimated(from);
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
                const auto from = nodeCenters();
                try {
                    joint_editor_->relocateNode(node->shared_from_this(), new_x, -new_y);
                }
                catch (const std::exception&) {
                    rebuildAnimated(from);
                    return;
                }
                rebuildAnimated(from);
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
        // Whatever was moving or animating refers to items about to be deleted.
        stopAnimations();
        clearGuides();
        box_drag_.reset();
        if (piece_move_) endPieceMove();
        node_items_.clear();
        edge_items_.clear();
        equivalence_items_.clear();

        HypergraphRenderer::render(
            currentGraph(), this, node_items_, edge_items_, equivalence_items_,
            [](Node* node, const QRectF& rect) {
                return new NodeItem(node, rect);
            },
            [](Hyperedge* edge, const QPainterPath& solid, const QPainterPath& dashed) {
                return new HyperedgeItem(edge, solid, dashed);
            },
            [](Node* left, Node* right, const QPainterPath& line, bool continuous) {
                return new EquivalenceItem(left, right, line, continuous);
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
            case InteractionState::WaitingForSecondNode_AddEquivalence:
                ok = g.canAddEquivalence(f, n); break;
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
        for (EquivalenceItem* item : equivalence_items_) {
            const bool touches_origin = pending_node_ && item->touches(pending_node_);
            item->setSelectionRole(touches_origin ? EquivalenceItem::SelectionRole::None
                                                  : EquivalenceItem::SelectionRole::Dimmed);
        }
    }

    void DiagramScene::clearSelectionMarks() {
        for (auto& [raw, item] : node_items_) item->setSelectionMark(NodeItem::SelectionMark::None);
        for (auto& [raw, item] : edge_items_) item->setSelectionRole(HyperedgeItem::SelectionRole::None);
        for (EquivalenceItem* item : equivalence_items_) item->setSelectionRole(EquivalenceItem::SelectionRole::None);
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
        case InteractionState::WaitingForSecondNode_AddEquivalence:
            return QStringLiteral("Elige la caja que será equivalente a «%1»").arg(name);
        case InteractionState::Idle:
            break;
        }
        return QString();
    }

    // ============================================================================
    // Mouse and keyboard
    // ============================================================================

    void DiagramScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
        if (piece_move_) {
            // The piece is dropped when the button is released (a click drops it
            // where it is; a drag, where the drag ends); a right click puts it back.
            if (event->button() == Qt::RightButton) {
                swallow_context_menu_ = true;
                cancelPieceMove();
            }
            event->accept();
            return;
        }
        swallow_context_menu_ = false; // a new click: its menu is a real one

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

    void DiagramScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
        if (piece_move_) {
            piece_move_->offset = event->scenePos() - piece_move_->grab;
            piece_move_->piece->setPos(piece_move_->offset * piece_move_->snap);
            updatePiecePreview();
            event->accept();
            return;
        }
        QGraphicsScene::mouseMoveEvent(event);
    }

    void DiagramScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
        if (piece_move_) {
            if (event->button() == Qt::LeftButton) dropPiece();
            event->accept();
            return;
        }
        QGraphicsScene::mouseReleaseEvent(event);
    }

    void DiagramScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
        if (piece_move_ || swallow_context_menu_) { // the right click already put it back
            swallow_context_menu_ = false;
            event->accept();
            return;
        }
        QGraphicsScene::contextMenuEvent(event);
    }

    void DiagramScene::keyPressEvent(QKeyEvent* event) {
        if (event->key() == Qt::Key_Escape && piece_move_) {
            cancelPieceMove();
            event->accept();
            return;
        }
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
        addPick(style::Icon::CurrentBelow, QStringLiteral("Caja «%1» por debajo de").arg(QString::fromStdString(node->getName())),
            InteractionState::WaitingForSecondNode_AddConnectionParent,
            QStringLiteral("Ninguna caja puede quedar por encima de esta"));
        addPick(style::Icon::CurrentAbove, QStringLiteral("Caja «%1» por encima de").arg(QString::fromStdString(node->getName())),
            InteractionState::WaitingForSecondNode_AddConnectionChild,
            QStringLiteral("Ninguna caja puede quedar por debajo de esta"));
        addPick(style::Icon::Fuse, QStringLiteral("Fusionar"),
            InteractionState::WaitingForSecondNode_FuseNodes,
            QStringLiteral("Ninguna caja puede fusionarse con esta"));

        addEquivalenceEntries(menu, node);

        const PieceChoice piece = pieceChoiceFor(node);
        addMoveEntries(menu, piece, node);
        addCopyEntries(menu, node);

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
            // And its equivalence lines, at most one on each side: the same as
            // choosing the line's style from the line itself.
            if (const EquivalenceChain* chain = currentGraph().getEquivalenceChain(node)) {
                const auto& members = chain->members;
                const auto at = std::find(members.begin(), members.end(), node_ptr) - members.begin();
                for (const auto k : { at - 1, at + 1 }) {
                    if (k < 0 || k >= static_cast<std::ptrdiff_t>(members.size())) continue;
                    Node* other = members[static_cast<size_t>(k)].get();
                    QAction* a = doubtful->addAction(QStringLiteral("Con «%1» (equivalencia)")
                        .arg(QFontMetrics(doubtful->font()).elidedText(QString::fromStdString(other->getName()),
                             Qt::ElideRight, 220)));
                    a->setCheckable(true);
                    const bool dashed = !currentGraph().isEquivalenceContinuous(node_ptr, other->shared_from_this());
                    a->setChecked(dashed);
                    a->setToolTip(QStringLiteral("Marca esta equivalencia si no estás seguro de ella: "
                                                 "la línea que las une se dibujará discontinua."));
                    connect(a, &QAction::triggered, this, [this, node, other, dashed] {
                        onSetEquivalenceStyle(node, other, dashed);
                    });
                    ++entries;
                }
            }
            if (entries == 0) {
                doubtful->menuAction()->setEnabled(false);
                doubtful->menuAction()->setToolTip(QStringLiteral("Esta caja no tiene conexiones ni equivalencias"));
            }
        }

        style::addMenuSection(menu, QStringLiteral("Eliminar"));
        addPick(style::Icon::RemovePartial, QStringLiteral("Eliminar conexión parcial"),
            InteractionState::WaitingForSecondNode_RemoveConnection,
            QStringLiteral("Esta caja no está conectada a ninguna otra"));
        menu->addAction(style::icon(style::Icon::RemoveBox), QStringLiteral("Eliminar caja"),
            [this, node] { onRemoveNode(node); });
        addRemoveBlockEntry(menu, node);

        help::addHelpButtons(menu, dialogParent());
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

        // A connection belongs to the same diagram and block as its boxes.
        Node* reference = edge->getSources().empty() ? nullptr : edge->getSources().front().get();
        const PieceChoice piece = pieceChoiceFor(reference);
        addMoveEntries(menu, piece, reference);
        addCopyEntries(menu, reference);

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
        addRemoveBlockEntry(menu, reference);

        help::addHelpButtons(menu, dialogParent());
        menu->popup(QCursor::pos());
    }

    void DiagramScene::addEquivalenceEntries(QMenu* menu, Node* node) {
        const GraphicalHypergraph& graph = currentGraph();
        const NodePtr node_ptr = node->shared_from_this();
        const EquivalenceChain* chain = graph.getEquivalenceChain(node);

        style::addMenuSection(menu, QStringLiteral("Equivalencias"));
        QAction* make = menu->addAction(style::icon(style::Icon::Equivalent), QStringLiteral("Hacer equivalente a…"),
            [this, node] { beginSelection(InteractionState::WaitingForSecondNode_AddEquivalence, node, nullptr); });
        make->setToolTip(QStringLiteral("Elige otra caja: quedarán una junto a la otra, en el mismo nivel, "
                                        "unidas por una línea horizontal"));
        if (candidatesFor(InteractionState::WaitingForSecondNode_AddEquivalence, node, nullptr).empty()) {
            make->setEnabled(false);
            const bool middle = chain && chain->members.front() != node_ptr && chain->members.back() != node_ptr;
            make->setToolTip(middle
                ? QStringLiteral("Esta caja ya tiene una caja equivalente a cada lado: solo las de los extremos "
                                 "de la cadena pueden hacerse equivalentes a otra")
                : QStringLiteral("Ninguna caja puede ser equivalente a esta: las conexiones que hay entre ellas "
                                 "lo impiden"));
        }

        // One entry per line it has: with the box on its left, then on its right.
        if (!chain) return;
        const auto& members = chain->members;
        const auto at = std::find(members.begin(), members.end(), node_ptr) - members.begin();
        for (const auto k : { at - 1, at + 1 }) {
            if (k < 0 || k >= static_cast<std::ptrdiff_t>(members.size())) continue;
            Node* other = members[static_cast<size_t>(k)].get();
            const QString name = QFontMetrics(menu->font()).elidedText(QString::fromStdString(other->getName()),
                Qt::ElideRight, 200);
            QAction* a = menu->addAction(style::icon(style::Icon::RemoveEquivalence),
                QStringLiteral("Quitar equivalencia con «%1»").arg(name),
                [this, node, other] { onRemoveEquivalence(node, other); });
            a->setToolTip(QStringLiteral("Borra la línea que las une: la cadena se parte en dos por ahí"));
        }
    }

    void DiagramScene::showEquivalenceContextMenu(EquivalenceItem* item, const QPointF&) {
        if (state_ != InteractionState::Idle) {
            cancelInteraction();
            return;
        }
        Node* left = item->left();
        Node* right = item->right();
        const bool continuous = item->isContinuous();

        QMenu* menu = style::createMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->setToolTipsVisible(true);

        const QString span = QStringLiteral("%1  ≡  %2")
            .arg(QString::fromStdString(left->getName()), QString::fromStdString(right->getName()));
        const qreal dpr = dialogParent() ? dialogParent()->devicePixelRatioF() : qApp->devicePixelRatio();
        style::addMenuHeader(menu,
            style::icon(continuous ? style::Icon::EquivalenceSolid : style::Icon::EquivalenceDashed).pixmap(QSize(28, 28), dpr),
            continuous ? QStringLiteral("Equivalencia") : QStringLiteral("Equivalencia hipotética"),
            QFontMetrics(menu->font()).elidedText(span, Qt::ElideRight, 260));

        style::addMenuSection(menu, QStringLiteral("Estilo"));
        if (continuous) {
            QAction* a = menu->addAction(style::icon(style::Icon::EquivalenceDashed), QStringLiteral("Usar línea discontinua"),
                [this, left, right] { onSetEquivalenceStyle(left, right, false); });
            a->setToolTip(QStringLiteral("Marca esta equivalencia como hipotética: las cajas de un lado de la "
                                         "línea dejan de ser seguras con las del otro"));
            a->setProperty(help::TOPIC_PROPERTY, QStringLiteral("equivalenceStyle"));
        }
        else {
            QAction* a = menu->addAction(style::icon(style::Icon::EquivalenceSolid), QStringLiteral("Usar línea continua"),
                [this, left, right] { onSetEquivalenceStyle(left, right, true); });
            a->setToolTip(QStringLiteral("Da esta equivalencia por segura: las cajas seguras con una lo pasan a ser "
                                         "con la otra, y alguna otra línea de la cadena puede volverse continua"));
            a->setProperty(help::TOPIC_PROPERTY, QStringLiteral("equivalenceStyle"));
        }

        style::addMenuSection(menu, QStringLiteral("Eliminar"));
        QAction* remove = menu->addAction(style::icon(style::Icon::RemoveEquivalence), QStringLiteral("Quitar equivalencia"),
            [this, left, right] { onRemoveEquivalence(left, right); });
        remove->setToolTip(QStringLiteral("Borra esta línea: la cadena se parte en dos por ahí, y cada parte vuelve "
                                          "al nivel que le corresponde"));

        help::addHelpButtons(menu, dialogParent());
        menu->popup(QCursor::pos());
    }

    void DiagramScene::showBackgroundContextMenu(const QPointF& scene_pos) {
        QMenu* menu = style::createMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->addAction(style::icon(style::Icon::NewBox), QStringLiteral("Nueva caja"),
            [this, scene_pos] { onCreateRootNode(scene_pos); });
        // Only when there is something to paste (possibly copied in another window).
        if (canPaste()) {
            menu->setToolTipsVisible(true);
            QAction* paste = menu->addAction(style::icon(style::Icon::Paste), QStringLiteral("Pegar"),
                [this, scene_pos] { pasteAt(scene_pos); });
            paste->setToolTip(QStringLiteral("Pega aquí %1").arg(clipboard::description()));
        }
        help::addHelpButtons(menu, dialogParent());
        menu->popup(QCursor::pos());
    }

    void DiagramScene::showJointBackgroundMenu(const QPointF& scene_pos) {
        // Every diagram of the project, each with a button to add it here (where
        // the user clicked) and one to take it out of the joint.
        const JointGraphicalHypergraph& joint = joint_editor_->getGraph();
        const std::vector<DiagramInfo> diagrams = diagram_catalog_ ? diagram_catalog_() : std::vector<DiagramInfo>{};
        const auto& added = joint.getIncorporatedIds();
        int in_joint = 0;
        for (const auto& d : diagrams) in_joint += added.count(d.id) ? 1 : 0;

        QMenu* menu = style::createMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->setToolTipsVisible(true);
        menu->setStyleSheet(menu->styleSheet() + QStringLiteral(R"(
QMenu QLabel#emptyHint { color: #8A90A2; padding: 2px 14px 8px 14px; }
)"));

        const qreal dpr = dialogParent() ? dialogParent()->devicePixelRatioF() : qApp->devicePixelRatio();
        style::addMenuHeader(menu, style::icon(style::Icon::Joint).pixmap(QSize(28, 28), dpr),
            QStringLiteral("Administrar esquemas"),
            diagrams.empty() ? QStringLiteral("El proyecto aún no tiene esquemas")
                             : QStringLiteral("%1 de %2 en el esquema conjunto").arg(in_joint).arg(diagrams.size()));

        if (diagrams.empty()) {
            auto* hint = new QLabel(QStringLiteral("Crea un esquema en otra pestaña\npara poder añadirlo aquí."), menu);
            hint->setObjectName("emptyHint");
            auto* action = new QWidgetAction(menu);
            action->setDefaultWidget(hint);
            menu->addAction(action);
        }
        else {
            style::addMenuSection(menu, QStringLiteral("Esquemas del proyecto"));
        }

        // All the rows live in one scrolling list: a long project does not make the
        // menu run off the screen, and clicks between the buttons do nothing.
        auto* sink = new ClickSink(menu);
        // The list's own look lives on the list: the menu's rules do not reach
        // widgets inside the scroll area.
        sink->setStyleSheet(QStringLiteral(R"(
QWidget#diagramRow { background: transparent; border-radius: 10px; }
QWidget#diagramRow:hover { background: #F4F5FF; }
QLabel { background: transparent; }
QLabel#diagramName { color: #1F2330; font-weight: 600; }
QLabel#diagramStatus { color: #8A90A2; font-size: 8pt; }
QLabel#diagramStatus[added="true"] { color: #4F46E5; }
QScrollArea#diagramList { background: transparent; border: none; }
QWidget#diagramListBody { background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 2px 0px 2px 0px; }
QScrollBar::handle:vertical { background: #D5D8E3; border-radius: 4px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: #B8BCCB; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QToolButton#rowButton { border: none; border-radius: 8px; padding: 3px; background: transparent; }
QToolButton#rowButton:hover { background: #FFFFFF; }
QToolButton#rowButton:pressed { background: #E4E7FF; }
)"));
        auto* sink_layout = new QVBoxLayout(sink);
        sink_layout->setContentsMargins(2, 0, 2, 2);
        auto* scroll = new QScrollArea(sink);
        scroll->setObjectName("diagramList");
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidgetResizable(true);
        scroll->viewport()->setAutoFillBackground(false);
        // In the scroll area before any row is built, so the rows pick up the
        // menu's style sheet (a parentless body only got it partly on reparenting).
        auto* body = new QWidget;
        body->setObjectName("diagramListBody");
        scroll->setWidget(body);
        body->setAutoFillBackground(false); // setWidget turns it on: the menu shows through
        auto* rows = new QVBoxLayout(body);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(2);

        for (const auto& d : diagrams) {
            const bool is_added = added.count(d.id) > 0;
            // Why it cannot be taken out, when it is here but bound to others; and
            // whether equivalences join it to others (it can still be taken out).
            QString blocked;
            bool equivalent_to_others = false;
            if (is_added) {
                try {
                    joint.getRemovableNodes(d.id);
                    equivalent_to_others = !joint.isSeparable(d.id);
                }
                catch (const std::exception& e) { blocked = QString::fromStdString(e.what()); }
            }

            auto* row = new QWidget(body);
            row->setObjectName("diagramRow");
            row->setAttribute(Qt::WA_StyledBackground);
            row->setAttribute(Qt::WA_Hover);
            auto* layout = new QHBoxLayout(row);
            layout->setContentsMargins(12, 5, 8, 5);
            layout->setSpacing(10);

            layout->addWidget(new StatusDot(is_added, row), 0, Qt::AlignVCenter);

            auto* texts = new QVBoxLayout;
            texts->setSpacing(0);
            auto* name = new QLabel(row);
            name->setObjectName("diagramName");
            name->setText(name->fontMetrics().elidedText(d.name, Qt::ElideRight, 190));
            name->setToolTip(d.name);
            texts->addWidget(name);
            auto* status = new QLabel(row);
            status->setObjectName("diagramStatus");
            status->setProperty("added", is_added);
            status->setText(!is_added ? QStringLiteral("Fuera del conjunto")
                          : !blocked.isEmpty() ? QStringLiteral("En el conjunto · unido a otros")
                          : equivalent_to_others ? QStringLiteral("En el conjunto · con equivalencias a otros")
                          : QStringLiteral("En el conjunto"));
            texts->addWidget(status);
            layout->addLayout(texts, 1);
            layout->addSpacing(14);

            auto makeButton = [&](style::Icon icon, bool enabled, const QString& tip) {
                auto* b = new QToolButton(row);
                b->setObjectName("rowButton");
                b->setIcon(style::icon(icon));
                b->setIconSize(QSize(20, 20));
                b->setAutoRaise(true);
                b->setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
                b->setEnabled(enabled);
                b->setToolTip(tip);
                // A disabled button still explains itself on hover.
                b->setAttribute(Qt::WA_AlwaysShowToolTips);
                layout->addWidget(b, 0, Qt::AlignVCenter);
                return b;
            };
            QToolButton* add = makeButton(style::Icon::Add, !is_added,
                is_added ? QStringLiteral("Ya está en el esquema conjunto")
                         : QStringLiteral("Añadirlo aquí, donde has hecho clic"));
            QToolButton* remove = makeButton(style::Icon::Trash, is_added && blocked.isEmpty(),
                !is_added ? QStringLiteral("No está en el esquema conjunto")
                : !blocked.isEmpty() ? blocked + QStringLiteral("\nTambién puedes usar «Quitar bloque» en una de sus cajas.")
                : equivalent_to_others ? QStringLiteral("Quitarlo del esquema conjunto, con sus equivalencias con "
                                                        "otros esquemas (el esquema original no cambia)")
                : QStringLiteral("Quitarlo del esquema conjunto (el esquema original no cambia)"));

            const QString id = QString::fromStdString(d.id);
            const double click_x = scene_pos.x();
            connect(add, &QToolButton::clicked, this, [this, menu, id, click_x] {
                menu->close();
                emit addHypergraphRequested(id, click_x);
            });
            connect(remove, &QToolButton::clicked, this, [this, menu, id] {
                menu->close();
                // The confirmation opens once the menu is gone.
                QTimer::singleShot(0, this, [this, id] { onRemoveDiagram(id.toStdString()); });
            });

            rows->addWidget(row);
        }

        if (!diagrams.empty()) {
            // At most five rows show at once; past that the list scrolls.
            constexpr int VISIBLE_ROWS = 5;
            const int full = body->sizeHint().height();
            const int row_height = rows->itemAt(0)->sizeHint().height() + rows->spacing();
            const int most = VISIBLE_ROWS * row_height - rows->spacing();
            const bool scrolls = full > most;
            scroll->setFixedHeight(std::min(full, most));
            scroll->setMinimumWidth(body->sizeHint().width()
                + (scrolls ? scroll->verticalScrollBar()->sizeHint().width() + 4 : 0));
            sink_layout->addWidget(scroll);

            auto* action = new QWidgetAction(menu);
            action->setDefaultWidget(sink);
            menu->addAction(action);
        }
        else {
            delete sink; // and with it the scroll area and its (empty) body
        }

        menu->popup(QCursor::pos());
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
        // Every box glides from where it is now: what an operation moves (a chain
        // coming down to its equivalent's layer, boxes making room) is seen moving.
        const auto from = nodeCenters();

        try {
            if (st == InteractionState::WaitingForSecondNode_AddEquivalence) {
                if (is_joint_) joint_editor_->addEquivalence(first_ptr, second_ptr);
                else           regular_editor_->addEquivalence(first_ptr, second_ptr);
            }
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

        rebuildAnimated(from);
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
            // The rest glides together: in a chain, its equivalents close the gap.
            const auto from = nodeCenters();
            NodePtr ptr = node->shared_from_this();
            if (is_joint_) joint_editor_->removeNode(ptr);
            else           regular_editor_->removeNode(ptr);
            rebuildAnimated(from);
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

    void DiagramScene::onSetEquivalenceStyle(Node* a, Node* b, bool continuous) {
        try {
            const NodePtr pa = a->shared_from_this(), pb = b->shared_from_this();
            if (is_joint_) joint_editor_->setEquivalenceContinuous(pa, pb, continuous);
            else           regular_editor_->setEquivalenceContinuous(pa, pb, continuous);
            rebuild();
            emit graphChanged();
        }
        catch (const std::exception& e) { showError(e); }
    }

    void DiagramScene::onRemoveEquivalence(Node* a, Node* b) {
        try {
            // Each part of the chain may go back up to its own layer: seen gliding.
            const auto from = nodeCenters();
            const NodePtr pa = a->shared_from_this(), pb = b->shared_from_this();
            if (is_joint_) joint_editor_->removeEquivalence(pa, pb);
            else           regular_editor_->removeEquivalence(pa, pb);
            rebuildAnimated(from);
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

    // ============================================================================
    // Node attribute dialogs
    // ============================================================================

    void DiagramScene::createNodeWithDialog(const QString& default_name,
        const std::function<void(const NodeAttributes&)>& create)
    {
        cancelInteraction();
        const std::string name = default_name.toStdString();
        NodeDialog dlg(NodeDialog::Mode::Create, new_attributes_ ? new_attributes_(name) : NodeAttributes(name),
            recentColours(), dialogParent());
        if (dlg.exec() != QDialog::Accepted) return;

        try {
            create(dlg.attributes());
        }
        catch (const std::exception& e) { showError(e); return; }

        rememberColours(dlg.pickedColours());
        applyProjectDefaults(dlg);
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
        const bool font_size_for_all = dlg.fontSizeForAll();
        if (updated == node_ptr->getAttributes() && !font_size_for_all) {
            applyProjectDefaults(dlg); // the box stays as it was, the project may not
            return;
        }
        // A new shape or size may move the boxes around it: they glide there.
        const auto from = nodeCenters();
        try {
            if (font_size_for_all) {
                if (is_joint_) joint_editor_->setNodeAttributesWithFontSizeForAll(node_ptr, updated);
                else           regular_editor_->setNodeAttributesWithFontSizeForAll(node_ptr, updated);
            }
            else {
                if (is_joint_) joint_editor_->setNodeAttributes(node_ptr, updated);
                else           regular_editor_->setNodeAttributes(node_ptr, updated);
            }
        }
        catch (const std::exception& e) { showError(e); return; }

        rememberColours(dlg.pickedColours());
        applyProjectDefaults(dlg);
        rebuildAnimated(from);
        emit graphChanged();
    }

    bool DiagramScene::canResizeBoxes() const {
        return state_ == InteractionState::Idle && !piece_move_ && !box_drag_;
    }

    void DiagramScene::resizeNode(Node* node, const QSizeF& size, Qt::Orientations sides) {
        // Only what was dragged becomes the box's own: the other side keeps following its default.
        NodeAttributes resized = node->getAttributes();
        if (sides & Qt::Horizontal) resized.width = std::round(size.width());
        if (sides & Qt::Vertical)   resized.height = std::round(size.height());
        if (resized == node->getAttributes()) {
            rebuild(); // dropped at the size it had
            return;
        }
        // The resized box is already drawn at its new size; the rest glides.
        const auto from = nodeCenters();
        try {
            if (is_joint_) joint_editor_->setNodeAttributes(node->shared_from_this(), resized);
            else           regular_editor_->setNodeAttributes(node->shared_from_this(), resized);
        }
        catch (const std::exception& e) {
            showError(e);
            rebuild(); // back to the size it had
            return;
        }
        rebuildAnimated(from);
        emit graphChanged();
    }

    void DiagramScene::setProjectDefaultsStore(
        std::function<NodeAttributes(const std::string&)> new_attributes,
        std::function<void(const QColor&)> set_colour,
        std::function<void(const QColor&)> set_font_colour,
        std::function<void(int)> set_font_size)
    {
        new_attributes_ = std::move(new_attributes);
        set_default_colour_ = std::move(set_colour);
        set_default_font_colour_ = std::move(set_font_colour);
        set_default_font_size_ = std::move(set_font_size);
    }

    void DiagramScene::applyProjectDefaults(const NodeDialog& dialog) {
        const NodeAttributes a = dialog.attributes();
        bool changed = false;
        if (dialog.colourAsDefault() && set_default_colour_) {
            set_default_colour_(node_visuals::toQColor(a.colour));
            changed = true;
        }
        if (dialog.fontColourAsDefault() && set_default_font_colour_) {
            set_default_font_colour_(node_visuals::toQColor(a.font_colour));
            changed = true;
        }
        if (dialog.fontSizeAsDefault() && set_default_font_size_) {
            set_default_font_size_(a.font_size);
            changed = true;
        }
        if (changed) emit graphChanged(); // the title shows the unsaved change
    }

    void DiagramScene::setColourStore(std::function<QList<QColor>()> recent,
        std::function<void(const QColor&)> remember)
    {
        recent_colours_ = std::move(recent);
        remember_colour_ = std::move(remember);
    }

    void DiagramScene::setDiagramCatalog(std::function<std::vector<DiagramInfo>()> catalog) {
        diagram_catalog_ = std::move(catalog);
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
    // Layout transitions
    // ============================================================================

    std::unordered_map<Node*, QPointF> DiagramScene::nodeCenters() const {
        std::unordered_map<Node*, QPointF> centers;
        for (const auto& [node, item] : node_items_)
            centers[node] = item->mapToScene(item->rect().center());
        return centers;
    }

    void DiagramScene::rebuildAnimated(const std::unordered_map<Node*, QPointF>& from) {
        rebuild();

        struct Glide { NodeItem* item; QPointF offset; };
        auto glides = std::make_shared<std::vector<Glide>>();
        auto arrivals = std::make_shared<std::vector<NodeItem*>>(); // boxes that were not there
        std::unordered_set<Node*> changed;
        for (const auto& [node, item] : node_items_) {
            auto it = from.find(node);
            if (it == from.end()) {
                arrivals->push_back(item);
                changed.insert(node);
                continue;
            }
            const QPointF offset = it->second - item->mapToScene(item->rect().center());
            if (std::hypot(offset.x(), offset.y()) < 0.5) continue;
            glides->push_back({ item, offset });
            changed.insert(node);
        }
        // Connections and equivalence lines of what moved are redrawn in their
        // new shape: they fade in.
        auto redrawn = std::make_shared<std::vector<QGraphicsItem*>>();
        for (const auto& [edge, item] : edge_items_) {
            bool touches = false;
            for (const auto& s : edge->getSources()) touches = touches || changed.count(s.get());
            for (const auto& t : edge->getTargets()) touches = touches || changed.count(t.get());
            if (touches) redrawn->push_back(item);
        }
        for (EquivalenceItem* item : equivalence_items_)
            if (changed.count(item->left()) || changed.count(item->right())) redrawn->push_back(item);
        if (glides->empty() && arrivals->empty()) return;

        auto apply = [glides, arrivals, redrawn](double raw) {
            const double t = easeOutCubic(raw);
            for (const auto& g : *glides) g.item->setPos(g.offset * (1.0 - t));
            for (NodeItem* item : *arrivals) {
                item->setOpacity(t);
                item->setScale(0.92 + 0.08 * t);
            }
            // The connections come back once the boxes are nearly in place.
            const double edges = std::clamp((raw - 0.35) / 0.65, 0.0, 1.0);
            for (QGraphicsItem* item : *redrawn) item->setOpacity(edges);
        };
        for (NodeItem* item : *arrivals) item->setTransformOriginPoint(item->rect().center());
        apply(0.0);

        transition_ = new QVariantAnimation(this);
        transition_->setDuration(GLIDE_MS);
        transition_->setStartValue(0.0);
        transition_->setEndValue(1.0);
        connect(transition_, &QVariantAnimation::valueChanged, this, [apply](const QVariant& v) { apply(v.toDouble()); });
        connect(transition_, &QVariantAnimation::finished, this, [apply] { apply(1.0); });
        transition_->start(QAbstractAnimation::DeleteWhenStopped);
    }

    void DiagramScene::stopAnimations() {
        // Stopping never emits finished(), so nothing runs on deleted items.
        for (QPointer<QVariantAnimation>* a : { &transition_, &lift_, &fade_, &follow_ })
            if (*a) (*a)->stop();
    }

    // ============================================================================
    // Guides drawn while something moves
    // ============================================================================

    void DiagramScene::createGuides() {
        clearGuides();
        Guides g;
        auto keep = [this](QGraphicsItem* item, double z) {
            item->setZValue(z);
            item->setVisible(false);
            item->setAcceptedMouseButtons(Qt::NoButton);
            addItem(item);
        };
        g.band = new QGraphicsPathItem;
        keep(g.band, -1.0); // behind the boxes
        QColor glow = style::palette::accent;
        glow.setAlpha(55);
        g.halo = new QGraphicsPathItem;
        g.halo->setPen(QPen(glow, 12.0, Qt::SolidLine, Qt::RoundCap));
        keep(g.halo, 40);
        g.bar = new QGraphicsPathItem;
        keep(g.bar, 41);
        g.dot_top = new QGraphicsEllipseItem(-5.5, -5.5, 11.0, 11.0);
        g.dot_top->setPen(QPen(Qt::white, 2.0));
        g.dot_top->setBrush(style::palette::accent);
        keep(g.dot_top, 42);
        g.dot_bottom = new QGraphicsEllipseItem(-5.5, -5.5, 11.0, 11.0);
        g.dot_bottom->setPen(QPen(Qt::white, 2.0));
        g.dot_bottom->setBrush(style::palette::violet);
        keep(g.dot_bottom, 42);
        g.chip = new ChipItem;
        keep(g.chip, 60);
        g.ghost = new QGraphicsPathItem;
        keep(g.ghost, -0.5); // behind the boxes, over the regions
        guides_ = std::move(g);
    }

    void DiagramScene::clearGuides() {
        if (!guides_) return;
        std::vector<QGraphicsItem*> items{ guides_->band, guides_->halo, guides_->bar,
            guides_->dot_top, guides_->dot_bottom, guides_->chip, guides_->ghost };
        items.insert(items.end(), guides_->regions.begin(), guides_->regions.end());
        guides_.reset();
        for (QGraphicsItem* item : items) {
            removeItem(item);
            delete item;
        }
    }

    void DiagramScene::placeBand(const QRectF& rect, bool dashed) {
        if (!guides_) return;
        QPainterPath path;
        path.addRoundedRect(rect, 18.0, 18.0);
        guides_->band->setPath(path);
        QColor fill = style::palette::accent;
        fill.setAlpha(dashed ? 10 : 18);
        guides_->band->setBrush(fill);
        QColor edge = style::palette::accent;
        edge.setAlpha(dashed ? 150 : 45);
        QPen pen(edge, 1.4, dashed ? Qt::DashLine : Qt::SolidLine);
        pen.setCosmetic(true);
        guides_->band->setPen(pen);
        guides_->band->setVisible(true);
    }

    void DiagramScene::placeLandingMarker(double x, double top_y, double bottom_y) {
        if (!guides_) return;
        // An insertion bar: a soft glow, the bar and a dot at each end.
        QPainterPath line;
        line.moveTo(x, top_y);
        line.lineTo(x, bottom_y);
        guides_->halo->setPath(line);
        guides_->bar->setPath(line);
        QLinearGradient g(x, top_y, x, bottom_y);
        g.setColorAt(0, style::palette::accent);
        g.setColorAt(1, style::palette::violet);
        guides_->bar->setPen(QPen(QBrush(g), 4.0, Qt::SolidLine, Qt::RoundCap));
        guides_->dot_top->setPos(x, top_y);
        guides_->dot_bottom->setPos(x, bottom_y);
        for (QGraphicsItem* item : { static_cast<QGraphicsItem*>(guides_->halo), static_cast<QGraphicsItem*>(guides_->bar),
                                     static_cast<QGraphicsItem*>(guides_->dot_top), static_cast<QGraphicsItem*>(guides_->dot_bottom) })
            item->setVisible(true);
    }

    void DiagramScene::placeChip(const QString& text, const QPointF& anchor) {
        if (!guides_) return;
        auto* chip = static_cast<ChipItem*>(guides_->chip);
        if (text.isEmpty()) { chip->setVisible(false); return; }
        chip->setText(text);
        chip->setPos(anchor);
        // Same size on screen at any zoom of the editing view.
        for (QGraphicsView* v : views())
            if (v->isInteractive() && v->isVisible() && v->transform().m11() > 0.0) {
                chip->setScale(1.0 / v->transform().m11());
                break;
            }
        chip->setVisible(true);
    }

    double DiagramScene::rowSceneY(int layer) const {
        const auto& ly = currentGraph().getLayerLayout();
        if (ly.empty()) return 0.0;
        int last = 0;
        for (const auto& [l, y] : ly) last = std::max(last, l);
        const double y0 = -ly.at(0);
        const double gap = ly.count(1) ? -ly.at(1) - y0 : 170.0;
        if (layer < 0) return y0 + layer * gap;
        if (layer > last) return -ly.at(last) + (layer - last) * gap;
        auto it = ly.find(layer);
        return it != ly.end() ? -it->second : y0 + layer * gap;
    }

    double DiagramScene::newLayerEdgeY(bool above) const {
        const auto& layers = currentGraph().getLayers();
        if (layers.empty()) return 0.0;
        const auto& [layer, data] = above ? *layers.begin() : *layers.rbegin();
        double tallest = 0.0;
        for (const auto& n : data.nodes)
            if (!n->isDummy()) tallest = std::max(tallest, n->getHeight());
        const double reach = (tallest + currentGraph().getLayoutSettings().layer_gap) / 2.0;
        return above ? rowSceneY(layer) - reach : rowSceneY(layer) + reach;
    }

    QPointF DiagramScene::cursorScenePos() const {
        for (QGraphicsView* v : views())
            if (v->isInteractive() && v->isVisible())
                return v->mapToScene(v->viewport()->mapFromGlobal(QCursor::pos()));
        return QPointF();
    }

    // ============================================================================
    // Dragging one box
    // ============================================================================

    void DiagramScene::beginBoxDrag(NodeItem* item) {
        cancelInteraction();
        stopAnimations();
        BoxDrag drag;
        drag.item = item;

        // Its chain, if it has one: the members may move along (see BoxDrag).
        std::unordered_set<Node*> moving{ item->node() };
        if (const EquivalenceChain* chain = currentGraph().getEquivalenceChain(item->node())) {
            for (const auto& m : chain->members) {
                drag.chain.push_back(m.get());
                moving.insert(m.get());
                if (m.get() != item->node()) drag.shown[m.get()] = QPointF();
            }
            for (EquivalenceItem* line : equivalence_items_)
                if (moving.count(line->left())) drag.chain_lines.push_back(line);
            if (const auto span = currentGraph().chainReorderSpan(item->node())) drag.reorder_span = *span;
        }
        for (const auto& [edge, edge_item] : edge_items_) {
            bool touches = false;
            for (const auto& s : edge->getSources()) touches = touches || moving.count(s.get());
            for (const auto& t : edge->getTargets()) touches = touches || moving.count(t.get());
            if (touches) drag.edges.push_back(edge_item);
        }
        box_drag_ = drag;
        // The other members ride just under the lifted box, their lines under them.
        for (Node* m : box_drag_->chain)
            if (m != item->node()) node_items_.at(m)->setZValue(19);
        for (EquivalenceItem* line : box_drag_->chain_lines) line->setZValue(18);
        createGuides();

        // Lift: the box rises above everything, grows a little and casts a
        // shadow; its connections step back, since they will be redrawn.
        item->setZValue(20);
        item->setTransformOriginPoint(item->rect().center());
        item->setCursor(Qt::ClosedHandCursor);
        auto* shadow = new QGraphicsDropShadowEffect;
        shadow->setColor(QColor(31, 35, 48, 95));
        shadow->setBlurRadius(0);
        shadow->setOffset(0, 0);
        item->setGraphicsEffect(shadow);

        auto edges = drag.edges;
        lift_ = new QVariantAnimation(this);
        lift_->setDuration(LIFT_MS);
        lift_->setStartValue(0.0);
        lift_->setEndValue(1.0);
        lift_->setEasingCurve(QEasingCurve::OutCubic);
        connect(lift_, &QVariantAnimation::valueChanged, this, [item, shadow, edges](const QVariant& v) {
            const double t = v.toDouble();
            item->setScale(1.0 + 0.06 * t);
            shadow->setBlurRadius(30.0 * t);
            shadow->setOffset(0, 12.0 * t);
            for (QGraphicsItem* e : edges) e->setOpacity(1.0 - 0.75 * t);
        });
        lift_->start(QAbstractAnimation::DeleteWhenStopped);
    }

    void DiagramScene::updateBoxDrag(NodeItem* item) {
        const GraphicalHypergraph& graph = currentGraph();
        const QPointF c = item->mapToScene(item->rect().center());
        const int layer = graph.layerForY(-c.y());
        const bool new_layer = graph.getLayers().find(layer) == graph.getLayers().end();
        const QRectF bounds = sceneRect();
        updateChainPreview(c, layer);
        const bool whole_chain = box_drag_ && box_drag_->mode == ChainMode::Whole;
        const bool in_chain = box_drag_ && box_drag_->mode == ChainMode::Reorder;

        if (new_layer) {
            // A new layer opens right where the box crossed the edge of the
            // outermost one: a slim dashed band on that line, and the label over
            // the box (alone in its new layer, the box itself shows its spot).
            // The row itself will sit further out, which can be off screen: too
            // far to notice the change.
            const double y = newLayerEdgeY(layer < 0);
            placeBand(QRectF(bounds.left(), y - 14.0, bounds.width(), 28.0), true);
            if (guides_)
                for (QGraphicsItem* g : { static_cast<QGraphicsItem*>(guides_->halo), static_cast<QGraphicsItem*>(guides_->bar),
                                          static_cast<QGraphicsItem*>(guides_->dot_top), static_cast<QGraphicsItem*>(guides_->dot_bottom) })
                    g->setVisible(false);
            placeChip(QString(layer < 0 ? QStringLiteral("Nuevo nivel arriba") : QStringLiteral("Nuevo nivel abajo"))
                    + (whole_chain ? QStringLiteral(" · con su cadena") : QString()),
                QPointF(c.x(), item->mapRectToScene(item->rect()).top() - 10.0));
            return;
        }
        const double y = rowSceneY(layer);

        // The target layer as a soft band across the diagram...
        placeBand(QRectF(bounds.left(), y - 40.0, bounds.width(), 80.0), false);

        // ...and, inside it, where the box would land: next to the nearer end of a chain
        // it would fall inside (not its own)...
        if (!whole_chain && !in_chain) {
            if (const auto end = chainEndLanding(item->node(), layer, c.x())) {
                placeLandingMarker(end->first, y - 34.0, y + 34.0);
                placeChip(end->second ? QStringLiteral("A la izquierda de la cadena")
                                      : QStringLiteral("A la derecha de la cadena"),
                    QPointF(end->first, y - 40.0));
                return;
            }
        }
        // ...or among its neighbours, boxes and lines alike (all of its chain moving with
        // it, when it does).
        std::unordered_set<Node*> moving{ item->node() };
        if (whole_chain) moving.insert(box_drag_->chain.begin(), box_drag_->chain.end());
        double left = -std::numeric_limits<double>::infinity();
        double right = std::numeric_limits<double>::infinity();
        for (const auto& n : graph.getLayers().at(layer).nodes) {
            if (moving.count(n.get())) continue;
            const double x = graph.getNodeLayout().at(n.get()).x;
            const double half = n->getWidth() / 2.0;
            if (x < c.x()) left = std::max(left, x + half);
            else           right = std::min(right, x - half);
        }
        double x = c.x();
        if (std::isfinite(left) && std::isfinite(right)) x = (left + right) / 2.0;
        else if (std::isfinite(left))                    x = left + 36.0;
        else if (std::isfinite(right))                   x = right - 36.0;
        placeLandingMarker(x, y - 34.0, y + 34.0);
        placeChip(whole_chain ? QStringLiteral("Toda la cadena se mueve") : QString(), QPointF(x, y - 40.0));
    }

    void DiagramScene::updateChainPreview(const QPointF& c, int target_layer) {
        if (!box_drag_ || box_drag_->chain.empty()) return;
        BoxDrag& drag = *box_drag_;
        Node* dragged = drag.item->node();

        // Reorder when dropped in its own layer before reaching a box of no part of
        // the chain; past one, the whole chain goes along (as relocateNodeInLayer).
        const bool reorder = target_layer == dragged->getLayer()
            && drag.reorder_span.first < c.x() && c.x() < drag.reorder_span.second;
        const ChainMode mode = reorder ? ChainMode::Reorder : ChainMode::Whole;

        // Reordering, the other members stay where they are; otherwise every one
        // follows the box, by as much as it has been moved.
        for (Node* m : drag.chain)
            if (m != dragged) drag.target[m] = reorder ? QPointF() : drag.item->pos();

        if (mode != drag.mode) {
            // From wherever they are shown, they glide into the new mode.
            drag.mode = mode;
            drag.from = drag.shown;
            drag.blend = 0.0;
            if (follow_) follow_->stop();
            follow_ = new QVariantAnimation(this);
            follow_->setDuration(FOLLOW_MS);
            follow_->setStartValue(0.0);
            follow_->setEndValue(1.0);
            follow_->setEasingCurve(QEasingCurve::OutCubic);
            connect(follow_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
                if (!box_drag_) return;
                box_drag_->blend = v.toDouble();
                showChainPreview();
            });
            follow_->start(QAbstractAnimation::DeleteWhenStopped);
        }
        showChainPreview();
    }

    void DiagramScene::showChainPreview() {
        if (!box_drag_) return;
        BoxDrag& drag = *box_drag_;
        for (auto& [m, offset] : drag.shown) {
            const QPointF from = drag.from.count(m) ? drag.from.at(m) : QPointF();
            const QPointF to = drag.target.count(m) ? drag.target.at(m) : QPointF();
            offset = from + (to - from) * drag.blend;
            node_items_.at(m)->setPos(offset);
        }
        // Following, the lines go with the members; reordering, they would join the
        // wrong boxes, so they step back until the drop redraws them.
        const bool whole = drag.mode == ChainMode::Whole;
        for (EquivalenceItem* line : drag.chain_lines) {
            Node* end = line->left() == drag.item->node() ? line->right() : line->left();
            line->setPos(whole ? drag.shown.at(end) : QPointF());
            line->setOpacity(whole ? 1.0 : 1.0 - 0.85 * drag.blend);
        }
    }

    std::optional<std::pair<double, bool>> DiagramScene::chainEndLanding(Node* box, int layer, double x) const {
        const GraphicalHypergraph& graph = currentGraph();
        const auto& layout = graph.getNodeLayout();
        for (const auto& chain : graph.getEquivalences()) {
            if (chain.members.front()->getLayer() != layer) continue;
            if (std::any_of(chain.members.begin(), chain.members.end(), [&](const NodePtr& m) { return m.get() == box; }))
                continue;
            const double lo = layout.at(chain.members.front().get()).x;
            const double hi = layout.at(chain.members.back().get()).x;
            if (!(lo < x && x < hi)) continue;

            // The nearer end (the left one on a tie, as keepEquivalencesTogether), and
            // the gap there between the chain and whatever is beyond it.
            const bool to_left = x <= (lo + hi) / 2.0;
            Node* end = to_left ? chain.members.front().get() : chain.members.back().get();
            const double edge = layout.at(end).x + (to_left ? -1.0 : 1.0) * end->getWidth() / 2.0;
            double beyond = to_left ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
            for (const auto& n : graph.getLayers().at(layer).nodes) {
                if (n.get() == box) continue;
                const double nx = layout.at(n.get()).x;
                const double half = n->getWidth() / 2.0;
                if (to_left && nx < lo) beyond = std::max(beyond, nx + half);
                if (!to_left && nx > hi) beyond = std::min(beyond, nx - half);
            }
            const double marker = std::isfinite(beyond) ? (edge + beyond) / 2.0 : edge + (to_left ? -36.0 : 36.0);
            return std::make_pair(marker, to_left);
        }
        return std::nullopt;
    }

    void DiagramScene::endBoxDrag() {
        // Called while the release is being delivered to the box: the guides
        // are only hidden here; the rebuild that follows the drop removes them.
        // The chain stays where the preview showed it: the drop glides it from there.
        if (lift_) lift_->stop();
        if (follow_) follow_->stop();
        if (guides_) {
            for (QGraphicsItem* item : { static_cast<QGraphicsItem*>(guides_->band), static_cast<QGraphicsItem*>(guides_->halo),
                                         static_cast<QGraphicsItem*>(guides_->bar), static_cast<QGraphicsItem*>(guides_->dot_top),
                                         static_cast<QGraphicsItem*>(guides_->dot_bottom), guides_->chip })
                item->setVisible(false);
        }
        box_drag_.reset();
    }

    // ============================================================================
    // Moving and removing a piece of the diagram
    // ============================================================================

    DiagramScene::PieceChoice DiagramScene::pieceChoiceFor(Node* box) const {
        PieceChoice choice;
        if (!box) return choice;
        const GraphicalHypergraph& graph = currentGraph();
        const size_t everything = graph.getAllNodes().size();

        std::unordered_set<Node*> block;
        try { block = graph.getComponentNodes(box); }
        catch (const std::exception&) { return choice; }

        // In the joint, the box's whole diagram too.
        std::unordered_set<Node*> diagram;
        if (is_joint_) {
            const JointGraphicalHypergraph& joint = joint_editor_->getGraph();
            const auto ids = joint.graphsOf(box);
            if (ids.size() == 1 && !ids.begin()->empty() && joint.isSeparable(*ids.begin())) {
                choice.diagram_id = *ids.begin();
                choice.diagram_name = QString::fromStdString(joint.getIncorporatedName(choice.diagram_id));
                diagram = joint.getHypergraphNodes(choice.diagram_id);
                // Moving everything there is would change nothing.
                choice.move_diagram = diagram.size() < everything;
            }
        }
        // The block is offered when it is something else than the whole diagram.
        choice.move_block = block.size() < everything && block != diagram;
        return choice;
    }

    void DiagramScene::addMoveEntries(QMenu* menu, const PieceChoice& choice, Node* box) {
        if (!choice.move_diagram && !choice.move_block) return;
        style::addMenuSection(menu, QStringLiteral("Mover"));
        if (choice.move_diagram) {
            const QString name = QFontMetrics(menu->font()).elidedText(choice.diagram_name, Qt::ElideRight, 200);
            QAction* a = menu->addAction(style::icon(style::Icon::Move),
                name.isEmpty() ? QStringLiteral("Mover este esquema") : QStringLiteral("Mover esquema «%1»").arg(name),
                [this, id = choice.diagram_id] { startPieceMove(id, nullptr); });
            a->setToolTip(QStringLiteral("Llévalo entero a otro sitio o a otro nivel: se mueve con el ratón "
                                         "y se deja con un clic"));
        }
        if (choice.move_block) {
            QAction* a = menu->addAction(style::icon(style::Icon::Move), QStringLiteral("Mover bloque"),
                [this, box] { startPieceMove(std::string(), box); });
            a->setToolTip(QStringLiteral("Mueve esta caja junto con todas las que están unidas a ella"));
        }
    }

    void DiagramScene::addRemoveBlockEntry(QMenu* menu, Node* box) {
        if (!box) return;
        if (!is_joint_) {
            // In a diagram of its own the block's boxes are deleted. A box on its
            // own already has "Eliminar caja".
            std::unordered_set<Node*> block;
            try { block = currentGraph().getComponentNodes(box); }
            catch (const std::exception&) { return; }
            const auto boxes = std::count_if(block.begin(), block.end(), [](Node* n) { return !n->isDummy(); });
            if (boxes < 2) return;
            QAction* a = menu->addAction(style::icon(style::Icon::RemoveBox), QStringLiteral("Eliminar bloque"),
                [this, box] { onRemoveBlock(box); });
            a->setToolTip(QStringLiteral("Elimina esta caja y todas las que están unidas a ella, con sus conexiones"));
            return;
        }
        // In the joint: the box's whole diagram, while it is not mixed with others
        // (equivalences aside, which just go with it), and its block, when that is
        // something else: the diagram has more blocks, or the block reaches into
        // other diagrams (through an equivalence, or because they are mixed).
        const JointGraphicalHypergraph& joint = joint_editor_->getGraph();
        std::unordered_set<Node*> block;
        try { block = joint.getComponentNodes(box); }
        catch (const std::exception&) { return; }
        auto realBoxes = [](const std::unordered_set<Node*>& nodes) {
            std::unordered_set<Node*> out;
            for (Node* n : nodes) if (!n->isDummy()) out.insert(n);
            return out;
        };

        std::string diagram_id;
        std::unordered_set<Node*> diagram;
        const auto ids = joint.graphsOf(box);
        if (ids.size() == 1 && !ids.begin()->empty() && joint.isRemovable(*ids.begin())) {
            diagram_id = *ids.begin();
            try { diagram = realBoxes(joint.getRemovableNodes(diagram_id)); }
            catch (const std::exception&) { diagram_id.clear(); }
        }

        if (!diagram_id.empty()) {
            const QString name = QFontMetrics(menu->font()).elidedText(
                QString::fromStdString(joint.getIncorporatedName(diagram_id)), Qt::ElideRight, 200);
            QAction* a = menu->addAction(style::icon(style::Icon::TakeOut),
                name.isEmpty() ? QStringLiteral("Quitar este esquema") : QStringLiteral("Quitar esquema «%1»").arg(name),
                [this, diagram_id] { onRemoveDiagram(diagram_id); });
            a->setToolTip(QStringLiteral("Quita del esquema conjunto todas las cajas de este esquema; "
                                         "el esquema original no cambia"));
        }
        if (diagram_id.empty() || realBoxes(block) != diagram) {
            QAction* a = menu->addAction(style::icon(style::Icon::TakeOut), QStringLiteral("Quitar bloque"),
                [this, box] { onRemoveBlock(box); });
            a->setToolTip(QStringLiteral("Quita del esquema conjunto esta caja y todas las que están unidas a ella "
                                         "(también por equivalencias); los esquemas originales no cambian"));
        }
    }

    void DiagramScene::onRemoveDiagram(const std::string& diagram_id) {
        const JointGraphicalHypergraph& joint = joint_editor_->getGraph();
        const QString name = QString::fromStdString(joint.getIncorporatedName(diagram_id));
        std::unordered_set<Node*> doomed;
        try { doomed = joint.getRemovableNodes(diagram_id); }
        catch (const std::exception& e) { showError(e); return; }

        // Equivalences with other diagrams go with it: the joint alone had them.
        const bool equivalent_to_others = !joint.isSeparable(diagram_id);
        if (!dialogs::confirm(dialogParent(),
                name.isEmpty() ? QStringLiteral("¿Quitar este esquema del esquema conjunto?")
                               : QStringLiteral("¿Quitar «%1» del esquema conjunto?").arg(name),
                QStringLiteral("Se quitarán del esquema conjunto todas sus cajas y conexiones%1. "
                               "El esquema original no cambia y podrás volver a añadirlo.")
                    .arg(equivalent_to_others
                        ? QStringLiteral(", y sus equivalencias con cajas de otros esquemas")
                        : QString()),
                QStringLiteral("Quitar"), true))
            return;

        takeOut(doomed, [this, diagram_id] { joint_editor_->removeHypergraph(diagram_id); });
    }

    void DiagramScene::onRemoveBlock(Node* box) {
        std::unordered_set<Node*> doomed;
        try { doomed = currentGraph().getComponentNodes(box); }
        catch (const std::exception& e) { showError(e); return; }

        if (!is_joint_) {
            const auto boxes = std::count_if(doomed.begin(), doomed.end(), [](Node* n) { return !n->isDummy(); });
            if (!dialogs::confirm(dialogParent(), QStringLiteral("¿Eliminar este bloque?"),
                    QStringLiteral("Se eliminarán sus %1 cajas y sus conexiones. "
                                   "Puedes deshacerlo con Ctrl+Z.").arg(boxes),
                    QStringLiteral("Eliminar"), true))
                return;
            takeOut(doomed, [this, ptr = box->shared_from_this()] { regular_editor_->removeComponent(ptr); });
            return;
        }

        const JointGraphicalHypergraph& joint = joint_editor_->getGraph();

        // Say what goes: how many boxes, and which diagrams they came from.
        int boxes = 0;
        std::set<std::string> ids;
        for (Node* n : doomed) {
            if (n->isDummy()) continue;
            ++boxes;
            for (const auto& id : joint.graphsOf(n))
                if (!id.empty()) ids.insert(id);
        }
        QStringList names;
        for (const auto& id : ids) {
            const QString name = QString::fromStdString(joint.getIncorporatedName(id));
            if (!name.isEmpty()) names << QStringLiteral("«%1»").arg(name);
        }
        QString detail = boxes == 1
            ? QStringLiteral("Se quitará esta caja del esquema conjunto.")
            : QStringLiteral("Se quitarán sus %1 cajas y sus conexiones del esquema conjunto.").arg(boxes);
        if (!names.isEmpty())
            detail += (names.size() == 1 ? QStringLiteral(" Son de %1.") : QStringLiteral(" Vienen de %1."))
                .arg(names.join(QStringLiteral(", ")));
        detail += QStringLiteral(" Los esquemas originales no cambian.");

        if (!dialogs::confirm(dialogParent(), QStringLiteral("¿Quitar este bloque del conjunto?"), detail,
                QStringLiteral("Quitar"), true))
            return;

        takeOut(doomed, [this, ptr = box->shared_from_this()] { joint_editor_->removeComponent(ptr); });
    }

    // ============================================================================
    // Copying and pasting
    // ============================================================================

    void DiagramScene::addCopyEntries(QMenu* menu, Node* box) {
        if (!box) return;
        const GraphicalHypergraph& graph = currentGraph();
        std::unordered_set<Node*> block;
        try { block = graph.getComponentNodes(box); }
        catch (const std::exception&) { return; }

        // The whole diagram: in the joint, the box's own while it is not mixed.
        std::string diagram_id;
        QString diagram_name;
        std::unordered_set<Node*> diagram;
        bool copy_diagram = !is_joint_;
        if (is_joint_) {
            const JointGraphicalHypergraph& joint = joint_editor_->getGraph();
            const auto ids = joint.graphsOf(box);
            if (ids.size() == 1 && !ids.begin()->empty() && joint.isSeparable(*ids.begin())) {
                diagram_id = *ids.begin();
                diagram_name = QString::fromStdString(joint.getIncorporatedName(diagram_id));
                diagram = joint.getHypergraphNodes(diagram_id);
                copy_diagram = true;
            }
        }
        // The block is offered when it is something else than that diagram.
        const bool copy_block = is_joint_ ? block != diagram : block.size() < graph.getAllNodes().size();
        if (!copy_diagram && !copy_block) return;

        style::addMenuSection(menu, QStringLiteral("Copiar"));
        if (copy_block) {
            QAction* a = menu->addAction(style::icon(style::Icon::Copy), QStringLiteral("Copiar bloque"),
                [this, box] { copyBlock(box); });
            a->setToolTip(QStringLiteral("Copia esta caja y todas las que están unidas a ella, "
                                         "para pegarlas en cualquier esquema"));
        }
        if (copy_diagram) {
            QString text = QStringLiteral("Copiar esquema");
            if (is_joint_) {
                const QString name = QFontMetrics(menu->font()).elidedText(diagram_name, Qt::ElideRight, 200);
                text = name.isEmpty() ? QStringLiteral("Copiar este esquema") : QStringLiteral("Copiar esquema «%1»").arg(name);
            }
            QAction* a = menu->addAction(style::icon(style::Icon::Copy), text,
                [this, diagram_id] { copyDiagram(diagram_id); });
            a->setToolTip(QStringLiteral("Copia el esquema entero, con todos sus bloques, "
                                         "para pegarlo en cualquier esquema"));
        }
    }

    void DiagramScene::copyBlock(Node* box) {
        std::unordered_set<Node*> nodes;
        try { nodes = currentGraph().getComponentNodes(box); }
        catch (const std::exception& e) { showError(e); return; }
        copyNodes(nodes, QStringLiteral("el bloque de «%1»").arg(QString::fromStdString(box->getName())));
    }

    void DiagramScene::copyDiagram(const std::string& diagram_id) {
        std::unordered_set<Node*> nodes;
        QString description;
        if (is_joint_ && !diagram_id.empty()) {
            const JointGraphicalHypergraph& joint = joint_editor_->getGraph();
            try { nodes = joint.getHypergraphNodes(diagram_id); }
            catch (const std::exception& e) { showError(e); return; }
            description = QStringLiteral("el esquema «%1»").arg(QString::fromStdString(joint.getIncorporatedName(diagram_id)));
        }
        else {
            for (const auto& n : currentGraph().getAllNodes()) nodes.insert(n.get());
            description = is_joint_ ? QStringLiteral("el esquema conjunto")
                : QStringLiteral("el esquema «%1»").arg(QString::fromStdString(currentGraph().getName()));
        }
        copyNodes(nodes, description);
    }

    void DiagramScene::copyNodes(const std::unordered_set<Node*>& nodes, const QString& description) {
        if (nodes.empty()) return;
        try { clipboard::copy(currentGraph().copyOf(nodes), description); }
        catch (const std::exception& e) { showError(e); return; }
        emit notice(QStringLiteral("Copiado %1. Pégalo con clic derecho › Pegar o con Ctrl+V, "
                                   "también en otra ventana.").arg(description));
    }

    QString DiagramScene::copyAt(const QPointF& scene_pos) {
        if (piece_move_ || state_ != InteractionState::Idle) return QString();

        // The box or connection under the point (or under one of its parts).
        Node* box = nullptr;
        for (QGraphicsItem* it : items(scene_pos)) {
            for (QGraphicsItem* p = it; p && !box; p = p->parentItem()) {
                if (auto* n = qgraphicsitem_cast<NodeItem*>(p)) box = n->node();
                else if (auto* e = dynamic_cast<HyperedgeItem*>(p); e && !e->edge()->getSources().empty())
                    box = e->edge()->getSources().front().get();
            }
            if (box) break;
        }
        if (box) copyBlock(box);
        else if (!currentGraph().getAllNodes().empty()) copyDiagram(std::string());
        else return QString();
        return clipboard::description();
    }

    bool DiagramScene::canPaste() const {
        return !is_joint_ && clipboard::hasPiece();
    }

    bool DiagramScene::pasteAt(const QPointF& scene_pos) {
        if (is_joint_ || piece_move_) return false;
        cancelInteraction();
        auto piece = clipboard::piece();
        if (!piece) return false;

        const auto from = nodeCenters();
        std::vector<NodePtr> boxes;
        try {
            // Its first row in the layer at the point (-1 or past the deepest one
            // opens a new layer), at its x among the blocks.
            boxes = regular_editor_->paste(std::move(*piece), scene_pos.x(),
                currentGraph().layerForY(-scene_pos.y()));
        }
        catch (const std::exception& e) {
            showError(e);
            return false;
        }
        rebuildAnimated(from); // the new boxes fade in
        emit graphChanged();
        emit notice(boxes.size() == 1 ? QStringLiteral("Se ha pegado 1 caja")
                                      : QStringLiteral("Se han pegado %1 cajas").arg(boxes.size()));
        return true;
    }

    void DiagramScene::takeOut(const std::unordered_set<Node*>& doomed, std::function<void()> apply) {
        // Its boxes and connections shrink away; then the rest closes the gap.
        auto items = std::make_shared<std::vector<QGraphicsItem*>>();
        for (const auto& [node, item] : node_items_)
            if (doomed.count(node)) {
                item->setTransformOriginPoint(item->rect().center());
                items->push_back(item);
            }
        for (const auto& [edge, item] : edge_items_)
            if (!edge->getSources().empty() && doomed.count(edge->getSources().front().get()))
                items->push_back(item);
        for (EquivalenceItem* item : equivalence_items_)
            if (doomed.count(item->left()) || doomed.count(item->right())) items->push_back(item);

        stopAnimations();
        fade_ = new QVariantAnimation(this);
        fade_->setDuration(TAKE_OUT_MS);
        fade_->setStartValue(0.0);
        fade_->setEndValue(1.0);
        fade_->setEasingCurve(QEasingCurve::InCubic);
        connect(fade_, &QVariantAnimation::valueChanged, this, [items](const QVariant& v) {
            const double t = v.toDouble();
            for (QGraphicsItem* item : *items) {
                item->setOpacity(1.0 - t);
                item->setScale(1.0 - 0.12 * t);
            }
        });
        connect(fade_, &QVariantAnimation::finished, this, [this, apply = std::move(apply)] {
            const auto from = nodeCenters();
            try { apply(); }
            catch (const std::exception& e) {
                showError(e);
                rebuildAnimated(from);
                return;
            }
            rebuildAnimated(from);
            emit graphChanged();
        });
        fade_->start(QAbstractAnimation::DeleteWhenStopped);
    }

    void DiagramScene::startPieceMove(const std::string& diagram_id, Node* anchor) {
        cancelInteraction();
        stopAnimations();
        const GraphicalHypergraph& graph = currentGraph();

        PieceMove move;
        move.diagram_id = diagram_id;
        move.anchor = anchor;
        try {
            move.nodes = diagram_id.empty() ? graph.getComponentNodes(anchor)
                                            : joint_editor_->getGraph().getHypergraphNodes(diagram_id);
        }
        catch (const std::exception& e) { showError(e); return; }
        if (move.nodes.empty()) return;
        move.regions = graph.getOccupiedRegionsExcluding(move.nodes);

        double left = std::numeric_limits<double>::infinity(), right = -left;
        move.top_layer = std::numeric_limits<int>::max();
        move.bottom_layer = std::numeric_limits<int>::min();
        for (Node* n : move.nodes) {
            move.top_layer = std::min(move.top_layer, n->getLayer());
            move.bottom_layer = std::max(move.bottom_layer, n->getLayer());
            const double x = graph.getNodeLayout().at(n).x;
            left = std::min(left, x - n->getWidth() / 2.0);
            right = std::max(right, x + n->getWidth() / 2.0);
        }
        move.center_x = (left + right) / 2.0;
        move.top_row_y = rowSceneY(move.top_layer);

        // Its boxes and connections become one piece, lifted above the rest.
        std::vector<QGraphicsItem*> others;
        move.piece = new QGraphicsItemGroup;
        addItem(move.piece);
        move.piece->setZValue(30);
        for (const auto& [node, item] : node_items_) {
            if (move.nodes.count(node)) move.piece->addToGroup(item);
            else others.push_back(item);
        }
        for (const auto& [edge, item] : edge_items_) {
            if (!edge->getSources().empty() && move.nodes.count(edge->getSources().front().get()))
                move.piece->addToGroup(item);
            else others.push_back(item);
        }
        for (EquivalenceItem* item : equivalence_items_) {
            if (move.nodes.count(item->left())) move.piece->addToGroup(item);
            else others.push_back(item);
        }
        // A grip at its top middle: that is where the mouse holds the piece, so
        // it hangs from the cursor and lands where the cursor lets it go.
        QRectF bounds;
        for (const auto& [node, item] : node_items_)
            if (move.nodes.count(node)) bounds |= item->mapRectToScene(item->rect());
        auto* grip = new QGraphicsPathItem;
        QPainterPath pill;
        pill.addRoundedRect(QRectF(-22.0, -7.0, 44.0, 14.0), 7.0, 7.0);
        grip->setPath(pill);
        QLinearGradient grip_fill(-22.0, 0.0, 22.0, 0.0);
        grip_fill.setColorAt(0, style::palette::accent);
        grip_fill.setColorAt(1, style::palette::violet);
        grip->setBrush(grip_fill);
        grip->setPen(QPen(Qt::white, 2.0));
        for (int i = -1; i <= 1; ++i) {
            auto* dot = new QGraphicsEllipseItem(i * 8.0 - 1.8, -1.8, 3.6, 3.6, grip);
            dot->setPen(Qt::NoPen);
            dot->setBrush(Qt::white);
        }
        move.bounds = bounds.isNull() ? QRectF(move.center_x, move.top_row_y, 0.0, 0.0) : bounds;
        const QPointF grip_at(move.center_x, move.bounds.top() - 16.0);
        grip->setPos(grip_at);
        move.piece->addToGroup(grip);

        auto* shadow = new QGraphicsDropShadowEffect;
        shadow->setColor(QColor(31, 35, 48, 90));
        shadow->setBlurRadius(0);
        shadow->setOffset(0, 0);
        move.piece->setGraphicsEffect(shadow);

        move.grab = grip_at;
        move.offset = cursorScenePos() - grip_at;
        move.snap = 0.0;
        piece_move_ = move;

        // Guides: one outline per other piece's region, the insertion bar and its label.
        createGuides();
        for (size_t i = 0; i < piece_move_->regions.size(); ++i) {
            auto* region = new QGraphicsPathItem;
            region->setZValue(-1.0);
            region->setAcceptedMouseButtons(Qt::NoButton);
            addItem(region);
            guides_->regions.push_back(region);
        }

        // Pick-up: the piece hops so that its grip sits under the cursor, casting
        // a growing shadow while the rest steps back.
        lift_ = new QVariantAnimation(this);
        lift_->setDuration(PICK_UP_MS);
        lift_->setStartValue(0.0);
        lift_->setEndValue(1.0);
        lift_->setEasingCurve(QEasingCurve::OutBack);
        connect(lift_, &QVariantAnimation::valueChanged, this, [this, shadow, others](const QVariant& v) {
            const double t = v.toDouble();
            const double soft = std::clamp(t, 0.0, 1.0);
            shadow->setBlurRadius(34.0 * soft);
            shadow->setOffset(0, 14.0 * soft);
            for (QGraphicsItem* item : others) item->setOpacity(1.0 - 0.6 * soft);
            if (piece_move_) {
                piece_move_->snap = t;
                piece_move_->piece->setPos(piece_move_->offset * t);
                updatePiecePreview();
            }
        });
        connect(lift_, &QVariantAnimation::finished, this, [this] {
            if (!piece_move_) return;
            piece_move_->snap = 1.0;
            piece_move_->piece->setPos(piece_move_->offset);
            updatePiecePreview();
        });
        lift_->start(QAbstractAnimation::DeleteWhenStopped);

        for (QGraphicsView* v : views()) {
            if (!v->isInteractive()) continue;
            v->viewport()->setMouseTracking(true);
            v->viewport()->setCursor(Qt::ClosedHandCursor);
        }
        const QString what = diagram_id.empty()
            ? QStringLiteral("el bloque")
            : QStringLiteral("«%1»").arg(QString::fromStdString(joint_editor_->getGraph().getIncorporatedName(diagram_id)));
        emit interactionHintChanged(QStringLiteral("Llevas %1 colgando del ratón: haz clic donde quieras dejarlo "
                                                   "· Esc para cancelar").arg(what));
        updatePiecePreview();
    }

    void DiagramScene::updatePiecePreview() {
        if (!piece_move_ || !guides_) return;
        const PieceMove& move = *piece_move_;

        const QRectF bounds = sceneRect();
        const QPointF shown = move.offset * move.snap; // where it is drawn right now
        const double piece_x = move.center_x + shown.x();
        const double at_x = JointGraphicalHypergraph::placementPoint(move.regions, piece_x);

        // The other pieces' regions, the one under the piece a little stronger.
        for (size_t i = 0; i < move.regions.size() && i < guides_->regions.size(); ++i) {
            const auto [lo, hi] = move.regions[i];
            const bool under = lo <= piece_x && piece_x <= hi;
            QGraphicsPathItem* region = guides_->regions[i];
            QPainterPath path;
            path.addRoundedRect(QRectF(lo - 14.0, bounds.top() + 6.0, hi - lo + 28.0, bounds.height() - 12.0), 20.0, 20.0);
            region->setPath(path);
            QColor fill = style::palette::accent;
            fill.setAlpha(under ? 26 : 12);
            region->setBrush(fill);
            QColor line = style::palette::accent;
            line.setAlpha(under ? 120 : 50);
            QPen pen(line, 1.4, Qt::DashLine);
            pen.setCosmetic(true);
            region->setPen(pen);
        }

        // Where it lands: a ghost of the piece in the rows and the gap it will
        // take (the piece hangs from the cursor, so the ghost is right under it
        // in open space, and beside the region it would otherwise overlap), and
        // a label saying which layer it starts at.
        const GraphicalHypergraph& graph = currentGraph();
        const int target = graph.layerForY(-(move.top_row_y + shown.y()));
        int last = 0;
        for (const auto& [l, data] : graph.getLayers()) last = std::max(last, l);
        const double half = move.bounds.width() / 2.0;
        double ghost_x = at_x;
        for (const auto& [lo, hi] : move.regions) {
            if (at_x == hi) ghost_x = hi + currentGraph().getLayoutSettings().block_sep + half;
            else if (at_x == lo) ghost_x = lo - currentGraph().getLayoutSettings().block_sep - half;
        }
        // A new layer above or below everything is shown where it opens: a slim
        // dashed band on the edge of the outermost layer, with the ghost's first
        // row just past it (the real row ends up further out, often off screen).
        const bool new_above = target < 0, new_below = target > last;
        double top_row = rowSceneY(target);
        if (new_above || new_below) {
            const double edge = newLayerEdgeY(new_above);
            const double half_top = move.top_row_y - move.bounds.top(); // half its first row
            top_row = new_above ? edge - half_top - 20.0 : edge + half_top + 20.0;
            placeBand(QRectF(bounds.left(), edge - 14.0, bounds.width(), 28.0), true);
        }
        else if (guides_->band->isVisible()) {
            guides_->band->setVisible(false);
        }
        const QRectF ghost = move.bounds
            .translated(ghost_x - move.bounds.center().x(), top_row - move.top_row_y)
            .adjusted(-12.0, -12.0, 12.0, 12.0);
        QPainterPath ghost_path;
        ghost_path.addRoundedRect(ghost, 16.0, 16.0);
        guides_->ghost->setPath(ghost_path);
        QColor ghost_fill = style::palette::accent;
        ghost_fill.setAlpha(22);
        guides_->ghost->setBrush(ghost_fill);
        QPen ghost_pen(style::palette::accent, 1.8, Qt::DashLine);
        ghost_pen.setCosmetic(true);
        guides_->ghost->setPen(ghost_pen);
        guides_->ghost->setVisible(true);

        placeChip(new_above ? QStringLiteral("Nuevo nivel arriba")
              : new_below ? QStringLiteral("Nuevo nivel abajo")
              : QStringLiteral("Desde el nivel %1").arg(target + 1),
            move.grab + shown - QPointF(0.0, 12.0)); // just above the grip, i.e. the cursor
    }

    void DiagramScene::dropPiece() {
        if (!piece_move_) return;
        const PieceMove move = *piece_move_;
        if (std::hypot(move.offset.x(), move.offset.y()) < 4.0) { // a click without moving
            cancelPieceMove();
            return;
        }

        const auto from = nodeCenters(); // the piece where it was dropped
        const double click_x = move.center_x + move.offset.x();
        const int top_layer = currentGraph().layerForY(-(move.top_row_y + move.offset.y()));
        endPieceMove();
        try {
            const NodePtr anchor = move.anchor ? move.anchor->shared_from_this() : nullptr;
            if (!move.diagram_id.empty()) joint_editor_->moveHypergraph(move.diagram_id, click_x, top_layer);
            else if (is_joint_)           joint_editor_->moveComponent(anchor, click_x, top_layer);
            else                          regular_editor_->moveComponent(anchor, click_x, top_layer);
        }
        catch (const std::exception& e) {
            showError(e);
            rebuildAnimated(from); // glides back
            return;
        }
        rebuildAnimated(from);
        emit graphChanged();
    }

    void DiagramScene::cancelPieceMove() {
        if (!piece_move_) return;
        const auto from = nodeCenters();
        endPieceMove();
        rebuildAnimated(from); // everything glides back to where it was
    }

    void DiagramScene::endPieceMove() {
        // No item holds the mouse during a piece move (the scene handles it),
        // so the guides can go right away.
        if (lift_) lift_->stop();
        clearGuides();
        piece_move_.reset();
        for (QGraphicsView* v : views())
            if (v->isInteractive()) v->viewport()->setCursor(Qt::ArrowCursor);
        emit interactionHintChanged(QString());
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
