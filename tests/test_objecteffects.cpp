#include "curvy/canvasview.h"
#include "curvy/sketchdocument.h"

#include <blend2d/blend2d.h>

#include <gtest/gtest.h>

namespace {

    class ObjectEffectsTest : public ::testing::Test {
    protected:
        void SetUp() override {
            document_.newBlank("Effects", 100.0f, 100.0f);
            layer_ = document_.addLayer();
        }

        // The page at 1:1 on white, as 0xAARRGGBB pixels.
        std::vector<std::uint32_t> render() {
            BLImage image(100, 100, BL_FORMAT_PRGB32);
            BLContext ctx(image);
            ctx.fill_all(BLRgba32(0xFFFFFFFFu));
            curvy::CanvasView::renderArtworkAtScale(ctx, document_, 0.0, 0.0, 1.0);
            ctx.end();
            BLImageData data;
            image.get_data(&data);
            std::vector<std::uint32_t> pixels(100 * 100);
            for (int y = 0; y < 100; ++y) {
                const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride);
                for (int x = 0; x < 100; ++x) {
                    pixels[std::size_t(y) * 100 + std::size_t(x)] = row[x];
                }
            }
            return pixels;
        }
        static int channel(std::uint32_t pixel, int shift) {
            return int((pixel >> shift) & 0xFFu);
        }

        curvy::SketchDocument document_;
        curvy::NodeRef layer_;
    };

}

TEST_F(ObjectEffectsTest, aLayerAtHalfOpacityBlendsItsShapesWithThePageBelow) {
    document_.addCircle(layer_, 50.0f, 50.0f, 20.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(channel(render()[50 * 100 + 50], 8), 0);
    document_.setGraphParam(layer_, "opacity", 0.5f);
    const std::uint32_t pixel = render()[50 * 100 + 50];
    EXPECT_NEAR(channel(pixel, 8), 128, 3);
    EXPECT_GT(channel(pixel, 16), 250);
}

TEST_F(ObjectEffectsTest, aFadedGroupFadesAsOneSoItsOverlappingShapesDoNotShowThrough) {
    const curvy::NodeRef group = document_.addGroup(layer_);
    document_.addCircle(group, 40.0f, 50.0f, 20.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
    document_.addCircle(group, 60.0f, 50.0f, 20.0f, newui::Color(0.0f, 0.0f, 1.0f, 1.0f));
    document_.setGraphParam(group, "opacity", 0.5f);
    const std::vector<std::uint32_t> pixels = render();
    const std::uint32_t overlap = pixels[50 * 100 + 50];   // inside both circles: the blue one is on top
    const std::uint32_t blueOnly = pixels[50 * 100 + 70];
    EXPECT_NEAR(channel(overlap, 16), channel(blueOnly, 16), 3) << "the red circle below shows through the blue one";
    EXPECT_NEAR(channel(overlap, 0), channel(blueOnly, 0), 3);
}

TEST_F(ObjectEffectsTest, theMultiplyBlendDarkensWhatIsBelow) {
    document_.addCircle(layer_, 50.0f, 50.0f, 30.0f, newui::Color(1.0f, 1.0f, 0.0f, 1.0f));
    const curvy::NodeRef top = document_.addLayer("Top");
    document_.addCircle(top, 50.0f, 50.0f, 15.0f, newui::Color(0.0f, 1.0f, 1.0f, 1.0f));
    document_.setGraphParam(top, "blend", 1.0f);
    const std::uint32_t pixel = render()[50 * 100 + 50];
    EXPECT_LT(channel(pixel, 16), 10);   // yellow times cyan is green
    EXPECT_GT(channel(pixel, 8), 245);
    EXPECT_LT(channel(pixel, 0), 10);
}

TEST_F(ObjectEffectsTest, anObjectAtTheDefaultsPaintsExactlyAsBefore) {
    document_.addCircle(layer_, 50.0f, 50.0f, 20.0f, newui::Color(1.0f, 0.0f, 0.0f, 1.0f));
    const std::vector<std::uint32_t> before = render();
    document_.setGraphParam(layer_, "opacity", 0.5f);
    document_.setGraphParam(layer_, "opacity", 1.0f);
    EXPECT_EQ(render(), before);
}

TEST(ObjectEffectsFiles, aSampleSavedBeforeTheParametersExistedGetsThemWhenOpened) {
    curvy::SketchDocument document;
    ASSERT_TRUE(document.load("Resources/samples/layersAndGroups.curvy")) << document.lastError();
    const std::vector<curvy::NodeRef> layers = document.childrenOf();
    ASSERT_FALSE(layers.empty());
    EXPECT_TRUE(document.node(layers[0]).hasFloat("opacity"));
    EXPECT_TRUE(document.node(layers[0]).hasFloat("blend"));
}
