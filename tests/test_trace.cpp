#include "curvy/sketchdocument.h"
#include "curvy/tracelayer.h"

#include "testsupport.h"

#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include <newui/pen.h>

#include <filesystem>

using curvy::TraceItem;

TEST(TraceLayer, aRectIsNormalisedWhicheverWayTheCornersComeIn) {
    const TraceItem item = TraceItem::rect(50.0, 80.0, 10.0, 20.0);
    EXPECT_EQ(10.0, item.x);
    EXPECT_EQ(20.0, item.y);
    EXPECT_EQ(40.0, item.width);
    EXPECT_EQ(60.0, item.height);
    EXPECT_TRUE(item.valid());
    EXPECT_FALSE(TraceItem::rect(5.0, 5.0, 5.0, 9.0).valid()) << "no width is not a shape";
}

TEST(TraceLayer, hitTestFindsTheOutlineAndNotTheInside) {
    curvy::TraceLayer layer;
    layer.add(TraceItem::rect(10.0, 10.0, 110.0, 60.0));
    EXPECT_EQ(0u, layer.hitTest(BLPoint(10.0, 30.0), 2.0));
    EXPECT_EQ(0u, layer.hitTest(BLPoint(60.0, 61.0), 2.0));
    EXPECT_EQ(1u, layer.hitTest(BLPoint(60.0, 35.0), 2.0)) << "the middle of a rectangle is not on its outline";
    EXPECT_EQ(1u, layer.hitTest(BLPoint(300.0, 300.0), 2.0));
}

TEST(TraceDocument, everyEditIsOneUndoStepAndRedoRepeatsIt) {
    curvy::SketchDocument document;
    document.newBlank("Trace", 612.0f, 792.0f);
    EXPECT_TRUE(document.trace().empty());
    EXPECT_EQ(0u, document.addTraceItem(TraceItem::rect(0.0, 0.0, 10.0, 10.0)));
    EXPECT_EQ(1u, document.addTraceItem(TraceItem::rect(20.0, 20.0, 40.0, 50.0)));
    document.setTraceItem(0, TraceItem::rect(1.0, 1.0, 11.0, 11.0));
    EXPECT_EQ(1.0, document.trace().items()[0].x);
    document.undoStack().undo();
    EXPECT_EQ(0.0, document.trace().items()[0].x);
    document.undoStack().redo();
    EXPECT_EQ(1.0, document.trace().items()[0].x);

    document.removeTraceItem(0);
    ASSERT_EQ(1u, document.trace().items().size());
    document.undoStack().undo();
    ASSERT_EQ(2u, document.trace().items().size());
    EXPECT_EQ(1.0, document.trace().items()[0].x) << "undo puts it back at its place";

    EXPECT_TRUE(document.clearTrace());
    EXPECT_FALSE(document.clearTrace());
    document.undoStack().undo();
    EXPECT_EQ(2u, document.trace().items().size());
}

TEST(TraceDocument, badEditsAndALockedLayerAreRefused) {
    curvy::SketchDocument document;
    document.newBlank("Trace", 612.0f, 792.0f);
    EXPECT_THROW(document.addTraceItem(TraceItem::rect(0.0, 0.0, 0.0, 10.0)), std::runtime_error);
    EXPECT_THROW(document.removeTraceItem(0), std::runtime_error);
    document.addTraceItem(TraceItem::rect(0.0, 0.0, 10.0, 10.0));
    const std::string last = document.undoStack().undoDescription();
    document.setTraceItem(0, TraceItem::rect(0.0, 0.0, 10.0, 10.0));
    EXPECT_EQ(last, document.undoStack().undoDescription()) << "an edit that changes nothing is not a step";

    document.setTraceLocked(true);
    EXPECT_THROW(document.addTraceItem(TraceItem::rect(0.0, 0.0, 5.0, 5.0)), std::runtime_error);
    EXPECT_THROW(document.removeTraceItem(0), std::runtime_error);
    EXPECT_EQ(1u, document.trace().items().size());
}

