#include "testsupport.h"

#include "pythonconsole.h"

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

TEST_F(ScriptEditorPanelTest, matchCaseMakesALowercaseQueryCaseSensitive) {
    panel_->setScript("alpha ALPHA Alpha");
    panel_->showFind();
    panel_->setFindText("alpha");
    EXPECT_EQ(panel_->findStatus(), "1 of 3");
    panel_->setFindOptions(true, false, false);
    EXPECT_EQ(panel_->findStatus(), "1 of 1");
    panel_->setFindText("ALPHA");
    EXPECT_EQ(panel_->findStatus(), "1 of 1");
    panel_->setFindOptions(false, false, false);
    EXPECT_EQ(panel_->findStatus(), "1 of 1");   // a capital still makes it exact
}

TEST_F(ScriptEditorPanelTest, wholeWordSkipsMatchesInsideLongerWords) {
    panel_->setScript("id idx _id id2 id (id)");
    panel_->showFind();
    panel_->setFindText("id");
    EXPECT_EQ(panel_->findStatus(), "1 of 6");
    panel_->setFindOptions(false, true, false);
    EXPECT_EQ(panel_->findStatus(), "1 of 3");   // the first, the one after id2, and the one in the brackets
    panel_->setFindText("(id)");
    EXPECT_EQ(panel_->findStatus(), "1 of 1");   // a query that starts and ends with a symbol is not blocked by its neighbours
}

TEST_F(ScriptEditorPanelTest, aRegularExpressionFindsAndReplacesWithGroups) {
    panel_->setScript("x1 = 10\ny22 = 5\nz = 7");
    panel_->showReplace();
    panel_->setFindOptions(false, false, true);
    panel_->setFindText("([a-z])(\\d+) =");
    EXPECT_EQ(panel_->findStatus(), "1 of 2");
    panel_->setReplaceText("$2_$1 =");
    EXPECT_EQ(panel_->replaceAll(), 2u);
    EXPECT_EQ(panel_->script(), "1_x = 10\n22_y = 5\nz = 7");
    EXPECT_TRUE(panel_->textControl()->undo());   // all of it in one step
    EXPECT_EQ(panel_->script(), "x1 = 10\ny22 = 5\nz = 7");
}

TEST_F(ScriptEditorPanelTest, aRegularExpressionIsCaseInsensitiveUnlessMatchCaseIsOn) {
    panel_->setScript("Foo foo");
    panel_->showFind();
    panel_->setFindOptions(false, false, true);
    panel_->setFindText("f.o");
    EXPECT_EQ(panel_->findStatus(), "1 of 2");
    panel_->setFindOptions(true, false, true);
    EXPECT_EQ(panel_->findStatus(), "1 of 1");
}

TEST_F(ScriptEditorPanelTest, aBadPatternIsNamedAndFindsNothingAndEmptyMatchesAreIgnored) {
    panel_->setScript("abc");
    panel_->showFind();
    panel_->setFindOptions(false, false, true);
    panel_->setFindText("(");
    EXPECT_EQ(panel_->findStatus(), "Bad pattern");
    EXPECT_FALSE(panel_->findNext());
    EXPECT_TRUE(panel_->textControl()->decorations().empty());
    panel_->setFindText("x*");
    EXPECT_EQ(panel_->findStatus(), "No match") << "a pattern that matches nothing but the empty string has no matches";
    panel_->setFindText("b+");
    EXPECT_EQ(panel_->findStatus(), "1 of 1");
}

TEST_F(ScriptEditorPanelTest, theFindSwitchesAreButtonsThatChangeTheSearch) {
    panel_->setScript("a.c abc");
    panel_->showFind();
    panel_->setFindText("a.c");
    EXPECT_EQ(panel_->findStatus(), "1 of 1");
    auto* regex = dynamic_cast<newui::Button*>(panel_->findView("pyEditorRegex"));
    ASSERT_NE(nullptr, regex);
    regex->setChecked(true);
    EXPECT_TRUE(panel_->regex());
    EXPECT_EQ(panel_->findStatus(), "1 of 2");
    regex->setChecked(false);
    EXPECT_FALSE(panel_->regex());
    EXPECT_EQ(panel_->findStatus(), "1 of 1");
}

