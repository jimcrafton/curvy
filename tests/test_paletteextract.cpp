#include "curvy/paletteextract.h"

#include <gtest/gtest.h>

#include <cmath>

using curvy::PaletteExtract;
using curvy::PaletteExtractOptions;
using curvy::Swatch;

namespace {

    struct Rgba {
        int r, g, b, a = 255;
    };

    // An image of `width` x `height`, made of vertical bands: each band is a colour and how many columns wide it is.
    std::vector<std::uint8_t> bands(int width, int height, const std::vector<std::pair<Rgba, int>>& list) {
        std::vector<std::uint8_t> pixels(std::size_t(width) * std::size_t(height) * 4);
        for (int y = 0; y < height; ++y) {
            int x = 0;
            for (const auto& [color, columns] : list) {
                for (int i = 0; i < columns && x < width; ++i, ++x) {
                    std::uint8_t* p = &pixels[(std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4];
                    p[0] = std::uint8_t(color.r);
                    p[1] = std::uint8_t(color.g);
                    p[2] = std::uint8_t(color.b);
                    p[3] = std::uint8_t(color.a);
                }
            }
        }
        return pixels;
    }

    std::vector<Swatch> extract(const std::vector<std::uint8_t>& pixels, int width, int height, const PaletteExtractOptions& options) {
        return PaletteExtract::fromPixels(pixels.data(), width, height, options);
    }

    bool is(const Swatch& s, int r, int g, int b) {
        return std::lround(s.r * 255.0f) == r && std::lround(s.g * 255.0f) == g && std::lround(s.b * 255.0f) == b;
    }

    const Rgba red{200, 30, 30}, blue{30, 40, 200}, green{40, 190, 60}, yellow{230, 210, 40};

}

TEST(PaletteExtract, twoBlocksGiveTwoSwatchesMostUsedFirst) {
    const auto image = bands(40, 10, {{red, 30}, {blue, 10}});
    PaletteExtractOptions options;
    options.count = 2;
    const auto found = extract(image, 40, 10, options);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_TRUE(is(found[0], 200, 30, 30));
    EXPECT_TRUE(is(found[1], 30, 40, 200));
    EXPECT_EQ(found[0].link, 0u);
    EXPECT_TRUE(found[0].name.empty());
}

TEST(PaletteExtract, theCountLimitsTheSwatches) {
    const auto image = bands(40, 10, {{red, 10}, {blue, 10}, {green, 10}, {yellow, 10}});
    PaletteExtractOptions options;
    options.count = 4;
    EXPECT_EQ(extract(image, 40, 10, options).size(), 4u);
    options.count = 2;
    EXPECT_EQ(extract(image, 40, 10, options).size(), 2u);
    options.count = 1;
    EXPECT_EQ(extract(image, 40, 10, options).size(), 1u);
}

TEST(PaletteExtract, anImageWithFewerColoursGivesFewerSwatches) {
    const auto image = bands(20, 10, {{red, 10}, {blue, 10}});
    PaletteExtractOptions options;
    options.count = 8;
    EXPECT_EQ(extract(image, 20, 10, options).size(), 2u);
}

TEST(PaletteExtract, nearWhiteAndNearBlackCanBeLeftOut) {
    const auto image = bands(40, 10, {{{255, 255, 255}, 20}, {{0, 0, 0}, 10}, {red, 10}});
    PaletteExtractOptions options;
    options.count = 4;
    options.ignoreNearWhiteAndBlack = true;
    auto found = extract(image, 40, 10, options);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_TRUE(is(found[0], 200, 30, 30));

    options.ignoreNearWhiteAndBlack = false;
    found = extract(image, 40, 10, options);
    ASSERT_EQ(found.size(), 3u);
    EXPECT_TRUE(is(found[0], 255, 255, 255));
}

TEST(PaletteExtract, nearDuplicatesAreMergedOnRequest) {
    const auto image = bands(30, 10, {{{200, 30, 30}, 10}, {{210, 36, 30}, 10}, {blue, 10}});
    PaletteExtractOptions options;
    options.count = 3;
    options.mergeNearDuplicates = false;
    EXPECT_EQ(extract(image, 30, 10, options).size(), 3u);
    options.mergeNearDuplicates = true;
    const auto found = extract(image, 30, 10, options);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_TRUE(is(found[0], 205, 33, 30)) << "the merged swatch is the average of its two";
}

TEST(PaletteExtract, theOrderCanBeLightnessOrHue) {
    const auto image = bands(40, 10, {{{250, 240, 120}, 20}, {{20, 20, 120}, 10}, {{200, 30, 30}, 10}});
    PaletteExtractOptions options;
    options.count = 3;
    options.order = PaletteExtractOptions::Order::Lightness;
    auto found = extract(image, 40, 10, options);
    ASSERT_EQ(found.size(), 3u);
    EXPECT_TRUE(is(found[0], 20, 20, 120));
    EXPECT_TRUE(is(found[2], 250, 240, 120));

    options.order = PaletteExtractOptions::Order::Hue;
    found = extract(image, 40, 10, options);
    ASSERT_EQ(found.size(), 3u);
    EXPECT_TRUE(is(found[0], 200, 30, 30)) << "red is hue 0";
    EXPECT_TRUE(is(found[1], 250, 240, 120)) << "yellow comes before blue";
    EXPECT_TRUE(is(found[2], 20, 20, 120));
}

TEST(PaletteExtract, transparentPixelsAreIgnored) {
    const auto image = bands(20, 10, {{{10, 200, 10, 0}, 10}, {red, 10}});
    PaletteExtractOptions options;
    options.count = 4;
    const auto found = extract(image, 20, 10, options);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_TRUE(is(found[0], 200, 30, 30));
}

TEST(PaletteExtract, aLargeImageIsSampledAndGivesTheSameColours) {
    const auto image = bands(1000, 600, {{red, 500}, {blue, 500}});
    PaletteExtractOptions options;
    options.count = 2;
    const auto found = extract(image, 1000, 600, options);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_TRUE(is(found[0], 200, 30, 30) || is(found[0], 30, 40, 200));
}

TEST(PaletteExtract, nothingUsableGivesNothing) {
    PaletteExtractOptions options;
    EXPECT_TRUE(PaletteExtract::fromPixels(nullptr, 10, 10, options).empty());
    const std::vector<std::uint8_t> one(4, 255);
    EXPECT_TRUE(PaletteExtract::fromPixels(one.data(), 0, 1, options).empty());
    EXPECT_TRUE(extract(one, 1, 1, options).empty()) << "a single white pixel is left out";
    options.ignoreNearWhiteAndBlack = false;
    EXPECT_EQ(extract(one, 1, 1, options).size(), 1u);
}

TEST(PaletteExtract, theSameImageGivesTheSameSwatches) {
    const auto image = bands(60, 10, {{red, 20}, {blue, 20}, {green, 10}, {yellow, 10}});
    PaletteExtractOptions options;
    options.count = 6;
    EXPECT_EQ(extract(image, 60, 10, options), extract(image, 60, 10, options));
}
