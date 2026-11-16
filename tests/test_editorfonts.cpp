#include "curvy/editorfonts.h"

#include <newui/fontmanager.h>

#include <gtest/gtest.h>

#include <algorithm>

TEST(EditorFonts, theMonospaceListIsSortedWithoutDuplicatesItalicsOrScriptFaces) {
    const std::vector<std::string> fonts = curvy::installedMonospaceFonts();
    EXPECT_TRUE(std::is_sorted(fonts.begin(), fonts.end()));
    EXPECT_EQ(fonts.end(), std::adjacent_find(fonts.begin(), fonts.end()));
    for (const std::string& name : fonts) {
        EXPECT_EQ(std::string::npos, name.find("Italic")) << name;
        EXPECT_EQ(std::string::npos, name.find("Monotype")) << name;
        EXPECT_TRUE(newui::FontManager::isAvailable(name)) << name;
    }
}

TEST(EditorFonts, theAutomaticFontIsInstalled) {
    EXPECT_TRUE(newui::FontManager::isAvailable(curvy::automaticEditorFont()));
}

TEST(EditorFonts, aChosenFontIsUsedOnlyWhenInstalled) {
    const std::string automatic = curvy::automaticEditorFont();
    EXPECT_EQ(automatic, curvy::scriptEditorFont("", 14).name());
    EXPECT_EQ(automatic, curvy::scriptEditorFont("A Face Nobody Installed", 14).name());
    EXPECT_FLOAT_EQ(17.0f, curvy::scriptEditorFont("", 17).size());
    EXPECT_EQ("Consolas", curvy::scriptEditorFont("Consolas", 14).name());
}
