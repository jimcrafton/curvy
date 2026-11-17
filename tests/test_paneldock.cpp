#include "curvy/floatingpanels.h"
#include "curvy/paneldock.h"

#include "testsupport.h"

using curvy::DockSide;
using newui::Point;

namespace {

    class PanelDockTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            curvytest::WorkspaceTest::SetUp();
            window().presentMenu = [](const std::vector<curvy::PanelMenuEntry>&, const Point&) {};   // the native popup would block
            layout().setUserHidden("node", true);   // these tests are about two panels: Document and the sample
            layout().setUserHidden("transform", true);
            layout().setUserHidden("assets", true);
            window().docks().setCollapsed(DockSide::Right, false);   // the dock must have a size for "outside it" to mean something
            frame_.rootView().updateLayout();
        }

        curvy::PanelDock& dock() { return window().rightDock(); }
        curvy::DockLayout& layout() { return window().panelLayout(); }

        // Adds a second panel under the Document panel.
        curvy::Panel& addSample() {
            curvy::PanelInfo info;
            info.id = "sample";
            info.title = "Sample";
            layout().add(info);
            auto* panel = new curvy::Panel("sample", "Sample");
            dock().addPanel(panel);
            return *panel;
        }

        // The screen point in the middle of the panel's title bar, `dy` lower.
        Point titlePoint(curvy::Panel& panel, float dy = 0.0f) {
            return panel.localToScreen(Point(60.0f, 10.0f + dy));
        }

        void drag(curvy::Panel& panel, const Point& to) {
            const Point from = titlePoint(panel);
            panel.pressTitle(from);
            panel.moveTitle(Point(from.x, from.y + 6.0f), true);
            panel.moveTitle(to, true);
        }
    };
}

TEST_F(PanelDockTest, PanelsStackInTheLayoutOrder) {
    curvy::Panel& sample = addSample();
    curvy::Panel& document = window().documentPanel();
    EXPECT_LT(document.bounds().top(), sample.bounds().top());
    EXPECT_FLOAT_EQ(document.bounds().top() + document.desiredHeight() + curvy::PanelDock::kSpacing, sample.bounds().top());
}

TEST_F(PanelDockTest, CollapsingAPanelPullsTheNextOneUp) {
    curvy::Panel& sample = addSample();
    curvy::Panel& document = window().documentPanel();
    const float before = sample.bounds().top();
    document.setCollapsed(true);
    EXPECT_TRUE(layout().find("document")->collapsed);
    EXPECT_LT(sample.bounds().top(), before);
}

TEST_F(PanelDockTest, CloseHidesTheDockedPanel) {
    curvy::Panel& sample = addSample();
    curvy::Panel& document = window().documentPanel();
    sample.closeButton().onClick(sample.closeButton());
    EXPECT_TRUE(layout().find("sample")->userHidden);
    EXPECT_FALSE(sample.isVisible());
    EXPECT_TRUE(document.isVisible());
}

TEST_F(PanelDockTest, DraggingATitleBelowTheNextPanelReordersThem) {
    curvy::Panel& sample = addSample();
    curvy::Panel& document = window().documentPanel();
    drag(document, titlePoint(sample, 120.0f));
    EXPECT_GE(dock().dropLineY(), 0.0f);
    document.releaseTitle(titlePoint(sample, 120.0f));
    EXPECT_LT(dock().dropLineY(), 0.0f);
    EXPECT_EQ((std::vector<std::string>{"sample", "document"}), layout().docked(DockSide::Right));
    EXPECT_LT(sample.bounds().top(), document.bounds().top());
}

TEST_F(PanelDockTest, DroppingOnItsOwnPlaceChangesNothing) {
    curvy::Panel& sample = addSample();
    curvy::Panel& document = window().documentPanel();
    drag(document, titlePoint(document, 4.0f));
    document.releaseTitle(titlePoint(document, 4.0f));
    EXPECT_EQ((std::vector<std::string>{"document", "sample"}), layout().docked(DockSide::Right));
    (void)sample;
}

TEST_F(PanelDockTest, TheMenuButtonIsPassedOn) {
    curvy::Panel& document = window().documentPanel();
    int menus = 0;
    dock().onPanelMenu += [&](curvy::PanelDock&, curvy::Panel& p, const Point&) {
        EXPECT_EQ(&document, &p);
        ++menus;
        return newui::SyncReturn::Handled;
    };
    document.menuButton().onClick(document.menuButton());
    EXPECT_EQ(1, menus);
}