TEST(TraceDocument, severalShapesMoveOrGoInOneUndoStep) {
    curvy::SketchDocument document;
    document.newBlank("Trace", 612.0f, 792.0f);
    document.addTraceItem(TraceItem::rect(0.0, 0.0, 10.0, 10.0));
    document.addTraceItem(TraceItem::rect(20.0, 20.0, 30.0, 30.0));
    document.addTraceItem(TraceItem::rect(40.0, 40.0, 50.0, 50.0));

    document.moveTraceItems({0, 2, 2}, 5.0, 7.0);
    EXPECT_EQ(5.0, document.trace().items()[0].x);
    EXPECT_EQ(20.0, document.trace().items()[1].x) << "the one that was not named stays";
    EXPECT_EQ(45.0, document.trace().items()[2].x) << "a repeated index moves once";
    document.undoStack().undo();
    EXPECT_EQ(0.0, document.trace().items()[0].x);
    EXPECT_EQ(40.0, document.trace().items()[2].x);

    const std::string last = document.undoStack().undoDescription();
    document.moveTraceItems({0}, 0.0, 0.0);
    document.moveTraceItems({}, 3.0, 3.0);
    EXPECT_EQ(last, document.undoStack().undoDescription()) << "no movement is not a step";
    EXPECT_THROW(document.moveTraceItems({0, 9}, 1.0, 1.0), std::runtime_error);
    EXPECT_EQ(0.0, document.trace().items()[0].x) << "a bad index changes nothing";
    EXPECT_THROW(document.moveTraceItems({0}, std::nan(""), 1.0), std::runtime_error);

    document.removeTraceItems({2, 0});
    ASSERT_EQ(1u, document.trace().items().size());
    EXPECT_EQ(20.0, document.trace().items()[0].x);
    document.undoStack().undo();
    ASSERT_EQ(3u, document.trace().items().size());
    EXPECT_EQ(40.0, document.trace().items()[2].x);
}

TEST(TraceDocument, theLayerIsSavedInItsOwnSectionAndReadBack) {
    const std::filesystem::path path = curvytest::tempFile("curvyTraceRoundTrip.curvy");
    {
        curvy::SketchDocument document;
        document.newBlank("Trace", 612.0f, 792.0f);
        document.addTraceItem(TraceItem::rect(12.5, 20.0, 112.5, 70.0));
        document.addTraceItem(TraceItem::rect(0.0, 0.0, 30.0, 30.0));
        document.setTraceVisible(false);
        ASSERT_TRUE(document.save(path.u8string())) << document.lastError();
    }
    curvy::SketchDocument loaded;
    ASSERT_TRUE(loaded.load(path.u8string())) << loaded.lastError();
    ASSERT_EQ(2u, loaded.trace().items().size());
    EXPECT_EQ(TraceItem::rect(12.5, 20.0, 112.5, 70.0), loaded.trace().items()[0]);
    EXPECT_FALSE(loaded.trace().visible());
    EXPECT_FALSE(loaded.trace().locked());
    EXPECT_TRUE(loaded.guides().empty()) << "the trace layer is not the guides";
    std::filesystem::remove(path);
}

TEST(TraceDocument, theLayerIsTranslucentByDefaultAndItsOpacityIsKept) {
    const std::filesystem::path path = curvytest::tempFile("curvyTraceOpacity.curvy");
    curvy::SketchDocument document;
    document.newBlank("Trace", 612.0f, 792.0f);
    EXPECT_EQ(curvy::TraceLayer::kDefaultOpacity, document.trace().opacity());
    EXPECT_LT(document.trace().opacity(), 1.0);
    document.setTraceOpacity(0.25);
    document.setTraceOpacity(7.0);
    EXPECT_EQ(1.0, document.trace().opacity()) << "clamped";
    document.setTraceOpacity(0.0);
    EXPECT_EQ(curvy::TraceLayer::kMinOpacity, document.trace().opacity()) << "never fully invisible";
    EXPECT_THROW(document.setTraceOpacity(std::nan("")), std::runtime_error);
    document.setTraceOpacity(0.25);
    ASSERT_TRUE(document.save(path.u8string())) << document.lastError();

    curvy::SketchDocument loaded;
    ASSERT_TRUE(loaded.load(path.u8string())) << loaded.lastError();
    EXPECT_EQ(0.25, loaded.trace().opacity());
    std::filesystem::remove(path);
}

