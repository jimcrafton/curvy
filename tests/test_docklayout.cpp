#include "curvy/docklayout.h"

#include <gtest/gtest.h>

#include <stdexcept>

using curvy::DockBox;
using curvy::DockLayout;
using curvy::DockSide;
using curvy::PanelInfo;

namespace {

    PanelInfo panel(const char* id, DockSide side = DockSide::Right, bool canTearOff = true) {
        PanelInfo info;
        info.id = id;
        info.title = id;
        info.side = side;
        info.canTearOff = canTearOff;
        return info;
    }

    // The right dock holds a, b, c, d; the toolbar and the timeline are fixed in their docks.
    struct Layout {
        Layout() {
            for (const char* id : {"a", "b", "c", "d"}) {
                dock.add(panel(id));
            }
            dock.add(panel("tools", DockSide::Left, false));
            dock.add(panel("timeline", DockSide::Bottom, false));
            dock.onChanged = [this]() { ++changes; };
        }
        std::vector<std::string> right() const { return dock.docked(DockSide::Right); }

        DockLayout dock;
        int changes = 0;
    };

    using Ids = std::vector<std::string>;

}

TEST(DockLayout, panelsDockInTheOrderTheyWereAddedPerDock) {
    Layout f;
    EXPECT_EQ(f.right(), (Ids{"a", "b", "c", "d"}));
    EXPECT_EQ(f.dock.docked(DockSide::Left), (Ids{"tools"}));
    EXPECT_EQ(f.dock.docked(DockSide::Bottom), (Ids{"timeline"}));
    EXPECT_TRUE(f.dock.docked(DockSide::Top).empty());
    EXPECT_TRUE(f.dock.floating().empty());
}

TEST(DockLayout, aPanelNeedsAUniqueId) {
    Layout f;
    EXPECT_THROW(f.dock.add(panel("")), std::runtime_error);
    EXPECT_THROW(f.dock.add(panel("a")), std::runtime_error);
    EXPECT_EQ(f.dock.panels().size(), 6u);
}

TEST(DockLayout, movingReordersWithinTheDock) {
    Layout f;
    f.dock.move("d", 0);
    EXPECT_EQ(f.right(), (Ids{"d", "a", "b", "c"}));
    f.dock.move("d", 2);   // among the others (a, b, c): after b
    EXPECT_EQ(f.right(), (Ids{"a", "b", "d", "c"}));
    f.dock.move("a", DockLayout::kEnd);
    EXPECT_EQ(f.right(), (Ids{"b", "d", "c", "a"}));
    f.dock.move("c", 99);
    EXPECT_EQ(f.right(), (Ids{"b", "d", "a", "c"}));
}

TEST(DockLayout, movingToWhereItIsChangesNothingAndSaysNothing) {
    Layout f;
    f.dock.move("b", 1);
    EXPECT_EQ(f.right(), (Ids{"a", "b", "c", "d"}));
    EXPECT_EQ(f.changes, 0);
    f.dock.move("b", 0);
    EXPECT_EQ(f.changes, 1);
}

TEST(DockLayout, aFixedPanelCannotBeMovedOrTornOff) {
    Layout f;
    EXPECT_THROW(f.dock.move("tools", 0), std::runtime_error);
    EXPECT_THROW(f.dock.tearOff("timeline", 10.0f, 10.0f), std::runtime_error);
    EXPECT_EQ(f.dock.docked(DockSide::Left), (Ids{"tools"}));
    DockBox box{900.0f, 50.0f, 1200.0f, 600.0f};
    EXPECT_FALSE(f.dock.shouldTearOff("tools", box, 100.0f, 300.0f)) << "far outside, but it cannot leave";
    EXPECT_EQ(f.changes, 0);
}

TEST(DockLayout, tearingOffMakesItFloatAndTakesItOutOfTheDock) {
    Layout f;
    f.dock.tearOff("b", 300.0f, 120.0f);
    EXPECT_EQ(f.right(), (Ids{"a", "c", "d"}));
    EXPECT_EQ(f.dock.floating(), (Ids{"b"}));
    ASSERT_NE(f.dock.find("b"), nullptr);
    EXPECT_TRUE(f.dock.find("b")->floating);
    EXPECT_FLOAT_EQ(f.dock.find("b")->floatX, 300.0f);
    EXPECT_FLOAT_EQ(f.dock.find("b")->floatY, 120.0f);
    EXPECT_THROW(f.dock.move("b", 0), std::runtime_error) << "a floating panel is docked before it is moved";
}

TEST(DockLayout, aFloatingPanelFollowsTheFrame) {
    Layout f;
    f.dock.tearOff("b", 300.0f, 120.0f);
    f.changes = 0;
    f.dock.moveFloating("b", 310.0f, 130.0f);
    EXPECT_FLOAT_EQ(f.dock.find("b")->floatX, 310.0f);
    EXPECT_EQ(f.changes, 1);
    f.dock.moveFloating("b", 310.0f, 130.0f);
    EXPECT_EQ(f.changes, 1);
    EXPECT_THROW(f.dock.moveFloating("a", 1.0f, 1.0f), std::runtime_error);
}

TEST(DockLayout, dockingPutsItBackWhereItWasDropped) {
    Layout f;
    f.dock.tearOff("b", 300.0f, 120.0f);
    f.dock.dock("b", 2);
    EXPECT_EQ(f.right(), (Ids{"a", "c", "b", "d"}));
    EXPECT_TRUE(f.dock.floating().empty());
    EXPECT_THROW(f.dock.dock("b"), std::runtime_error) << "already docked";
}

