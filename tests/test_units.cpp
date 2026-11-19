#include "curvy/newdocumentmodel.h"
#include "curvy/settings.h"
#include "curvy/sketchdocument.h"
#include "curvy/units.h"

#include "testsupport.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using curvy::AngleUnit;
using curvy::DocumentUnits;
using curvy::LengthUnit;
using curvy::Units;

TEST(UnitsConversion, PointsPerUnit) {
    EXPECT_DOUBLE_EQ(1.0, Units::pointsPer(LengthUnit::Points, 72.0));
    EXPECT_DOUBLE_EQ(72.0, Units::pointsPer(LengthUnit::Inches, 72.0));
    EXPECT_DOUBLE_EQ(864.0, Units::pointsPer(LengthUnit::Feet, 72.0));
    EXPECT_DOUBLE_EQ(12.0, Units::pointsPer(LengthUnit::Picas, 72.0));
    EXPECT_NEAR(72.0 / 25.4, Units::pointsPer(LengthUnit::Millimeters, 72.0), 1e-12);
    EXPECT_NEAR(72.0 / 2.54, Units::pointsPer(LengthUnit::Centimeters, 72.0), 1e-12);
    EXPECT_NEAR(72.0 / 0.0254, Units::pointsPer(LengthUnit::Meters, 72.0), 1e-9);
    EXPECT_DOUBLE_EQ(0.75, Units::pointsPer(LengthUnit::DisplayPoints, 300.0));   // a Dip is 1/96 in whatever the ppi
}

TEST(UnitsConversion, PixelsFollowThePpi) {
    EXPECT_DOUBLE_EQ(1.0, Units::pointsPer(LengthUnit::Pixels, 72.0));
    EXPECT_DOUBLE_EQ(0.5, Units::pointsPer(LengthUnit::Pixels, 144.0));
    EXPECT_DOUBLE_EQ(1.0, Units::pointsPer(LengthUnit::Pixels, 0.0));   // a bad ppi counts as 72
    EXPECT_DOUBLE_EQ(150.0, Units::fromPoints(36.0, LengthUnit::Pixels, 300.0));
}

TEST(UnitsConversion, ARoundTripKeepsTheValue) {
    for (int i = 0; i < Units::kLengthCount; ++i) {
        const LengthUnit unit = LengthUnit(i);
        EXPECT_NEAR(12.5, Units::fromPoints(Units::toPoints(12.5, unit, 96.0), unit, 96.0), 1e-9) << Units::name(unit);
    }
}

TEST(UnitsConversion, Angles) {
    EXPECT_NEAR(180.0, Units::toDegrees(3.14159265358979, AngleUnit::Radians), 1e-9);
    EXPECT_NEAR(3.14159265358979 / 2.0, Units::fromDegrees(90.0, AngleUnit::Radians), 1e-12);
    EXPECT_DOUBLE_EQ(45.0, Units::toDegrees(45.0, AngleUnit::Degrees));
}

TEST(UnitsNames, EveryUnitNameReadsBack) {
    for (int i = 0; i < Units::kLengthCount; ++i) {
        const LengthUnit unit = LengthUnit(i);
        const std::optional<LengthUnit> back = Units::lengthFromName(Units::name(unit));
        ASSERT_TRUE(back.has_value()) << i;
        EXPECT_EQ(unit, *back);
    }
    EXPECT_EQ(LengthUnit::Inches, *Units::lengthFromName(" Inches "));
    EXPECT_EQ(LengthUnit::Millimeters, *Units::lengthFromName("MM"));
    EXPECT_FALSE(Units::lengthFromName("furlong").has_value());
    EXPECT_EQ(AngleUnit::Radians, *Units::angleFromName("Radians"));
    EXPECT_FALSE(Units::angleFromName("grad").has_value());
}

TEST(UnitsFormat, ShortestText) {
    EXPECT_EQ("8.5", Units::number(8.5));
    EXPECT_EQ("11", Units::number(11.0));
    EXPECT_EQ("0.333", Units::number(1.0 / 3.0));
    EXPECT_EQ("0", Units::number(-0.0001));
    EXPECT_EQ("12.5 mm", Units::format(12.5, LengthUnit::Millimeters));
    EXPECT_EQ("1.571 rad", Units::format(1.5707963, AngleUnit::Radians));
}