TEST_F(ScriptEditorPanelTest, theOpenFileIsRememberedForTheNextSession) {
    std::filesystem::path told;
    panel_->onFileChanged = [&told](const std::filesystem::path& path) { told = path; };
    panel_->setScript("print(1)");
    std::string error;
    ASSERT_TRUE(panel_->save(file_, error)) << error;
    EXPECT_EQ(told, file_);

    auto* next = new curvy::ScriptEditorPanel();
    frame_.rootView().addChild(next);
    next->restoreFile(file_);
    EXPECT_EQ(next->file(), file_);
    EXPECT_EQ(next->script(), "print(1)");
    EXPECT_FALSE(next->modified());

    // A file that is gone and nothing to restore: the editor stays empty and untitled.
    auto* other = new curvy::ScriptEditorPanel();
    frame_.rootView().addChild(other);
    other->restoreFile(std::filesystem::temp_directory_path() / "curvyNoSuchScript.py");
    EXPECT_TRUE(other->file().empty());
    EXPECT_TRUE(other->script().empty());
}

TEST_F(ScriptEditorPanelTest, unsavedTextOfARememberedFileComesBackAsThatFileStillModified) {
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "curvyScriptEditorScratch2.py";
    std::filesystem::remove(scratch);
    panel_->setScratchFile(scratch);
    panel_->textControl()->model().insert(0, L"edited = True\n");

    auto* next = new curvy::ScriptEditorPanel();
    frame_.rootView().addChild(next);
    next->setScratchFile(scratch);
    next->restoreFile(file_);
    EXPECT_EQ(next->script(), "edited = True\n");
    EXPECT_EQ(next->file(), file_);
    EXPECT_TRUE(next->modified());
    EXPECT_NE(next->title().find(file_.filename().string()), std::string::npos);
    std::filesystem::remove(scratch);
}

TEST_F(ScriptEditorPanelTest, aSyntaxErrorIsNamedUnderTheEditorAndMarkedInTheText) {
    panel_->onCheckSyntax = [](const std::string& code) -> std::optional<curvy::SyntaxProblem> {
        if (code.find("oops") == std::string::npos) {
            return std::nullopt;
        }
        curvy::SyntaxProblem problem;
        problem.line = 2;
        problem.column = 3;
        problem.endLine = 2;
        problem.endColumn = 7;
        problem.message = "invalid syntax";
        return problem;
    };
    panel_->setScript("a = 1\nb oops\n");
    ASSERT_TRUE(panel_->syntaxProblem().has_value());
    EXPECT_EQ(panel_->problemText(), "Line 2, col 3: invalid syntax");
    int squiggles = 0;
    for (const auto& mark : panel_->textControl()->decorations()) {
        if (mark.kind == newui::text::TextDecorationKind::Squiggle) {
            ++squiggles;
            EXPECT_EQ(mark.start, 8u);    // line 2 starts at 6; column 3 is the third character
            EXPECT_EQ(mark.length, 4u);
        }
    }
    EXPECT_EQ(squiggles, 1);

    // The find marks and the problem live together, and fixing the text clears the problem.
    panel_->showFind();
    panel_->setFindText("a");
    int others = 0;
    for (const auto& mark : panel_->textControl()->decorations()) {
        others += mark.kind != newui::text::TextDecorationKind::Squiggle ? 1 : 0;
    }
    EXPECT_GT(others, 0);
    panel_->textControl()->model().replace(newui::text::TextRange(8, 4), L"ok");
    EXPECT_FALSE(panel_->syntaxProblem().has_value());
    EXPECT_TRUE(panel_->problemText().empty());
}

