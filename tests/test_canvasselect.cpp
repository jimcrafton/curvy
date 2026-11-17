#include "testsupport.h"

#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include <newui/pen.h>

namespace {

    class CanvasSelectTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Select", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
            canvas().zoomToFit(1.0f);
            layer_ = window().document().addLayer();
            look_ = window().document().addCircle(layer_, 100.0f, 100.0f, 30.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
            second_ = window().document().addCircle(layer_, 300.0f, 100.0f, 30.0f, newui::Color(0.0f, 0.0f, 1.0f, 1.0f));
        }

        curvy::CanvasView& canvas() { return window().canvas(); }
        curvy::SelectionModel& selection() { return window().graph().selectionModel(); }
        newui::Point at(double x, double y) {
            const BLPoint p = canvas().toView(BLPoint(x, y));
            return newui::Point(float(p.x), float(p.y));
        }
        bool press(double x, double y, std::uint32_t keys = 0) { return window().pressed(pen_, at(x, y), newui::mbmLeftButton, keys); }
        std::string primaryType() { return window().document().node(selection().primary()).typeName(); }

        newui::PenState pen_;
        curvy::NodeRef layer_, look_, second_;
    };

}

TEST_F(CanvasSelectTest, clickingAShapeSelectsTheNodeThatMadeIt) {
    EXPECT_TRUE(selection().empty());
    EXPECT_TRUE(press(100.0, 100.0));
    ASSERT_EQ(selection().items().size(), 1u);
    EXPECT_EQ(primaryType(), "circle");
    EXPECT_EQ(selection().fill(), look_);   // the look nodes of that shape are found from it
}

TEST_F(CanvasSelectTest, clickingAnotherShapeReplacesTheSelection) {
    press(100.0, 100.0);
    const curvy::NodeRef first = selection().primary();
    EXPECT_TRUE(press(300.0, 100.0));
    ASSERT_EQ(selection().items().size(), 1u);
    EXPECT_NE(selection().primary(), first);
    EXPECT_EQ(selection().fill(), second_);
}

TEST_F(CanvasSelectTest, shiftAddsAndEmptyCanvasClears) {
    press(100.0, 100.0);
    EXPECT_TRUE(press(300.0, 100.0, newui::kmShift));
    EXPECT_EQ(selection().items().size(), 2u);

    EXPECT_FALSE(press(600.0, 700.0, newui::kmShift)) << "shift on empty canvas keeps the selection";
    EXPECT_EQ(selection().items().size(), 2u);

    EXPECT_TRUE(press(600.0, 700.0));
    EXPECT_TRUE(selection().empty());
    EXPECT_FALSE(press(600.0, 700.0)) << "nothing to clear: the press is not used";
}

TEST_F(CanvasSelectTest, clickingTheSelectedShapeAgainGoesUpToItsLayerThenStartsOver) {
    press(100.0, 100.0);
    ASSERT_EQ(primaryType(), "circle");
    EXPECT_TRUE(press(100.0, 100.0));
    ASSERT_EQ(selection().items().size(), 1u);
    EXPECT_EQ(selection().primary().id, window().document().childrenOf()[0].id) << "the layer";
    EXPECT_TRUE(selection().isShapeKeySelected(std::string(window().document().artwork()->shapes().back().key))) << "the layer holds both shapes";

    EXPECT_TRUE(press(300.0, 100.0));
    EXPECT_EQ(primaryType(), "circle") << "at the top it starts over, at the shape that was clicked";
    EXPECT_EQ(selection().fill(), second_);
}

TEST_F(CanvasSelectTest, stepUpDoesNothingWhenNothingEncloses) {
    EXPECT_FALSE(selection().stepUp());
    press(100.0, 100.0);
    press(100.0, 100.0);
    EXPECT_FALSE(selection().stepUp()) << "a layer is the top";
}

TEST_F(CanvasSelectTest, theSelectionIsSharedWithTheGraphView) {
    press(100.0, 100.0);
    EXPECT_EQ(window().graph().selectionModel().primary(), selection().primary());
    EXPECT_TRUE(selection().isShapeKeySelected(std::string(window().document().artwork()->shapes().front().key)));
}

TEST_F(CanvasSelectTest, spaceHeldStillPansInsteadOfSelecting) {
    canvas().onKeyFilter(0, newui::vkSpaceBar);
    EXPECT_TRUE(press(100.0, 100.0));
    EXPECT_TRUE(selection().empty());
    canvas().onMouseUp(canvas(), at(100.0, 100.0), newui::mbmLeftButton, 0);
    canvas().onKeyUp(canvas(), 0, 0, 1, newui::vkSpaceBar);
}

TEST_F(CanvasSelectTest, aClosedPathIsSelectedByClickingInsideItAndItsGeometryNodeIsTheSelection) {
    curvy::SketchDocument::PathLook look;
    look.fill = newui::Color(0.0f, 1.0f, 0.0f, 1.0f);
    const curvy::NodeRef last = window().document().addPath(layer_, {{400.0f, 400.0f}, {500.0f, 400.0f}, {500.0f, 500.0f}, {400.0f, 500.0f}}, true, false, look);
    ASSERT_TRUE(last.valid());
    EXPECT_TRUE(press(450.0, 450.0));
    ASSERT_EQ(selection().items().size(), 1u);
    EXPECT_EQ(primaryType(), "geometry");
    EXPECT_EQ(selection().primary(), window().document().pathNodeOf(last));
}

TEST_F(CanvasSelectTest, aStrokedOpenPathIsSelectedOnItsLineNotInTheGapBesideIt) {
    curvy::SketchDocument::PathLook look;
    look.hasFill = false;
    look.hasStroke = true;
    look.strokeWidth = 6.0f;
    window().document().addPath(layer_, {{400.0f, 600.0f}, {550.0f, 600.0f}}, false, false, look);
    EXPECT_FALSE(press(475.0, 640.0));
    EXPECT_TRUE(selection().empty());
    EXPECT_TRUE(press(475.0, 601.0));
    EXPECT_EQ(primaryType(), "geometry");
}

TEST_F(CanvasSelectTest, aPathStaysSelectableAfterItsPointsAreEdited) {
    curvy::SketchDocument::PathLook look;
    const curvy::NodeRef last = window().document().addPath(layer_, {{400.0f, 400.0f}, {500.0f, 400.0f}, {500.0f, 500.0f}}, true, false, look);
    const curvy::NodeRef geometry = window().document().pathNodeOf(last);
    window().document().setPathPoints(geometry, {{300.0f, 600.0f}, {400.0f, 600.0f}, {400.0f, 700.0f}});
    EXPECT_FALSE(press(450.0, 430.0)) << "the old place is empty now";
    EXPECT_TRUE(press(380.0, 620.0));
    EXPECT_EQ(selection().primary(), geometry);
}
