#include "curvy/dockcontroller.h"

#include "testsupport.h"

#include <newui/mouse_constants.h>

#include <filesystem>

using curvy::DockSide;
using curvy::Settings;

namespace {

    class DockTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            store_ = curvytest::tempFile("curvy_test_dock.json5");
            std::filesystem::remove(store_);
            window().setSettingsPath(store_.u8string());   // the only file a test writes to
        }

        void TearDown() override {
            std::filesystem::remove(store_);
            WorkspaceTest::TearDown();
        }

        curvy::DockController& docks() { return window().docks(); }
        float canvasWidth() { return window().canvas().bounds().size().width; }
        float canvasHeight() { return window().canvas().bounds().size().height; }
        void layout() { frame_.rootView().updateLayout(); }

        // Press on a splitter's divider, drag it along the given positions (measured along the splitter), and let go.
        void drag(DockSide side, std::initializer_list<float> path) {
            newui::Splitter* splitter = docks().splitter(side);
            const bool horizontal = side == DockSide::Left || side == DockSide::Right;
            auto at = [horizontal](float along) { return horizontal ? newui::Point(along, 100.0f) : newui::Point(100.0f, along); };
            splitter->onMouseDown(*splitter, at(splitter->splitPosition() + 2.0f), newui::mbmLeftButton, 0);
            for (float along : path) {
                splitter->onMouseMove(*splitter, at(along), newui::mbmLeftButton, 0);
                layout();
            }
            splitter->onMouseUp(*splitter, at(path.end()[-1]), newui::mbmLeftButton, 0);
            layout();
        }

        std::filesystem::path store_;
    };
}

TEST_F(DockTest, EveryAreaStartsHidden) {
    for (DockSide side : {DockSide::Top, DockSide::Left, DockSide::Right, DockSide::Bottom}) {
        ASSERT_NE(nullptr, docks().splitter(side));
        EXPECT_TRUE(docks().collapsed(side));
        EXPECT_FALSE(docks().area(side)->isVisible());
    }
}

TEST_F(DockTest, ShowingAnAreaGivesItsSizeAndTheCanvasTakesTheRest) {
    const float full = canvasWidth();
    docks().toggle(DockSide::Left);
    layout();
    EXPECT_FALSE(docks().collapsed(DockSide::Left));
    EXPECT_TRUE(docks().area(DockSide::Left)->isVisible());
    EXPECT_EQ(220.0f, docks().splitter(DockSide::Left)->splitPosition());
    EXPECT_EQ(full - 220.0f, canvasWidth());
}

TEST_F(DockTest, ADoubleClickOnTheSplitterHidesAndRestoresTheArea) {
    const float full = canvasWidth();
    docks().toggle(DockSide::Left);
    layout();
    newui::Splitter* left = docks().splitter(DockSide::Left);
    left->onMouseDblClick(*left, newui::Point(left->splitPosition() + 2.0f, 100.0f), newui::mbmLeftButton, 0);
    layout();
    EXPECT_TRUE(docks().collapsed(DockSide::Left));
    EXPECT_FALSE(docks().area(DockSide::Left)->isVisible());
    EXPECT_EQ(full, canvasWidth());
    left->onMouseDblClick(*left, newui::Point(2.0f, 100.0f), newui::mbmLeftButton, 0);
    layout();
    EXPECT_FALSE(docks().collapsed(DockSide::Left));
    EXPECT_EQ(220.0f, left->splitPosition()) << "it comes back at the same size";
}

TEST_F(DockTest, DraggingResizesAnAreaAndTheSizeComesBackAfterHiding) {
    docks().toggle(DockSide::Left);
    layout();
    drag(DockSide::Left, {250.0f, 303.0f});   // the divider centres on the cursor: 6 thick, so its near edge ends at 300
    EXPECT_EQ(300.0f, docks().size(DockSide::Left));
    docks().toggle(DockSide::Left);
    docks().toggle(DockSide::Left);
    EXPECT_EQ(300.0f, docks().splitter(DockSide::Left)->splitPosition());
}

TEST_F(DockTest, DraggingTheTopOrLeftSplitterFarPastTheEdgeStopsAtTheMinimum) {
    for (DockSide side : {DockSide::Top, DockSide::Left}) {
        docks().setCollapsed(side, false);
        layout();
        const float grab = docks().splitter(side)->splitPosition();
        drag(side, {grab - 50.0f, 20.0f, -5.0f, -500.0f});
        EXPECT_FALSE(docks().collapsed(side));
        EXPECT_TRUE(docks().area(side)->isVisible());
        EXPECT_EQ(Settings::minDockSize, docks().splitter(side)->splitPosition());
        EXPECT_EQ(Settings::minDockSize, docks().size(side));
    }
    EXPECT_GT(canvasWidth(), 0.0f);
    EXPECT_GT(canvasHeight(), 0.0f);
}

TEST_F(DockTest, DraggingTheOtherWayLeavesTheCanvasRoom) {
    for (DockSide side : {DockSide::Top, DockSide::Left}) {
        docks().setCollapsed(side, false);
        layout();
        docks().splitter(side)->setSplitPosition(100000.0f);
        layout();
        EXPECT_GE(canvasWidth(), 50.0f);
        EXPECT_GE(canvasHeight(), 50.0f);
        docks().setSize(side, 300.0f);
    }
}

TEST_F(DockTest, DraggingAHiddenAreasSplitterOpensIt) {
    ASSERT_TRUE(docks().collapsed(DockSide::Left));
    drag(DockSide::Left, {40.0f, 150.0f});
    EXPECT_FALSE(docks().collapsed(DockSide::Left));
    EXPECT_TRUE(docks().area(DockSide::Left)->isVisible()) << "the area is shown again, not just made room for";
    EXPECT_GE(docks().area(DockSide::Left)->bounds().size().width, Settings::minDockSize);
}

TEST_F(DockTest, TheViewRingShowsTheRightArea) {
    window().runCommand("view.properties");
    layout();
    EXPECT_FALSE(docks().collapsed(DockSide::Right));
    EXPECT_EQ(268.0f, docks().splitter(DockSide::Right)->splitPosition());
    EXPECT_EQ(docks().area(DockSide::Right), docks().splitter(DockSide::Right)->childViews()[1]);
    EXPECT_EQ(268.0f, docks().area(DockSide::Right)->bounds().size().width);
}

TEST_F(DockTest, SizesAndHiddenFlagsAreSavedAndReadBack) {
    docks().toggle(DockSide::Left);
    docks().setSize(DockSide::Left, 300.0f);
    docks().toggle(DockSide::Right);
    const Settings back = Settings::load(store_.u8string());
    EXPECT_EQ(300.0f, back.docks[1].size);
    EXPECT_FALSE(back.docks[1].collapsed);
    EXPECT_FALSE(back.docks[2].collapsed);
    EXPECT_TRUE(back.docks[0].collapsed);
    EXPECT_TRUE(back.docks[3].collapsed);
}

TEST_F(DockTest, SwitchingTheThemeIsSaved) {
    window().runCommand("view.theme");
    EXPECT_EQ(Settings::ThemeChoice::Light, Settings::load(store_.u8string()).theme);
    window().runCommand("view.theme");
    EXPECT_EQ(Settings::ThemeChoice::Dark, Settings::load(store_.u8string()).theme);
}
