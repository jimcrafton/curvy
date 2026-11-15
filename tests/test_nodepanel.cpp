#include "testsupport.h"

#include <newui/mouse_constants.h>
#include <newui/pen.h>

namespace {

    class NodePanelTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Node", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
            canvas().zoomToFit(1.0f);
            layer_ = window().document().addLayer();
            look_ = window().document().addCircle(layer_, 100.0f, 100.0f, 30.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
        }

        curvy::CanvasView& canvas() { return window().canvas(); }
        bool click(double x, double y) {
            const BLPoint p = canvas().toView(BLPoint(x, y));
            return window().pressed(pen_, newui::Point(float(p.x), float(p.y)), newui::mbmLeftButton, 0);
        }
        std::size_t rowOf(const std::string& caption) {
            for (std::size_t i = 0; i < window().nodePanel().paramCount(); ++i) {
                if (window().nodePanel().paramCaption(i) == caption) {
                    return i;
                }
            }
            return ~std::size_t(0);
        }

        newui::PenState pen_;
        curvy::NodeRef layer_, look_;
    };

}

TEST_F(NodePanelTest, theNodePanelIsInTheRightDockAndNotInTheGraphView) {
    curvy::Panel& panel = window().nodePanel();
    EXPECT_EQ(panel.id(), "node");
    EXPECT_EQ(panel.title(), "Node");
    ASSERT_NE(window().rightDock().panel("node"), nullptr);
    EXPECT_EQ(window().rightDock().panel("node"), &panel);
    EXPECT_NE(window().panelLayout().find("node"), nullptr);
    EXPECT_EQ(window().graph().findView("graphNameField"), nullptr);
    EXPECT_NE(panel.findView("graphNameField"), nullptr);
    EXPECT_NE(panel.findView("graphDataButton"), nullptr);
}

TEST_F(NodePanelTest, nothingIsShownUntilSomethingIsSelected) {
    EXPECT_EQ(window().nodePanel().title(), "Nothing selected");
    EXPECT_EQ(window().nodePanel().paramCount(), 0u);
}

TEST_F(NodePanelTest, clickingAShapeOnTheCanvasShowsItsNodeWithTheGraphHidden) {
    ASSERT_FALSE(window().graphShown());
    ASSERT_TRUE(click(100.0, 100.0));
    EXPECT_EQ(window().nodePanel().kind(), "circle");
    EXPECT_EQ(window().nodePanel().title(), window().document().node(window().graph().selected()).name());
    EXPECT_NE(rowOf("radius"), ~std::size_t(0));

    EXPECT_TRUE(click(600.0, 700.0));   // empty canvas clears the selection
    EXPECT_EQ(window().nodePanel().title(), "Nothing selected");
}

TEST_F(NodePanelTest, aValueTypedIntoThePanelEditsTheNodeAsOneUndoStep) {
    ASSERT_TRUE(click(100.0, 100.0));
    const curvy::NodeRef circle = window().graph().selected();
    const std::size_t radius = rowOf("radius");
    ASSERT_NE(radius, ~std::size_t(0));
    window().nodePanel().setParam(radius, "12");
    EXPECT_EQ(window().document().node(circle).getFloat("radius"), 12.0f);
    window().document().undoStack().undo();
    EXPECT_EQ(window().document().node(circle).getFloat("radius"), 30.0f);
}

TEST_F(NodePanelTest, selectingInTheGraphViewShowsTheSameNode) {
    window().toggleGraph();
    window().graph().select(layer_);
    EXPECT_EQ(window().nodePanel().title(), window().document().node(layer_).name());
    EXPECT_EQ(window().nodePanel().kind(), window().document().node(layer_).typeName());
}

TEST_F(NodePanelTest, theDockOpensWhenTheGraphIsShown) {
    ASSERT_TRUE(window().docks().collapsed(curvy::DockSide::Right));
    window().toggleGraph();
    EXPECT_TRUE(window().graphShown());
    EXPECT_FALSE(window().docks().collapsed(curvy::DockSide::Right));
}

TEST_F(NodePanelTest, theDockOpensWhenAClickSelectsAShape) {
    ASSERT_TRUE(window().docks().collapsed(curvy::DockSide::Right));
    EXPECT_FALSE(click(600.0, 700.0));   // empty canvas selects nothing and leaves the dock alone
    EXPECT_TRUE(window().docks().collapsed(curvy::DockSide::Right));
    ASSERT_TRUE(click(100.0, 100.0));
    EXPECT_FALSE(window().docks().collapsed(curvy::DockSide::Right));
}

TEST_F(NodePanelTest, aDockTheUserClosedStaysClosedUntilSomethingNeedsIt) {
    window().docks().setCollapsed(curvy::DockSide::Right, false);
    window().docks().setCollapsed(curvy::DockSide::Right, true);
    EXPECT_TRUE(window().docks().collapsed(curvy::DockSide::Right));
    window().toggleGraph();
    window().toggleGraph();   // hiding the graph does not close the dock again
    EXPECT_FALSE(window().docks().collapsed(curvy::DockSide::Right));
}

TEST_F(NodePanelTest, thePanelCanBeCollapsedLikeAnyOther) {
    curvy::Panel& panel = window().nodePanel();
    const float open = panel.desiredHeight();
    panel.setCollapsed(true);
    EXPECT_LT(panel.desiredHeight(), open);
}