TEST(TraceLayer, theSheetWashesTheCanvasWhiteAtTheLayersOpacityAndOutlinesStaySolid) {
    BLImage image(40, 40, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.set_fill_style(BLRgba32(0xFF000000u));
    ctx.fill_all();

    curvy::TraceLayer layer;
    layer.setOpacity(0.6);
    layer.add(TraceItem::rect(10.0, 10.0, 30.0, 30.0));
    layer.paintSheet(ctx, BLRect(0.0, 0.0, 40.0, 40.0));
    layer.paint(ctx, BLPoint(0.0, 0.0), 1.0);
    ctx.end();

    BLImageData data;
    image.get_data(&data);
    const auto pixel = [&](int x, int y) { return static_cast<const std::uint32_t*>(data.pixel_data)[y * (data.stride / 4) + x]; };
    const std::uint32_t corner = pixel(2, 2);
    EXPECT_NEAR(0.6 * 255.0, double(corner & 0xFF), 2.0) << "black washed with 60% white";
    EXPECT_NEAR(0.6 * 255.0, double((corner >> 8) & 0xFF), 2.0);
    const double expectedRed = curvy::TraceLayer::kLineOpacity * 224.0 + (1.0 - curvy::TraceLayer::kLineOpacity) * (0.6 * 255.0);
    EXPECT_NEAR(expectedRed, double((pixel(10, 20) >> 16) & 0xFF), 3.0) << "the outline is the trace colour at 80%, over the sheet";

    layer.setVisible(false);
    BLContext again(image);
    again.set_fill_style(BLRgba32(0xFF000000u));
    again.fill_all();
    layer.paintSheet(again, BLRect(0.0, 0.0, 40.0, 40.0));
    again.end();
    image.get_data(&data);
    EXPECT_EQ(0xFF000000u, pixel(2, 2)) << "a hidden layer paints no sheet";
}

TEST(TraceDocument, aNewDocumentStartsWithAnEmptyLayer) {
    curvy::SketchDocument document;
    document.newBlank("Trace", 612.0f, 792.0f);
    document.addTraceItem(TraceItem::rect(0.0, 0.0, 10.0, 10.0));
    document.setTraceVisible(false);
    document.newBlank("Next", 612.0f, 792.0f);
    EXPECT_TRUE(document.trace().empty());
    EXPECT_TRUE(document.trace().visible());
}

namespace {

    class TraceRectToolTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Trace", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
            canvas().zoomToFit(1.0f);
            window().runCommand("tool.traceRect");
        }

        curvy::CanvasView& canvas() { return window().canvas(); }
        newui::Point at(double x, double y) {
            const BLPoint p = canvas().toView(BLPoint(x, y));
            return newui::Point(float(p.x), float(p.y));
        }
        void drag(double x1, double y1, double x2, double y2, std::uint32_t keys = 0) {
            window().pressed(pen_, at(x1, y1), newui::mbmLeftButton, keys);
            canvas().onMouseMove(canvas(), at((x1 + x2) / 2.0, (y1 + y2) / 2.0), newui::mbmLeftButton, keys);
            canvas().onMouseMove(canvas(), at(x2, y2), newui::mbmLeftButton, keys);
            canvas().onMouseUp(canvas(), at(x2, y2), newui::mbmLeftButton, keys);
        }
        const curvy::TraceLayer& trace() { return window().document().trace(); }

        newui::PenState pen_;
    };

}

TEST_F(TraceRectToolTest, aDragAddsOneRectToTheTraceLayerAndNothingToTheGraph) {
    const std::size_t nodesBefore = window().document().network().nodeCount();
    drag(100.0, 150.0, 300.0, 260.0);
    ASSERT_EQ(1u, trace().items().size());
    const TraceItem& item = trace().items()[0];
    EXPECT_NEAR(100.0, item.x, 0.6);
    EXPECT_NEAR(150.0, item.y, 0.6);
    EXPECT_NEAR(200.0, item.width, 0.6);
    EXPECT_NEAR(110.0, item.height, 0.6);
    EXPECT_EQ(nodesBefore, window().document().network().nodeCount()) << "the trace layer never touches the graph";
    EXPECT_TRUE(window().document().artwork() == nullptr || window().document().artwork()->shapes().empty());

    window().document().undoStack().undo();
    EXPECT_TRUE(trace().empty());
}

