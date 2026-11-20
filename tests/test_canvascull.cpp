#include "curvy/canvasview.h"
#include "curvy/sketchdocument.h"

#include <blend2d/blend2d.h>

#include <gtest/gtest.h>

namespace {

    // A 200 x 100 page with a red circle at (50, 50) and a blue one at (150, 50), both of radius 20, drawn at 1:1 on white.
    class CanvasCullTest : public ::testing::Test {
    protected:
        void SetUp() override {
            document_.newBlank("Cull", 200.0f, 100.0f);
            const curvy::NodeRef layer = document_.addLayer();
            red_ = document_.addCircle(layer, 50.0f, 50.0f, 20.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
            document_.addCircle(layer, 150.0f, 50.0f, 20.0f, newui::Color(0.0f, 0.0f, 1.0f, 1.0f));
        }

        std::uint32_t pixelAt(const BLBox* visible, int x, int y) {
            BLImage image(200, 100, BL_FORMAT_PRGB32);
            BLContext ctx(image);
            ctx.fill_all(BLRgba32(0xFFFFFFFFu));
            curvy::CanvasView::renderArtworkAtScale(ctx, document_, 0.0, 0.0, 1.0, {}, visible);
            ctx.end();
            BLImageData data;
            image.get_data(&data);
            return reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride)[x];
        }

        curvy::SketchDocument document_;
        curvy::NodeRef red_;
    };

}

TEST_F(CanvasCullTest, withoutARegionEveryShapeIsDrawn) {
    EXPECT_EQ(pixelAt(nullptr, 50, 50) & 0xFFFFFFu, 0xFF0000u);
    EXPECT_EQ(pixelAt(nullptr, 150, 50) & 0xFFFFFFu, 0x0000FFu);
}

TEST_F(CanvasCullTest, aShapeWhollyOutsideTheRegionIsSkippedAndTheOthersAreStillDrawn) {
    const BLBox leftHalf(0.0, 0.0, 100.0, 100.0);
    EXPECT_EQ(pixelAt(&leftHalf, 50, 50) & 0xFFFFFFu, 0xFF0000u);
    EXPECT_EQ(pixelAt(&leftHalf, 150, 50) & 0xFFFFFFu, 0xFFFFFFu) << "the blue circle lies outside the repaint region";
}

TEST_F(CanvasCullTest, aShapeTouchingTheRegionEdgeIsStillDrawn) {
    const BLBox sliver(65.0, 0.0, 130.0, 100.0);
    EXPECT_EQ(pixelAt(&sliver, 68, 50) & 0xFFFFFFu, 0xFF0000u) << "the red circle reaches x = 70";
    EXPECT_EQ(pixelAt(&sliver, 132, 50) & 0xFFFFFFu, 0x0000FFu) << "the blue circle reaches x = 130";
}

TEST_F(CanvasCullTest, aStrokeThatReachesIntoTheRegionKeepsItsShapeDrawn) {
    document_.setShapeLook(red_,curvy::SketchDocument::LookKind::Stroke, {{"visible", 1.0f}, {"r", 0.0f}, {"g", 0.0f}, {"b", 0.0f}, {"alpha", 1.0f}, {"width", 20.0f}});
    const BLBox beyondPath(75.0, 0.0, 200.0, 100.0);
    EXPECT_EQ(pixelAt(&beyondPath, 78, 50) & 0xFFFFFFu, 0x000000u) << "the path ends at x = 70 but its 20 wide stroke reaches 80";
}
