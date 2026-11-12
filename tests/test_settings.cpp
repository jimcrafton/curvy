#include "curvy/settings.h"

#include "testsupport.h"

#include <filesystem>

using curvy::Settings;

namespace {

    Settings everythingChanged() {
        Settings changed;
        changed.theme = Settings::ThemeChoice::FollowWindows;
        changed.canvasBackground = 0x123456;
        changed.canvasFollowsTheme = false;
        changed.propertyTextSize = 14;
        changed.rulersVisible = false;
        changed.hintsFade = false;
        changed.recentLength = 25;
        changed.scrubAxis = Settings::ScrubAxis::Horizontal;
        changed.scrubSensitivity = 150;
        changed.spaceTapMs = 450;
        changed.barrelButton = Settings::PenButton::Pan;
        changed.eraserEnd = Settings::EraserAction::Undo;
        changed.snapGrid = true;
        changed.gridSize = 12.5;
        changed.snapGuides = false;
        changed.toolDefaultsInDocument = true;
        changed.polygonSides = 7;
        changed.starPoints = 9;
        changed.cornerRadius = 30;
        changed.starInner = 35;
        changed.pencilSmoothing = 80;
        changed.autosave = false;
        changed.autosaveMinutes = 15;
        changed.exportFolder = "D:/exports";
        changed.window = {-1200, 80, 1100, 700, true, "\\\\.\\DISPLAY2"};
        changed.docks[0] = {90.0f, false};
        changed.docks[2] = {300.0f, false};
        return changed;
    }
}

TEST(SettingsFile, AMissingFileGivesTheDefaults) {
    const auto file = curvytest::tempFile("curvy_test_missing.json5");
    std::filesystem::remove(file);
    const Settings loaded = Settings::load(file.u8string());
    EXPECT_EQ(Settings::ThemeChoice::Dark, loaded.theme);
    EXPECT_FALSE(loaded.window.known());
    EXPECT_TRUE(loaded.docks[1].collapsed);
}

TEST(SettingsFile, EverySettingRoundTrips) {
    const auto file = curvytest::tempFile("curvy_test_settings.json5");
    std::filesystem::remove(file);
    const Settings changed = everythingChanged();
    ASSERT_TRUE(changed.save(file.u8string()));
    const Settings back = Settings::load(file.u8string());
    std::filesystem::remove(file);

    EXPECT_EQ(changed.theme, back.theme);
    EXPECT_EQ(0x123456u, back.canvasBackground);
    EXPECT_FALSE(back.canvasFollowsTheme);
    EXPECT_EQ(14, back.propertyTextSize);
    EXPECT_FALSE(back.rulersVisible);
    EXPECT_FALSE(back.hintsFade);
    EXPECT_EQ(25, back.recentLength);
    EXPECT_EQ(Settings::ScrubAxis::Horizontal, back.scrubAxis);
    EXPECT_EQ(150, back.scrubSensitivity);
    EXPECT_EQ(450, back.spaceTapMs);
    EXPECT_EQ(Settings::PenButton::Pan, back.barrelButton);
    EXPECT_EQ(Settings::EraserAction::Undo, back.eraserEnd);
    EXPECT_TRUE(back.snapGrid);
    EXPECT_EQ(12.5, back.gridSize);
    EXPECT_FALSE(back.snapGuides);
    EXPECT_TRUE(back.toolDefaultsInDocument);
    EXPECT_EQ(7, back.polygonSides);
    EXPECT_EQ(9, back.starPoints);
    EXPECT_EQ(30, back.cornerRadius);
    EXPECT_EQ(35, back.starInner);
    EXPECT_EQ(80, back.pencilSmoothing);
    EXPECT_FALSE(back.autosave);
    EXPECT_EQ(15, back.autosaveMinutes);
    EXPECT_EQ("D:/exports", back.exportFolder);
}

TEST(SettingsFile, TheWindowRoundTripsWithItsScreen) {
    const auto file = curvytest::tempFile("curvy_test_window.json5");
    std::filesystem::remove(file);
    ASSERT_TRUE(everythingChanged().save(file.u8string()));
    const Settings back = Settings::load(file.u8string());
    std::filesystem::remove(file);

    EXPECT_EQ(-1200, back.window.x) << "a negative x is a screen to the left";
    EXPECT_EQ(80, back.window.y);
    EXPECT_EQ(1100, back.window.width);
    EXPECT_EQ(700, back.window.height);
    EXPECT_TRUE(back.window.maximized);
    EXPECT_EQ("\\\\.\\DISPLAY2", back.window.monitor);
}

TEST(SettingsFile, TheDockAreasRoundTrip) {
    const auto file = curvytest::tempFile("curvy_test_docks.json5");
    std::filesystem::remove(file);
    ASSERT_TRUE(everythingChanged().save(file.u8string()));
    const Settings back = Settings::load(file.u8string());
    std::filesystem::remove(file);

    EXPECT_EQ(90.0f, back.docks[0].size);
    EXPECT_FALSE(back.docks[0].collapsed);
    EXPECT_TRUE(back.docks[1].collapsed);
    EXPECT_EQ(300.0f, back.docks[2].size);
    EXPECT_FALSE(back.docks[2].collapsed);
}

TEST(SettingsFile, UnknownAndOutOfRangeValuesAreSkipped) {
    Settings bad;
    bad.fromText("{ appearance: { theme: 'neon', canvasBackground: 'red', propertyTextSize: 99 }, pen: { spaceTapMs: 5 }, "
                 "window: { x: 10, y: 10, width: 5, height: 5 }, docks: { left: { size: 10, collapsed: 'yes' } } }");
    EXPECT_EQ(Settings::ThemeChoice::Dark, bad.theme);
    EXPECT_EQ(0x2A2A2Au, bad.canvasBackground);
    EXPECT_EQ(11, bad.propertyTextSize);
    EXPECT_EQ(300, bad.spaceTapMs);
    EXPECT_FALSE(bad.window.known()) << "a tiny window is skipped";
    EXPECT_EQ(220.0f, bad.docks[1].size) << "a tiny dock size is skipped";
    EXPECT_TRUE(bad.docks[1].collapsed);
}

TEST(SettingsFile, ADamagedFileChangesNothing) {
    Settings broken;
    broken.fromText("{ appearance: { theme: 'light' ");
    EXPECT_EQ(Settings::ThemeChoice::Dark, broken.theme);
}

TEST(SettingsFile, ResettingAPagePutsBackOnlyThatPage) {
    Settings reset = everythingChanged();
    reset.resetPage(curvy::SettingsPage::Pen);
    EXPECT_EQ(300, reset.spaceTapMs);
    EXPECT_EQ(Settings::ThemeChoice::FollowWindows, reset.theme);
}