TEST_F(PanelDockTest, DraggingFarOutsideTheDockTearsThePanelOff) {
    curvy::Panel& document = window().documentPanel();
    const Point inside = titlePoint(document);
    const Point outside(inside.x - 600.0f, inside.y);
    drag(document, outside);
    EXPECT_LT(dock().dropLineY(), 0.0f);   // no drop line while it would tear off
    document.releaseTitle(outside);
    EXPECT_TRUE(layout().find("document")->floating);
    ASSERT_NE(nullptr, window().floatingPanels().frame("document"));
    EXPECT_TRUE(window().floatingPanels().showing("document"));
    EXPECT_EQ(&window().floatingPanels().frame("document")->rootView(), document.parent());
    EXPECT_FALSE(document.movable());
}

TEST_F(PanelDockTest, ADragThatStaysCloseDoesNotTearOff) {
    curvy::Panel& document = window().documentPanel();
    const Point inside = titlePoint(document);
    const Point closeBy(inside.x - 10.0f, inside.y + 5.0f);
    drag(document, closeBy);
    document.releaseTitle(closeBy);
    EXPECT_FALSE(layout().find("document")->floating);
    EXPECT_EQ(&dock(), document.parent());
}

TEST_F(PanelDockTest, ClosingTheFloatingPanelDocksItBack) {
    curvy::Panel& document = window().documentPanel();
    layout().tearOff("document", 200.0f, 200.0f);
    ASSERT_EQ(window().floatingPanels().frame("document")->rootView().childViews().size(), 1u);
    document.closeButton().onClick(document.closeButton());
    EXPECT_FALSE(layout().find("document")->floating);
    EXPECT_FALSE(layout().find("document")->userHidden);
    EXPECT_EQ(&dock(), document.parent());
    EXPECT_TRUE(document.movable());
    EXPECT_TRUE(document.isVisible());
    EXPECT_FALSE(window().floatingPanels().showing("document"));
}

TEST_F(PanelDockTest, ClosingTheFrameDocksThePanelBack) {
    curvy::Panel& document = window().documentPanel();
    layout().tearOff("document", 200.0f, 200.0f);
    window().floatingPanels().frame("document")->onClosed(*window().floatingPanels().frame("document"));
    EXPECT_FALSE(layout().find("document")->floating);
    EXPECT_EQ(&dock(), document.parent());
}

TEST_F(PanelDockTest, ATornOffPanelCanFloatAgain) {
    curvy::Panel& document = window().documentPanel();
    layout().tearOff("document", 200.0f, 200.0f);
    layout().dock("document");
    layout().tearOff("document", 300.0f, 250.0f);
    EXPECT_TRUE(window().floatingPanels().showing("document"));
    EXPECT_EQ(&window().floatingPanels().frame("document")->rootView(), document.parent());
}

TEST_F(PanelDockTest, TheFloatingPanelFillsItsFrame) {
    curvy::Panel& document = window().documentPanel();
    layout().tearOff("document", 200.0f, 200.0f);
    const newui::Size size = window().floatingPanels().frame("document")->rootView().bounds().size();
    EXPECT_NEAR(size.width, document.bounds().width(), 1.0f);
    EXPECT_NEAR(size.height, document.bounds().height(), 1.0f);
}

TEST_F(PanelDockTest, PressingAPanelMakesItTheOnlyActiveOne) {
    EXPECT_EQ("", layout().activeId());
    dock().panel("document")->onActivateRequested(*dock().panel("document"));
    EXPECT_EQ("document", layout().activeId());
    EXPECT_TRUE(dock().panel("document")->active());
    layout().setActive("");
    EXPECT_FALSE(dock().panel("document")->active());
}

TEST_F(PanelDockTest, aPanelNobodyResizedSavesNoHeightSoItsContentStaysInCharge) {
    curvy::Panel& panel = *dock().panel("document");
    ASSERT_FALSE(panel.userResized());
    panel.setCollapsed(true);   // any change captures every panel's state
    const curvy::Settings::PanelState* state = nullptr;
    for (const auto& s : settings_.panels) {
        if (s.id == "document") {
            state = &s;
        }
    }
    ASSERT_NE(nullptr, state);
    EXPECT_FLOAT_EQ(0.0f, state->bodyHeight);
}

TEST_F(PanelDockTest, ResizingAPanelIsCapturedInTheSettings) {
    curvy::Panel& panel = *dock().panel("document");
    const float start = panel.bodyHeight();   // what the panel itself asked for
    panel.pressResize(newui::Point(5.0f, 100.0f));
    panel.moveResize(newui::Point(5.0f, 150.0f), true);
    panel.releaseResize();
    const curvy::Settings::PanelState* state = nullptr;
    for (const auto& s : settings_.panels) {
        if (s.id == "document") {
            state = &s;
        }
    }
    ASSERT_NE(nullptr, state);
    EXPECT_FLOAT_EQ(start + 50.0f, state->bodyHeight);
    panel.setCollapsed(true);
    for (const auto& s : settings_.panels) {
        if (s.id == "document") {
            EXPECT_TRUE(s.collapsed);
        }
    }
}