TEST(UnitsParse, ALengthWithAnOptionalSuffix) {
    double points = 0.0;
    EXPECT_TRUE(Units::parseLength("1in", LengthUnit::Millimeters, 72.0, points));
    EXPECT_DOUBLE_EQ(72.0, points);
    EXPECT_TRUE(Units::parseLength(" 25.4 MM ", LengthUnit::Pixels, 72.0, points));
    EXPECT_NEAR(72.0, points, 1e-9);
    EXPECT_TRUE(Units::parseLength("96 dip", LengthUnit::Pixels, 72.0, points));
    EXPECT_NEAR(72.0, points, 1e-9);
    EXPECT_TRUE(Units::parseLength("10", LengthUnit::Inches, 72.0, points));   // a bare number is in the fallback unit
    EXPECT_DOUBLE_EQ(720.0, points);
    EXPECT_TRUE(Units::parseLength("-2ft", LengthUnit::Pixels, 72.0, points));
    EXPECT_DOUBLE_EQ(-1728.0, points);
}

TEST(UnitsParse, GarbageIsRefusedAndLeavesTheValue) {
    double points = 123.0;
    EXPECT_FALSE(Units::parseLength("", LengthUnit::Pixels, 72.0, points));
    EXPECT_FALSE(Units::parseLength("mm", LengthUnit::Pixels, 72.0, points));
    EXPECT_FALSE(Units::parseLength("5 furlongs", LengthUnit::Pixels, 72.0, points));
    EXPECT_FALSE(Units::parseLength("5mm extra", LengthUnit::Pixels, 72.0, points));
    EXPECT_FALSE(Units::parseLength("nan", LengthUnit::Pixels, 72.0, points));
    EXPECT_DOUBLE_EQ(123.0, points);
}

TEST(UnitsParse, Angles) {
    double degrees = 0.0;
    EXPECT_TRUE(Units::parseAngle("90", AngleUnit::Degrees, degrees));
    EXPECT_DOUBLE_EQ(90.0, degrees);
    EXPECT_TRUE(Units::parseAngle("0.5 rad", AngleUnit::Degrees, degrees));
    EXPECT_NEAR(28.6478897565, degrees, 1e-6);
    EXPECT_TRUE(Units::parseAngle("1", AngleUnit::Radians, degrees));
    EXPECT_NEAR(57.2957795131, degrees, 1e-6);
    EXPECT_TRUE(Units::parseAngle("45\xC2\xB0", AngleUnit::Radians, degrees));
    EXPECT_DOUBLE_EQ(45.0, degrees);
    EXPECT_FALSE(Units::parseAngle("90 grad", AngleUnit::Degrees, degrees));
}

TEST(DocumentUnitsFile, AreSavedWithTheDocumentAndReadBack) {
    const std::filesystem::path path = curvytest::tempFile("curvy_test_units.curvy");
    std::filesystem::remove(path);
    curvy::SketchDocument saved;
    saved.newBlank("Millimetres", 595.0f, 842.0f, 300.0f, curvy::ColorSpace::CMYK, newui::Color(1.0f, 1.0f, 1.0f, 1.0f),
                   DocumentUnits{LengthUnit::Millimeters, AngleUnit::Radians});
    ASSERT_TRUE(saved.save(path.u8string()));

    curvy::SketchDocument loaded;
    ASSERT_TRUE(loaded.load(path.u8string()));
    EXPECT_EQ(LengthUnit::Millimeters, loaded.units().length);
    EXPECT_EQ(AngleUnit::Radians, loaded.units().angle);
    EXPECT_FALSE(loaded.isModified());
    std::filesystem::remove(path);
}

TEST(DocumentUnitsFile, AFileWithoutUnitsOrWithAnUnknownNameOpensAsPixelsAndDegrees) {
    const std::filesystem::path path = curvytest::tempFile("curvy_test_units_old.curvy");
    curvy::SketchDocument saved;
    saved.newBlank("Old", 100.0f, 100.0f, 72.0f, curvy::ColorSpace::RGB, newui::Color(1.0f, 1.0f, 1.0f, 1.0f),
                   DocumentUnits{LengthUnit::Feet, AngleUnit::Radians});
    ASSERT_TRUE(saved.save(path.u8string()));
    std::string text;
    {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
    }
    const std::size_t at = text.find("\"ft\"");
    ASSERT_NE(std::string::npos, at);
    text.replace(at, 4, "\"furlong\"");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }
    curvy::SketchDocument loaded;
    ASSERT_TRUE(loaded.load(path.u8string())) << loaded.lastError();
    EXPECT_EQ(LengthUnit::Pixels, loaded.units().length);
    EXPECT_EQ(AngleUnit::Radians, loaded.units().angle);   // each name is judged alone
    std::filesystem::remove(path);
}

