#include <gtest/gtest.h>

#include "NodeDialogs.h"

#include <QApplication>
#include <QPushButton>

using namespace hypergraph_logic;

namespace {

    void ensureApplication() {
        if (QCoreApplication::instance()) return;
        static int argc = 1;
        static char name[] = "app_logic_tests";
        static char* argv[] = { name, nullptr };
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static QApplication app(argc, argv);
    }

    // The value cards, left then right for every row: name, shape, size, background,
    // text colour, font size, state.
    QList<QPushButton*> cards(QWidget& dialog) {
        return dialog.findChildren<QPushButton*>(QStringLiteral("chip"));
    }

} // namespace

// Clicking a value card takes it into the result, and the card shows it is the one.
TEST(FuseDialog, ClickingAValueTakesIt) {
    ensureApplication();
    NodeAttributes left("Izquierda");
    NodeAttributes right("Derecha");
    right.font_size = 14;
    ui::FuseNodesDialog dialog(left, right, {});
    const QList<QPushButton*> c = cards(dialog);
    ASSERT_EQ(c.size(), 14);

    EXPECT_TRUE(c[0]->property("chosen").toBool());   // the result starts as the left box
    EXPECT_FALSE(c[1]->property("chosen").toBool());
    c[1]->click();                                     // the right box's name
    EXPECT_EQ(dialog.attributes().name, "Derecha");
    EXPECT_FALSE(c[0]->property("chosen").toBool());
    EXPECT_TRUE(c[1]->property("chosen").toBool());

    c[11]->click();                                    // the right box's font size
    EXPECT_EQ(dialog.attributes().font_size, 14);
}

// Every row is shown, also where both boxes agree (both cards are marked then).
TEST(FuseDialog, EveryRowIsShown) {
    ensureApplication();
    NodeAttributes left("A");
    NodeAttributes right("B"); // only the name differs
    ui::FuseNodesDialog dialog(left, right, {});
    const QList<QPushButton*> c = cards(dialog);
    ASSERT_EQ(c.size(), 14);
    for (QPushButton* card : c) EXPECT_FALSE(card->isHidden());
    EXPECT_TRUE(c[6]->property("chosen").toBool());    // the backgrounds agree
    EXPECT_TRUE(c[7]->property("chosen").toBool());
}

// While a fire state imposes the colours, their cards cannot be taken.
TEST(FuseDialog, ColoursLockedByAFireCannotBeTaken) {
    ensureApplication();
    NodeAttributes left("A");
    left.fire = FireState::FireWithAshes;
    NodeAttributes right("B");
    right.colour = Color{ 200, 230, 255, 255 };
    ui::FuseNodesDialog dialog(left, right, {});
    const QList<QPushButton*> c = cards(dialog);
    ASSERT_EQ(c.size(), 14);
    EXPECT_FALSE(c[7]->isEnabled()); // the right box's background
}