TEST_F(TraceRectToolTest, dragRightToLeftAndShiftMakesASquare) {
    drag(300.0, 300.0, 200.0, 250.0, newui::kmShift);
    ASSERT_EQ(1u, trace().items().size());
    const TraceItem& item = trace().items()[0];
    EXPECT_NEAR(item.width, item.height, 1e-6);
    EXPECT_NEAR(100.0, item.width, 0.6);
    EXPECT_NEAR(200.0, item.x, 0.6);
}

TEST_F(TraceRectToolTest, aClickAddsNothingAndEscapeDropsTheDrag) {
    drag(100.0, 100.0, 100.5, 100.5);
    EXPECT_TRUE(trace().empty());

    window().pressed(pen_, at(100.0, 100.0), newui::mbmLeftButton, 0);
    canvas().onMouseMove(canvas(), at(200.0, 200.0), newui::mbmLeftButton, 0);
    EXPECT_TRUE(window().tools().current()->busy());
    EXPECT_TRUE(window().tools().current()->keyDown(curvy::ToolKeyEvent{newui::vkEscape, 0}));
    canvas().onMouseUp(canvas(), at(200.0, 200.0), newui::mbmLeftButton, 0);
    EXPECT_TRUE(trace().empty());
}

TEST_F(TraceRectToolTest, theCornersSnapToAnExistingShapeAndCtrlTurnsItOff) {
    window().document().addTraceItem(TraceItem::rect(300.0, 300.0, 400.0, 360.0));
    drag(303.0, 297.0, 500.0, 520.0);
    ASSERT_EQ(2u, trace().items().size());
    EXPECT_EQ(300.0, trace().items()[1].x) << "the first corner landed on the corner of the other rectangle";
    EXPECT_EQ(300.0, trace().items()[1].y);
    EXPECT_NEAR(500.0, trace().items()[1].x + trace().items()[1].width, 0.6) << "nothing near the far corner";

    drag(303.0, 297.0, 520.0, 540.0, newui::kmCtrl);
    ASSERT_EQ(3u, trace().items().size());
    EXPECT_NEAR(303.0, trace().items()[2].x, 0.6) << "Ctrl draws where the pointer is";
    EXPECT_NEAR(297.0, trace().items()[2].y, 0.6);
}

TEST_F(TraceRectToolTest, aFreeCornerLinesUpWithAnotherShapeAndWithTheFirstCorner) {
    window().document().addTraceItem(TraceItem::rect(300.0, 300.0, 400.0, 380.0));
    drag(100.0, 100.0, 220.0, 303.0);
    ASSERT_EQ(2u, trace().items().size());
    EXPECT_NEAR(200.0, trace().items()[1].height, 0.01) << "the bottom edge lined up in y with the other rectangle's top";
    EXPECT_NEAR(120.0, trace().items()[1].width, 0.6);

    drag(100.0, 100.0, 104.0, 250.0);
    EXPECT_EQ(2u, trace().items().size()) << "lined up with its own start in x it has no width, so it is refused";
}

TEST_F(TraceRectToolTest, aLockedLayerRefusesTheDrawing) {
    window().document().setTraceLocked(true);
    drag(100.0, 100.0, 200.0, 200.0);
    EXPECT_TRUE(trace().empty());
}

TEST(TraceLayer, aLineHasAnEndHitTestsOnItsLengthAndNeedsALength) {
    const TraceItem line = TraceItem::line(10.0, 10.0, 110.0, 60.0);
    EXPECT_EQ(110.0, line.endX());
    EXPECT_EQ(60.0, line.endY());
    EXPECT_TRUE(line.valid());
    EXPECT_FALSE(TraceItem::line(5.0, 5.0, 5.0, 5.0).valid());
    EXPECT_TRUE(TraceItem::line(5.0, 5.0, 5.0, 9.0).valid()) << "a vertical line has no width and is still a line";
    EXPECT_TRUE(TraceItem::line(50.0, 50.0, 0.0, 0.0).valid()) << "a line may run up and to the left";

    curvy::TraceLayer layer;
    layer.add(line);
    EXPECT_EQ(0u, layer.hitTest(BLPoint(60.0, 35.0), 2.0));
    EXPECT_EQ(1u, layer.hitTest(BLPoint(60.0, 45.0), 2.0));
    EXPECT_EQ(3u, line.snapPoints().size());
    EXPECT_EQ(1u, line.segments().size());
}

