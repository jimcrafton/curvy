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
        changed.editorFont = "Hack-Regular";
        changed.editorFontSize = 16;
        changed.editorTabWidth = 2;
        changed.scriptTimeout = true;
        changed.scriptTimeoutSeconds = 120;
        changed.editorColors = {{"keyword", 0xFF8800}, {"string", 0x00AA55}, {"text", 0xE0E0E0}, {"background", 0x101820}};
        changed.editorFile = "D:/scripts/my script.py";
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
    EXPECT_EQ("Hack-Regular", back.editorFont);
    EXPECT_EQ(16, back.editorFontSize);
    EXPECT_EQ(2, back.editorTabWidth);
    EXPECT_TRUE(back.scriptTimeout);
    EXPECT_EQ(120, back.scriptTimeoutSeconds);
    EXPECT_EQ(changed.editorColors, back.editorColors);
    EXPECT_EQ("D:/scripts/my script.py", back.editorFile);
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

TEST(SettingsFile, theScriptEditorDefaultsAreAutomaticFontFourSpacesAndNoColourOverrides) {
    const Settings defaults;
    EXPECT_TRUE(defaults.editorFont.empty());
    EXPECT_EQ(14, defaults.editorFontSize);
    EXPECT_EQ(4, defaults.editorTabWidth);
    EXPECT_TRUE(defaults.editorColors.empty());
    EXPECT_FALSE(defaults.scriptTimeout) << "scripts run until they end unless the user opts in";
    EXPECT_EQ(60, defaults.scriptTimeoutSeconds);
}

TEST(SettingsFile, badScriptEditorValuesAreSkipped) {
    Settings bad;
    bad.fromText("{ scriptEditor: { fontSize: 500, tabWidth: 0, scriptTimeoutSeconds: 0, colors: { keyword: '#FF0000', string: 'red', comment: '#12', number: 5 } } }");
    EXPECT_EQ(60, bad.scriptTimeoutSeconds);
    EXPECT_EQ(14, bad.editorFontSize);
    EXPECT_EQ(4, bad.editorTabWidth);
    ASSERT_EQ(1u, bad.editorColors.size()) << "only the well-formed #RRGGBB colour is kept";
    EXPECT_EQ(0xFF0000u, bad.editorColors.at("keyword"));
}

TEST(SettingsFile, resettingTheScriptEditorPageLeavesTheRestAlone) {
    Settings reset = everythingChanged();
    reset.resetPage(curvy::SettingsPage::ScriptEditor);
    EXPECT_TRUE(reset.editorFont.empty());
    EXPECT_EQ(14, reset.editorFontSize);
    EXPECT_EQ(4, reset.editorTabWidth);
    EXPECT_FALSE(reset.scriptTimeout);
    EXPECT_EQ(60, reset.scriptTimeoutSeconds);
    EXPECT_TRUE(reset.editorColors.empty());
    EXPECT_EQ(450, reset.spaceTapMs);
    EXPECT_EQ("D:/scripts/my script.py", reset.editorFile) << "the open file is state, not a preference: a page reset keeps it";
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

TEST(SettingsFile, theOldBodyHeightOfEveryPanelIsIgnoredSoAPanelsOwnPreferredHeightIsUsed) {
    const std::string text = R"({ panels: { document: { collapsed: false, bodyHeight: 120 }, transform: { userBodyHeight: 260 } } })";
    Settings back;
    back.fromText(text);
    ASSERT_EQ(2u, back.panels.size());
    for (const auto& panel : back.panels) {
        if (panel.id == "document") {
            EXPECT_FLOAT_EQ(0.0f, panel.bodyHeight) << "a height nobody chose was saved under the old name, and must not override the panel";
        } else {
            EXPECT_FLOAT_EQ(260.0f, panel.bodyHeight) << "a height the user dragged is kept";
        }
    }
}

TEST(SettingsFile, PanelStatesRoundTripInOrder) {
    Settings changed;
    changed.panels = {{"zeta", true, false, false, 0.0f, 0.0f, 0.0f}, {"alpha", false, true, true, 310.0f, -20.0f, 180.0f}};
    Settings back;
    back.fromText(changed.toText());
    ASSERT_EQ(2u, back.panels.size());
    EXPECT_EQ("zeta", back.panels[0].id);
    EXPECT_TRUE(back.panels[0].collapsed);
    EXPECT_EQ("alpha", back.panels[1].id);
    EXPECT_TRUE(back.panels[1].userHidden);
    EXPECT_TRUE(back.panels[1].floating);
    EXPECT_FLOAT_EQ(310.0f, back.panels[1].floatX);
    EXPECT_FLOAT_EQ(-20.0f, back.panels[1].floatY);
    EXPECT_FLOAT_EQ(180.0f, back.panels[1].bodyHeight);
}
