// ============================================================================
// capture_pictures
//
// Retakes the pictures of the options' help and of the introductory tour that
// show a box's or a connection's menu, an equivalence line's menu, a box being
// dragged or the box dialog: it builds each example, opens the real menu or
// dialog on it (they flash on screen for a moment), or drags the box with
// mouse events, and saves what it looks like.
//
//   capture_pictures <resources folder> [shot]     (app/ui/resources)
//
// Only the pictures listed below are written; every other one is left as is.
// With `shot`, only those of the shots whose name contains it (e.g. "resize").
// Run it on the screen the pictures are meant for: they are taken at its
// device pixel ratio. The diagram is drawn `zoom` times larger than the menus,
// and a picture with a menu is at least as wide as it is tall, so the menu's
// height does not make it a narrow strip.
// ============================================================================

#include "DiagramScene.h"
#include "EquivalenceItem.h"
#include "HypergraphEditor.h"
#include "LayoutPreview.h"
#include "SettingsWidgets.h"
#include "NodeDialogs.h"
#include "NodeEditorWidgets.h"
#include "ProjectSettingsDialog.h"
#include "AppDialogs.h"
#include "HyperedgeItem.h"
#include "MainWindow.h"
#include "DiagramTabBar.h"
#include "DiagramTabWidget.h"
#include "NodeItem.h"
#include "Project.h"
#include "Tutorial.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>

using namespace app_logic;
using namespace hypergraph_logic;
using namespace ui;

namespace {

    double zoom = 1.5;                  // the diagram, against the menus (the tour's are 1:1)
    bool menu_at_top = false;           // menus start level with the diagram's top (the tour's)
    constexpr double MARGIN = 28.0;     // around the diagram, in scene units
    constexpr double PADDING = 14.0;    // around everything, in pixels

    QDir resources;

    void settle(int ms = 120) {
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    qreal pixelRatio() { return qApp->devicePixelRatio(); }

    NodePtr named(const GraphicalHypergraph& g, const std::string& name) {
        for (const auto& n : g.getAllNodes())
            if (!n->isDummy() && n->getName() == name) return n;
        throw std::runtime_error("No box named " + name);
    }

    NodeItem* itemOf(DiagramScene& scene, const std::string& name) {
        for (QGraphicsItem* item : scene.items())
            if (auto* n = dynamic_cast<NodeItem*>(item); n && n->node()->getName() == name) return n;
        throw std::runtime_error("No item for box " + name);
    }

    EquivalenceItem* lineItem(DiagramScene& scene) {
        for (QGraphicsItem* item : scene.items())
            if (auto* e = dynamic_cast<EquivalenceItem*>(item)) return e;
        throw std::runtime_error("No equivalence line");
    }

    // Where a click lands on the connection that reaches `box` from above: just
    // over the box, on the line that goes into it.
    QPointF intoBox(DiagramScene& scene, const std::string& box) {
        const QRectF r = itemOf(scene, box)->sceneBoundingRect();
        return QPointF(r.center().x(), r.top() - 12.0);
    }

    void mouse(DiagramScene& scene, QEvent::Type type, const QPointF& at, Qt::MouseButton button,
               Qt::MouseButtons buttons, const QPointF& down);

    // What a right click at scene_pos opens: a box's, a connection's or a line's
    // menu, or on the background (which the scene opens on the press) its own.
    QMenu* openMenuAt(DiagramScene& scene, const QPointF& scene_pos, bool background = false) {
        if (background) {
            mouse(scene, QEvent::GraphicsSceneMousePress, scene_pos, Qt::RightButton, Qt::RightButton, scene_pos);
        }
        else {
            QGraphicsSceneContextMenuEvent event(QEvent::GraphicsSceneContextMenu);
            event.setScenePos(scene_pos);
            event.setReason(QGraphicsSceneContextMenuEvent::Mouse);
            QApplication::sendEvent(&scene, &event);
        }
        settle();
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) throw std::runtime_error("No menu opened");
        return menu;
    }

    QAction* actionNamed(QMenu* menu, const QString& text) {
        QStringList there;
        for (QAction* a : menu->actions()) {
            if (a->text() == text) return a;
            if (!a->text().isEmpty()) there << a->text();
        }
        throw std::runtime_error("No option " + text.toStdString() + " in: " + there.join(" | ").toStdString());
    }

    // ── Composing a picture ───────────────────────────────────────────────────
    //
    // The diagram (scene units), drawn `zoom` times larger, with pixmaps (menus)
    // laid over it: each one's top left is at `anchor` (a scene point) moved by
    // `offset` pixels.
    struct Overlay {
        QPixmap pixmap;
        QPointF anchor;
        QPointF offset;
    };

    // What is on show: items hidden or faded out (a move's leftovers) do not count.
    QRectF diagramArea(DiagramScene& scene) {
        QRectF shown;
        for (QGraphicsItem* item : scene.items())
            if (item->isVisible() && item->effectiveOpacity() > 0.02)
                shown |= item->sceneBoundingRect();
        return shown.adjusted(-MARGIN, -MARGIN, MARGIN, MARGIN);
    }

