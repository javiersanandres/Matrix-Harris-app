#include <gtest/gtest.h>

#include "NodeVisuals.h"
#include "ProjectSettingsDialog.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>
#include <QTemporaryDir>

using app_logic::ProjectSettings;
using namespace hypergraph_logic;

namespace {

    // The application's defaults kept in a throwaway INI file, never in the user's settings.
    class AppDefaultsTest : public ::testing::Test {
    protected:
        QTemporaryDir dir;
        QSettings settings{ dir.filePath("settings.ini"), QSettings::IniFormat };
    };

} // namespace

TEST_F(AppDefaultsTest, WithoutAnySavedTheBuiltInOnesApply) {
    EXPECT_EQ(ui::app_defaults::load(settings), ProjectSettings());
}

TEST_F(AppDefaultsTest, SavedOnesComeBack) {
    ProjectSettings s;
    s.colour = Color{ 0, 32, 96, 255 };
    s.font_size = 12;
    s.shape = NodeShape::Circle;
    s.equivalence_continuous = false;
    s.layout.block_sep = 90.0;
    s.layout.box_sizes.rhombus = BoxSize{ 120.0, 70.0 };
    ui::app_defaults::save(settings, s);
    settings.sync();

    QSettings reopened(dir.filePath("settings.ini"), QSettings::IniFormat);
    EXPECT_EQ(ui::app_defaults::load(reopened), s);
}

TEST_F(AppDefaultsTest, SomethingElseStoredThereGivesTheBuiltInOnes) {
    settings.setValue("project/defaults", "not json at all");
    EXPECT_EQ(ui::app_defaults::load(settings), ProjectSettings());
}

namespace {

    void ensureApplication() {
        if (QCoreApplication::instance()) return;
        static int argc = 1;
        static char name[] = "app_logic_tests";
        static char* argv[] = { name, nullptr };
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static QApplication app(argc, argv);
    }

    QSpinBox* fieldNamed(QWidget& dialog, const QString& tooltip_start) {
        for (auto* spin : dialog.findChildren<QSpinBox*>())
            if (spin->toolTip().startsWith(tooltip_start)) return spin;
        return nullptr;
    }

    void type(QSpinBox* spin, const QString& text, int key) {
        spin->setFocus();
        QLineEdit* edit = spin->findChild<QLineEdit*>();
        edit->selectAll();
        edit->insert(text);
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(edit, &press);
    }

} // namespace

// Enter in a field keeps what was typed, and the window stays open.
TEST(ProjectSettingsDialogFields, EnterKeepsTheTypedValue) {
    ensureApplication();
    ui::ProjectSettingsDialog dialog(ProjectSettings(), ProjectSettings(), {});
    dialog.show();
    QSpinBox* spin = fieldNamed(dialog, QStringLiteral("Entre cajas vecinas"));
    ASSERT_TRUE(spin);
    type(spin, QStringLiteral("90"), Qt::Key_Return);
    EXPECT_EQ(spin->value(), 90);
    EXPECT_EQ(dialog.settings().layout.block_sep, 90.0);
    EXPECT_TRUE(dialog.isVisible());
}

// Its "↺" brings a distance back to the project's, and is off when nothing changed.
TEST(ProjectSettingsDialogFields, RevertBringsBackTheProjectsValue) {
    ensureApplication();
    ui::ProjectSettingsDialog dialog(ProjectSettings(), ProjectSettings(), {});
    dialog.show();
    QSpinBox* spin = fieldNamed(dialog, QStringLiteral("Entre cajas vecinas"));
    ASSERT_TRUE(spin);
    type(spin, QStringLiteral("90"), Qt::Key_Enter);
    QToolButton* revert = nullptr;
    for (auto* b : dialog.findChildren<QToolButton*>(QStringLiteral("previewReset")))
        if (b->parentWidget() == spin->parentWidget()) revert = b;
    ASSERT_TRUE(revert);
    EXPECT_TRUE(revert->isEnabled());
    revert->click();
    EXPECT_EQ(dialog.settings().layout.block_sep, ProjectSettings().layout.block_sep);
    EXPECT_EQ(spin->value(), qRound(ProjectSettings().layout.block_sep));
    EXPECT_FALSE(revert->isEnabled());
}

// From a corner, a box keeps its proportions; from a side, only that side changes.
TEST(ResizedBox, CornersKeepTheProportions) {
    namespace nv = ui::node_visuals;
    const QSizeF corner = nv::resizedBox(QSizeF(100, 50), nv::ResizeRight | nv::ResizeBottom, QPointF(30, 2));
    EXPECT_NEAR(corner.width() / corner.height(), 2.0, 0.05);
    EXPECT_GT(corner.width(), 100.0);
    const QSizeF side = nv::resizedBox(QSizeF(100, 50), nv::ResizeRight, QPointF(30, 2));
    EXPECT_EQ(side, QSizeF(160, 50));
}