TEST(DockLayout, closingAFrameDocksItAtTheBottom) {
    Layout f;
    f.dock.tearOff("a", 300.0f, 120.0f);
    f.dock.dock("a");
    EXPECT_EQ(f.right(), (Ids{"b", "c", "d", "a"}));
}

TEST(DockLayout, aHiddenPanelKeepsItsPlaceInTheOrder) {
    Layout f;
    f.dock.setUserHidden("b", true);
    EXPECT_EQ(f.right(), (Ids{"a", "c", "d"}));
    EXPECT_FALSE(f.dock.shown("b"));
    f.dock.move("d", 1);   // among the shown others (a, c): between them
    EXPECT_EQ(f.right(), (Ids{"a", "d", "c"}));
    f.dock.setUserHidden("b", false);
    EXPECT_EQ(f.right(), (Ids{"a", "b", "d", "c"})) << "b was between a and the rest, and comes back there";
}

TEST(DockLayout, hidingAFloatingPanelHidesItsFrame) {
    Layout f;
    f.dock.tearOff("c", 300.0f, 120.0f);
    f.dock.setUserHidden("c", true);
    EXPECT_TRUE(f.dock.floating().empty());
    f.dock.setUserHidden("c", false);
    EXPECT_EQ(f.dock.floating(), (Ids{"c"})) << "it comes back floating";
}

TEST(DockLayout, aPanelIsShownOnlyWhenTheUserAndTheContextBothAllowIt) {
    Layout f;
    f.dock.setContextVisible("c", false);
    EXPECT_FALSE(f.dock.shown("c"));
    f.dock.setUserHidden("c", true);
    f.dock.setContextVisible("c", true);
    EXPECT_FALSE(f.dock.shown("c")) << "the selection coming back does not undo the user's Hide";
    f.dock.setUserHidden("c", false);
    EXPECT_TRUE(f.dock.shown("c"));
    EXPECT_EQ(f.right(), (Ids{"a", "b", "c", "d"}));
}

TEST(DockLayout, collapsingDoesNotMoveOrHideAPanel) {
    Layout f;
    f.dock.setCollapsed("b", true);
    EXPECT_TRUE(f.dock.find("b")->collapsed);
    EXPECT_EQ(f.right(), (Ids{"a", "b", "c", "d"}));
    f.changes = 0;
    f.dock.setCollapsed("b", true);
    EXPECT_EQ(f.changes, 0);
}

TEST(DockLayout, aPanelIsTornOffOnlyBeyondTheDistanceOutsideItsDock) {
    Layout f;
    const DockBox box{900.0f, 50.0f, 1200.0f, 600.0f};
    EXPECT_FALSE(f.dock.shouldTearOff("b", box, 1000.0f, 300.0f)) << "inside";
    EXPECT_FALSE(f.dock.shouldTearOff("b", box, 870.0f, 300.0f)) << "30 outside";
    EXPECT_TRUE(f.dock.shouldTearOff("b", box, 860.0f, 300.0f)) << "40 outside";
    EXPECT_TRUE(f.dock.shouldTearOff("b", box, 1000.0f, 700.0f)) << "100 below";
    f.dock.tearOff("b", 0.0f, 0.0f);
    EXPECT_FALSE(f.dock.shouldTearOff("b", box, 100.0f, 300.0f)) << "already floating";
    EXPECT_FALSE(f.dock.shouldTearOff("nothing", box, 100.0f, 300.0f));
}

TEST(DockLayout, theDropIndexCountsThePanelsAboveThePointer) {
    const std::vector<float> middles = {40.0f, 120.0f, 200.0f};
    EXPECT_EQ(DockLayout::dropIndex(middles, 10.0f), 0u);
    EXPECT_EQ(DockLayout::dropIndex(middles, 100.0f), 1u);
    EXPECT_EQ(DockLayout::dropIndex(middles, 150.0f), 2u);
    EXPECT_EQ(DockLayout::dropIndex(middles, 500.0f), 3u);
    EXPECT_EQ(DockLayout::dropIndex({}, 50.0f), 0u);
}

TEST(DockLayout, panelsOfDifferentDocksDoNotMixWhenOneMoves) {
    Layout f;
    f.dock.add(panel("e", DockSide::Left));
    f.dock.move("e", 0);
    EXPECT_EQ(f.dock.docked(DockSide::Left), (Ids{"e", "tools"}));
    EXPECT_EQ(f.right(), (Ids{"a", "b", "c", "d"}));
    EXPECT_EQ(f.dock.docked(DockSide::Bottom), (Ids{"timeline"}));
}

TEST(DockLayout, aDockWithNothingShownStillKeepsItsHiddenOrder) {
    Layout f;
    for (const char* id : {"a", "b", "c", "d"}) {
        f.dock.setUserHidden(id, true);
    }
    EXPECT_TRUE(f.right().empty());
    f.dock.setUserHidden("c", false);
    f.dock.setUserHidden("a", false);
    EXPECT_EQ(f.right(), (Ids{"a", "c"}));
}

TEST(DockLayout, unknownPanelsAreRefused) {
    Layout f;
    EXPECT_THROW(f.dock.move("zzz", 0), std::runtime_error);
    EXPECT_THROW(f.dock.tearOff("zzz", 0.0f, 0.0f), std::runtime_error);
    EXPECT_THROW(f.dock.setUserHidden("zzz", true), std::runtime_error);
    EXPECT_FALSE(f.dock.shown("zzz"));
    EXPECT_EQ(f.dock.find("zzz"), nullptr);
}
