#include <gtest/gtest.h>

#include "ProjectSettingsDialog.h"

#include <QSettings>
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