TEST_F(ScriptEditorPanelTest, theRealConsoleChecksTheScriptWithPythonItself) {
    panel_->onCheckSyntax = [](const std::string& code) { return curvytest::sharedConsole().checkSyntax(code); };
    panel_->setScript("x = 1\nprint(x)\n");
    EXPECT_FALSE(panel_->syntaxProblem().has_value());
    panel_->setScript("def f(:\n    pass\n");
    ASSERT_TRUE(panel_->syntaxProblem().has_value());
    EXPECT_EQ(panel_->syntaxProblem()->line, 1);
    EXPECT_EQ(panel_->problemText().rfind("Line 1, col ", 0), 0u) << panel_->problemText();

    panel_->setScript("if True:\nprint(1)\n");
    ASSERT_TRUE(panel_->syntaxProblem().has_value());
    EXPECT_EQ(panel_->syntaxProblem()->line, 2) << "an IndentationError is a syntax error too";
}

TEST_F(ScriptEditorPanelTest, runRefusesCodeThatDoesNotCompileAndSaysWhere) {
    panel_->onCheckSyntax = [](const std::string& code) { return curvytest::sharedConsole().checkSyntax(code); };
    std::vector<std::string> ran;
    std::vector<std::string> said;
    panel_->onRun = [&ran](const std::string& code, const std::string&) { ran.push_back(code); };
    panel_->onMessage = [&said](const std::string& message) { said.push_back(message); };

    panel_->setScript("x = 1\ny = (2 +\n");
    EXPECT_EQ(panel_->runButtonText(), "\xE2\x9A\xA0 Run") << "a warning sign while the script does not compile";
    panel_->run();
    EXPECT_TRUE(ran.empty());
    ASSERT_EQ(said.size(), 1u);
    EXPECT_EQ(said[0].rfind("Not run - line ", 0), 0u) << said[0];
    EXPECT_NE(said[0].find(", col "), std::string::npos) << said[0];

    // Selected lines that compile run even though the script as a whole does not.
    panel_->textControl()->selection().setRange(newui::text::TextRange(0, 5));
    EXPECT_EQ(panel_->runButtonText(), "Run Selection");
    panel_->run();
    ASSERT_EQ(ran.size(), 1u);
    EXPECT_EQ(ran[0], "x = 1");

    // ... and a selection that does not compile is refused as a selection.
    panel_->textControl()->selection().setRange(newui::text::TextRange(6, 9));
    panel_->run();
    EXPECT_EQ(ran.size(), 1u);
    ASSERT_EQ(said.size(), 2u);
    EXPECT_NE(said[1].find("selection line "), std::string::npos) << said[1];

    panel_->textControl()->selection().clear();
    panel_->setScript("y = 2\n");
    EXPECT_EQ(panel_->runButtonText(), "Run");
    panel_->run();
    EXPECT_EQ(ran.size(), 2u);
}

TEST_F(ScriptEditorPanelTest, withoutACheckerNoProblemIsShown) {
    panel_->setScript("def (:");
    EXPECT_FALSE(panel_->syntaxProblem().has_value());
    EXPECT_TRUE(panel_->problemText().empty());
}

TEST_F(ScriptEditorPanelTest, theTextAndBackgroundColoursOfTheSettingsAreAppliedAndThemeLookIsRestored) {
    const auto backgroundOf = [this]() { return panel_->textControl()->style().backgroundFill().color(); };
    const newui::Color themeBackground = backgroundOf();
    const newui::Color themeText = panel_->textControl()->textColor();
    curvy::Settings settings;
    settings.editorColors["text"] = 0x112233;
    settings.editorColors["background"] = 0x445566;
    panel_->applySettings(settings);
    EXPECT_NEAR(float(0x11) / 255.0f, panel_->textControl()->textColor().r, 0.01f);
    EXPECT_NEAR(float(0x44) / 255.0f, backgroundOf().r, 0.01f);
    EXPECT_NEAR(float(0x66) / 255.0f, backgroundOf().b, 0.01f);

    settings.editorColors.clear();
    panel_->applySettings(settings);
    EXPECT_NEAR(themeBackground.r, backgroundOf().r, 0.001f);
    EXPECT_NEAR(themeBackground.b, backgroundOf().b, 0.001f);
    EXPECT_NEAR(themeText.r, panel_->textControl()->textColor().r, 0.001f);
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
