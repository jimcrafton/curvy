#include "testsupport.h"

#include "curvy/editorfonts.h"
#include "curvy/scripteditorpanel.h"

#include <newui/fontmanager.h>
#include <newui/keyboard_constants.h>

#include <filesystem>
#include <fstream>

namespace {

    class ScriptEditorPanelTest : public curvytest::WorkspaceTest {
    protected:
        void SetUp() override {
            WorkspaceTest::SetUp();
            panel_ = new curvy::ScriptEditorPanel();
            frame_.rootView().addChild(panel_);   // the view tree owns it from here
            file_ = std::filesystem::temp_directory_path() / "curvyScriptEditorTest.py";
            std::filesystem::remove(file_);
        }

        void TearDown() override {
            std::filesystem::remove(file_);
            WorkspaceTest::TearDown();
        }

        curvy::ScriptEditorPanel* panel_ = nullptr;
        std::filesystem::path file_;
    };

}

TEST_F(ScriptEditorPanelTest, theScriptTextRoundTrips) {
    panel_->setScript("a = 1\nprint(a)\n");
    EXPECT_EQ(panel_->script(), "a = 1\nprint(a)\n");
}

TEST_F(ScriptEditorPanelTest, saveThenLoadKeepsTheTextAndRemembersTheFile) {
    panel_->setScript("print('x')\n");
    std::string error;
    ASSERT_TRUE(panel_->save(file_, error)) << error;
    EXPECT_EQ(panel_->file(), file_);

    panel_->setScript("other");
    ASSERT_TRUE(panel_->load(file_, error)) << error;
    EXPECT_EQ(panel_->script(), "print('x')\n");
}

TEST_F(ScriptEditorPanelTest, loadDropsCarriageReturnsAndABom) {
    {
        std::ofstream out(file_, std::ios::binary);
        out << "\xEF\xBB\xBF" << "a = 1\r\nb = 2\r\n";
    }
    std::string error;
    ASSERT_TRUE(panel_->load(file_, error)) << error;
    EXPECT_EQ(panel_->script(), "a = 1\nb = 2\n");
}

