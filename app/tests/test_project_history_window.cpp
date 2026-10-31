#include <gtest/gtest.h>

#include "DiagramTabBar.h"
#include "MainWindow.h"
#include "Project.h"

#include <QAction>
#include <QApplication>

using app_logic::Project;

namespace {

    void ensureApplication() {
        if (QCoreApplication::instance()) return;
        static int argc = 1;
        static char name[] = "app_logic_tests";
        static char* argv[] = { name, nullptr };
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static QApplication app(argc, argv);
    }

    QAction* actionWithShortcut(QWidget& window, const QString& keys) {
        for (QAction* a : window.findChildren<QAction*>())
            if (a->shortcuts().contains(QKeySequence(keys))) return a;
        return nullptr;
    }

    QAction* actionNamed(QWidget& window, const QString& text) {
        for (QAction* a : window.findChildren<QAction*>())
            if (a->text() == text) return a;
        return nullptr;
    }

} // namespace

// A diagram removed from the window comes back with Proyecto › Deshacer, in
// its tab, and leaves again with Rehacer; the menu says what each one does.
TEST(ProjectHistoryWindow, RemovingADiagramIsUndone) {
    ensureApplication();
    Project project("P");
    project.getEditor(0).setName("A");
    project.addDiagram();
    project.getEditor(1).setName("B");
    project.setActive(0);
    ui::MainWindow window(std::move(project));
    auto* bar = window.findChild<ui::DiagramTabBar*>();
    ASSERT_TRUE(bar);

    QAction* undo = actionWithShortcut(window, QStringLiteral("Ctrl+Alt+Z"));
    QAction* redo = actionWithShortcut(window, QStringLiteral("Ctrl+Alt+Y"));
    ASSERT_TRUE(undo && redo);
    EXPECT_TRUE(undo->isEnabled()); // adding B
    QAction* remove = actionNamed(window, QStringLiteral("Eliminar esquema"));
    ASSERT_TRUE(remove);

    remove->trigger(); // no question asked
    EXPECT_EQ(bar->tabCount(), 1);
    EXPECT_EQ(undo->text(), QStringLiteral("Deshacer: eliminar «A»"));
    EXPECT_FALSE(redo->isEnabled());

    undo->trigger();
    ASSERT_EQ(bar->tabCount(), 2);
    EXPECT_EQ(redo->text(), QStringLiteral("Rehacer: eliminar «A»"));

    redo->trigger();
    EXPECT_EQ(bar->tabCount(), 1);
}

// Opening the settings is Ctrl+Alt+S now.
TEST(ProjectHistoryWindow, SettingsHaveTheirShortcut) {
    ensureApplication();
    ui::MainWindow window(Project("P"));
    QAction* settings = actionWithShortcut(window, QStringLiteral("Ctrl+Alt+S"));
    ASSERT_TRUE(settings);
    EXPECT_EQ(settings->text(), QStringLiteral("Ajustes del proyecto…"));
}
