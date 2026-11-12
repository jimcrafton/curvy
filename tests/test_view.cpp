#include "testsupport.h"

#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include <newui/pen.h>

#include <algorithm>
#include <cmath>

namespace {

    class ViewTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("View", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
            canvas().zoomToFit(1.0f);
            const newui::Rect client = canvas().getClientBounds();
            fitZoom_ = (std::min)((client.width() - 80.0f) / 612.0f, (client.height() - 80.0f) / 792.0f);
        }

        curvy::CanvasView& canvas() { return window().canvas(); }

        float fitZoom_ = 1.0f;
        const newui::Point anchor_{200.0f, 150.0f};
    };
}

TEST_F(ViewTest, ANewDocumentOpensFittedToTheView) {
    EXPECT_GT(canvas().getClientBounds().width(), 200.0f);
    EXPECT_NEAR((std::min)(fitZoom_, 1.0f), canvas().zoom(), 1e-4f);
}

TEST_F(ViewTest, TheHintBarShowsTheZoom) {
    EXPECT_EQ(std::to_string(int(std::lround(canvas().zoom() * 100.0f))) + "%", window().workspace().hintInfo());
    canvas().setZoom(0.5f);
    EXPECT_EQ("50%", window().workspace().hintInfo());
}

TEST_F(ViewTest, ZoomingKeepsThePointUnderThePointerStill) {
    const BLPoint before = canvas().toDocument(anchor_);
    const float zoomBefore = canvas().zoom();
    canvas().zoomAt(anchor_, zoomBefore * 2.0f);
    const BLPoint after = canvas().toDocument(anchor_);
    EXPECT_NEAR(zoomBefore * 2.0f, canvas().zoom(), 1e-4f);
    EXPECT_NEAR(before.x, after.x, 1e-3);
    EXPECT_NEAR(before.y, after.y, 1e-3);
}

TEST_F(ViewTest, ZoomStaysWithinItsLimits) {
    canvas().zoomAt(anchor_, 1000.0f);
    EXPECT_EQ(curvy::CanvasView::kMaxZoom, canvas().zoom());
    canvas().zoomAt(anchor_, 0.0001f);
    EXPECT_EQ(curvy::CanvasView::kMinZoom, canvas().zoom());
}

TEST_F(ViewTest, TheWheelPansThePage) {
    const float y0 = canvas().panY();
    canvas().onMouseWheel(canvas(), newui::Point(100.0f, 100.0f), 120.0f);   // one notch away from the user, with neither Ctrl nor Shift held
    EXPECT_EQ(y0 + 40.0f, canvas().panY());
}

TEST_F(ViewTest, APlainPressIsLeftToTheToolsButSpaceHeldPansOnDrag) {
    newui::PenState mouse;
    const newui::Point at(300.0f, 300.0f);
    EXPECT_FALSE(window().pressed(mouse, at, newui::mbmLeftButton, 0));

    canvas().onKeyFilter(0, newui::vkSpaceBar);   // Space goes down
    const float x0 = canvas().panX(), y0 = canvas().panY();
    EXPECT_TRUE(window().pressed(mouse, at, newui::mbmLeftButton, 0)) << "with Space held a press is taken";
    canvas().onMouseMove(canvas(), newui::Point(325.0f, 310.0f), newui::mbmLeftButton, 0);
    EXPECT_EQ(x0 + 25.0f, canvas().panX());
    EXPECT_EQ(y0 + 10.0f, canvas().panY());
    canvas().onMouseUp(canvas(), newui::Point(325.0f, 310.0f), newui::mbmLeftButton, 0);
    canvas().onKeyUp(canvas(), 0, 0, 1, newui::vkSpaceBar);
    EXPECT_FALSE(window().menu().isOpen()) << "letting go of Space after a pan does not open the rings";
}

TEST_F(ViewTest, CtrlSpaceAndADragZoomsAroundThePress) {
    newui::PenState pen;
    pen.inRange = true;
    const newui::Point at(300.0f, 300.0f);
    const float z0 = canvas().zoom();
    const BLPoint under = canvas().toDocument(at);

    canvas().onKeyFilter(newui::kmCtrl, newui::vkSpaceBar);   // Ctrl+Space goes down
    EXPECT_TRUE(window().pressed(pen, at, newui::mbmLeftButton, newui::kmCtrl));
    canvas().onMouseMove(canvas(), newui::Point(300.0f, 100.0f), newui::mbmLeftButton, 0);   // 200 pixels up: one doubling
    EXPECT_NEAR(z0 * 2.0f, canvas().zoom(), 1e-3f);
    EXPECT_NEAR(under.x, canvas().toDocument(at).x, 1e-3) << "the point pressed stays where it is";
    EXPECT_NEAR(under.y, canvas().toDocument(at).y, 1e-3);
    canvas().onMouseMove(canvas(), newui::Point(300.0f, 500.0f), newui::mbmLeftButton, 0);   // 200 pixels below the press: one halving
    EXPECT_NEAR(z0 * 0.5f, canvas().zoom(), 1e-3f);
    canvas().onMouseMove(canvas(), newui::Point(500.0f, 300.0f), newui::mbmLeftButton, 0);   // 200 pixels to the right zooms in too
    EXPECT_NEAR(z0 * 2.0f, canvas().zoom(), 1e-3f);

    canvas().onMouseUp(canvas(), newui::Point(500.0f, 300.0f), newui::mbmLeftButton, 0);
    const float zoomed = canvas().zoom();
    canvas().onMouseMove(canvas(), newui::Point(100.0f, 100.0f), 0, 0);
    EXPECT_EQ(zoomed, canvas().zoom()) << "letting go ends the zoom";
    canvas().onKeyUp(canvas(), 0, 0, 1, newui::vkSpaceBar);
    EXPECT_FALSE(window().menu().isOpen());
}

TEST_F(ViewTest, CtrlSpaceWithoutADragDoesNotOpenTheRingsButASpaceTapDoes) {
    canvas().onKeyFilter(newui::kmCtrl, newui::vkSpaceBar);
    canvas().onKeyUp(canvas(), 0, 0, 1, newui::vkSpaceBar);
    EXPECT_FALSE(window().menu().isOpen());

    canvas().onKeyFilter(0, newui::vkSpaceBar);
    canvas().onKeyUp(canvas(), 0, 0, 1, newui::vkSpaceBar);
    EXPECT_TRUE(window().menu().isOpen());
}

TEST_F(ViewTest, FitPagePutsTheZoomBack) {
    canvas().zoomAt(anchor_, 3.0f);
    window().runCommand("view.fitPage");
    EXPECT_NEAR(fitZoom_, canvas().zoom(), 1e-4f);
}
