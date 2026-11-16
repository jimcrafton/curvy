#include "curvy/swatchimport.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>

using curvy::Swatch;
using curvy::SwatchImport;
using curvy::SwatchImportResult;

namespace {

    // Builds the big-endian bytes of a swatch file.
    class Bytes {
    public:
        Bytes& u16(unsigned value) {
            bytes_.push_back(std::uint8_t(value >> 8));
            bytes_.push_back(std::uint8_t(value & 0xFF));
            return *this;
        }
        Bytes& u32(std::uint32_t value) {
            u16(value >> 16);
            return u16(value & 0xFFFF);
        }
        Bytes& f32(float value) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return u32(bits);
        }
        Bytes& tag(const char* text) {
            for (int i = 0; i < 4; ++i) {
                bytes_.push_back(std::uint8_t(text[i]));
            }
            return *this;
        }
        // UTF-16 code units, then a zero; the caller gives the unit count (with the zero) first when the format wants it.
        Bytes& units(const std::vector<unsigned>& list) {
            for (unsigned unit : list) {
                u16(unit);
            }
            return u16(0);
        }
        const std::vector<std::uint8_t>& get() const { return bytes_; }

    private:
        std::vector<std::uint8_t> bytes_;
    };

    // "Red" followed by the terminating zero is four units.
    const std::vector<unsigned> kRed = {'R', 'e', 'd'};

    void expectColor(const Swatch& swatch, float r, float g, float b, float tolerance = 0.002f) {
        EXPECT_NEAR(swatch.r, r, tolerance);
        EXPECT_NEAR(swatch.g, g, tolerance);
        EXPECT_NEAR(swatch.b, b, tolerance);
    }

}

TEST(SwatchImportTest, cmykInksCombineWithTheBlack) {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    SwatchImport::cmykToRgb(0.0f, 0.0f, 0.0f, 0.0f, r, g, b);
    EXPECT_EQ(r + g + b, 3.0f);
    SwatchImport::cmykToRgb(0.0f, 0.0f, 0.0f, 1.0f, r, g, b);
    EXPECT_EQ(r + g + b, 0.0f);
    SwatchImport::cmykToRgb(1.0f, 0.0f, 0.0f, 0.0f, r, g, b);
    EXPECT_EQ(r, 0.0f);
    EXPECT_EQ(g, 1.0f);
    EXPECT_EQ(b, 1.0f);
}

TEST(SwatchImportTest, hsbGivesThePrimaries) {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    SwatchImport::hsbToRgb(0.0f, 1.0f, 1.0f, r, g, b);
    EXPECT_NEAR(r, 1.0f, 1e-5f);
    EXPECT_NEAR(g + b, 0.0f, 1e-5f);
    SwatchImport::hsbToRgb(120.0f, 1.0f, 1.0f, r, g, b);
    EXPECT_NEAR(g, 1.0f, 1e-5f);
    EXPECT_NEAR(r + b, 0.0f, 1e-5f);
    SwatchImport::hsbToRgb(240.0f, 1.0f, 0.5f, r, g, b);
    EXPECT_NEAR(b, 0.5f, 1e-5f);
    SwatchImport::hsbToRgb(-120.0f, 1.0f, 1.0f, r, g, b);   // a negative hue wraps round
    EXPECT_NEAR(b, 1.0f, 1e-5f);
}

TEST(SwatchImportTest, labGivesWhiteBlackAndARecognisableRed) {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    SwatchImport::labToRgb(100.0f, 0.0f, 0.0f, r, g, b);
    EXPECT_NEAR(r, 1.0f, 0.01f);
    EXPECT_NEAR(g, 1.0f, 0.01f);
    EXPECT_NEAR(b, 1.0f, 0.01f);
    SwatchImport::labToRgb(0.0f, 0.0f, 0.0f, r, g, b);
    EXPECT_NEAR(r + g + b, 0.0f, 0.01f);
    SwatchImport::labToRgb(53.2f, 80.1f, 67.2f, r, g, b);   // sRGB red in Lab
    EXPECT_NEAR(r, 1.0f, 0.05f);
    EXPECT_NEAR(g, 0.0f, 0.08f);
    EXPECT_NEAR(b, 0.0f, 0.08f);
}

TEST(SwatchImportTest, anAcoFileOfTheFirstVersionGivesUnnamedSwatches) {
    Bytes file;
    file.u16(1).u16(3);
    file.u16(0).u16(65535).u16(0).u16(0).u16(0);                 // RGB red
    file.u16(2).u16(0).u16(65535).u16(65535).u16(65535);         // CMYK stored inverted: full cyan
    file.u16(8).u16(5000).u16(0).u16(0).u16(0);                  // Gray, 0..10000
    const SwatchImportResult result = SwatchImport::fromAco(file.get(), "Mine");
    ASSERT_EQ(result.error, "");
    ASSERT_EQ(result.palettes.size(), 1u);
    EXPECT_EQ(result.palettes[0].name, "Mine");
    ASSERT_EQ(result.palettes[0].swatches.size(), 3u);
    expectColor(result.palettes[0].swatches[0], 1.0f, 0.0f, 0.0f);
    expectColor(result.palettes[0].swatches[1], 0.0f, 1.0f, 1.0f);
    expectColor(result.palettes[0].swatches[2], 0.5f, 0.5f, 0.5f);
    EXPECT_EQ(result.palettes[0].swatches[0].name, "");
    EXPECT_EQ(result.colorCount(), 3u);
}