TEST(TraceDocument, linesMoveAndSurviveSaveAndOpen) {
    curvy::SketchDocument document;
    document.newBlank("Trace", 612.0f, 792.0f);
    document.addTraceItem(TraceItem::line(10.0, 20.0, 110.0, 5.0));
    document.addTraceItem(TraceItem::rect(0.0, 0.0, 10.0, 10.0));
    document.moveTraceItems({0}, 5.0, 5.0);
    EXPECT_EQ(15.0, document.trace().items()[0].x);
    EXPECT_EQ(115.0, document.trace().items()[0].endX());

    const std::filesystem::path path = curvytest::tempFile("curvyTraceLine.curvy");
    ASSERT_TRUE(document.save(path.u8string())) << document.lastError();
    curvy::SketchDocument reopened;
    ASSERT_TRUE(reopened.load(path.u8string())) << reopened.lastError();
    ASSERT_EQ(2u, reopened.trace().items().size());
    EXPECT_EQ(document.trace().items()[0], reopened.trace().items()[0]);
    EXPECT_EQ(TraceItem::Kind::Line, reopened.trace().items()[0].kind);
    EXPECT_EQ(TraceItem::Kind::Rect, reopened.trace().items()[1].kind);
    std::filesystem::remove(path);
}

namespace {

    class TraceLineToolTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Trace", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
            canvas().zoomToFit(1.0f);
            window().runCommand("tool.traceLine");
        }

        curvy::CanvasView& canvas() { return window().canvas(); }
        newui::Point at(double x, double y) {
            const BLPoint p = canvas().toView(BLPoint(x, y));
            return newui::Point(float(p.x), float(p.y));
        }
        void drag(double x1, double y1, double x2, double y2, std::uint32_t keys = 0) {
            window().pressed(pen_, at(x1, y1), newui::mbmLeftButton, keys);
            canvas().onMouseMove(canvas(), at((x1 + x2) / 2.0, (y1 + y2) / 2.0), newui::mbmLeftButton, keys);
            canvas().onMouseMove(canvas(), at(x2, y2), newui::mbmLeftButton, keys);
            canvas().onMouseUp(canvas(), at(x2, y2), newui::mbmLeftButton, keys);
        }
        const curvy::TraceLayer& trace() { return window().document().trace(); }

        newui::PenState pen_;
    };

}

TEST_F(TraceLineToolTest, aDragAddsOneLineAndNothingToTheGraph) {
    const std::size_t nodesBefore = window().document().network().nodeCount();
    drag(100.0, 150.0, 300.0, 260.0);
    ASSERT_EQ(1u, trace().items().size());
    const TraceItem& item = trace().items()[0];
    EXPECT_EQ(TraceItem::Kind::Line, item.kind);
    EXPECT_NEAR(100.0, item.x, 0.6);
    EXPECT_NEAR(300.0, item.endX(), 0.6);
    EXPECT_NEAR(260.0, item.endY(), 0.6);
    EXPECT_EQ(nodesBefore, window().document().network().nodeCount());
    window().document().undoStack().undo();
    EXPECT_TRUE(trace().empty());
}

TEST_F(TraceLineToolTest, shiftLocksTheAngleAndAClickAddsNothing) {
    drag(100.0, 100.0, 300.0, 130.0, newui::kmShift);
    ASSERT_EQ(1u, trace().items().size());
    EXPECT_NEAR(0.0, trace().items()[0].height, 1e-6) << "30 points off horizontal over 200 goes onto the 0 degree ray";
    EXPECT_NEAR(std::hypot(200.0, 30.0), trace().items()[0].width, 1.0) << "the length is kept, only the angle changes";

    drag(100.0, 100.0, 100.5, 100.5);
    EXPECT_EQ(1u, trace().items().size());
}

