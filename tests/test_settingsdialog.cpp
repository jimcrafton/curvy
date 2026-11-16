#include "curvy/settingsdialogsession.h"

#include "testsupport.h"

#include <newui/keyboard_constants.h>
#include <newui/uiinputmanager.h>

#include <optional>

using curvy::Settings;
using curvy::settingsdialog::narrow;
using curvy::settingsdialog::Session;

namespace {

    // The dialog loaded from Resources/settings.newui and driven without showing it.
    class SettingsDialogTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            dialog_.setName("settings");
            ASSERT_TRUE(newui::Bundle::instance().loadDialog(dialog_));
            session_ = std::make_unique<Session>(dialog_.rootView(), working_, [this](const Settings& s) { ++applied_; last_ = s; });
            session_->wire([this](newui::DialogResult result) { (result == newui::DialogResult::Ok ? closedOk_ : closedCancel_)++; });
            session_->start();
            dialog_.rootView().updateLayout();
        }

        void TearDown() override {
            session_.reset();
            WorkspaceTest::TearDown();
        }

        template <typename T>
        T* find(const char* name) {
            return dynamic_cast<T*>(dialog_.rootView().findView(name));
        }

        newui::Dialog dialog_;
        Settings working_, last_;
        std::unique_ptr<Session> session_;
        int applied_ = 0, closedOk_ = 0, closedCancel_ = 0;
    };
}

TEST_F(SettingsDialogTest, OpensOnAppearanceShowingTheSettings) {
    EXPECT_TRUE(find<newui::View>("pageAppearance")->isVisible());
    EXPECT_FALSE(find<newui::View>("pagePen")->isVisible());
    EXPECT_EQ(std::optional<std::size_t>(0), find<newui::DropDownList>("themeChoice")->selectedIndex()) << "theme shows Dark";
    EXPECT_EQ("11", narrow(find<newui::TextField>("propertyTextSizeValue")->text()));
    EXPECT_EQ(11.0f, find<newui::Slider>("propertyTextSize")->value());
    EXPECT_TRUE(find<newui::Toggle>("rulersVisible")->isChecked());
    EXPECT_FALSE(find<newui::Toggle>("toolbarVisible")->isChecked());
    EXPECT_FALSE(find<newui::Toggle>("propertiesVisible")->isChecked());
}

TEST_F(SettingsDialogTest, RowsForThingsTheAppLacksAreHidden) {
    EXPECT_FALSE(find<newui::View>("showWelcome")->parent()->isVisible());
    EXPECT_FALSE(find<newui::View>("defaultFillWell")->parent()->isVisible());
}

TEST_F(SettingsDialogTest, ChangesApplyLive) {
    find<newui::DropDownList>("themeChoice")->setSelectedIndex(std::size_t(1));
    EXPECT_EQ(Settings::ThemeChoice::Light, working_.theme);
    EXPECT_EQ(1, applied_);
    EXPECT_EQ(Settings::ThemeChoice::Light, last_.theme);

    find<newui::Slider>("propertyTextSize")->setValue(14.0f);
    EXPECT_EQ(14, working_.propertyTextSize);
    EXPECT_EQ("14", narrow(find<newui::TextField>("propertyTextSizeValue")->text()));

    find<newui::Toggle>("rulersVisible")->setChecked(false);
    EXPECT_FALSE(working_.rulersVisible);
}

TEST_F(SettingsDialogTest, TheToolbarAndRightHandBoxesTogglesAreTheLeftAndRightAreas) {
    find<newui::Toggle>("toolbarVisible")->setChecked(true);
    find<newui::Toggle>("propertiesVisible")->setChecked(true);
    EXPECT_FALSE(working_.docks[1].collapsed);
    EXPECT_FALSE(working_.docks[2].collapsed);
    EXPECT_TRUE(working_.docks[0].collapsed);
}

TEST_F(SettingsDialogTest, ThePenPageShowsInThePagesArea) {
    auto* navPen = find<newui::Button>("navPen");
    navPen->onClick(*navPen);
    EXPECT_FALSE(find<newui::View>("pageAppearance")->isVisible());
    newui::View* pen = find<newui::View>("pagePen");
    ASSERT_TRUE(pen->isVisible());
    ASSERT_NE(nullptr, pen->parent());
    EXPECT_EQ("settingsPages", pen->parent()->name()) << "the pages are siblings, not nested in the first one";
    EXPECT_LT(pen->bounds().top(), 4.0f);
    EXPECT_GT(pen->bounds().height(), 100.0f) << "the shown page fills the pages area";
}

