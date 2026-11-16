#include "curvy/paletteextract.h"

#include <blend2d/blend2d.h>
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>

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

namespace {

    // A 4 by 4 PNG: the left half one colour, the right half another.
    std::string writePng(const char* name, Rgba left, Rgba right) {
        const std::string path = (std::filesystem::temp_directory_path() / name).u8string();
        BLImage image(4, 4, BL_FORMAT_PRGB32);
        BLImageData data;
        image.get_data(&data);
        for (int y = 0; y < 4; ++y) {
            std::uint32_t* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(data.pixel_data) + std::ptrdiff_t(y) * data.stride);
            for (int x = 0; x < 4; ++x) {
                const Rgba& c = x < 2 ? left : right;
                row[x] = (std::uint32_t(c.a) << 24) | (std::uint32_t(c.r * c.a / 255) << 16) | (std::uint32_t(c.g * c.a / 255) << 8) | std::uint32_t(c.b * c.a / 255);
            }
        }
        image.write_to_file(path.c_str());
        return path;
    }

}

TEST(PaletteExtract, anImageFileGivesItsMainColours) {
    const std::string path = writePng("curvyPaletteTest.png", {200, 40, 40}, {40, 40, 200});
    std::string error;
    PaletteExtractOptions options;
    options.count = 2;
    const std::vector<Swatch> swatches = PaletteExtract::fromFile(path, options, &error);
    std::filesystem::remove(path);
    EXPECT_EQ(error, "");
    ASSERT_EQ(swatches.size(), 2u);
    const auto near = [](const Swatch& s, int r, int g, int b) {
        return std::abs(int(std::lround(s.r * 255.0f)) - r) <= 1 && std::abs(int(std::lround(s.g * 255.0f)) - g) <= 1 && std::abs(int(std::lround(s.b * 255.0f)) - b) <= 1;
    };
    EXPECT_TRUE((near(swatches[0], 200, 40, 40) && near(swatches[1], 40, 40, 200)) || (near(swatches[1], 200, 40, 40) && near(swatches[0], 40, 40, 200)));
}

TEST(PaletteExtract, aTranslucentPixelKeepsItsStraightColour) {
    const std::string path = writePng("curvyPaletteAlpha.png", {200, 40, 40, 128}, {200, 40, 40, 128});
    PaletteExtractOptions options;
    options.count = 1;
    const std::vector<Swatch> swatches = PaletteExtract::fromFile(path, options);
    std::filesystem::remove(path);
    ASSERT_EQ(swatches.size(), 1u);
    EXPECT_NEAR(swatches[0].r * 255.0f, 200.0f, 3.0f) << "the premultiplied value is turned back into the colour, not left darker";
    EXPECT_NEAR(swatches[0].g * 255.0f, 40.0f, 3.0f);
}

TEST(PaletteExtract, aFileThatIsNotAnImageSaysSo) {
    const std::filesystem::path text = std::filesystem::temp_directory_path() / "curvyNotAnImage.png";
    {
        std::ofstream out(text);
        out << "not an image";
    }
    std::string error;
    EXPECT_TRUE(PaletteExtract::fromFile(text.u8string(), {}, &error).empty());
    EXPECT_NE(error, "");
    std::filesystem::remove(text);
    error.clear();
    EXPECT_TRUE(PaletteExtract::fromFile((std::filesystem::temp_directory_path() / "curvyMissing.png").u8string(), {}, &error).empty());
    EXPECT_NE(error, "");
}
