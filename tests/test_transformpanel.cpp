#include "testsupport.h"

#include <newui/mouse_constants.h>
#include <newui/pen.h>

namespace {

    using Part = curvy::TransformPanel::Part;

    class TransformPanelTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Transform", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
            canvas().zoomToFit(1.0f);
            layer_ = window().document().addLayer();
            look_ = window().document().addCircle(layer_, 100.0f, 100.0f, 30.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
        }

        curvy::CanvasView& canvas() { return window().canvas(); }
        curvy::TransformPanel& panel() { return window().transformPanel(); }
        curvy::SelectionModel& selection() { return window().graph().selectionModel(); }
        bool click(double x, double y) {
            const BLPoint p = canvas().toView(BLPoint(x, y));
            return window().pressed(pen_, newui::Point(float(p.x), float(p.y)), newui::mbmLeftButton, 0);
        }
        // The first point of what the shape cooks to, to see where it ended up.
        float firstX() {
            const jitnodes::GeometryHandle geometry = window().document().artwork();
            return geometry->x()[0];
        }

        newui::PenState pen_;
        curvy::NodeRef layer_, look_;
    };

}

TEST_F(TransformPanelTest, thePanelIsInTheRightDock) {
    EXPECT_EQ(panel().id(), "transform");
    EXPECT_EQ(panel().title(), "Transform");
    EXPECT_EQ(window().rightDock().panel("transform"), &panel());
    EXPECT_NE(window().panelLayout().find("transform"), nullptr);
}

TEST_F(TransformPanelTest, nothingIsShownUntilSomethingIsSelected) {
    EXPECT_FALSE(panel().hasNode(Part::Object));
    EXPECT_FALSE(panel().hasNode(Part::Local));
    EXPECT_EQ(panel().note(Part::Object), "Nothing selected");
    EXPECT_FALSE(panel().addButtonShown());
}

TEST_F(TransformPanelTest, aSelectedShapeShowsItsObjectTransformAndOffersALocalOne) {
    ASSERT_TRUE(click(100.0, 100.0));
    EXPECT_TRUE(panel().hasNode(Part::Object)) << "the layer around the shape";
    EXPECT_EQ(panel().value(Part::Object, "sx"), "1");
    EXPECT_EQ(panel().value(Part::Object, "tx"), "0");
    EXPECT_FALSE(panel().hasNode(Part::Local));
    EXPECT_EQ(panel().note(Part::Local), "This shape has no local transform");
    EXPECT_TRUE(panel().addButtonShown());
}

TEST_F(TransformPanelTest, theTabsNameWhichTransformTheyEdit) {
    ASSERT_TRUE(click(100.0, 100.0));
    EXPECT_EQ(panel().note(Part::Object), "Object transform: objectXform") << "the transform of the whole layer, not of the shape";
    ASSERT_TRUE(panel().addLocalTransform());
    EXPECT_EQ(panel().note(Part::Local), "Local transform: localXform");
}

TEST_F(TransformPanelTest, aTypedValueMovesTheObjectAsOneUndoStep) {
    ASSERT_TRUE(click(100.0, 100.0));
    const float before = firstX();
    panel().setValue(Part::Object, "tx", "25");
    EXPECT_EQ(panel().value(Part::Object, "tx"), "25");
    EXPECT_NEAR(firstX(), before + 25.0f, 1e-3f);
    window().document().undoStack().undo();
    panel().refresh();
    EXPECT_EQ(panel().value(Part::Object, "tx"), "0");
    EXPECT_NEAR(firstX(), before, 1e-3f);
}

TEST_F(TransformPanelTest, textThatIsNotANumberIsRefusedAndChangesNothing) {
    ASSERT_TRUE(click(100.0, 100.0));
    std::string refused;
    panel().onRefused = [&](const std::string& message) { refused = message; };
    panel().setValue(Part::Object, "tx", "abc");
    EXPECT_EQ(refused, "'abc' is not a number");
    EXPECT_EQ(panel().value(Part::Object, "tx"), "0");
}

TEST_F(TransformPanelTest, addingALocalTransformSitsBetweenTheShapeAndItsLookAndIsOneUndoStep) {
    ASSERT_TRUE(click(100.0, 100.0));
    ASSERT_TRUE(panel().addLocalTransform());
    EXPECT_TRUE(panel().hasNode(Part::Local));
    EXPECT_FALSE(panel().addButtonShown());
    EXPECT_FALSE(panel().addLocalTransform()) << "a shape has one";
    EXPECT_EQ(selection().localTransform(), selection().find(curvy::SelectionModel::Role::LocalTransform));

    const float before = firstX();
    panel().setValue(Part::Local, "tx", "40");
    EXPECT_NEAR(firstX(), before + 40.0f, 1e-3f) << "the shape is moved by its own transform, so the fill still follows it";

    window().document().undoStack().undo();   // the move
    window().document().undoStack().undo();   // the add
    panel().refresh();
    EXPECT_FALSE(panel().hasNode(Part::Local));
    EXPECT_TRUE(panel().addButtonShown());
    EXPECT_NEAR(firstX(), before, 1e-3f);
}

TEST_F(TransformPanelTest, thePanelAsksForTheHeightItsRowsNeed) {
    EXPECT_FALSE(panel().userResized());
    EXPECT_GE(panel().bodyHeight(), curvy::Panel::kTabStripHeight + 8 * 24.0f) << "the strip and the eight rows fit without scrolling";
}

TEST_F(TransformPanelTest, eachPageScrollsInsteadOfClippingAndIsLaidOutWhenItsTabIsChosen) {
    window().docks().setCollapsed(curvy::DockSide::Right, false);
    frame_.rootView().updateLayout();
    ASSERT_TRUE(click(100.0, 100.0));
    EXPECT_TRUE(panel().scrollHasSize(Part::Object));
    EXPECT_GT(panel().contentHeight(Part::Object), 8 * 20.0f) << "the eight rows are all in the page's content";
    panel().tabs().selectTab(1);
    EXPECT_TRUE(panel().scrollHasSize(Part::Local)) << "a blank page: it was never laid out when it was shown";
}

TEST_F(TransformPanelTest, selectingSomethingElseShowsThatThingsTransforms) {
    ASSERT_TRUE(click(100.0, 100.0));
    panel().setValue(Part::Object, "tx", "7");
    EXPECT_TRUE(click(600.0, 700.0));   // empty canvas clears
    EXPECT_FALSE(panel().hasNode(Part::Object));
    EXPECT_EQ(panel().note(Part::Object), "Nothing selected");
}