TEST_F(SettingsDialogTest, TypedNumbersAreTakenOrRefused) {
    auto* navPen = find<newui::Button>("navPen");
    navPen->onClick(*navPen);
    auto* hold = find<newui::TextField>("spaceTap");
    hold->setText(L"650");
    hold->onReturnPressed(*hold);
    EXPECT_EQ(650, working_.spaceTapMs);
    hold->setText(L"5");
    hold->onReturnPressed(*hold);
    EXPECT_EQ(650, working_.spaceTapMs) << "an out-of-range threshold is refused";
    EXPECT_EQ(std::to_string(working_.spaceTapMs), narrow(hold->text())) << "the box goes back to the kept value";
}

TEST_F(SettingsDialogTest, ResetPageResetsOnlyTheShownPage) {
    find<newui::DropDownList>("themeChoice")->setSelectedIndex(std::size_t(1));
    auto* navPen = find<newui::Button>("navPen");
    navPen->onClick(*navPen);
    working_.spaceTapMs = 650;
    auto* reset = find<newui::Button>("settingsReset");
    reset->onClick(*reset);
    EXPECT_EQ(300, working_.spaceTapMs);
    EXPECT_EQ(Settings::ThemeChoice::Light, working_.theme);
}

namespace {

    void showScriptEditorPage(newui::Dialog& dialog) {
        auto* nav = dynamic_cast<newui::Button*>(dialog.rootView().findView("navScriptEditor"));
        nav->onClick(*nav);
    }

}

TEST_F(SettingsDialogTest, theScriptEditorPageShowsTheEditorSettings) {
    showScriptEditorPage(dialog_);
    EXPECT_TRUE(find<newui::View>("pageScriptEditor")->isVisible());
    EXPECT_FALSE(find<newui::View>("pageAppearance")->isVisible());
    EXPECT_EQ(std::optional<std::size_t>(0), find<newui::DropDownList>("editorFont")->selectedIndex()) << "Automatic";
    EXPECT_EQ("14", narrow(find<newui::TextField>("editorFontSize")->text()));
    EXPECT_EQ("4", narrow(find<newui::TextField>("editorTabWidth")->text()));
    EXPECT_EQ("#569CD6", narrow(find<newui::TextField>("syntaxHex_keyword")->text())) << "the dark theme's keyword colour";
    EXPECT_GT(find<newui::View>("pageScriptEditor")->bounds().height(), 100.0f);
}

TEST_F(SettingsDialogTest, theScriptEditorPageFitsItsAreaWithRoomForThePreview) {
    showScriptEditorPage(dialog_);
    newui::View* page = find<newui::View>("pageScriptEditor");
    newui::View* preview = find<newui::View>("editorPreview");
    ASSERT_NE(nullptr, preview);
    EXPECT_GT(preview->bounds().height(), 60.0f) << "the preview is not squeezed out by the rows above it";
    EXPECT_LE(preview->bounds().bottom(), page->bounds().height() + 0.5f) << "and it does not run past the page";
    for (const auto& style : curvy::pythonSyntaxStyles()) {
        newui::View* well = find<newui::View>((std::string("syntaxWell_") + style.name).c_str());
        EXPECT_GT(well->bounds().width(), 10.0f) << style.name;
        EXPECT_LT(well->bounds().bottom(), preview->bounds().top() + page->bounds().top() + 200.0f) << style.name;
    }
}

TEST_F(SettingsDialogTest, everySyntaxStyleHasAHexFieldAndAWell) {
    for (const auto& style : curvy::pythonSyntaxStyles()) {
        EXPECT_NE(nullptr, find<newui::TextField>((std::string("syntaxHex_") + style.name).c_str())) << style.name;
        EXPECT_NE(nullptr, find<newui::View>((std::string("syntaxWell_") + style.name).c_str())) << style.name;
    }
}

TEST_F(SettingsDialogTest, theFontIsChosenFromTheInstalledMonospaceFaces) {
    auto* font = find<newui::DropDownList>("editorFont");
    const auto installed = curvy::installedMonospaceFonts();
    ASSERT_FALSE(installed.empty());
    font->setSelectedIndex(std::size_t(1));   // 0 is Automatic
    EXPECT_EQ(installed[0], working_.editorFont);
    EXPECT_EQ(1, applied_);
    font->setSelectedIndex(std::size_t(0));
    EXPECT_TRUE(working_.editorFont.empty());
}

