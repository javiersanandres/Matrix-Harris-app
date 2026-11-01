#include <gtest/gtest.h>

#include "DiagramTabWidget.h"
#include "DiagramView.h"

#include <QApplication>
#include <QEnterEvent>
#include <QGraphicsScene>
#include <QScrollBar>
#include <QToolButton>
#include <QWheelEvent>

namespace {

    void ensureApplication() {
        if (QCoreApplication::instance()) return;
        static int argc = 1;
        static char name[] = "app_logic_tests";
        static char* argv[] = { name, nullptr };
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static QApplication app(argc, argv);
    }

    // A view of a 200 x 100 diagram, fitted into 600 x 400.
    struct FittedView {
        QGraphicsScene scene;
        ui::DiagramView view{ &scene };
        FittedView() {
            scene.addRect(0, 0, 200, 100);
            scene.setSceneRect(0, 0, 200, 100);
            view.resize(600, 400);
            view.show();
            view.fitWithMargin(scene.sceneRect());
        }
    };

} // namespace

// No scrollbars, ever, however far the view is zoomed in.
TEST(DiagramView, NoScrollbarsShow) {
    ensureApplication();
    FittedView f;
    for (int i = 0; i < 10; ++i) f.view.zoomIn();
    EXPECT_FALSE(f.view.horizontalScrollBar()->isVisible());
    EXPECT_FALSE(f.view.verticalScrollBar()->isVisible());
}

// The plain wheel zooms, as Ctrl+wheel does.
TEST(DiagramView, ThePlainWheelZooms) {
    ensureApplication();
    FittedView f;
    QWidget* vp = f.view.viewport();
    const QPointF at(10, 10); // over the background, not the box
    for (const Qt::KeyboardModifiers mods : { Qt::KeyboardModifiers(Qt::NoModifier),
                                              Qt::KeyboardModifiers(Qt::ControlModifier) }) {
        const double zoom = f.view.currentZoom();
        QWheelEvent wheel(at, vp->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, mods,
            Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(vp, &wheel);
        EXPECT_GT(f.view.currentZoom(), zoom);
    }
}

// Hovering a card shows the miniature's magnifiers, unless the diagram is empty.
TEST(DiagramView, MagnifiersShowOnHoverWhenNotEmpty) {
    ensureApplication();
    QGraphicsScene scene;
    ui::DiagramTabWidget tab(&scene, QStringLiteral("A"), false);
    tab.show();
    auto hover = [&tab] {
        QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), tab.mapToGlobal(QPointF(5, 5)));
        QCoreApplication::sendEvent(&tab, &enter);
    };
    auto shown = [&tab] {
        const QList<QToolButton*> buttons = tab.findChildren<QToolButton*>(QStringLiteral("miniatureZoom"));
        return buttons.size() == 2 && buttons[0]->isVisible() && buttons[1]->isVisible();
    };
    hover();
    EXPECT_FALSE(shown());
    scene.addRect(0, 0, 100, 50);
    scene.setSceneRect(0, 0, 100, 50);
    EXPECT_TRUE(shown());
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(&tab, &leave);
    EXPECT_FALSE(shown());
}