TEST_F(TraceLineToolTest, aLineStartedOnAnEdgeLocksSquareToItWhenTheDragIsNearlySquare) {
    window().document().addTraceItem(TraceItem::line(100.0, 200.0, 300.0, 200.0));
    drag(220.0, 200.0, 226.0, 330.0);
    ASSERT_EQ(2u, trace().items().size());
    EXPECT_NEAR(220.0, trace().items()[1].x, 0.01);
    EXPECT_NEAR(0.0, trace().items()[1].width, 1e-6) << "6 points over 130 is about 2.6 degrees off square, so it locks";
    EXPECT_NEAR(130.0, trace().items()[1].height, 1.0);

    drag(220.0, 200.0, 220.0, 70.0);
    EXPECT_NEAR(-130.0, trace().items()[2].height, 1.0) << "either side of the edge works";
    EXPECT_NEAR(0.0, trace().items()[2].width, 1e-6);

    drag(240.0, 200.0, 280.0, 330.0);
    EXPECT_NEAR(40.0, trace().items()[3].width, 1.0) << "17 degrees off square does not lock";

    drag(240.0, 200.0, 246.0, 330.0, newui::kmCtrl);
    EXPECT_NEAR(6.0, trace().items()[4].width, 1.0) << "Ctrl turns the lock off";
}

TEST_F(TraceLineToolTest, theEndStillSnapsWhenTheStartSnappedOntoAnEdge) {
    window().document().addTraceItem(TraceItem::line(100.0, 200.0, 300.0, 200.0));
    window().document().addTraceItem(TraceItem::rect(350.0, 300.0, 450.0, 380.0));
    drag(220.0, 200.0, 347.0, 303.0);
    ASSERT_EQ(3u, trace().items().size());
    EXPECT_NEAR(350.0, trace().items()[2].endX(), 0.01) << "the far end landed on the rectangle's corner";
    EXPECT_NEAR(300.0, trace().items()[2].endY(), 0.01);

    drag(220.0, 200.0, 217.0, 304.0);
    ASSERT_EQ(4u, trace().items().size());
    EXPECT_NEAR(0.0, trace().items()[3].width, 0.01) << "nearly square: locks square";

    window().document().addTraceItem(TraceItem::rect(222.0, 300.0, 260.0, 340.0));
    drag(220.0, 200.0, 224.0, 296.0);
    ASSERT_EQ(6u, trace().items().size());
    EXPECT_NEAR(222.0, trace().items()[5].endX(), 0.01) << "a corner within reach beats the square lock";
    EXPECT_NEAR(300.0, trace().items()[5].endY(), 0.01);
}

TEST_F(TraceLineToolTest, aSquareLockedEndAlsoSnapsToWhereTheRayMeetsAnotherShape) {
    window().document().addTraceItem(TraceItem::line(100.0, 200.0, 300.0, 200.0));
    window().document().addTraceItem(TraceItem::line(50.0, 100.0, 500.0, 100.0));
    drag(220.0, 200.0, 222.0, 104.0);
    ASSERT_EQ(3u, trace().items().size());
    EXPECT_NEAR(0.0, trace().items()[2].width, 1e-6) << "square to the first line";
    EXPECT_NEAR(100.0, trace().items()[2].endY(), 0.01) << "and ended exactly on the second line";

    window().document().addGuide(false, 60.0);
    drag(220.0, 200.0, 222.0, 64.0);
    ASSERT_EQ(4u, trace().items().size());
    EXPECT_NEAR(60.0, trace().items()[3].endY(), 0.01) << "a horizontal guide is met by the ray too";
}

TEST_F(TraceLineToolTest, aSquareLockedEndLinesUpWithTheYOfAnotherShapesEnd) {
    window().document().addTraceItem(TraceItem::line(100.0, 100.0, 500.0, 100.0));
    window().document().addTraceItem(TraceItem::line(400.0, 100.0, 400.0, 340.0));
    drag(200.0, 100.0, 203.0, 336.0);
    ASSERT_EQ(3u, trace().items().size());
    EXPECT_NEAR(0.0, trace().items()[2].width, 1e-6) << "square to the top line";
    EXPECT_NEAR(340.0, trace().items()[2].endY(), 0.01) << "same y as the end of the vertical line on the right";

    drag(250.0, 100.0, 253.0, 380.0);
    EXPECT_NEAR(380.0, trace().items()[3].endY(), 0.6) << "nothing to line up with out of reach";
}