TEST_F(ScriptEditorPanelTest, aMissingFileIsReportedAndLeavesTheTextAlone) {
    panel_->setScript("keep");
    std::string error;
    EXPECT_FALSE(panel_->load(file_, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(panel_->script(), "keep");
    EXPECT_TRUE(panel_->file().empty());
}

TEST_F(ScriptEditorPanelTest, runHandsTheTextAndTheFileNameToTheOwner) {
    std::string code;
    std::string name;
    panel_->onRun = [&](const std::string& text, const std::string& label) {
        code = text;
        name = label;
    };
    panel_->setScript("print(1)");
    panel_->run();
    EXPECT_EQ(code, "print(1)");
    EXPECT_EQ(name, "script");

    std::string error;
    ASSERT_TRUE(panel_->save(file_, error)) << error;
    panel_->run();
    EXPECT_EQ(name, "curvyScriptEditorTest.py");
}

TEST_F(ScriptEditorPanelTest, nonAsciiTextSurvivesTheEditorAndTheFile) {
    const std::string text = "# caf\xC3\xA9 \xE2\x9C\x93\nprint('\xC3\xA9')\n";
    panel_->setScript(text);
    const std::wstring shown = panel_->textControl()->text();
    EXPECT_NE(shown.find(std::wstring(L"caf") + wchar_t(0xE9)), std::wstring::npos);   // one code unit for the accented letter, not two bytes
    EXPECT_EQ(panel_->script(), text);
    std::string error;
    ASSERT_TRUE(panel_->save(file_, error)) << error;
    panel_->setScript("");
    ASSERT_TRUE(panel_->load(file_, error)) << error;
    EXPECT_EQ(panel_->script(), text);
}

TEST_F(ScriptEditorPanelTest, theTextIsColouredAndFolded) {
    panel_->setScript("def f():\n    return 1\n");
    EXPECT_FALSE(panel_->textControl()->styledRanges().empty());
    ASSERT_EQ(panel_->textControl()->folds().size(), 1u);
    EXPECT_TRUE(panel_->textControl()->gutterVisible());
}

TEST_F(ScriptEditorPanelTest, aFoldStaysClosedWhileTheTextIsEdited) {
    panel_->setScript("def f():\n    return 1\n");
    panel_->textControl()->setFoldCollapsed(0, true);
    panel_->textControl()->model().insert(panel_->textControl()->text().size(), L"x = 2\n");
    panel_->refreshHighlight();
    ASSERT_EQ(panel_->textControl()->folds().size(), 1u);
    EXPECT_TRUE(panel_->textControl()->folds()[0].collapsed);
}

TEST_F(ScriptEditorPanelTest, theTitleShowsTheFileAndWhetherItHasUnsavedChanges) {
    EXPECT_EQ(panel_->titleText(), "Python Script Editor - untitled");
    panel_->setScript("a = 1\n");
    EXPECT_FALSE(panel_->modified());
    panel_->textControl()->model().insert(0, L"b");
    EXPECT_TRUE(panel_->modified());
    EXPECT_EQ(panel_->titleText(), "Python Script Editor - untitled *");

    std::string error;
    ASSERT_TRUE(panel_->save(file_, error)) << error;
    EXPECT_FALSE(panel_->modified());
    EXPECT_EQ(panel_->titleText(), "Python Script Editor - curvyScriptEditorTest.py");

    panel_->textControl()->model().insert(0, L"c");
    EXPECT_TRUE(panel_->modified());
    EXPECT_TRUE(panel_->textControl()->undo());
    EXPECT_FALSE(panel_->modified());   // undone back to what was saved
}

TEST_F(ScriptEditorPanelTest, runSendsTheSelectedLinesDedented) {
    std::string code;
    std::string name;
    panel_->onRun = [&](const std::string& text, const std::string& label) {
        code = text;
        name = label;
    };
    panel_->setScript("a = 1\n    b = 2\nc = 3\n");
    EXPECT_EQ(panel_->runButtonText(), "Run");

    panel_->textControl()->selection().setRange(newui::text::TextRange(10, 2));   // "b " inside the second line
    EXPECT_EQ(panel_->runButtonText(), "Run Selection");
    panel_->run();
    EXPECT_EQ(code, "b = 2\n");
    EXPECT_EQ(name, "script (selection)");

    panel_->textControl()->selection().clear();
    EXPECT_EQ(panel_->runButtonText(), "Run");
    panel_->run();
    EXPECT_EQ(code, "a = 1\n    b = 2\nc = 3\n");
    EXPECT_EQ(name, "script");
}

TEST_F(ScriptEditorPanelTest, controlEnterAndF5RunTheScript) {
    int runs = 0;
    panel_->onRun = [&runs](const std::string&, const std::string&) { ++runs; };
    auto& controller = panel_->textControl()->controller();
    EXPECT_EQ(controller.handleKeyDown(newui::kmCtrl, 0, 1, newui::vkReturn), newui::SyncReturn::Handled);
    EXPECT_EQ(runs, 1);
    EXPECT_EQ(controller.handleKeyDown(0, 0, 1, newui::vkF5), newui::SyncReturn::Handled);
    EXPECT_EQ(runs, 2);
}

namespace {

    // Puts `text` in the editor with the caret at `caret` (and `length` characters selected from there) and presses a key.
    void press(curvy::ScriptEditorPanel& panel, const std::string& text, std::size_t caret, std::uint32_t vk, std::uint32_t mask = 0, std::size_t length = 0) {
        panel.setScript(text);
        auto* control = panel.textControl();
        if (length > 0) {
            control->selection().setRange(newui::text::TextRange(caret, length));
        }
        control->caret().setPosition(newui::text::TextPosition(caret + length));
        EXPECT_EQ(control->controller().handleKeyDown(mask, 0, 1, vk), newui::SyncReturn::Handled);
    }

    std::size_t caretOf(curvy::ScriptEditorPanel& panel) {
        return panel.textControl()->caret().position().offset();
    }

}

TEST_F(ScriptEditorPanelTest, findStepsThroughMatchesAndWraps) {
    panel_->setScript("Alpha beta\nalpha gamma\nALPHA");
    panel_->showFind();
    panel_->setFindText("alpha");
    auto selectedStart = [this]() { return panel_->textControl()->selection().ranges()[0].start(); };
    EXPECT_EQ(selectedStart(), 0u);   // typing selects the first match from the caret
    EXPECT_EQ(panel_->findStatus(), "1 of 3");

    EXPECT_TRUE(panel_->findNext());
    EXPECT_EQ(selectedStart(), 11u);
    EXPECT_TRUE(panel_->findNext());
    EXPECT_EQ(selectedStart(), 23u);
    EXPECT_TRUE(panel_->findNext());   // wraps
    EXPECT_EQ(selectedStart(), 0u);
    EXPECT_TRUE(panel_->findNext(true));   // backwards wraps too
    EXPECT_EQ(selectedStart(), 23u);
    EXPECT_EQ(panel_->findStatus(), "3 of 3");
}

TEST_F(ScriptEditorPanelTest, findIsCaseSensitiveOnlyWithACapital) {
    panel_->setScript("alpha ALPHA");
    panel_->showFind();
    panel_->setFindText("ALPHA");
    EXPECT_EQ(panel_->textControl()->selection().ranges()[0].start(), 6u);
    EXPECT_EQ(panel_->findStatus(), "1 of 1");
    panel_->setFindText("zeta");
    EXPECT_EQ(panel_->findStatus(), "No match");
    EXPECT_FALSE(panel_->findNext());
}

TEST_F(ScriptEditorPanelTest, controlFOpensTheFindBarWithTheSelectedWordAndEscapeClosesIt) {
    panel_->setScript("print(value)");
    panel_->textControl()->selection().setRange(newui::text::TextRange(6, 5));
    auto& controller = panel_->textControl()->controller();
    EXPECT_EQ(controller.handleKeyDown(newui::kmCtrl, 0, 1, newui::vkLetterF), newui::SyncReturn::Handled);
    EXPECT_TRUE(panel_->findVisible());
    EXPECT_EQ(panel_->findText(), "value");
    EXPECT_EQ(controller.handleKeyDown(0, 0, 1, newui::vkEscape), newui::SyncReturn::Handled);
    EXPECT_FALSE(panel_->findVisible());
}

TEST_F(ScriptEditorPanelTest, everyMatchIsMarkedWhileTheBarIsOpenAndTheMarksGoWhenItCloses) {
    panel_->setScript("ab ab ab");
    panel_->showFind();
    panel_->setFindText("ab");
    int backgrounds = 0;
    int frames = 0;
    for (const auto& mark : panel_->textControl()->decorations()) {
        backgrounds += mark.kind == newui::text::TextDecorationKind::Background ? 1 : 0;
        frames += mark.kind == newui::text::TextDecorationKind::Box ? 1 : 0;
    }
    EXPECT_EQ(backgrounds, 3);
    EXPECT_EQ(frames, 1);
    panel_->hideFind();
    EXPECT_TRUE(panel_->textControl()->decorations().empty());
}

TEST_F(ScriptEditorPanelTest, replaceChangesTheCurrentMatchAndMovesOn) {
    panel_->setScript("cat dog cat");
    panel_->showReplace();
    panel_->setFindText("cat");
    panel_->setReplaceText("bird");
    EXPECT_TRUE(panel_->replaceCurrent());   // replaces the first, selects the second
    EXPECT_EQ(panel_->script(), "bird dog cat");
    EXPECT_EQ(panel_->textControl()->selection().ranges()[0].start(), 9u);
    panel_->replaceCurrent();
    EXPECT_EQ(panel_->script(), "bird dog bird");
}

TEST_F(ScriptEditorPanelTest, replaceAllIsOneUndoStep) {
    panel_->setScript("x = 1\nx = x + 1\n");
    panel_->showReplace();
    panel_->setFindText("x");
    panel_->setReplaceText("total");
    EXPECT_EQ(panel_->replaceAll(), 3u);
    EXPECT_EQ(panel_->script(), "total = 1\ntotal = total + 1\n");
    EXPECT_TRUE(panel_->textControl()->undo());
    EXPECT_EQ(panel_->script(), "x = 1\nx = x + 1\n");
}

TEST_F(ScriptEditorPanelTest, enterAfterAColonIndentsOneLevel) {
    press(*panel_, "def f():", 8, newui::vkReturn);
    EXPECT_EQ(panel_->script(), "def f():\n    ");
    EXPECT_EQ(caretOf(*panel_), 13u);
}

TEST_F(ScriptEditorPanelTest, enterKeepsTheIndentationOfTheLine) {
    press(*panel_, "def f():\n    x = 1", 18, newui::vkReturn);
    EXPECT_EQ(panel_->script(), "def f():\n    x = 1\n    ");
    EXPECT_EQ(caretOf(*panel_), 23u);
}

TEST_F(ScriptEditorPanelTest, enterAfterReturnLeavesTheBlock) {
    press(*panel_, "def f():\n    return 1", 21, newui::vkReturn);
    EXPECT_EQ(panel_->script(), "def f():\n    return 1\n");
}

TEST_F(ScriptEditorPanelTest, enterInTheMiddleOfALineSplitsItWithTheIndent) {
    press(*panel_, "    ab", 5, newui::vkReturn);
    EXPECT_EQ(panel_->script(), "    a\n    b");
    EXPECT_EQ(caretOf(*panel_), 10u);
}

TEST_F(ScriptEditorPanelTest, enterReplacesASelection) {
    press(*panel_, "x = 1: y", 4, newui::vkReturn, 0, 3);
    EXPECT_EQ(panel_->script(), "x = \ny");   // the selection "1: " is gone and the rest of the line follows on a new line
}

TEST_F(ScriptEditorPanelTest, tabInsertsSpacesUpToTheNextMultipleOfFour) {
    press(*panel_, "", 0, newui::vkTab);
    EXPECT_EQ(panel_->script(), "    ");
    EXPECT_EQ(caretOf(*panel_), 4u);
    press(*panel_, "ab", 2, newui::vkTab);
    EXPECT_EQ(panel_->script(), "ab  ");
    press(*panel_, "abcd\nx", 6, newui::vkTab);   // the column counts from the start of the line
    EXPECT_EQ(panel_->script(), "abcd\nx   ");
}

TEST_F(ScriptEditorPanelTest, theTabCharacterOfTheKeyPressIsNotTyped) {
    panel_->setScript("a");
    panel_->textControl()->caret().setPosition(newui::text::TextPosition(1));
    EXPECT_EQ(panel_->textControl()->controller().handleKeyPress(0, L'\t', 1, newui::vkTab), newui::SyncReturn::Handled);
    EXPECT_EQ(panel_->script(), "a");
}

TEST_F(ScriptEditorPanelTest, tabIndentsEverySelectedLineAndShiftTabTakesItBack) {
    const std::string text = "a\n\nb\nc";
    press(*panel_, text, 0, newui::vkTab, 0, 4);   // "a\n\nb" : two code lines and a blank one between
    EXPECT_EQ(panel_->script(), "    a\n\n    b\nc");   // the blank line stays empty
    auto* control = panel_->textControl();
    EXPECT_EQ(control->controller().handleKeyDown(newui::kmShift, 0, 1, newui::vkTab), newui::SyncReturn::Handled);
    EXPECT_EQ(panel_->script(), text);
}

TEST_F(ScriptEditorPanelTest, aBlockIndentIsOneUndoStep) {
    press(*panel_, "a\nb\nc", 0, newui::vkTab, 0, 5);
    EXPECT_EQ(panel_->script(), "    a\n    b\n    c");
    EXPECT_TRUE(panel_->textControl()->undo());
    EXPECT_EQ(panel_->script(), "a\nb\nc");
}

TEST_F(ScriptEditorPanelTest, shiftTabTakesAtMostOneLevelOffTheCaretLine) {
    press(*panel_, "      x", 7, newui::vkTab, newui::kmShift);
    EXPECT_EQ(panel_->script(), "  x");
    EXPECT_EQ(caretOf(*panel_), 3u);
    press(*panel_, "  x", 3, newui::vkTab, newui::kmShift);
    EXPECT_EQ(panel_->script(), "x");   // less than a level: all of it
    press(*panel_, "x", 1, newui::vkTab, newui::kmShift);
    EXPECT_EQ(panel_->script(), "x");   // nothing to take
}

TEST_F(ScriptEditorPanelTest, theSettingsChangeTheFontSizeTabWidthAndColours) {
    curvy::Settings settings;
    settings.editorFontSize = 20;
    settings.editorTabWidth = 2;
    settings.editorColors["keyword"] = 0xFF8800;
    panel_->applySettings(settings);
    EXPECT_FLOAT_EQ(20.0f, panel_->textControl()->font().size());
    EXPECT_EQ(2u, panel_->textControl()->controller().tabWidth());
    const auto* keyword = panel_->textControl()->styleSheet()->style("keyword");
    ASSERT_NE(nullptr, keyword);
    EXPECT_NEAR(1.0f, keyword->color().r, 0.01f);

    press(*panel_, "def f():", 8, newui::vkReturn);
    EXPECT_EQ(panel_->script(), "def f():\n  ") << "Enter indents by the tab width";
    press(*panel_, "", 0, newui::vkTab);
    EXPECT_EQ(panel_->script(), "  ") << "so does Tab";
}

TEST_F(ScriptEditorPanelTest, aChosenInstalledFontIsUsedAndAMissingOneFallsBackToAutomatic) {
    const auto installed = curvy::installedMonospaceFonts();
    ASSERT_FALSE(installed.empty());
    curvy::Settings settings;
    settings.editorFont = installed.front();
    panel_->applySettings(settings);
    EXPECT_EQ(installed.front(), panel_->fontName());

    settings.editorFont = "A Face Nobody Installed";
    panel_->applySettings(settings);
    EXPECT_EQ(curvy::automaticEditorFont(), panel_->fontName());
}

TEST_F(ScriptEditorPanelTest, theEditorUsesAnInstalledMonospaceFont) {
    GTEST_LOG_(INFO) << "editor font: " << panel_->fontName();
    EXPECT_FALSE(panel_->fontName().empty());
    EXPECT_TRUE(newui::FontManager::isAvailable(panel_->fontName())) << panel_->fontName();
}

TEST_F(ScriptEditorPanelTest, unsavedTextComesBackInTheNextSessionFromTheScratchFile) {
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "curvyScriptEditorScratch.py";
    std::filesystem::remove(scratch);
    panel_->setScratchFile(scratch);
    panel_->textControl()->model().insert(0, L"x = 1\n");
    ASSERT_TRUE(std::filesystem::exists(scratch));

    auto* next = new curvy::ScriptEditorPanel();
    frame_.rootView().addChild(next);
    next->setScratchFile(scratch);
    EXPECT_EQ(next->script(), "x = 1\n");
    EXPECT_TRUE(next->modified());
    EXPECT_TRUE(next->file().empty());

    std::string error;
    ASSERT_TRUE(next->save(file_, error)) << error;
    EXPECT_FALSE(std::filesystem::exists(scratch));   // saved: nothing left to restore
    std::filesystem::remove(scratch);
}

TEST_F(ScriptEditorPanelTest, openAndSaveUseTheChosenPaths) {
    panel_->setScript("saved text");
    panel_->onChooseSave = [this](const std::filesystem::path&) { return file_; };
    panel_->saveToFile();
    EXPECT_TRUE(std::filesystem::exists(file_));

    panel_->setScript("");
    panel_->onChooseOpen = [this]() { return file_; };
    panel_->open();
    EXPECT_EQ(panel_->script(), "saved text");

    panel_->onChooseOpen = []() { return std::filesystem::path(); };
    panel_->setScript("untouched");
    panel_->open();
    EXPECT_EQ(panel_->script(), "untouched");
}
