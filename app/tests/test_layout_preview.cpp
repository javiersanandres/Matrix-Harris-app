#include <gtest/gtest.h>

#include "LayoutPreview.h"
#include "SettingsWidgets.h"

#include <QApplication>
#include <QKeyEvent>

#include <vector>

using namespace hypergraph_logic;
using ui::LayoutPreview;
using D = LayoutPreview::Distance;

namespace {

    void ensureApplication() {
        if (QCoreApplication::instance()) return;
        static int argc = 1;
        static char name[] = "app_logic_tests";
        static char* argv[] = { name, nullptr };
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static QApplication app(argc, argv);
    }

    void expectShows(const LayoutPreview& preview, const LayoutSettings& s) {
        ASSERT_TRUE(preview.measured(D::BlockSep));
        ASSERT_TRUE(preview.measured(D::LayerGap));
        ASSERT_TRUE(preview.measured(D::HorizontalSep));
        EXPECT_NEAR(*preview.measured(D::BlockSep), s.block_sep, 0.5);
        EXPECT_NEAR(*preview.measured(D::LayerGap), s.layer_gap, 0.5);
        EXPECT_NEAR(*preview.measured(D::HorizontalSep), s.horizontal_sep, 0.5);
    }

} // namespace

// The sample shows every distance, as long as it is set.
TEST(LayoutPreview, MarksEveryDistanceAsItIs) {
    ensureApplication();
    LayoutPreview preview;
    expectShows(preview, LayoutSettings());
}

TEST(LayoutPreview, FollowsNewSettings) {
    ensureApplication();
    LayoutPreview preview;
    LayoutSettings s;
    s.block_sep = 90.0;
    s.layer_gap = 70.0;
    s.horizontal_sep = 40.0;
    preview.setSettings(s);
    expectShows(preview, s);

    // At the bottom of the ranges, too.
    s.block_sep = LayoutSettings::MIN_BLOCK_SEP_SETTING;
    s.layer_gap = LayoutSettings::MIN_LAYER_GAP_SETTING;
    s.horizontal_sep = LayoutSettings::MIN_HORIZONTAL_SEP_SETTING;
    preview.setSettings(s);
    expectShows(preview, s);
}

// The slider's point, moved with the keyboard, stays inside its range.
TEST(DistanceSlider, KeysMoveItWithinItsRange) {
    ensureApplication();
    ui::DistanceSlider slider(20, 300);
    slider.setValue(60);
    std::vector<int> reported;
    QObject::connect(&slider, &ui::DistanceSlider::moved, [&](int v) { reported.push_back(v); });
    auto press = [&](int key) {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(&slider, &event);
    };
    press(Qt::Key_Right);
    press(Qt::Key_PageUp);
    press(Qt::Key_Home);
    press(Qt::Key_Left); // already at the bottom: nothing to report
    EXPECT_EQ(reported, (std::vector<int>{ 61, 71, 20 }));
    EXPECT_EQ(slider.value(), 20);
    // A value set from outside is not reported as the user's.
    slider.setValue(1000);
    EXPECT_EQ(slider.value(), 300);
    EXPECT_EQ(reported.size(), 3u);
}

// The default box sizes do not reach the sample: only the distances do.
TEST(LayoutPreview, IgnoresTheDefaultBoxSizes) {
    ensureApplication();
    LayoutPreview preview;
    const auto before = preview.measured(D::LayerGap);
    LayoutSettings s;
    s.box_sizes.rectangle = BoxSize{ 220.0, 140.0 };
    preview.setSettings(s);
    EXPECT_EQ(preview.measured(D::LayerGap), before);
    expectShows(preview, LayoutSettings());
}