TEST(SwatchImportTest, theSecondVersionSectionBringsTheNames) {
    Bytes file;
    file.u16(1).u16(1).u16(0).u16(65535).u16(0).u16(0).u16(0);
    file.u16(2).u16(1);
    file.u16(0).u16(65535).u16(0).u16(0).u16(0).u32(4).units(kRed);
    const SwatchImportResult result = SwatchImport::fromAco(file.get(), "Mine");
    ASSERT_EQ(result.error, "");
    ASSERT_EQ(result.palettes[0].swatches.size(), 1u);
    EXPECT_EQ(result.palettes[0].swatches[0].name, "Red");
}

TEST(SwatchImportTest, namesKeepNonAsciiLettersAsUtf8) {
    Bytes file;
    file.u16(2).u16(1);
    file.u16(0).u16(0).u16(65535).u16(0).u16(0).u32(2).units({0x00FC});   // "ü"
    const SwatchImportResult result = SwatchImport::fromAco(file.get(), "Mine");
    ASSERT_EQ(result.error, "");
    EXPECT_EQ(result.palettes[0].swatches[0].name, "\xC3\xBC");
}

TEST(SwatchImportTest, anAcoFileThatIsCutShortIsRefusedWithAMessage) {
    Bytes file;
    file.u16(1).u16(2).u16(0).u16(65535).u16(0).u16(0).u16(0);   // says two colours, holds one
    const SwatchImportResult result = SwatchImport::fromAco(file.get(), "Mine");
    EXPECT_NE(result.error, "");
    EXPECT_TRUE(result.palettes.empty());
    EXPECT_NE(SwatchImport::fromAco({}, "x").error, "");
    Bytes wrong;
    wrong.u16(7).u16(0);
    EXPECT_NE(SwatchImport::fromAco(wrong.get(), "x").error, "");
}

TEST(SwatchImportTest, anAseFileGivesOnePalettePerGroupAndOneForLooseColours) {
    Bytes file;
    file.tag("ASEF").u16(1).u16(0).u32(5);
    // A loose colour, 2 + 2*(3+1) + 4 + 12 + 2 = 28 bytes.
    file.u16(0x0001).u32(28).u16(4).units(kRed).tag("RGB ").f32(1.0f).f32(0.0f).f32(0.0f).u16(2);
    // A group "Ab": 2 + 2*3 = 8 bytes, a Gray colour "Red" inside it (the length counts the same fields), then the end.
    file.u16(0xC001).u32(8).u16(3).units({'A', 'b'});
    file.u16(0x0001).u32(2 + 8 + 4 + 4 + 2).u16(4).units(kRed).tag("Gray").f32(0.25f).u16(0);
    file.u16(0xC002).u32(0);
    file.u16(0x0001).u32(2 + 8 + 4 + 16 + 2).u16(4).units(kRed).tag("CMYK").f32(0.0f).f32(1.0f).f32(1.0f).f32(0.0f).u16(1);   // a loose red in CMYK
    const SwatchImportResult result = SwatchImport::fromAse(file.get(), "Loose");
    ASSERT_EQ(result.error, "");
    ASSERT_EQ(result.palettes.size(), 2u);
    EXPECT_EQ(result.palettes[0].name, "Loose");
    ASSERT_EQ(result.palettes[0].swatches.size(), 2u);
    expectColor(result.palettes[0].swatches[0], 1.0f, 0.0f, 0.0f);
    expectColor(result.palettes[0].swatches[1], 1.0f, 0.0f, 0.0f);
    EXPECT_EQ(result.palettes[0].swatches[0].name, "Red");
    EXPECT_EQ(result.palettes[1].name, "Ab");
    ASSERT_EQ(result.palettes[1].swatches.size(), 1u);
    expectColor(result.palettes[1].swatches[0], 0.25f, 0.25f, 0.25f);
}

TEST(SwatchImportTest, anAseBlockOfAKindThatIsNotUnderstoodIsSkippedByItsLength) {
    Bytes file;
    file.tag("ASEF").u16(1).u16(0).u32(2);
    file.u16(0x7777).u32(6).u16(1).u16(2).u16(3);
    file.u16(0x0001).u32(2 + 8 + 4 + 12 + 2).u16(4).units(kRed).tag("RGB ").f32(0.0f).f32(1.0f).f32(0.0f).u16(2);
    const SwatchImportResult result = SwatchImport::fromAse(file.get(), "Loose");
    ASSERT_EQ(result.error, "");
    expectColor(result.palettes[0].swatches[0], 0.0f, 1.0f, 0.0f);
}

TEST(SwatchImportTest, anAseFileWithTheWrongMagicOrNoColoursIsRefused) {
    Bytes wrong;
    wrong.tag("NOPE").u16(1).u16(0).u32(0);
    EXPECT_NE(SwatchImport::fromAse(wrong.get(), "x").error, "");
    Bytes empty;
    empty.tag("ASEF").u16(1).u16(0).u32(0);
    EXPECT_NE(SwatchImport::fromAse(empty.get(), "x").error, "");
}

TEST(SwatchImportTest, aFileIsChosenByItsExtensionAndNamedAfterItsStem) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    const std::filesystem::path aco = dir / "curvyImportTest.ACO";
    Bytes file;
    file.u16(1).u16(1).u16(0).u16(65535).u16(0).u16(0).u16(0);
    {
        std::ofstream out(aco, std::ios::binary);
        out.write(reinterpret_cast<const char*>(file.get().data()), std::streamsize(file.get().size()));
    }
    const SwatchImportResult result = SwatchImport::fromFile(aco.u8string());
    EXPECT_EQ(result.error, "");
    ASSERT_EQ(result.palettes.size(), 1u);
    EXPECT_EQ(result.palettes[0].name, "curvyImportTest");
    std::filesystem::remove(aco);

    EXPECT_NE(SwatchImport::fromFile((dir / "notes.txt").u8string()).error, "");
    EXPECT_NE(SwatchImport::fromFile((dir / "missing.ase").u8string()).error, "");
}