TEST_F(TraceLineToolTest, aFreeEndLinesUpWithAnotherShapeOrWithItsOwnStart) {
    window().document().addTraceItem(TraceItem::rect(300.0, 300.0, 400.0, 380.0));
    drag(100.0, 100.0, 220.0, 303.0);
    ASSERT_EQ(2u, trace().items().size());
    EXPECT_NEAR(220.0, trace().items()[1].endX(), 0.6) << "nothing to line up with in x";
    EXPECT_NEAR(300.0, trace().items()[1].endY(), 0.01) << "y lined up with the rectangle's top corners";

    drag(100.0, 100.0, 400.0, 104.0);
    EXPECT_NEAR(100.0, trace().items()[2].endY(), 0.01) << "level with its own start";
    EXPECT_NEAR(400.0, trace().items()[2].endX(), 0.01) << "x lined up with the rectangle's right edge corner";

    drag(100.0, 100.0, 220.0, 303.0, newui::kmCtrl);
    EXPECT_NEAR(303.0, trace().items()[3].endY(), 0.6) << "Ctrl turns the line-up off too";
}

TEST_F(TraceLineToolTest, endsSnapToTheCrossingOfTwoShapesToo) {
    window().document().addTraceItem(TraceItem::line(100.0, 200.0, 300.0, 200.0));
    window().document().addTraceItem(TraceItem::line(200.0, 100.0, 200.0, 300.0));
    drag(203.0, 197.0, 400.0, 400.0);
    ASSERT_EQ(3u, trace().items().size());
    EXPECT_EQ(200.0, trace().items()[2].x);
    EXPECT_EQ(200.0, trace().items()[2].y);
}

namespace {

    class TraceSelectToolTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            window().newDocument("Trace", 612.0f, 792.0f);
            frame_.rootView().updateLayout();
            canvas().zoomToFit(1.0f);
            window().document().addTraceItem(TraceItem::rect(100.0, 100.0, 200.0, 180.0));
            window().document().addTraceItem(TraceItem::rect(300.0, 300.0, 400.0, 360.0));
            window().runCommand("tool.traceSelect");
        }

        curvy::CanvasView& canvas() { return window().canvas(); }
        curvy::TraceSelectTool& tool() { return *static_cast<curvy::TraceSelectTool*>(window().tools().find("traceSelect")); }
        newui::Point at(double x, double y) {
            const BLPoint p = canvas().toView(BLPoint(x, y));
            return newui::Point(float(p.x), float(p.y));
        }
        void drag(double x1, double y1, double x2, double y2, std::uint32_t keys = 0) {
            window().pressed(pen_, at(x1, y1), newui::mbmLeftButton, keys);
            canvas().onMouseMove(canvas(), at((x1 + x2) / 2.0, (y1 + y2) / 2.0), newui::mbmLeftButton, keys);
            canvas().onMouseMove(canvas(), at(x2, y2), newui::mbmLeftButton, keys);
            canvas().onMouseUp(canvas(), at(x2, y2), newui::mbmLeftButton, keys);
        }
        const curvy::TraceLayer& trace() { return window().document().trace(); }
        bool key(std::uint32_t vk) { return tool().keyDown(curvy::ToolKeyEvent{vk, 0}); }

        newui::PenState pen_;
    };

}

TEST_F(TraceSelectToolTest, aClickOnAnOutlineSelectsItAndTheInsideDoesNot) {
    drag(150.0, 100.0, 150.0, 100.0);
    EXPECT_EQ((std::vector<std::size_t>{0}), tool().selection());
    drag(350.0, 330.0, 350.0, 330.0);
    EXPECT_TRUE(tool().selection().empty()) << "the middle of a rectangle is empty canvas, which clears the selection";
    drag(350.0, 300.0, 350.0, 300.0);
    EXPECT_EQ((std::vector<std::size_t>{1}), tool().selection());
}

TEST_F(TraceSelectToolTest, shiftAddsAndRemoves) {
    drag(150.0, 100.0, 150.0, 100.0);
    drag(350.0, 300.0, 350.0, 300.0, newui::kmShift);
    EXPECT_EQ((std::vector<std::size_t>{0, 1}), tool().selection());
    drag(150.0, 100.0, 150.0, 100.0, newui::kmShift);
    EXPECT_EQ((std::vector<std::size_t>{1}), tool().selection());
}

TEST_F(TraceSelectToolTest, draggingAnOutlineMovesTheSelectionInOneUndoStep) {
    drag(150.0, 100.0, 170.0, 130.0);
    ASSERT_EQ(2u, trace().items().size());
    EXPECT_NEAR(120.0, trace().items()[0].x, 0.6);
    EXPECT_NEAR(130.0, trace().items()[0].y, 0.6);
    EXPECT_EQ(300.0, trace().items()[1].x) << "the other shape stays";
    EXPECT_EQ((std::vector<std::size_t>{0}), tool().selection()) << "it is still selected";
    window().document().undoStack().undo();
    EXPECT_EQ(100.0, trace().items()[0].x);
}

