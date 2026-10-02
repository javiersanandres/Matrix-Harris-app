#include <gtest/gtest.h>

#include "GurobiNotice.h"

#include <QSettings>
#include <QTemporaryDir>

using hypergraph_logic::GurobiStatus;
using namespace ui::gurobi;

namespace {

    // Reminders kept in a throwaway INI file, never in the user's settings.
    class GurobiNoticeTest : public ::testing::Test {
    protected:
        QTemporaryDir dir;
        QSettings settings{ dir.filePath("settings.ini"), QSettings::IniFormat };
        const QString group = REMINDERS_GROUP;
    };

} // namespace

TEST_F(GurobiNoticeTest, FirstStartWithoutGurobiAsksToInstallIt) {
    EXPECT_EQ(pendingNotice(GurobiStatus::NotInstalled, settings, group), Notice::Install);
    // Until silenced, it comes back on every start.
    EXPECT_EQ(pendingNotice(GurobiStatus::NotInstalled, settings, group), Notice::Install);
}

TEST_F(GurobiNoticeTest, InstalledWithoutLicenseAsksForOne) {
    EXPECT_EQ(pendingNotice(GurobiStatus::NoLicense, settings, group), Notice::License);
}

TEST_F(GurobiNoticeTest, ReadyShowsNothing) {
    EXPECT_EQ(pendingNotice(GurobiStatus::Ready, settings, group), Notice::None);
}

TEST_F(GurobiNoticeTest, DontRemindSilencesThatNoticeOnly) {
    stopReminding(Notice::Install, settings, group);
    EXPECT_EQ(pendingNotice(GurobiStatus::NotInstalled, settings, group), Notice::None);
    // Gurobi installed later, still without a license: that is news.
    EXPECT_EQ(pendingNotice(GurobiStatus::NoLicense, settings, group), Notice::License);

    stopReminding(Notice::License, settings, group);
    EXPECT_EQ(pendingNotice(GurobiStatus::NoLicense, settings, group), Notice::None);
}

TEST_F(GurobiNoticeTest, DontRemindSurvivesReopeningTheSettings) {
    stopReminding(Notice::Install, settings, group);
    settings.sync();
    QSettings reopened(dir.filePath("settings.ini"), QSettings::IniFormat);
    EXPECT_EQ(pendingNotice(GurobiStatus::NotInstalled, reopened, group), Notice::None);
}

TEST_F(GurobiNoticeTest, AValidLicenseSwitchesTheRemindersBackOn) {
    stopReminding(Notice::Install, settings, group);
    stopReminding(Notice::License, settings, group);
    EXPECT_EQ(pendingNotice(GurobiStatus::Ready, settings, group), Notice::None);
    // The license expires afterwards: the user hears about it again.
    EXPECT_EQ(pendingNotice(GurobiStatus::NoLicense, settings, group), Notice::License);
    EXPECT_EQ(pendingNotice(GurobiStatus::NotInstalled, settings, group), Notice::Install);
}

TEST_F(GurobiNoticeTest, TestGroupIsKeptApartFromTheRealOne) {
    stopReminding(Notice::Install, settings, TEST_REMINDERS_GROUP);
    EXPECT_EQ(pendingNotice(GurobiStatus::NotInstalled, settings, TEST_REMINDERS_GROUP), Notice::None);
    EXPECT_EQ(pendingNotice(GurobiStatus::NotInstalled, settings, REMINDERS_GROUP), Notice::Install);

    resetReminders(settings, TEST_REMINDERS_GROUP);
    EXPECT_EQ(pendingNotice(GurobiStatus::NotInstalled, settings, TEST_REMINDERS_GROUP), Notice::Install);
}

TEST(GurobiStartupOptions, TakesTheTestOptionsAndLeavesTheRest) {
    QStringList args{ "MatrixHarrisApp.exe", "--gurobi-test=no-license", "proyecto.json", "--gurobi-test-reset" };
    const StartupOptions options = takeStartupOptions(args);
    ASSERT_TRUE(options.simulated.has_value());
    EXPECT_EQ(*options.simulated, GurobiStatus::NoLicense);
    EXPECT_TRUE(options.reset_reminders);
    EXPECT_EQ(args, (QStringList{ "MatrixHarrisApp.exe", "proyecto.json" }));
}

TEST(GurobiStartupOptions, EveryStateAndUnknownOnes) {
    QStringList a{ "app", "--gurobi-test=not-installed" };
    EXPECT_EQ(takeStartupOptions(a).simulated, GurobiStatus::NotInstalled);
    QStringList b{ "app", "--gurobi-test=ready" };
    EXPECT_EQ(takeStartupOptions(b).simulated, GurobiStatus::Ready);
    QStringList c{ "app", "--gurobi-test=whatever" };
    EXPECT_FALSE(takeStartupOptions(c).simulated.has_value());
    EXPECT_EQ(c, QStringList{ "app" });
    QStringList d{ "app", "proyecto.json" };
    const StartupOptions none = takeStartupOptions(d);
    EXPECT_FALSE(none.simulated.has_value());
    EXPECT_FALSE(none.reset_reminders);
    EXPECT_EQ(d, (QStringList{ "app", "proyecto.json" }));
}
