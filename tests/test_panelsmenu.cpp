#include "curvy/panelsmenu.h"

#include "curvy/floatingpanels.h"

#include "testsupport.h"

using curvy::DockSide;
using curvy::PanelMenuEntry;

namespace {

    class PanelsMenuTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            curvytest::WorkspaceTest::SetUp();
            window().presentMenu = [this](const std::vector<PanelMenuEntry>& entries, const newui::Point&) { shown_ = entries; };
        }

        std::vector<PanelMenuEntry> shown_;   // what the last menu would have shown

        curvy::DockLayout& layout() { return window().panelLayout(); }

        static const PanelMenuEntry* line(const std::vector<PanelMenuEntry>& entries, const std::string& text) {
            for (const PanelMenuEntry& entry : entries) {
                if (entry.text == text) {
                    return &entry;
                }
            }
            return nullptr;
        }
    };
}

TEST_F(PanelsMenuTest, ThePanelsButtonListsEveryPanelWithATickOnTheShownOnes) {
    const auto entries = window().panelsMenuEntries(false);
    const PanelMenuEntry* document = line(entries, "Document");
    ASSERT_NE(nullptr, document);
    EXPECT_TRUE(document->checked);
    layout().setUserHidden("document", true);
    EXPECT_FALSE(line(window().panelsMenuEntries(false), "Document")->checked);
}

TEST_F(PanelsMenuTest, TheLastLineShowsOrHidesTheDock) {
    EXPECT_NE(nullptr, line(window().panelsMenuEntries(false), "Show panel dock"));
    EXPECT_NE(nullptr, line(window().panelsMenuEntries(true), "Hide panel dock"));
    EXPECT_TRUE(window().docks().collapsed(DockSide::Right));
    line(window().panelsMenuEntries(false), "Show panel dock")->run();
    EXPECT_FALSE(window().docks().collapsed(DockSide::Right));
}

TEST_F(PanelsMenuTest, ClickingAShownPanelHidesIt) {
    line(window().panelsMenuEntries(true), "Document")->run();
    EXPECT_TRUE(layout().find("document")->userHidden);
    EXPECT_FALSE(window().documentPanel().isVisible());
}

TEST_F(PanelsMenuTest, ClickingAHiddenPanelShowsItAndOpensTheDock) {
    layout().setUserHidden("document", true);
    ASSERT_TRUE(window().docks().collapsed(DockSide::Right));
    line(window().panelsMenuEntries(false), "Document")->run();
    EXPECT_FALSE(layout().find("document")->userHidden);
    EXPECT_TRUE(window().documentPanel().isVisible());
    EXPECT_FALSE(window().docks().collapsed(DockSide::Right));
}

TEST_F(PanelsMenuTest, AFloatingPanelShownFromTheMenuLeavesTheDockAlone) {
    layout().tearOff("document", 200.0f, 200.0f);
    layout().setUserHidden("document", true);
    EXPECT_FALSE(window().floatingPanels().showing("document"));
    line(window().panelsMenuEntries(false), "Document")->run();
    EXPECT_TRUE(window().floatingPanels().showing("document"));
    EXPECT_TRUE(window().docks().collapsed(DockSide::Right));
}

TEST_F(PanelsMenuTest, ThePanelsOwnMenuCollapsesAndExpands) {
    curvy::Panel& panel = window().documentPanel();
    auto entries = curvy::PanelsMenu::forPanel(layout(), panel, newui::Point(10.0f, 10.0f));
    ASSERT_NE(nullptr, line(entries, "Collapse"));
    line(entries, "Collapse")->run();
    EXPECT_TRUE(panel.collapsed());
    EXPECT_TRUE(layout().find("document")->collapsed);
    entries = curvy::PanelsMenu::forPanel(layout(), panel, newui::Point(10.0f, 10.0f));
    EXPECT_NE(nullptr, line(entries, "Expand"));
}

TEST_F(PanelsMenuTest, ThePanelsOwnMenuFloatsAndDocks) {
    curvy::Panel& panel = window().documentPanel();
    auto entries = curvy::PanelsMenu::forPanel(layout(), panel, newui::Point(300.0f, 200.0f));
    ASSERT_NE(nullptr, line(entries, "Float"));
    EXPECT_EQ(nullptr, line(entries, "Dock"));
    line(entries, "Float")->run();
    EXPECT_TRUE(layout().find("document")->floating);
    EXPECT_FLOAT_EQ(300.0f, layout().find("document")->floatX);
    EXPECT_TRUE(window().floatingPanels().showing("document"));
    entries = curvy::PanelsMenu::forPanel(layout(), panel, newui::Point(0.0f, 0.0f));
    ASSERT_NE(nullptr, line(entries, "Dock"));
    EXPECT_EQ(nullptr, line(entries, "Float"));
    line(entries, "Dock")->run();
    EXPECT_FALSE(layout().find("document")->floating);
}

TEST_F(PanelsMenuTest, ThePanelsOwnMenuHidesIt) {
    curvy::Panel& panel = window().documentPanel();
    line(curvy::PanelsMenu::forPanel(layout(), panel, newui::Point(0.0f, 0.0f)), "Hide")->run();
    EXPECT_TRUE(layout().find("document")->userHidden);
}

TEST_F(PanelsMenuTest, TheDocumentPanelsMenuButtonReachesTheWorkspace) {
    int menus = 0;
    window().rightDock().onPanelMenu += [&](curvy::PanelDock&, curvy::Panel&, const newui::Point&) {
        ++menus;
        return newui::SyncReturn::Handled;
    };
    window().documentPanel().menuButton().onClick(window().documentPanel().menuButton());
    EXPECT_EQ(1, menus);
    EXPECT_NE(nullptr, line(shown_, "Collapse"));   // and the workspace showed the panel's own menu
}

TEST_F(PanelsMenuTest, AFloatingPanelCannotCollapseOrBeResized) {
    curvy::Panel& panel = window().documentPanel();
    layout().tearOff("document", 200.0f, 200.0f);
    ASSERT_TRUE(window().floatingPanels().showing("document"));
    EXPECT_FALSE(panel.caret().isEnabled());
    EXPECT_FALSE(panel.resizeHandleVisible());
    const auto entries = curvy::PanelsMenu::forPanel(layout(), panel, newui::Point(10.0f, 10.0f));
    EXPECT_EQ(nullptr, line(entries, "Collapse"));
    EXPECT_EQ(nullptr, line(entries, "Expand"));
    layout().dock("document");
    EXPECT_TRUE(panel.caret().isEnabled());
    EXPECT_TRUE(panel.resizeHandleVisible());
}
