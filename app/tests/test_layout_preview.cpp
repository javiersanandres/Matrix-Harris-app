#include <gtest/gtest.h>

#include "LayoutPreview.h"
#include "NodeVisuals.h"
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

// A Hogar's label goes in the larger part: the white one on top, or the ashes
// once they take more than half of the box; their default comes from the shape.
TEST(HogarAshes, LabelGoesInTheLargerPart) {
    ensureApplication();
    namespace nv = ui::node_visuals;
    NodeAttributes a("Hogar");
    a.fire = FireState::FireWithAshes;
    const QRectF box(0, 0, 100, 100);

    a.ashes = 0.25;
    EXPECT_LE(nv::labelRect(a, box).bottom(), nv::ashesLineY(box, 0.25));
    a.ashes = 0.7;
    EXPECT_GE(nv::labelRect(a, box).top(), nv::ashesLineY(box, 0.7));

    // Without its own, the shape's default decides.
    a.ashes.reset();
    BoxSizes sizes;
    sizes.rectangle.ashes = 0.8;
    EXPECT_GE(nv::labelRect(a, box, &sizes).top(), nv::ashesLineY(box, 0.8));
}

// The line between the parts is found and moved in whole hundredths, within limits.
TEST(HogarAshes, LineIsMeasuredWithinItsLimits) {
    namespace nv = ui::node_visuals;
    const QRectF box(0, 0, 100, 200);
    EXPECT_DOUBLE_EQ(nv::ashesLineY(box, 0.25), 150.0);
    EXPECT_DOUBLE_EQ(nv::ashesShareAt(box, 100.0), 0.5);
    EXPECT_DOUBLE_EQ(nv::ashesShareAt(box, 101.0), 0.5); // whole hundredths
    EXPECT_DOUBLE_EQ(nv::ashesShareAt(box, -50.0), MAX_ASHES_SHARE);
    EXPECT_DOUBLE_EQ(nv::ashesShareAt(box, 500.0), MIN_ASHES_SHARE);
    EXPECT_TRUE(nv::onAshesLine(box, 0.25, QPointF(50.0, 153.0), 7.0));
    EXPECT_FALSE(nv::onAshesLine(box, 0.25, QPointF(50.0, 140.0), 7.0));
    EXPECT_FALSE(nv::onAshesLine(box, 0.25, QPointF(2.0, 150.0), 7.0)); // the side's own handle
}

// The sample's boxes take the default size of rectangles, and the distances
// it marks stay the ones set around them.
TEST(LayoutPreview, FollowsTheDefaultBoxSizes) {
    ensureApplication();
    LayoutPreview preview;
    LayoutSettings s;
    s.box_sizes.rectangle = BoxSize{ 220.0, 140.0 };
    preview.setSettings(s);
    expectShows(preview, s);
}