namespace {
    class SavedPanelsTest : public curvytest::WorkspaceTest {
    public:
        SavedPanelsTest() {
            curvy::Settings::PanelState saved;
            saved.id = "document";
            saved.collapsed = true;
            saved.floating = true;
            saved.floatX = 400.0f;
            saved.floatY = 300.0f;
            saved.bodyHeight = 210.0f;
            settings_.panels.push_back(saved);
            curvy::Settings::PanelState unknown;
            unknown.id = "gone";
            settings_.panels.push_back(unknown);
        }
    };
}

TEST_F(SavedPanelsTest, TheSavedStateIsAppliedAtStartup) {
    const curvy::PanelInfo* info = window().panelLayout().find("document");
    ASSERT_NE(nullptr, info);
    EXPECT_TRUE(info->floating);
    EXPECT_FLOAT_EQ(400.0f, info->floatX);
    curvy::Panel& panel = window().documentPanel();
    EXPECT_FALSE(panel.collapsed());   // a floating panel cannot stay collapsed
    EXPECT_FLOAT_EQ(210.0f, panel.bodyHeight());
    EXPECT_TRUE(window().floatingPanels().showing("document"));
}

TEST_F(PanelDockTest, DraggingAFloatingWindowOverTheDockOutlinesItAndDropsThePanelBack) {
    curvy::Panel& sample = addSample();
    layout().tearOff("document", 300.0f, 300.0f);
    ASSERT_TRUE(layout().find("document")->floating);
    const Point over = dock().localToScreen(Point(50.0f, dock().bounds().height() - 5.0f));
    const Point away = dock().localToScreen(Point(-400.0f, 50.0f));

    window().floatingPanels().hover("document", over);
    EXPECT_TRUE(dock().dropTargetShowing());
    EXPECT_GE(dock().dropLineY(), 0.0f);
    window().floatingPanels().hover("document", away);
    EXPECT_FALSE(dock().dropTargetShowing());

    window().floatingPanels().drop("document", away);
    EXPECT_TRUE(layout().find("document")->floating);   // let go elsewhere: it stays a window

    window().floatingPanels().hover("document", over);
    window().floatingPanels().drop("document", over);
    EXPECT_FALSE(layout().find("document")->floating);
    EXPECT_FALSE(dock().dropTargetShowing());
    EXPECT_EQ(&dock(), window().documentPanel().parent());
    EXPECT_EQ("document", layout().docked(DockSide::Right).back());   // below the sample panel
    (void)sample;
}

TEST_F(PanelDockTest, DroppingAboveThePanelsPutsTheFloatingPanelFirst) {
    addSample();
    layout().tearOff("document", 300.0f, 300.0f);
    const Point top = dock().localToScreen(Point(50.0f, 2.0f));
    window().floatingPanels().drop("document", top);
    EXPECT_EQ("document", layout().docked(DockSide::Right).front());
}

TEST_F(PanelDockTest, TheWheelScrollsWhenThePanelsAreTallerThanTheDock) {
    curvy::Panel& document = window().documentPanel();
    document.restoreBodyHeight(dock().bounds().height() + 300.0f);   // a height the user chose: the panel's own preferred height would otherwise replace it
    dock().sync();
    EXPECT_GT(dock().contentHeight(), dock().bounds().height());
    const float top = document.bounds().top();
    EXPECT_TRUE(dock().scrollBy(-1.0f));   // wheel towards the user: content moves up
    EXPECT_FLOAT_EQ(curvy::PanelDock::kWheelStep, dock().scrollOffset());
    EXPECT_FLOAT_EQ(top - curvy::PanelDock::kWheelStep, document.bounds().top());
    EXPECT_TRUE(dock().positionHintShowing());
    dock().scrollBy(-1000.0f);
    EXPECT_FLOAT_EQ(dock().contentHeight() - dock().bounds().height(), dock().scrollOffset());   // stops at the end
    dock().scrollBy(1000.0f);
    EXPECT_FLOAT_EQ(0.0f, dock().scrollOffset());
}

TEST_F(PanelDockTest, WhenEverythingFitsTheWheelDoesNothing) {
    EXPECT_FALSE(dock().scrollBy(-1.0f));
    EXPECT_FLOAT_EQ(0.0f, dock().scrollOffset());
    EXPECT_FALSE(dock().positionHintShowing());
}

TEST_F(PanelDockTest, ShrinkingThePanelsBringsTheScrollBack) {
    curvy::Panel& document = window().documentPanel();
    document.setBodyHeight(dock().bounds().height() + 300.0f);
    dock().sync();
    dock().scrollBy(-1000.0f);
    document.setBodyHeight(40.0f);
    dock().sync();
    EXPECT_FLOAT_EQ(0.0f, dock().scrollOffset());
}
