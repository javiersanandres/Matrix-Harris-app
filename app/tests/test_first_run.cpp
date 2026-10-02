#include <gtest/gtest.h>

#include "HelpNotifier.h"
#include "Project.h"
#include "Tutorial.h"

#include <QSettings>
#include <QTemporaryDir>

using namespace ui;

namespace {

    // Settings kept in a throwaway INI file, never in the user's.
    class FirstRunTest : public ::testing::Test {
    protected:
        QTemporaryDir dir;
        QSettings settings{ dir.filePath("settings.ini"), QSettings::IniFormat };
    };

} // namespace

TEST_F(FirstRunTest, TourIsPendingUntilSeen) {
    EXPECT_TRUE(tutorial::pendingAtStartup(settings));
    tutorial::markSeen(settings);
    EXPECT_FALSE(tutorial::pendingAtStartup(settings));

    settings.sync();
    QSettings reopened(dir.filePath("settings.ini"), QSettings::IniFormat);
    EXPECT_FALSE(tutorial::pendingAtStartup(reopened)) << "seen must survive a restart";
}

TEST_F(FirstRunTest, HelpFlagsAreKeptUntilCleared) {
    EXPECT_TRUE(HelpNotifier::storedFlags(settings).isEmpty());
    HelpNotifier::storeFlags(settings, { "tutorial", "gurobiInstall" });
    settings.sync();

    QSettings reopened(dir.filePath("settings.ini"), QSettings::IniFormat);
    EXPECT_EQ(HelpNotifier::storedFlags(reopened), (QStringList{ "tutorial", "gurobiInstall" }));

    HelpNotifier::storeFlags(reopened, {});
    EXPECT_TRUE(HelpNotifier::storedFlags(reopened).isEmpty());
}

TEST(TutorialContent, EveryStepHasTitleAndText) {
    const auto steps = tutorial::introSteps();
    ASSERT_FALSE(steps.empty());
    for (const auto& s : steps) {
        EXPECT_FALSE(s.title.isEmpty());
        EXPECT_FALSE(s.text.isEmpty());
    }
    // The first step greets (centred) and the last points to Ayuda.
    EXPECT_TRUE(steps.front().targets.empty());
    ASSERT_EQ(steps.back().targets.size(), 1u);
    EXPECT_EQ(steps.back().targets.front(), tutorial::Target::HelpMenu);
}

TEST(TutorialExample, TwoDiagramsReadyToPractise) {
    auto example = tutorial::exampleProject();
    ASSERT_TRUE(example);
    EXPECT_EQ(example->getDiagramCount(), 2);
    EXPECT_EQ(example->getActiveIndex(), 0);
    EXPECT_FALSE(example->hasUnsavedChanges()) << "nothing to save or undo before the user acts";
    EXPECT_TRUE(example->getFilePath().empty()) << "a save must ask where, not overwrite a temporary file";
    EXPECT_EQ(example->getEditor(0).getAllNodes().size(), 6u);
    EXPECT_EQ(example->getEditor(1).getAllNodes().size(), 3u);
    // Only the first diagram starts in the joint: adding the second is a step.
    EXPECT_EQ(example->getJointEditor().getAllNodes().size(), 6u);
}
