#include "curvy/panel.h"

#include "testsupport.h"

#include <newui/mouse_constants.h>

using newui::Point;

namespace {

    class PanelTest : public curvytest::WorkspaceTest {
    protected:
        curvy::Panel& makePanel() {
            auto* panel = new curvy::Panel("sample", "Sample");
            frame_.rootView().addChild(panel);   // the view tree owns it from here
            return *panel;
        }

        static void press(newui::ToolbarButton& button) {
            button.onClick(button);
        }
    };
}

TEST_F(PanelTest, StartsExpandedWithItsTitle) {
    curvy::Panel& panel = makePanel();
    EXPECT_EQ("sample", panel.id());
    EXPECT_EQ("Sample", panel.titleText());
    EXPECT_FALSE(panel.collapsed());
    EXPECT_TRUE(panel.body().isVisible());
    EXPECT_FLOAT_EQ(2.0f + 20.0f + curvy::Panel::kDefaultBodyHeight, panel.desiredHeight());
}

TEST_F(PanelTest, TheChromeFollowsThePanelsSize) {
    curvy::Panel& panel = makePanel();
    panel.setBodyHeight(380.0f);   // the root view's layout sizes the panel from its desired height, and gives it the width
    frame_.rootView().updateLayout();
    const newui::Rect own = panel.bounds();
    EXPECT_GT(own.width(), 300.0f);   // not the 268 the mockup is drawn at
    EXPECT_NEAR(own.width() - 2.0f * curvy::Panel::kBorder, panel.titleBar().bounds().width(), 1.0f);
    EXPECT_NEAR(own.width() - 2.0f * curvy::Panel::kBorder, panel.body().bounds().width(), 1.0f);
    EXPECT_NEAR(380.0f, panel.body().bounds().height(), 1.0f);
}

TEST_F(PanelTest, TheCaretCollapsesAndExpands) {
    curvy::Panel& panel = makePanel();
    int changes = 0;
    panel.onCollapsedChanged += [&](curvy::Panel&) {
        ++changes;
        return newui::SyncReturn::Handled;
    };
    const std::string open = panel.caretIcon();
    EXPECT_FALSE(open.empty());
    press(panel.caret());
    EXPECT_TRUE(panel.collapsed());
    EXPECT_FALSE(panel.body().isVisible());
    EXPECT_NE(open, panel.caretIcon());
    EXPECT_FLOAT_EQ(2.0f + 20.0f, panel.desiredHeight());
    press(panel.caret());
    EXPECT_FALSE(panel.collapsed());
    EXPECT_EQ(open, panel.caretIcon());
    EXPECT_EQ(2, changes);
    panel.setCollapsed(false);   // no change, no event
    EXPECT_EQ(2, changes);
}

TEST_F(PanelTest, TheBodyHeightSetsTheDesiredHeight) {
    curvy::Panel& panel = makePanel();
    panel.setBodyHeight(200.0f);
    EXPECT_FLOAT_EQ(222.0f, panel.desiredHeight());
    panel.setCollapsed(true);
    EXPECT_FLOAT_EQ(22.0f, panel.desiredHeight());
}

TEST_F(PanelTest, CloseAndMenuAreAnnounced) {
    curvy::Panel& panel = makePanel();
    int closed = 0, menus = 0;
    panel.onCloseRequested += [&](curvy::Panel&) {
        ++closed;
        return newui::SyncReturn::Handled;
    };
    panel.onMenuRequested += [&](curvy::Panel&, const Point&) {
        ++menus;
        return newui::SyncReturn::Handled;
    };
    press(panel.closeButton());
    press(panel.menuButton());
    EXPECT_EQ(1, closed);
    EXPECT_EQ(1, menus);
    EXPECT_FALSE(panel.collapsed());   // close does not collapse; the owner decides what it means
}

TEST_F(PanelTest, MenuAndCloseAreAlwaysVisible) {
    curvy::Panel& panel = makePanel();
    EXPECT_TRUE(panel.menuButton().isVisible());
    EXPECT_TRUE(panel.closeButton().isVisible());
}

TEST_F(PanelTest, DraggingTheTitleStartsPastTheThresholdAndEndsOnRelease) {
    curvy::Panel& panel = makePanel();
    std::vector<std::string> log;
    panel.onTitleDragStarted += [&](curvy::Panel&, const Point&) {
        log.push_back("start");
        return newui::SyncReturn::Handled;
    };
    panel.onTitleDragged += [&](curvy::Panel&, const Point& pt) {
        log.push_back("drag " + std::to_string(int(pt.x)) + "," + std::to_string(int(pt.y)));
        return newui::SyncReturn::Handled;
    };
    panel.onTitleDragEnded += [&](curvy::Panel&, const Point& pt) {
        log.push_back("end " + std::to_string(int(pt.x)) + "," + std::to_string(int(pt.y)));
        return newui::SyncReturn::Handled;
    };
    panel.pressTitle(Point(100.0f, 100.0f));
    panel.moveTitle(Point(101.0f, 101.0f), true);   // under the threshold: nothing yet
    EXPECT_TRUE(log.empty());
    EXPECT_FALSE(panel.dragging());
    panel.moveTitle(Point(110.0f, 100.0f), true);
    panel.moveTitle(Point(120.0f, 130.0f), true);
    panel.releaseTitle(Point(120.0f, 130.0f));
    ASSERT_EQ(4u, log.size());
    EXPECT_EQ("start", log[0]);
    EXPECT_EQ("drag 110,100", log[1]);
    EXPECT_EQ("drag 120,130", log[2]);
    EXPECT_EQ("end 120,130", log[3]);
    EXPECT_FALSE(panel.dragging());
}