    QImage compose(DiagramScene& scene, const QRectF& diagram, const std::vector<Overlay>& overlays) {
        auto toImage = [&](const QPointF& p) { return (p - diagram.topLeft()) * zoom; };
        const QRectF drawn(QPointF(0, 0), diagram.size() * zoom);
        QRectF canvas = drawn;
        std::vector<QRectF> placed;
        for (const Overlay& o : overlays) {
            placed.emplace_back(toImage(o.anchor) + o.offset, QSizeF(o.pixmap.size()) / o.pixmap.devicePixelRatio());
            canvas |= placed.back();
        }
        canvas.adjust(-PADDING, -PADDING, PADDING, PADDING);
        if (!overlays.empty() && canvas.width() < canvas.height()) {
            const double grow = (canvas.height() - canvas.width()) / 2.0;
            canvas.adjust(-grow, 0, grow, 0);
        }

        const qreal dpr = pixelRatio();
        QImage image((canvas.size() * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::white);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.translate(-canvas.topLeft());
        scene.render(&p, drawn, diagram);
        for (size_t i = 0; i < overlays.size(); ++i)
            p.drawPixmap(placed[i].topLeft(), overlays[i].pixmap);
        return image;
    }

    void save(const QImage& image, const QString& path) {
        const QString file = resources.filePath(path);
        QDir().mkpath(QFileInfo(file).path());
        if (!image.save(file)) throw std::runtime_error("Cannot write " + file.toStdString());
        std::printf("  %s\n", qPrintable(path));
    }

    QPixmap clickMark();

    // The menu opened by a right click at `click`, with `hover` (if any) under
    // the mouse and, if asked, the submenu `open_submenu` open. The menu starts
    // at the click and is lifted so the click sits about a third of the way down
    // it, as a menu that does not fit below the click is shown on screen.
    QImage menuPicture(DiagramScene& scene, const QPointF& click, const QString& hover,
                       const QString& open_submenu = {}, bool background = false)
    {
        const QRectF diagram = diagramArea(scene);
        QMenu* menu = openMenuAt(scene, click, background);
        // Away from the real mouse, which would otherwise choose what is hovered.
        menu->move(-20000, -20000);
        settle(40);
        const QPointF menu_offset(4.0, menu_at_top
            ? (diagram.top() + MARGIN / 2.0 - click.y()) * zoom
            : -menu->sizeHint().height() / 3.0);

        QPixmap sub_pm;
        QPointF sub_offset;
        if (!open_submenu.isEmpty()) {
            QAction* holder = actionNamed(menu, open_submenu);
            menu->setActiveAction(holder);
            const QRect r = menu->actionGeometry(holder);
            QMenu* sub = holder->menu();
            sub->popup(menu->mapToGlobal(QPoint(r.right(), r.top())));
            settle();
            sub_pm = sub->grab();
            sub_offset = menu_offset + QPointF(r.right() - 4, r.top() - 6);
        }
        if (!hover.isEmpty()) menu->setActiveAction(actionNamed(menu, hover));
        settle(60);
        std::vector<Overlay> overlays{ { menu->grab(), click, menu_offset } };
        if (!sub_pm.isNull()) overlays.push_back({ sub_pm, click, sub_offset });
        if (background) overlays.push_back({ clickMark(), click, QPointF(-15, -15) });
        menu->close();
        settle(40);
        return compose(scene, diagram, overlays);
    }

    QImage boxMenuPicture(DiagramScene& scene, const std::string& box, const QString& hover,
                          const QString& open_submenu = {})
    {
        scene.rebuild();
        settle();
        const QPointF click = itemOf(scene, box)->sceneBoundingRect().center() + QPointF(18, 6);
        return menuPicture(scene, click, hover, open_submenu);
    }

    // The menu of the connection that reaches `box` from above.
    QImage edgeMenuPicture(DiagramScene& scene, const std::string& box, const QString& hover) {
        scene.rebuild();
        settle();
        return menuPicture(scene, intoBox(scene, box), hover);
    }

    QImage lineMenuPicture(DiagramScene& scene, const QString& hover) {
        scene.rebuild();
        settle();
        return menuPicture(scene, lineItem(scene)->sceneBoundingRect().center(), hover);
    }

    // The scene while the option `option` of the menu opened at `click` (a box's
    // or a connection's) waits for the second box.
    QImage selectionAt(DiagramScene& scene, const QPointF& click, const QString& option) {
        QMenu* menu = openMenuAt(scene, click);
        QAction* a = actionNamed(menu, option);
        menu->close();
        a->trigger();
        settle(400); // half a breath of the candidates' animation
        QImage image = compose(scene, diagramArea(scene), {});
        scene.cancelInteraction();
        return image;
    }

    QImage selectionPicture(DiagramScene& scene, const std::string& box, const QString& option) {
        scene.rebuild();
        settle();
        return selectionAt(scene, itemOf(scene, box)->sceneBoundingRect().center(), option);
    }

    QImage scenePicture(DiagramScene& scene) {
        scene.rebuild();
        settle();
        return compose(scene, diagramArea(scene), {});
    }

    // ── Clicks on connections, the background and tabs ────────────────────────

    // The connection (not a segment) from `source` to `target`.
    HyperedgePtr edgeOf(const GraphicalHypergraph& g, const std::string& source, const std::string& target) {
        const NodePtr s = named(g, source), t = named(g, target);
        for (const auto& e : g.getAllHyperedges())
            if (!e->isSegment() && e->containsSource(s) && e->containsTarget(t)) return e;
        throw std::runtime_error("No connection " + source + " -> " + target);
    }

    // A point of the connection from `source` to `target` where a click lands on
    // it (and on nothing else), along its way down from the source.
    QPointF onEdge(DiagramScene& scene, const std::string& source, const std::string& target) {
        const Node* s = nullptr;
        const Node* t = nullptr;
        for (QGraphicsItem* item : scene.items())
            if (auto* n = dynamic_cast<NodeItem*>(item)) {
                if (n->node()->getName() == source) s = n->node();
                if (n->node()->getName() == target) t = n->node();
            }
        for (QGraphicsItem* item : scene.items()) {
            auto* line = dynamic_cast<HyperedgeItem*>(item);
            if (!line || !line->edge() || !s || !t) continue;
            bool from = false, to = false;
            for (const auto& n : line->edge()->getSources()) from = from || n.get() == s;
            for (const auto& n : line->edge()->getTargets()) to = to || n.get() == t;
            if (!from || !to) continue;
            const QPainterPath path = line->path();
            for (double f = 0.08; f < 0.95; f += 0.02) {
                const QPointF p = line->mapToScene(path.pointAtPercent(f));
                const auto hit = scene.items(p);
                if (!hit.isEmpty() && hit.front() == line) return p;
            }
        }
        throw std::runtime_error("No point on the connection " + source + " -> " + target);
    }

    // A ring around where the click was, as the pictures of a background or
    // tab menu mark it (there is no box there to show it).
    QPixmap clickMark() {
        const qreal dpr = pixelRatio();
        QPixmap pm(QSize(30, 30) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#6366F1"), 2.6));
        p.setBrush(QColor(238, 240, 255, 230));
        p.drawEllipse(QPointF(15, 15), 12, 12);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#6366F1"));
        p.drawEllipse(QPointF(15, 15), 4.5, 4.5);
        return pm;
    }

    // A picture of a widget's part (`base`, already grabbed) with pixmaps laid
    // over it at their positions in it, on white.
    QImage composeOver(const QPixmap& base, const std::vector<std::pair<QPixmap, QPointF>>& overlays) {
        QRectF canvas(QPointF(0, 0), QSizeF(base.size()) / base.devicePixelRatio());
        std::vector<QRectF> placed;
        for (const auto& [pm, at] : overlays) {
            placed.emplace_back(at, QSizeF(pm.size()) / pm.devicePixelRatio());
            canvas |= placed.back().adjusted(-PADDING, -PADDING, PADDING, PADDING);
        }
        const qreal dpr = pixelRatio();
        QImage image((canvas.size() * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::white);
        QPainter p(&image);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.translate(-canvas.topLeft());
        p.drawPixmap(QPointF(0, 0), base);
        for (size_t i = 0; i < overlays.size(); ++i) p.drawPixmap(placed[i].topLeft(), overlays[i].first);
        return image;
    }

    // ── Moving a piece with the mouse ─────────────────────────────────────────

    // Starts `option` (Mover bloque / Mover esquema «…») from `box`'s menu and
    // takes the piece to `to`, where it is left hanging: dropPiece lets it go.
    void movePiece(DiagramScene& scene, const std::string& box, const QString& option, const QPointF& to) {
        scene.rebuild();
        settle();
        const QPointF from = itemOf(scene, box)->sceneBoundingRect().center();
        QMenu* menu = openMenuAt(scene, from);
        QAction* a = actionNamed(menu, option);
        menu->close();
        a->trigger();
        settle(200);
        constexpr int STEPS = 16;
        for (int i = 1; i <= STEPS; ++i) {
            mouse(scene, QEvent::GraphicsSceneMouseMove, from + (to - from) * (double(i) / STEPS),
                  Qt::NoButton, Qt::NoButton, from);
            settle(25);
        }
        settle(500);
    }

    void dropPiece(DiagramScene& scene, const QPointF& at) {
        mouse(scene, QEvent::GraphicsSceneMousePress, at, Qt::LeftButton, Qt::LeftButton, at);
        mouse(scene, QEvent::GraphicsSceneMouseRelease, at, Qt::LeftButton, Qt::NoButton, at);
        settle(900);
    }

    // ── Dragging a box with the mouse ─────────────────────────────────────────

    void mouse(DiagramScene& scene, QEvent::Type type, const QPointF& at, Qt::MouseButton button,
               Qt::MouseButtons buttons, const QPointF& down)
    {
        QGraphicsSceneMouseEvent event(type);
        event.setScenePos(at);
        event.setScreenPos(at.toPoint());
        event.setLastScenePos(at);
        event.setButtonDownScenePos(button == Qt::NoButton ? Qt::LeftButton : button, down);
        event.setButton(button);
        event.setButtons(buttons);
        QApplication::sendEvent(&scene, &event);
    }

    // Presses on `box` and moves the mouse by `by` (scene units), in steps, as
    // a hand would. The box is left held: drop() lets it go.
    QPointF drag(DiagramScene& scene, const std::string& box, const QPointF& by) {
        scene.rebuild();
        settle();
        const QPointF start = itemOf(scene, box)->sceneBoundingRect().center();
        mouse(scene, QEvent::GraphicsSceneMousePress, start, Qt::LeftButton, Qt::LeftButton, start);
        constexpr int STEPS = 16;
        for (int i = 1; i <= STEPS; ++i) {
            mouse(scene, QEvent::GraphicsSceneMouseMove, start + by * (double(i) / STEPS),
                  Qt::NoButton, Qt::LeftButton, start);
            settle(25);
        }
        settle(400); // the chain glides into place
        return start;
    }

    void drop(DiagramScene& scene, const QPointF& start, const QPointF& by) {
        mouse(scene, QEvent::GraphicsSceneMouseRelease, start + by, Qt::LeftButton, Qt::NoButton, start);
        settle(900); // the drop and its animation
    }

    // ── The examples ──────────────────────────────────────────────────────────

    std::unique_ptr<HypergraphEditor> editor() {
        return std::make_unique<HypergraphEditor>(GraphicalHypergraph("Esquema"));
    }

    // 1 with 2 and 3 below, each through a connection of its own.
    std::unique_ptr<HypergraphEditor> fork() {
        auto ed = editor();
        auto n1 = ed->createNode(NodeAttributes("1"), 0, -1, nullptr);
        ed->createNode(NodeAttributes("2"), 1, -1, n1);
        ed->createNode(NodeAttributes("3"), 1, -1, n1);
        return ed;
    }

    // 1 with 2 and 3 below, through one connection.
    std::unique_ptr<HypergraphEditor> fan() {
        auto ed = editor();
        auto n1 = ed->createNode(NodeAttributes("1"), 0, -1, nullptr);
        ed->createNode(NodeAttributes("2"), 1, -1, n1);
        auto n3 = ed->createNode(NodeAttributes("3"), 1, -1, nullptr);
        ed->addTargetToEdge(ed->getAllHyperedges().front(), n3);
        return ed;
    }

    // 1 over 2, and 3 loose on the level of 1 (left of it) or of 2.
    std::unique_ptr<HypergraphEditor> pairAndLoose(bool loose_above) {
        auto ed = editor();
        auto n1 = ed->createNode(NodeAttributes("1"), 0, -1, nullptr);
        ed->createNode(NodeAttributes("2"), 1, -1, n1);
        if (loose_above) ed->createNode(NodeAttributes("3"), 0, 0, nullptr);
        else             ed->createNode(NodeAttributes("3"), 1, -1, nullptr);
        return ed;
    }

    // Two loose boxes, 1 and 2.
    std::unique_ptr<HypergraphEditor> loose() {
        auto ed = editor();
        ed->createNode(NodeAttributes("1"), 0, -1, nullptr);
        ed->createNode(NodeAttributes("2"), 0, -1, nullptr);
        return ed;
    }

    // Two blocks, 1-2 and 3-4.
    std::unique_ptr<HypergraphEditor> twoBlocks() {
        auto ed = editor();
        auto n1 = ed->createNode(NodeAttributes("1"), 0, -1, nullptr);
        ed->createNode(NodeAttributes("2"), 1, -1, n1);
        auto n3 = ed->createNode(NodeAttributes("3"), 0, -1, nullptr);
        ed->createNode(NodeAttributes("4"), 1, -1, n3);
        return ed;
    }

    // 1 over 2 over 3.
    std::unique_ptr<HypergraphEditor> column() {
        auto ed = editor();
        auto n1 = ed->createNode(NodeAttributes("1"), 0, -1, nullptr);
        auto n2 = ed->createNode(NodeAttributes("2"), 1, -1, n1);
        ed->createNode(NodeAttributes("3"), 2, -1, n2);
        return ed;
    }

    // 1 and 2 reaching 3 through one connection.
    std::unique_ptr<HypergraphEditor> joined() {
        auto ed = editor();
        auto n1 = ed->createNode(NodeAttributes("1"), 0, -1, nullptr);
        auto n2 = ed->createNode(NodeAttributes("2"), 0, -1, nullptr);
        ed->createNode(NodeAttributes("3"), 1, -1, n1);
        ed->addSourceToEdge(ed->getAllHyperedges().front(), n2);
        return ed;
    }

    // 1 over 3, and 2 over 4 over 5: 3 and 5 are on different levels.
    std::unique_ptr<HypergraphEditor> twoDepths() {
        auto ed = editor();
        auto n1 = ed->createNode(NodeAttributes("1"), 0, -1, nullptr);
        auto n2 = ed->createNode(NodeAttributes("2"), 0, -1, nullptr);
        ed->createNode(NodeAttributes("3"), 1, -1, n1);
        auto n4 = ed->createNode(NodeAttributes("4"), 1, -1, n2);
        ed->createNode(NodeAttributes("5"), 2, -1, n4);
        return ed;
    }

    // Under 1, the chain 2 ≡ 3, with 4 (under 1 too) to its right.
    std::unique_ptr<HypergraphEditor> chainAndNeighbour() {
        auto ed = editor();
        auto n1 = ed->createNode(NodeAttributes("1"), 0, -1, nullptr);
        auto n2 = ed->createNode(NodeAttributes("2"), 1, -1, n1);
        auto n3 = ed->createNode(NodeAttributes("3"), 1, -1, n1);
        ed->createNode(NodeAttributes("4"), 1, -1, n1);
        ed->addEquivalence(n2, n3);
        return ed;
    }

    // 1 and 2 above, 3 under 1, and 2 reaching 3 through a connection of its own.
    std::unique_ptr<HypergraphEditor> twoParents() {
        auto ed = editor();
        auto n1 = ed->createNode(NodeAttributes("1"), 0, -1, nullptr);
        auto n2 = ed->createNode(NodeAttributes("2"), 0, -1, nullptr);
        auto n3 = ed->createNode(NodeAttributes("3"), 1, -1, n1);
        ed->addConnection(n2, n3);
        return ed;
    }

    // One connection joining 1, 2 and 3 with 4 and 5.
    std::unique_ptr<HypergraphEditor> bigConnection() {
        auto ed = editor();
        auto n1 = ed->createNode(NodeAttributes("1"), 0, -1, nullptr);
        auto n2 = ed->createNode(NodeAttributes("2"), 0, -1, nullptr);
        auto n3 = ed->createNode(NodeAttributes("3"), 0, -1, nullptr);
        ed->createNode(NodeAttributes("4"), 1, -1, n1);
        auto n5 = ed->createNode(NodeAttributes("5"), 1, -1, nullptr);
        ed->addSourceToEdge(edgeOf(ed->getGraph(), "1", "4"), n2);
        ed->addSourceToEdge(edgeOf(ed->getGraph(), "1", "4"), n3);
        ed->addTargetToEdge(edgeOf(ed->getGraph(), "1", "4"), n5);
        return ed;
    }

    // 1, 2 and 3 above 4, 5 and 6: 2 and 3 reach 5 and 6 through one connection,
    // and 1 reaches 4 and 5 (from_one) or 1 and 2 reach 4 (otherwise).
    std::unique_ptr<HypergraphEditor> sixBoxes(bool from_one) {
        auto ed = editor();
        auto n1 = ed->createNode(NodeAttributes("1"), 0, -1, nullptr);
        auto n2 = ed->createNode(NodeAttributes("2"), 0, -1, nullptr);
        auto n3 = ed->createNode(NodeAttributes("3"), 0, -1, nullptr);
        ed->createNode(NodeAttributes("4"), 1, -1, n1);
        auto n5 = ed->createNode(NodeAttributes("5"), 1, -1, nullptr);
        auto n6 = ed->createNode(NodeAttributes("6"), 1, -1, nullptr);
        if (from_one) ed->addTargetToEdge(edgeOf(ed->getGraph(), "1", "4"), n5);
        else          ed->addSourceToEdge(edgeOf(ed->getGraph(), "1", "4"), n2);
        ed->addConnection(n2, n5);
        ed->addSourceToEdge(edgeOf(ed->getGraph(), "2", "5"), n3);
        ed->addTargetToEdge(edgeOf(ed->getGraph(), "2", "5"), n6);
        return ed;
    }

    NodePtr box(const std::unique_ptr<HypergraphEditor>& ed, const std::string& name) {
        return named(ed->getGraph(), name);
    }

    // How far below its row a dragged box is held, still in its level, so it does not
    // hide the boxes it passes nor the mark of where it would land.
    constexpr double LOWER = 42.0;

    double gapBetween(DiagramScene& scene, const std::string& a, const std::string& b) {
        return itemOf(scene, b)->sceneBoundingRect().center().x() - itemOf(scene, a)->sceneBoundingRect().center().x();
    }

    QString only; // when given, only the shots whose name contains it are taken

    void run(const char* what, const std::function<void()>& shot) {
        if (!only.isEmpty() && !QString::fromUtf8(what).contains(only)) return;
        try { shot(); }
        catch (const std::exception& e) { std::fprintf(stderr, "%s: %s\n", what, e.what()); }
    }

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "Usage: capture_pictures <app/ui/resources>\n");
        return 1;
    }
    resources = QDir(QString::fromLocal8Bit(argv[1]));
    if (argc > 2) only = QString::fromLocal8Bit(argv[2]);
    if (!resources.exists("information")) {
        std::fprintf(stderr, "%s has no information folder\n", argv[1]);
        return 1;
    }
    std::printf("Device pixel ratio %.2f\n", pixelRatio());

    // ── The box menu ──────────────────────────────────────────────────────────
    run("fork", [] {
        auto ed = fork();
        DiagramScene scene(ed.get());
        save(boxMenuPicture(scene, "3", QStringLiteral("Crear caja arriba")), "information/createAbove/01.png");
        save(boxMenuPicture(scene, "2", QStringLiteral("Crear caja debajo")), "information/createBelow/01.png");
        save(boxMenuPicture(scene, "2", QStringLiteral("Crear caja a la derecha")), "information/createSide/01.png");
        save(boxMenuPicture(scene, "2", {}, QStringLiteral("Conexiones hipotéticas")), "information/uncertain/01.png");
    });
    run("loose", [] {
        auto ed = loose();
        DiagramScene scene(ed.get());
        save(boxMenuPicture(scene, "2", QStringLiteral("Caja «2» por debajo de")), "information/addChildConnection/01.png");
        save(boxMenuPicture(scene, "2", QStringLiteral("Caja «2» por encima de")), "information/addParentConnection/01.png");
    });
    run("two blocks", [] {
        auto ed = twoBlocks();
        DiagramScene scene(ed.get());
        save(boxMenuPicture(scene, "2", QStringLiteral("Fusionar")), "information/fuse/01.png");
        save(boxMenuPicture(scene, "3", QStringLiteral("Mover bloque")), "information/moveBlock/01.png");
        save(boxMenuPicture(scene, "3", QStringLiteral("Copiar bloque")), "information/copyPiece/01.png");
        save(boxMenuPicture(scene, "3", QStringLiteral("Eliminar bloque")), "information/removeBlock/01.png");
    });
    run("column", [] {
        auto ed = column();
        DiagramScene scene(ed.get());
        save(boxMenuPicture(scene, "2", QStringLiteral("Eliminar caja")), "information/removeBox/01.png");
    });
    run("joined", [] {
        auto ed = joined();
        DiagramScene scene(ed.get());
        save(boxMenuPicture(scene, "2", QStringLiteral("Eliminar conexión parcial")), "information/removePartial/01.png");
    });

    // ── The connection menu ───────────────────────────────────────────────────
    run("fan", [] {
        auto ed = fan();
        DiagramScene scene(ed.get());
        save(edgeMenuPicture(scene, "3", QStringLiteral("Crear caja entre")), "information/createBetween/01.png");
        save(edgeMenuPicture(scene, "3", QStringLiteral("Bifurcar arriba con caja nueva")), "information/forkUpNew/01.png");
        save(edgeMenuPicture(scene, "3", QStringLiteral("Bifurcar abajo con caja nueva")), "information/forkDownNew/01.png");
        save(edgeMenuPicture(scene, "3", QStringLiteral("Usar línea discontinua")), "information/lineStyle/01.png");
        save(edgeMenuPicture(scene, "3", QStringLiteral("Simplificar conexión")), "information/simplify/01.png");
        save(edgeMenuPicture(scene, "3", QStringLiteral("Eliminar conexión")), "information/removeConnection/01.png");
    });
    run("add source", [] {
        auto ed = pairAndLoose(true);
        DiagramScene scene(ed.get());
        save(edgeMenuPicture(scene, "2", QStringLiteral("Bifurcar arriba con caja existente")), "information/addSource/01.png");
    });
    run("add target", [] {
        auto ed = pairAndLoose(false);
        DiagramScene scene(ed.get());
        save(edgeMenuPicture(scene, "2", QStringLiteral("Bifurcar abajo con caja existente")), "information/addTarget/01.png");
    });

    // ── Equivalences ──────────────────────────────────────────────────────────
    run("equivalences", [] {
        auto ed = twoDepths();
        DiagramScene scene(ed.get());
        save(boxMenuPicture(scene, "5", QStringLiteral("Hacer equivalente a…")), "information/addEquivalence/01.png");
        save(selectionPicture(scene, "5", QStringLiteral("Hacer equivalente a…")), "information/addEquivalence/02.png");
        ed->addEquivalence(named(ed->getGraph(), "5"), named(ed->getGraph(), "3"));
        save(scenePicture(scene), "information/addEquivalence/03.png");

        save(lineMenuPicture(scene, QStringLiteral("Usar línea discontinua")), "information/equivalenceStyle/01.png");
        ed->setEquivalenceContinuous(named(ed->getGraph(), "3"), named(ed->getGraph(), "5"), false);
        save(scenePicture(scene), "information/equivalenceStyle/02.png");
        ed->setEquivalenceContinuous(named(ed->getGraph(), "3"), named(ed->getGraph(), "5"), true);

        save(boxMenuPicture(scene, "3", QStringLiteral("Quitar equivalencia con «5»")), "information/removeEquivalence/01.png");
        ed->removeEquivalence(named(ed->getGraph(), "3"), named(ed->getGraph(), "5"));
        save(scenePicture(scene), "information/removeEquivalence/02.png");
    });
    run("moving equivalents", [] {
        {
            // 2 taken between 3 and 4: it changes places in the chain.
            auto ed = chainAndNeighbour();
            DiagramScene scene(ed.get());
            scene.rebuild();
            const QPointF by(gapBetween(scene, "2", "3") + gapBetween(scene, "3", "4") / 2.0, LOWER);
            const QPointF start = drag(scene, "2", by);
            save(compose(scene, diagramArea(scene), {}), "information/moveEquivalent/01.png");
            drop(scene, start, by);
            save(scenePicture(scene), "information/moveEquivalent/02.png");
        }
        {
            // 3 taken well past 4: the whole chain goes along.
            auto ed = chainAndNeighbour();
            DiagramScene scene(ed.get());
            scene.rebuild();
            const QPointF by(gapBetween(scene, "2", "4") + 170.0, LOWER);
            const QPointF start = drag(scene, "3", by);
            save(compose(scene, diagramArea(scene), {}), "information/moveEquivalent/03.png");
            drop(scene, start, by);
            save(scenePicture(scene), "information/moveEquivalent/04.png");
        }
    });

    // ── Resizing a box ────────────────────────────────────────────────────────
    run("resize box", [] {
        // The 2 widened by its right edge: held, with its size above it, and let go.
        auto ed = fork();
        DiagramScene scene(ed.get());
        scene.rebuild();
        settle();
        NodeItem* two = itemOf(scene, "2");
        const QRectF box = two->mapRectToScene(two->rect());
        const QPointF start(box.right(), box.center().y());
        const QPointF by(40.0, 0.0);
        mouse(scene, QEvent::GraphicsSceneMousePress, start, Qt::LeftButton, Qt::LeftButton, start);
        constexpr int STEPS = 10;
        for (int i = 1; i <= STEPS; ++i) {
            mouse(scene, QEvent::GraphicsSceneMouseMove, start + by * (double(i) / STEPS),
                  Qt::NoButton, Qt::LeftButton, start);
            settle(25);
        }
        settle(200);
        save(compose(scene, diagramArea(scene), {}), "information/resizeBox/01.png");
        mouse(scene, QEvent::GraphicsSceneMouseRelease, start + by, Qt::LeftButton, Qt::NoButton, start);
        settle(900); // the resize and its animation
        save(scenePicture(scene), "information/resizeBox/02.png");
    });
    run("resize box, preview", [] {
        // The dialog's preview of a box wider than its shape, with the shape's size behind.
        NodeAttributes wide("2");
        wide.width = 170.0;
        NodeDialog dialog(NodeDialog::Mode::Edit, wide, {});
        dialog.show();
        settle(400);
        auto* preview = dialog.findChild<NodePreview*>();
        if (!preview) throw std::runtime_error("No preview in the box dialog");
        save(preview->grab().toImage(), "information/resizeBox/03.png");
        dialog.close();
    });

    // ── The project's settings ────────────────────────────────────────────────
    run("project settings", [] {
        // The window, all of it even if this screen would make it scroll.
        const ProjectSettings built_in;
        ProjectSettingsDialog dialog(built_in, built_in, {});
        dialog.show();
        settle(400);
        dialog.setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        if (auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("dialogScroll"))) {
            for (int i = 0; i < 5 && scroll->verticalScrollBar()->maximum() > 0; ++i) {
                dialog.resize(dialog.width(), dialog.height() + scroll->verticalScrollBar()->maximum());
                settle();
            }
        }
        save(dialog.grab().toImage(), "information/projectSettings/01.png");

        // The distances with their sample, the one between stacked lines marked
        // (as when the mouse is over its field).
        auto* preview = dialog.findChild<LayoutPreview*>();
        if (!preview) throw std::runtime_error("No sample in the settings");
        preview->setActive(LayoutPreview::Distance::HorizontalSep);
        settle(200);
        QLabel* title = nullptr;
        for (auto* label : dialog.findChildren<QLabel*>(QStringLiteral("sectionTitle")))
            if (label->text() == QStringLiteral("DISTANCIAS")) title = label;
        if (!title) throw std::runtime_error("No distances in the settings");
        auto* card = preview->parentWidget();
        const QRect distances(QPoint(0, title->mapTo(card, QPoint(0, 0)).y() - 4),
            QPoint(card->width() - 1, preview->mapTo(card, QPoint(0, preview->height())).y() + 14));
        const QImage part = card->grab(distances).toImage();
        save(part, "information/projectSettings/03.png");
        save(part, "tutorial/project_settings.png");

        // The shapes compared, the mouse on the circle's right edge (found by
        // moving along their line until it can resize something).
        auto* sizes = dialog.findChild<ShapeSizesPreview*>();
        if (!sizes) throw std::runtime_error("No shape sizes in the settings");
        const double line_y = 26.0 + (sizes->height() - 26.0 - 48.0) / 2.0;
        for (int x = sizes->width() / 2; x < sizes->width(); ++x) {
            const QPointF at(x, line_y);
            QMouseEvent move(QEvent::MouseMove, at, sizes->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(sizes, &move);
            if (sizes->cursor().shape() != Qt::ArrowCursor) break;
        }
        settle(200);
        save(sizes->grab().toImage(), "information/projectSettings/04.png");
        dialog.close();

        // The question when a new default size reaches existing boxes.
        StyledDialog* ask = existingBoxesQuestion(nullptr);
        ask->show();
        settle(300);
        save(ask->grab().toImage(), "information/projectSettings/02.png");
        ask->close();
        delete ask;
    });

    // ── The joint diagram's box menu ──────────────────────────────────────────
    run("joint", [] {
        Project project("Proyecto");
        {
            auto& ed = project.getEditor(0);
            ed.setName("Esquema 1");
            auto n1 = ed.createNode(NodeAttributes("1"), 0, -1, nullptr);
            ed.createNode(NodeAttributes("2"), 1, -1, n1);
            auto n3 = ed.createNode(NodeAttributes("3"), 0, -1, nullptr);
            ed.createNode(NodeAttributes("4"), 1, -1, n3);
        }
        project.addDiagram();
        {
            auto& ed = project.getEditor(1);
            ed.setName("Esquema 2");
            auto n5 = ed.createNode(NodeAttributes("5"), 0, -1, nullptr);
            ed.createNode(NodeAttributes("6"), 1, -1, n5);
        }
        project.getJointEditor().addHypergraph(
            const_cast<GraphicalHypergraph&>(project.getEditor(0).getGraph()), 0.0);
        project.getJointEditor().addHypergraph(
            const_cast<GraphicalHypergraph&>(project.getEditor(1).getGraph()), false);
        project.syncJointNames();
        DiagramScene scene(&project.getJointEditor());
        save(boxMenuPicture(scene, "3", QStringLiteral("Mover esquema «Esquema 1»")), "information/moveDiagram/01.png");
        save(boxMenuPicture(scene, "3", QStringLiteral("Quitar esquema «Esquema 1»")), "information/removeDiagram/01.png");
        project.getJointEditor().removeHypergraph(project.getEditor(0).getId());
        save(scenePicture(scene), "information/removeDiagram/02.png");
    });

    // ── The tour ──────────────────────────────────────────────────────────────
    // ── What the options leave, and the second box being chosen ───────────────
    //
    // Each picture starts from a fresh example, so one never shows what another left.
    auto fresh = [](std::unique_ptr<HypergraphEditor> (*make)(), const QString& path,
                    const std::function<void(HypergraphEditor&)>& change) {
        run(qPrintable(path), [&] {
            auto ed = make();
            change(*ed);
            DiagramScene scene(ed.get());
            save(scenePicture(scene), path);
        });
    };
    auto g = [](HypergraphEditor& ed, const std::string& name) { return named(ed.getGraph(), name); };

    fresh(fork, "information/createAbove/02.png", [&](HypergraphEditor& ed) {
        ed.createParent(NodeAttributes("4"), g(ed, "3"));
    });
    fresh(fork, "information/createBelow/02.png", [&](HypergraphEditor& ed) {
        ed.createNode(NodeAttributes("4"), g(ed, "2")->getLayer() + 1, -1, g(ed, "2"));
    });
    fresh(fork, "information/createSide/02.png", [&](HypergraphEditor& ed) {
        ed.createNodeNextTo(NodeAttributes("4"), g(ed, "2"), false);
    });
    fresh(fork, "information/uncertain/02.png", [&](HypergraphEditor& ed) {
        ed.setConnectionEndUncertain(edgeOf(ed.getGraph(), "1", "2"), g(ed, "2"), true);
    });
    fresh(fan, "information/createBetween/02.png", [&](HypergraphEditor& ed) {
        ed.createNodeInEdge(NodeAttributes("4"), edgeOf(ed.getGraph(), "1", "2"));
    });
    fresh(fan, "information/forkUpNew/02.png", [&](HypergraphEditor& ed) {
        ed.createSource(NodeAttributes("4"), -1, edgeOf(ed.getGraph(), "1", "2"));
    });
    fresh(fan, "information/forkDownNew/02.png", [&](HypergraphEditor& ed) {
        ed.createTarget(NodeAttributes("4"), -1, edgeOf(ed.getGraph(), "1", "2"));
    });
    fresh(fan, "information/lineStyle/02.png", [&](HypergraphEditor& ed) {
        ed.setHyperedgeContinuous(edgeOf(ed.getGraph(), "1", "2"), false);
    });
    fresh(fan, "information/simplify/03.png", [&](HypergraphEditor& ed) {
        ed.removeTargetFromHyperedge(edgeOf(ed.getGraph(), "1", "3"), g(ed, "3"));
    });
    fresh(fan, "information/removeConnection/02.png", [&](HypergraphEditor& ed) {
        ed.removeHyperedge(edgeOf(ed.getGraph(), "1", "2"));
    });
    fresh(fan, "information/addChildConnection/03.png", [](HypergraphEditor&) {});
    fresh(fan, "information/addParentConnection/03.png", [](HypergraphEditor&) {});
    fresh(fan, "information/addChildConnection/05.png", [&](HypergraphEditor& ed) {
        ed.addConnection(g(ed, "1"), g(ed, "2"));
    });
    fresh(fan, "information/addParentConnection/05.png", [&](HypergraphEditor& ed) {
        ed.addConnection(g(ed, "1"), g(ed, "2"));
    });
    fresh(loose, "information/addChildConnection/02.png", [&](HypergraphEditor& ed) {
        ed.addConnection(g(ed, "1"), g(ed, "2"));
    });
    fresh(loose, "information/addParentConnection/02.png", [&](HypergraphEditor& ed) {
        ed.addConnection(g(ed, "2"), g(ed, "1"));
    });
    for (const char* topic : { "addChildConnection", "addParentConnection" }) {
        fresh(bigConnection, QStringLiteral("information/%1/06.png").arg(QLatin1String(topic)), [](HypergraphEditor&) {});
        fresh(bigConnection, QStringLiteral("information/%1/07.png").arg(QLatin1String(topic)), [&](HypergraphEditor& ed) {
            ed.addConnection(g(ed, "2"), g(ed, "4"));
        });
    }
    fresh([] { return pairAndLoose(true); }, "information/addSource/02.png", [&](HypergraphEditor& ed) {
        ed.addSourceToEdge(edgeOf(ed.getGraph(), "1", "2"), g(ed, "3"));
    });
    fresh(twoParents, "information/addSource/03.png", [](HypergraphEditor&) {});
    fresh(twoParents, "information/addSource/05.png", [&](HypergraphEditor& ed) {
        ed.addSourceToEdge(edgeOf(ed.getGraph(), "1", "3"), g(ed, "2"));
    });
    fresh([] { return sixBoxes(true); }, "information/addSource/06.png", [](HypergraphEditor&) {});
    fresh([] { return sixBoxes(true); }, "information/addSource/07.png", [&](HypergraphEditor& ed) {
        ed.addSourceToEdge(edgeOf(ed.getGraph(), "1", "4"), g(ed, "2"));
    });
    fresh([] { return pairAndLoose(false); }, "information/addTarget/02.png", [&](HypergraphEditor& ed) {
        ed.addTargetToEdge(edgeOf(ed.getGraph(), "1", "2"), g(ed, "3"));
    });
    fresh(fork, "information/addTarget/03.png", [](HypergraphEditor&) {});
    fresh(fork, "information/addTarget/05.png", [&](HypergraphEditor& ed) {
        ed.addTargetToEdge(edgeOf(ed.getGraph(), "1", "2"), g(ed, "3"));
    });
    fresh([] { return sixBoxes(false); }, "information/addTarget/06.png", [](HypergraphEditor&) {});
    fresh([] { return sixBoxes(false); }, "information/addTarget/07.png", [&](HypergraphEditor& ed) {
        ed.addTargetToEdge(edgeOf(ed.getGraph(), "1", "4"), g(ed, "5"));
    });
    fresh(twoBlocks, "information/fuse/03.png", [&](HypergraphEditor& ed) {
        ed.fuseNodes(g(ed, "2"), g(ed, "3"), NodeAttributes("2"));
    });
    fresh(twoBlocks, "information/removeBlock/02.png", [&](HypergraphEditor& ed) {
        ed.removeComponent(g(ed, "3"));
    });
    fresh(column, "information/removeBox/02.png", [&](HypergraphEditor& ed) {
        ed.removeNode(g(ed, "2"));
    });
    fresh(joined, "information/removePartial/03.png", [&](HypergraphEditor& ed) {
        ed.removeConnection(g(ed, "2"), g(ed, "3"));
    });

    // The second box being chosen: from a box's menu, or from a connection's.
    auto choosing = [](std::unique_ptr<HypergraphEditor> (*make)(), const QString& path,
                       const std::string& source, const std::string& target, const QString& option) {
        run(qPrintable(path), [&] {
            auto ed = make();
            DiagramScene scene(ed.get());
            scene.rebuild();
            settle();
            const QPointF click = target.empty() ? itemOf(scene, source)->sceneBoundingRect().center()
                                                 : onEdge(scene, source, target);
            save(selectionAt(scene, click, option), path);
        });
    };
    choosing(twoBlocks, "information/fuse/02.png", "2", "", QStringLiteral("Fusionar"));
    choosing(joined, "information/removePartial/02.png", "2", "", QStringLiteral("Eliminar conexión parcial"));
    choosing(fan, "information/simplify/02.png", "1", "3", QStringLiteral("Simplificar conexión"));
    choosing(fan, "information/addChildConnection/04.png", "2", "", QStringLiteral("Caja «2» por debajo de"));
    choosing(fan, "information/addParentConnection/04.png", "1", "", QStringLiteral("Caja «1» por encima de"));
    choosing(twoParents, "information/addSource/04.png", "1", "3", QStringLiteral("Bifurcar arriba con caja existente"));
    choosing(fork, "information/addTarget/04.png", "1", "2", QStringLiteral("Bifurcar abajo con caja existente"));

    // ── The background menu, where the click was ──────────────────────────────
    run("new box", [] {
        auto ed = fan();
        DiagramScene scene(ed.get());
        scene.rebuild();
        settle();
        {
            const QRectF two = itemOf(scene, "2")->sceneBoundingRect(), three = itemOf(scene, "3")->sceneBoundingRect();
            const QPointF between((two.right() + three.left()) / 2.0, two.center().y());
            save(menuPicture(scene, between, {}, {}, true), "information/newBox/01.png");
        }
        ed->createNode(NodeAttributes("4"), 1, 1, nullptr);
        save(scenePicture(scene), "information/newBox/02.png");
        {
            const QRectF one = itemOf(scene, "1")->sceneBoundingRect();
            const QPointF above(one.center().x() + 70.0, one.top() - 70.0);
            save(menuPicture(scene, above, {}, {}, true), "information/newBox/03.png");
        }
        ed->createNode(NodeAttributes("5"), -1, -1, nullptr);
        save(scenePicture(scene), "information/newBox/04.png");
    });
    run("paste", [] {
        auto ed = twoBlocks();
        DiagramScene scene(ed.get());
        scene.rebuild();
        settle();
        scene.copyAt(itemOf(scene, "1")->sceneBoundingRect().center()); // the block 1-2
        const QRectF four = itemOf(scene, "4")->sceneBoundingRect();
        const QPointF right(four.right() + 170.0, itemOf(scene, "3")->sceneBoundingRect().center().y());
        save(menuPicture(scene, right, QStringLiteral("Pegar"), {}, true), "information/paste/01.png");
        scene.pasteAt(right);
        save(scenePicture(scene), "information/paste/02.png");
    });

    // ── Moving a block, or a whole diagram in the joint ───────────────────────
    run("move block", [] {
        auto ed = twoBlocks();
        DiagramScene scene(ed.get());
        scene.rebuild();
        settle();
        const QRectF before = diagramArea(scene);
        const QRectF one = itemOf(scene, "1")->sceneBoundingRect();
        const QPointF left(one.left() - 190.0, one.center().y());
        movePiece(scene, "3", QStringLiteral("Mover bloque"), left);
        save(compose(scene, before | diagramArea(scene), {}), "information/moveBlock/02.png");
        dropPiece(scene, left);
        save(scenePicture(scene), "information/moveBlock/03.png");
    });
    run("move diagram", [] {
        Project project("Proyecto");
        {
            auto& ed = project.getEditor(0);
            ed.setName("Esquema 1");
            auto n1 = ed.createNode(NodeAttributes("1"), 0, -1, nullptr);
            ed.createNode(NodeAttributes("2"), 1, -1, n1);
            auto n3 = ed.createNode(NodeAttributes("3"), 0, -1, nullptr);
            ed.createNode(NodeAttributes("4"), 1, -1, n3);
        }
        project.addDiagram();
        {
            auto& ed = project.getEditor(1);
            ed.setName("Esquema 2");
            auto n5 = ed.createNode(NodeAttributes("5"), 0, -1, nullptr);
            ed.createNode(NodeAttributes("6"), 1, -1, n5);
        }
        project.getJointEditor().addHypergraph(
            const_cast<GraphicalHypergraph&>(project.getEditor(0).getGraph()), 0.0);
        project.getJointEditor().addHypergraph(
            const_cast<GraphicalHypergraph&>(project.getEditor(1).getGraph()), false);
        project.syncJointNames();
        DiagramScene scene(&project.getJointEditor());
        scene.rebuild();
        settle();
        const QRectF six = itemOf(scene, "6")->sceneBoundingRect();
        const QPointF right(six.right() + 260.0, itemOf(scene, "5")->sceneBoundingRect().center().y());
        movePiece(scene, "3", QStringLiteral("Mover esquema «Esquema 1»"), right);
        // Only what is on show: the whole diagram left, so its old place is empty.
        save(compose(scene, diagramArea(scene), {}), "information/moveDiagram/02.png");
        dropPiece(scene, right);
        save(scenePicture(scene), "information/moveDiagram/03.png");
    });

    // ── The tabs: a real main window ──────────────────────────────────────────
    run("tabs", [] {
        Project project("Proyecto");
        {
            auto& ed = project.getEditor(0);
            ed.setName("Esquema");
            auto n1 = ed.createNode(NodeAttributes("1"), 0, -1, nullptr);
            ed.createNode(NodeAttributes("2"), 1, -1, n1);
            auto n3 = ed.createNode(NodeAttributes("3"), 0, -1, nullptr);
            ed.createNode(NodeAttributes("4"), 1, -1, n3);
        }
        MainWindow window(std::move(project));
        window.resize(1100, 760);
        window.show();
        settle(900);
        auto* bar = window.findChild<DiagramTabBar*>();
        if (!bar) throw std::runtime_error("No tab bar");

        // The strip, from its left edge to just past the "+".
        auto strip = [bar] {
            const QWidget* add = bar->addButton();
            const int right = add->mapTo(bar, QPoint(add->width(), 0)).x() + 18;
            return bar->grab(QRect(0, 0, right, bar->height()));
        };

        QWidget* tab = bar->tabAt(0);
        const QPoint click = tab->mapTo(bar, QPoint(tab->width() * 3 / 4, tab->height() - 14));
        emit bar->tabMenuRequested(0, bar->mapToGlobal(click));
        settle();
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) throw std::runtime_error("No tab menu");
        menu->move(-20000, -20000);
        settle(40);
        QAction* duplicate = actionNamed(menu, QStringLiteral("Duplicar esquema"));
        menu->setActiveAction(duplicate);
        settle(60);
        const QPixmap menu_pm = menu->grab();
        const QPixmap base = strip();
        save(composeOver(base, { { menu_pm, QPointF(click) + QPointF(4, 4) },
                                 { clickMark(), QPointF(click) - QPointF(15, 15) } }),
             "information/duplicateDiagram/01.png");
        menu->close();
        duplicate->trigger(); // before the menu (deleted on close) takes its actions along
        settle(1200); // the new tab slides in
        save(composeOver(strip(), {}), "information/duplicateDiagram/02.png");
        window.close();
    });