TEST(DocumentUnitsEdit, SettingTheUnitsMarksTheDocumentModifiedButLeavesTheGeometry) {
    curvy::SketchDocument document;
    document.newBlank("Edit", 612.0f, 792.0f);
    EXPECT_EQ(LengthUnit::Pixels, document.units().length);
    EXPECT_FALSE(document.isModified());
    document.setUnits(DocumentUnits{LengthUnit::Inches, AngleUnit::Degrees});
    EXPECT_TRUE(document.isModified());
    EXPECT_EQ(612.0f, document.pageWidth());
    EXPECT_EQ(LengthUnit::Inches, document.units().length);
}

TEST(DocumentUnitsEdit, NewDocumentModelSetupShowsThePageInTheDocumentsUnit) {
    curvy::NewDocumentModel model;
    model.loadSetup(612.0, 792.0, 72.0, curvy::ColorSpace::RGB, curvy::PageBackground::White, LengthUnit::Inches);
    EXPECT_EQ(LengthUnit::Inches, model.unit());
    EXPECT_DOUBLE_EQ(8.5, model.width());
    EXPECT_DOUBLE_EQ(612.0, model.widthPoints());
}

TEST(UnitsQuantity, DisplayAndParseFollowTheDocumentUnits) {
    const DocumentUnits mm{LengthUnit::Millimeters, AngleUnit::Radians};
    EXPECT_NEAR(25.4, Units::toDisplay(72.0, curvy::Quantity::Length, mm, 72.0), 1e-9);
    EXPECT_NEAR(3.14159265358979, Units::toDisplay(180.0, curvy::Quantity::Angle, mm, 72.0), 1e-9);
    EXPECT_DOUBLE_EQ(7.0, Units::toDisplay(7.0, curvy::Quantity::None, mm, 72.0));

    double stored = 0.0;
    ASSERT_TRUE(Units::parseQuantity("25.4", curvy::Quantity::Length, mm, 72.0, stored));
    EXPECT_NEAR(72.0, stored, 1e-9);
    ASSERT_TRUE(Units::parseQuantity("1in", curvy::Quantity::Length, mm, 72.0, stored));
    EXPECT_NEAR(72.0, stored, 1e-9);
    ASSERT_TRUE(Units::parseQuantity("90deg", curvy::Quantity::Angle, mm, 72.0, stored));
    EXPECT_NEAR(90.0, stored, 1e-9);
    ASSERT_TRUE(Units::parseQuantity("2", curvy::Quantity::None, mm, 72.0, stored));
    EXPECT_DOUBLE_EQ(2.0, stored);
    EXPECT_FALSE(Units::parseQuantity("two", curvy::Quantity::None, mm, 72.0, stored));
    EXPECT_FALSE(Units::parseQuantity("5 parsecs", curvy::Quantity::Length, mm, 72.0, stored));
}

TEST(UnitsQuantity, DisplayScaleIsTheSizeOfOneDisplayedUnit) {
    const DocumentUnits inches{LengthUnit::Inches, AngleUnit::Degrees};
    EXPECT_NEAR(1.0 / 72.0, Units::displayScale(curvy::Quantity::Length, inches, 72.0), 1e-12);
    EXPECT_DOUBLE_EQ(1.0, Units::displayScale(curvy::Quantity::Angle, inches, 72.0));
    EXPECT_DOUBLE_EQ(1.0, Units::displayScale(curvy::Quantity::None, inches, 72.0));
    EXPECT_EQ(curvy::Quantity::Length, Units::quantityFromName("length"));
    EXPECT_EQ(curvy::Quantity::None, Units::quantityFromName("area"));
}

TEST(UnitsQuantity, TheDefaultUnitsSurviveTheSettingsFile) {
    curvy::Settings settings;
    settings.defaultUnits = DocumentUnits{LengthUnit::Feet, AngleUnit::Radians};
    curvy::Settings loaded;
    loaded.fromText(settings.toText());
    EXPECT_EQ(settings.defaultUnits, loaded.defaultUnits);

    curvy::Settings garbled;
    garbled.fromText("{ workspace: { defaultLength: \"furlongs\", defaultAngle: 7 } }");
    EXPECT_EQ(DocumentUnits(), garbled.defaultUnits) << "an unknown name keeps the default";
}