TEST_F(PanelTest, AClickOnTheTitleIsNotADrag) {
    curvy::Panel& panel = makePanel();
    int events = 0;
    panel.onTitleDragStarted += [&](curvy::Panel&, const Point&) {
        ++events;
        return newui::SyncReturn::Handled;
    };
    panel.onTitleDragEnded += [&](curvy::Panel&, const Point&) {
        ++events;
        return newui::SyncReturn::Handled;
    };
    panel.pressTitle(Point(50.0f, 50.0f));
    panel.releaseTitle(Point(50.0f, 50.0f));
    EXPECT_EQ(0, events);
}

TEST_F(PanelTest, ALostReleaseEndsTheDrag) {
    curvy::Panel& panel = makePanel();
    int ended = 0;
    panel.onTitleDragEnded += [&](curvy::Panel&, const Point&) {
        ++ended;
        return newui::SyncReturn::Handled;
    };
    panel.pressTitle(Point(0.0f, 0.0f));
    panel.moveTitle(Point(20.0f, 0.0f), true);
    panel.moveTitle(Point(30.0f, 0.0f), false);   // the button is up: the release was missed
    EXPECT_EQ(1, ended);
    EXPECT_FALSE(panel.dragging());
}

TEST_F(PanelTest, AFixedPanelDoesNotDrag) {
    curvy::Panel& panel = makePanel();
    panel.setMovable(false);
    EXPECT_FALSE(panel.grip().isVisible());
    int events = 0;
    panel.onTitleDragStarted += [&](curvy::Panel&, const Point&) {
        ++events;
        return newui::SyncReturn::Handled;
    };
    panel.pressTitle(Point(0.0f, 0.0f));
    panel.moveTitle(Point(50.0f, 50.0f), true);
    EXPECT_EQ(0, events);
    EXPECT_FALSE(panel.dragging());
}

TEST_F(PanelTest, ANonCollapsiblePanelStaysExpandedAndDisablesItsCaret) {
    curvy::Panel& panel = makePanel();
    panel.setCollapsed(true);
    panel.setCollapsible(false);
    EXPECT_FALSE(panel.collapsed());
    EXPECT_FALSE(panel.caret().isEnabled());
    panel.setCollapsed(true);
    EXPECT_FALSE(panel.collapsed());
    panel.setCollapsible(true);
    EXPECT_TRUE(panel.caret().isEnabled());
}

TEST_F(PanelTest, DraggingTheResizeStripChangesTheBodyHeight) {
    curvy::Panel& panel = makePanel();
    const float start = panel.bodyHeight();
    int changes = 0;
    panel.onBodyHeightChanged += [&](curvy::Panel&) {
        ++changes;
        return newui::SyncReturn::Handled;
    };
    EXPECT_TRUE(panel.resizeHandleVisible());
    panel.pressResize(Point(50.0f, 200.0f));
    panel.moveResize(Point(50.0f, 260.0f), true);
    EXPECT_FLOAT_EQ(start + 60.0f, panel.bodyHeight());
    panel.moveResize(Point(50.0f, -500.0f), true);
    EXPECT_FLOAT_EQ(curvy::Panel::kMinBodyHeight, panel.bodyHeight());
    panel.releaseResize();
    panel.moveResize(Point(50.0f, 300.0f), true);
    EXPECT_FLOAT_EQ(curvy::Panel::kMinBodyHeight, panel.bodyHeight());
    EXPECT_EQ(2, changes);
}

TEST_F(PanelTest, TheResizeStripIsHiddenWhenCollapsedOrNotResizable) {
    curvy::Panel& panel = makePanel();
    panel.setCollapsed(true);
    EXPECT_FALSE(panel.resizeHandleVisible());
    panel.setCollapsed(false);
    EXPECT_TRUE(panel.resizeHandleVisible());
    panel.setResizable(false);
    EXPECT_FALSE(panel.resizeHandleVisible());
}

TEST_F(PanelTest, APressInTheTitleOrTheResizeStripAsksToBeActive) {
    curvy::Panel& panel = makePanel();
    int asked = 0;
    panel.onActivateRequested += [&](curvy::Panel&) {
        ++asked;
        return newui::SyncReturn::Handled;
    };
    panel.pressTitle(Point(5.0f, 5.0f));
    panel.pressResize(Point(5.0f, 100.0f));
    EXPECT_EQ(2, asked);
    EXPECT_TRUE(panel.resizing());
    panel.releaseResize();
    EXPECT_FALSE(panel.resizing());
}

TEST_F(PanelTest, ActiveIsToggledByTheOwner) {
    curvy::Panel& panel = makePanel();
    EXPECT_FALSE(panel.active());
    panel.setActive(true);
    EXPECT_TRUE(panel.active());
}