TEST_F(TraceSelectToolTest, aSmallJitterIsAClickNotAMove) {
    drag(150.0, 100.0, 151.0, 100.5);
    EXPECT_EQ(100.0, trace().items()[0].x);
    EXPECT_EQ(2u, window().document().trace().items().size());
}

TEST_F(TraceSelectToolTest, aMarqueeTakesTheShapesItFullyContains) {
    drag(90.0, 90.0, 210.0, 190.0);
    EXPECT_EQ((std::vector<std::size_t>{0}), tool().selection());
    drag(250.0, 250.0, 350.0, 330.0);
    EXPECT_TRUE(tool().selection().empty()) << "touching a shape is not containing it, and a new marquee replaces the selection";
    drag(50.0, 50.0, 350.0, 330.0);
    EXPECT_EQ((std::vector<std::size_t>{0}), tool().selection()) << "the second shape sticks out of this box";
    drag(50.0, 50.0, 450.0, 400.0);
    EXPECT_EQ((std::vector<std::size_t>{0, 1}), tool().selection());
}

TEST_F(TraceSelectToolTest, deleteRemovesTheSelectionAndEscapeClearsIt) {
    drag(90.0, 90.0, 450.0, 400.0);
    EXPECT_TRUE(key(newui::vkEscape));
    EXPECT_TRUE(tool().selection().empty());
    EXPECT_FALSE(key(newui::vkEscape)) << "nothing left for Escape to do";

    EXPECT_FALSE(key(newui::vkDelete)) << "nothing selected";
    drag(350.0, 300.0, 350.0, 300.0);
    EXPECT_TRUE(key(newui::vkDelete));
    ASSERT_EQ(1u, trace().items().size());
    EXPECT_EQ(100.0, trace().items()[0].x);
    window().document().undoStack().undo();
    EXPECT_EQ(2u, trace().items().size());
}

TEST_F(TraceSelectToolTest, aMoveSnapsTheShapeOntoItsNeighbourAndCtrlTurnsItOff) {
    drag(150.0, 100.0, 347.0, 203.0 + 100.0);
    EXPECT_NEAR(300.0, trace().items()[0].x, 1e-6) << "the corner of the moved rectangle landed on the corner of the other";
    EXPECT_NEAR(300.0, trace().items()[0].y, 1e-6);

    window().document().undoStack().undo();
    drag(150.0, 100.0, 347.0, 303.0, newui::kmCtrl);
    EXPECT_NEAR(297.0, trace().items()[0].x, 0.6);
    EXPECT_NEAR(303.0, trace().items()[0].y, 0.6);
}

TEST_F(TraceSelectToolTest, aMoveLinesUpWithAnotherShapeWhenNothingCloserSnaps) {
    drag(150.0, 100.0, 210.0, 218.0);
    EXPECT_NEAR(160.0, trace().items()[0].x, 0.6) << "nothing to line up with in x";
    EXPECT_NEAR(220.0, trace().items()[0].y, 1e-6) << "the bottom edge (298) lined up in y with the top of the other rectangle (300)";

    window().document().undoStack().undo();
    drag(150.0, 100.0, 210.0, 218.0, newui::kmCtrl);
    EXPECT_NEAR(218.0, trace().items()[0].y, 0.6) << "Ctrl turns it off";
}

TEST_F(TraceSelectToolTest, aLockedLayerRefusesTheMoveAndTheDelete) {
    drag(150.0, 100.0, 150.0, 100.0);
    window().document().setTraceLocked(true);
    drag(150.0, 100.0, 190.0, 140.0);
    EXPECT_EQ(100.0, trace().items()[0].x);
    EXPECT_FALSE(key(newui::vkDelete));
    EXPECT_EQ(2u, trace().items().size());
}

TEST_F(TraceSelectToolTest, theGraphIsNeverTouched) {
    const std::size_t nodes = window().document().network().nodeCount();
    drag(150.0, 100.0, 170.0, 130.0);
    key(newui::vkDelete);
    EXPECT_EQ(nodes, window().document().network().nodeCount());
}