TEST_F(SettingsDialogTest, sizeAndTabWidthAreTakenOrRefused) {
    showScriptEditorPage(dialog_);
    auto* size = find<newui::TextField>("editorFontSize");
    size->setText(L"18");
    size->onReturnPressed(*size);
    EXPECT_EQ(18, working_.editorFontSize);
    size->setText(L"100");
    size->onReturnPressed(*size);
    EXPECT_EQ(18, working_.editorFontSize) << "a size out of range is refused";

    auto* tab = find<newui::TextField>("editorTabWidth");
    tab->setText(L"2");
    tab->onReturnPressed(*tab);
    EXPECT_EQ(2, working_.editorTabWidth);
    tab->setText(L"9");
    tab->onReturnPressed(*tab);
    EXPECT_EQ(2, working_.editorTabWidth);
    EXPECT_EQ("2", narrow(tab->text())) << "the box goes back to the kept value";
}

TEST_F(SettingsDialogTest, aTypedColourOverridesTheThemeAndTheDefaultClearsIt) {
    showScriptEditorPage(dialog_);
    auto* keyword = find<newui::TextField>("syntaxHex_keyword");
    keyword->setText(L"#FF8800");
    keyword->onReturnPressed(*keyword);
    ASSERT_EQ(1u, working_.editorColors.count("keyword"));
    EXPECT_EQ(0xFF8800u, working_.editorColors["keyword"]);
    EXPECT_GE(applied_, 1);

    keyword->setText(L"not a colour");
    keyword->onReturnPressed(*keyword);
    EXPECT_EQ(0xFF8800u, working_.editorColors["keyword"]) << "a bad colour is refused";
    EXPECT_EQ("#FF8800", narrow(keyword->text()));

    keyword->setText(L"");
    keyword->onReturnPressed(*keyword);
    EXPECT_EQ(0u, working_.editorColors.count("keyword")) << "empty goes back to the theme's colour";
    EXPECT_EQ("#569CD6", narrow(keyword->text()));

    keyword->setText(L"#FF8800");
    keyword->onReturnPressed(*keyword);
    keyword->setText(L"#569cd6");   // the theme's own colour again, in lower case
    keyword->onReturnPressed(*keyword);
    EXPECT_EQ(0u, working_.editorColors.count("keyword")) << "typing the default is no override";
}

TEST_F(SettingsDialogTest, theScriptEditorPreviewFollowsTheSettings) {
    showScriptEditorPage(dialog_);
    auto* preview = find<newui::TextControl>("editorPreview");
    ASSERT_NE(nullptr, preview);
    EXPECT_FALSE(preview->text().empty());
    EXPECT_FALSE(preview->styledRanges().empty());
    auto* size = find<newui::TextField>("editorFontSize");
    size->setText(L"20");
    size->onReturnPressed(*size);
    EXPECT_FLOAT_EQ(20.0f, preview->font().size());
}

TEST_F(SettingsDialogTest, resetPageOnTheScriptEditorPageClearsItsChanges) {
    showScriptEditorPage(dialog_);
    working_.editorFontSize = 20;
    working_.editorColors["keyword"] = 0x112233;
    auto* reset = find<newui::Button>("settingsReset");
    reset->onClick(*reset);
    EXPECT_EQ(14, working_.editorFontSize);
    EXPECT_TRUE(working_.editorColors.empty());
    EXPECT_EQ("14", narrow(find<newui::TextField>("editorFontSize")->text()));
}

TEST_F(SettingsDialogTest, TheKeysDoWhatTheDialogSays) {
    newui::RootView& root = dialog_.rootView();
    auto* hold = find<newui::TextField>("spaceTap");
    auto* reset = find<newui::Button>("settingsReset");
    auto* rulers = find<newui::Toggle>("rulersVisible");

    root.setFocusedSubView(hold);
    newui::UIInputManager::instance().routeDialogKeyDown(root, 0, 0, 1, newui::vkReturn);
    EXPECT_EQ(0, closedOk_) << "Return in a field with its own handler does not press OK";
    root.setFocusedSubView(reset);
    newui::UIInputManager::instance().routeDialogKeyDown(root, 0, 0, 1, newui::vkSpaceBar);
    EXPECT_EQ(0, closedOk_) << "Space on a focused button clicks that button, not OK";
    EXPECT_EQ(0, closedCancel_);
    root.setFocusedSubView(rulers);
    newui::UIInputManager::instance().routeDialogKeyDown(root, 0, 0, 1, newui::vkReturn);
    EXPECT_EQ(1, closedOk_) << "Return elsewhere presses the default OK button";
    newui::UIInputManager::instance().routeDialogKeyDown(root, 0, 0, 1, newui::vkEscape);
    EXPECT_EQ(1, closedCancel_) << "Escape presses Cancel";
    newui::UIInputManager::instance().routeDialogKeyDown(root, newui::kmCtrl, 0, 1, newui::vkEscape);
    EXPECT_EQ(1, closedCancel_) << "Ctrl+Escape is left alone";
}