    // The tour's card shows its pictures small: they are taken 1:1, so the
    // menus in them are shrunk as little as possible.
    zoom = 1.0;
    menu_at_top = true;
    run("tour", [] {
        std::unique_ptr<Project> example = tutorial::exampleProject();
        DiagramScene scene(&example->getEditor(0));
        save(scenePicture(scene), "tutorial/welcome.png");
        {
            // The background menu, opened right of the 3, on its level.
            const QRectF three = itemOf(scene, "3")->sceneBoundingRect();
            const QPointF empty(three.right() + 90.0, three.center().y());
            save(menuPicture(scene, empty, QStringLiteral("Nueva caja"), {}, true), "tutorial/new_box.png");
        }
        // The 6 has nothing to its right, so its menu hides none of the diagram.
        save(boxMenuPicture(scene, "6", {}), "tutorial/box_menu.png");
        save(selectionPicture(scene, "4", QStringLiteral("Caja «4» por debajo de")), "tutorial/pick_second.png");
        save(edgeMenuPicture(scene, "6", {}), "tutorial/edge_menu.png");
        {
            // The 6 on its way to the 4 (the step asks to try just that).
            const QPointF by(-gapBetween(scene, "4", "6") - 175.0, LOWER);
            drag(scene, "6", by);
            save(compose(scene, diagramArea(scene), {}), "tutorial/drag_box.png");
            // Put it back where it was before letting go.
            const QPointF start = itemOf(scene, "6")->sceneBoundingRect().center() - by;
            mouse(scene, QEvent::GraphicsSceneMouseMove, start, Qt::NoButton, Qt::LeftButton, start);
            settle(200);
            mouse(scene, QEvent::GraphicsSceneMouseRelease, start, Qt::LeftButton, Qt::NoButton, start);
            settle(900);
        }

        // The box dialog, as a double click on the 3 opens it.
        NodeDialog dialog(NodeDialog::Mode::Edit, named(example->getEditor(0).getGraph(), "3")->getAttributes(), {},
            nullptr, &example->getSettings());
        dialog.show();
        settle(300);
        // All of it, even if this screen would make it scroll.
        dialog.setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        if (auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("dialogScroll"))) {
            for (int i = 0; i < 5 && scroll->verticalScrollBar()->maximum() > 0; ++i) {
                dialog.resize(dialog.width(), dialog.height() + scroll->verticalScrollBar()->maximum());
                settle();
            }
        }
        save(dialog.grab().toImage(), "tutorial/properties.png");
        dialog.close();
    });
    run("tour, moving a block", [] {
        // The example's first diagram with the second's block beside it, as the
        // step has the user make: the 7-8-9 block hangs from the mouse.
        auto ed = editor();
        auto n1 = ed->createNode(NodeAttributes("1"), 0, -1, nullptr);
        auto n2 = ed->createNode(NodeAttributes("2"), 1, -1, n1);
        auto n3 = ed->createNode(NodeAttributes("3"), 1, -1, n1);
        ed->createNode(NodeAttributes("4"), 2, -1, n2);
        auto n5 = ed->createNode(NodeAttributes("5"), 2, -1, n2);
        ed->createNode(NodeAttributes("6"), 2, -1, n3);
        ed->addConnection(n3, n5);
        auto n7 = ed->createNode(NodeAttributes("7"), 0, -1, nullptr);
        ed->createNode(NodeAttributes("8"), 1, -1, n7);
        ed->createNode(NodeAttributes("9"), 1, -1, n7);
        DiagramScene scene(ed.get());
        scene.rebuild();
        settle();
        const QRectF before = diagramArea(scene);
        const QRectF seven = itemOf(scene, "7")->sceneBoundingRect();
        QMenu* menu = openMenuAt(scene, seven.center());
        QAction* move = actionNamed(menu, QStringLiteral("Mover bloque"));
        menu->close();
        move->trigger();
        settle(200);
        // Taken to the right of the diagram, a level down.
        const QPointF from = seven.center();
        const QPointF to(itemOf(scene, "6")->sceneBoundingRect().right() + 240.0, itemOf(scene, "2")->sceneBoundingRect().center().y());
        constexpr int STEPS = 16;
        for (int i = 1; i <= STEPS; ++i) {
            mouse(scene, QEvent::GraphicsSceneMouseMove, from + (to - from) * (double(i) / STEPS),
                  Qt::NoButton, Qt::NoButton, from);
            settle(25);
        }
        settle(500);
        save(compose(scene, before | diagramArea(scene), {}), "tutorial/move_block.png");
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&scene, &escape);
        settle(400);
    });

    return 0;
}
