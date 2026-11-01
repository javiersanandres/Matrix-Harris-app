#include <gtest/gtest.h>

#include "HelpNotifier.h"
#include "Project.h"
#include "Tutorial.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
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

// The title's dot goes when the menu opens; each entry's, when it is chosen.
TEST(HelpNotifierTest, EntriesStayFlaggedUntilChosen) {
    if (!QCoreApplication::instance()) {
        static int argc = 1;
        static char name[] = "app_logic_tests";
        static char* argv[] = { name, nullptr };
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static QApplication app(argc, argv);
    }
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    {
        QMenuBar bar;
        QMenu* help = bar.addMenu(QStringLiteral("Ayuda"));
        QAction* tutorial = help->addAction(QStringLiteral("Tutorial"));
        QAction* install = help->addAction(QStringLiteral("Instalar Gurobi"));
        HelpNotifier notifier(&bar, help);
        notifier.addEntry(QStringLiteral("tutorial"), tutorial);
        notifier.addEntry(QStringLiteral("gurobiInstall"), install);

        notifier.flag(QStringLiteral("gurobiInstall"));
        notifier.flag(QStringLiteral("tutorial"));
        QSettings settings;
        EXPECT_TRUE(HelpNotifier::storedTitleFlag(settings));

        emit help->aboutToShow(); // opening the menu: the title's news is seen
        emit help->aboutToHide();
        QSettings after_menu;
        EXPECT_FALSE(HelpNotifier::storedTitleFlag(after_menu));
        EXPECT_EQ(notifier.flagged(), (QStringList{ "gurobiInstall", "tutorial" }));

        install->trigger(); // that entry is seen; the other keeps its dot
        EXPECT_EQ(notifier.flagged(), QStringList{ "tutorial" });
        QSettings after_entry;
        EXPECT_EQ(HelpNotifier::storedFlags(after_entry), QStringList{ "tutorial" });

        notifier.flag(QStringLiteral("gurobiInstall")); // something new: the title again
        QSettings after_flag;
        EXPECT_TRUE(HelpNotifier::storedTitleFlag(after_flag));
    }
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}
