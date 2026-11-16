#pragma once

#include "curvy/panel.h"
#include "curvy/scriptconsole.h"
#include "curvy/settings.h"

#include <newui/controls.h>
#include <newui/runloop.h>
#include <newui/textfolding.h>
#include <newui/texthistory.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace curvy {

    // The Python script editor of the right dock: a code editor (line numbers, syntax colours, folding of indented blocks, undo) above
    // Open, Save and Run buttons. The editor is a newui::TextFoldingControl in a ScrollView, the way cpp_codetools' C++ editor is, with
    // colours and folds from the lex library (pythonhighlight.h).
    //
    // It only edits and stores text; Run hands the code to onRun (the owner sends it to the console, which shows the output). With text
    // selected, Run runs the selected lines instead of the whole script (Ctrl+Enter and F5 do the same as the button). Open and Save ask
    // the owner for a file name through onChooseOpen / onChooseSave. Files are UTF-8.
    //
    // The title says which file is open and whether it has unsaved changes. With setScratchFile() the text is also kept in that file as
    // it changes, so the script is still there in the next session.
    // @reflect ignore=true
    class ScriptEditorPanel : public Panel {
    public:
        ScriptEditorPanel();
        ~ScriptEditorPanel() override;

        std::string script() const;
        // Replaces the text and forgets the undo history; the script counts as unmodified.
        void setScript(const std::string& text);

        // The file the text came from or went to; empty for a script that was never saved.
        const std::filesystem::path& file() const { return file_; }
        // Changed since it was loaded or saved (undo back to that state clears it).
        bool modified() const { return modified_; }

        // False with `error` set when the file cannot be read or written.
        bool load(const std::filesystem::path& path, std::string& error);
        bool save(const std::filesystem::path& path, std::string& error);

        // Keeps the text in `path` while it has unsaved changes (written a moment after typing stops) and brings it back now if the file
        // exists and the editor is empty: an untitled, modified script. The file is removed when the script is saved or another file is
        // loaded, because nothing is left to restore then.
        void setScratchFile(const std::filesystem::path& path);
        // Gives back the script that was open at the end of the last session (call it after setScratchFile): when unsaved text came back it
        // is the text of that file, still modified; otherwise, if the editor is empty and the file still exists, the file is loaded. An empty
        // path or a missing file with nothing to restore does nothing.
        void restoreFile(const std::filesystem::path& path);

        // The user's font, size, tab width and syntax colours (and the light or dark colour set, from the theme choice). Call it when the
        // settings or the theme change; it applies at once.
        void applySettings(const Settings& settings);

        // The same as the buttons: the selected lines if any text is selected, else everything.
        void run();
        void open();
        void saveToFile();

        // The find bar (Ctrl+F) between the editor and the buttons: a field, the match count, Prev, Next and a close button. Typing
        // selects the first match at or after the caret; Enter or F3 goes to the next one, Shift+Enter or Shift+F3 to the previous one,
        // both wrapping round; Esc closes the bar. The search ignores case unless the text searched for has a capital letter.
        //
        // Every match is marked in the text (the current one with a frame), because the editor does not draw its selection while the find
        // field has the keyboard focus. Ctrl+H (showReplace) adds a second row: the replacement, Replace (replaces the current match,
        // then goes to the next) and All (every match, one undo step).
        void showFind();
        void showReplace();
        void hideFind();
        void setReplaceText(const std::string& text);
        std::string replaceText() const;
        // Replaces the selected match (when the selection is one) and selects the next; false when nothing was left to find.
        bool replaceCurrent();
        // Returns how many matches were replaced.
        std::size_t replaceAll();
        bool findVisible() const { return findShown_; }
        void setFindText(const std::string& text);
        std::string findText() const;
        // Selects the next (or previous) match after the selection; false when there is none.
        bool findNext(bool backward = false);
        // "2 of 7", "No match", "Bad pattern" (a regular expression that does not parse), or empty when nothing is searched for.
        std::string findStatus() const;

        // The three switches of the find bar (buttons "Aa", "W" and ".*"). Plain text ignores case unless it has a capital letter or
        // matchCase is on; a regular expression (ECMAScript) ignores case unless matchCase is on. wholeWord keeps only matches that are not
        // part of a longer word (letters, digits, underscore). In a regular expression the replacement may use $1 ... and $&; an empty match
        // is never a match.
        void setFindOptions(bool matchCase, bool wholeWord, bool regex);
        bool matchCase() const { return matchCase_; }
        bool wholeWord() const { return wholeWord_; }
        bool regex() const { return regex_; }

        // Gets the code and the name to show for it (the file name or "script", with " (selection)" for selected lines).
        std::function<void(const std::string&, const std::string&)> onRun;
        // While the owner says a script runs (setRunning(true)) the Run button reads "Stop" and calls onStop; Run does nothing.
        void setRunning(bool running);
        bool running() const { return running_; }
        std::function<void()> onStop;
        // Return an empty path when the user cancels.
        std::function<std::filesystem::path()> onChooseOpen;
        std::function<std::filesystem::path(const std::filesystem::path& current)> onChooseSave;
        // Asks the owner for the script's first syntax error (the console compiles it without running it). With it set, a moment after
        // typing stops the problem is marked in the text with a squiggle and named in a line under the editor ("Line 4: invalid syntax");
        // without it only lexical problems (an unterminated string, a bad number) show. The squiggle of the find marks and the problem do
        // not hide each other.
        std::function<std::optional<SyntaxProblem>(const std::string&)> onCheckSyntax;
        // Checks now instead of after the pause (what the timer calls); clears the problem when onCheckSyntax is not set.
        void checkSyntax();
        const std::optional<SyntaxProblem>& syntaxProblem() const { return problem_; }
        // The text under the editor: "Line 4, col 7: invalid syntax", or empty when the script compiles. Run (button, Ctrl+Enter, F5) asks
        // the checker about the code it is about to send, the selected lines or the whole script, and does not send code that does not
        // compile: onMessage gets "Not run - line 4, col 7: invalid syntax". While the whole script has a problem the Run button shows a
        // warning sign.
        std::string problemText() const { return problemLabel_->text(); }

        // Called with the file's path after a successful Open or Save, so the owner can remember it for the next session.
        std::function<void(const std::filesystem::path&)> onFileChanged;
        // A message for the user (a file that could not be read or written).
        std::function<void(const std::string&)> onMessage;
        std::function<void()> onDestroyed;

        // What is on show, for tests.
        newui::TextFoldingControl* textControl() const { return text_; }
        std::string runButtonText() const { return runButton_->text(); }
        std::string fontName() const { return text_->font().name(); }
        // Colours and folds follow the text a moment after an edit; this brings them up to date now.
        void refreshHighlight();

        static constexpr float kPad = 6.0f;
        static constexpr float kButtonHeight = 24.0f;
        static constexpr float kProblemHeight = 20.0f;   // the line under the editor that names a syntax problem

    private:
        void layoutParts();
        void historyChanged();
        void updateTitle();
        void updateRunLabel();
        void scheduleHighlight();
        void scheduleSyntaxCheck();
        // Sets the text's marks: the find marks given plus the syntax problem's squiggle.
        void applyMarks(std::vector<newui::text::TextDecoration> findMarks);
        void scheduleScratch();
        void writeScratch();
        // The code Run would send, and its name.
        void codeToRun(std::string& code, std::string& name) const;
        bool hasSelection() const;
        struct FindMatch {
            std::size_t start = 0;
            std::size_t length = 0;
            std::wstring replacement;   // only when asked for
        };
        // Every match of the query in the text under the current switches. False with `badPattern` set when a regular expression does not parse.
        bool collectMatches(std::vector<FindMatch>& matches, bool withReplacements, bool& badPattern) const;
        // Selects the match at or after (or, backward, before) `from`, wrapping round; false when the text has none.
        bool selectMatch(std::size_t from, bool backward);
        void updateFindStatus();
        void revealCaret();
        void openFindBar(bool withReplace);
        void applyFindVisibility();
        void findOptionsChanged();
        // The theme's backgrounds of the text and its scroll view, kept while the user's background colour replaces them.
        struct SavedFill {
            newui::gfx::PaintKind kind = newui::gfx::PaintKind::None;
            newui::Color color;
        };
        std::optional<std::pair<SavedFill, SavedFill>> themeBackground_;
        std::optional<SyntaxProblem> problem_;
        bool running_ = false;
        newui::Label* problemLabel_ = nullptr;
        std::vector<newui::text::TextDecoration> findMarks_;
        newui::RunLoop::TimerHandle syntaxTimer_ = newui::RunLoop::kInvalidTimerHandle;
        newui::Button* matchCaseButton_ = nullptr;
        newui::Button* wholeWordButton_ = nullptr;
        newui::Button* regexButton_ = nullptr;
        bool matchCase_ = false;
        bool wholeWord_ = false;
        bool regex_ = false;
        newui::TextField* replaceField_ = nullptr;
        newui::Button* replaceButton_ = nullptr;
        newui::Button* replaceAllButton_ = nullptr;
        bool replaceShown_ = false;

        newui::TextField* findField_ = nullptr;
        newui::Label* findLabel_ = nullptr;
        newui::Button* findPrevButton_ = nullptr;
        newui::Button* findNextButton_ = nullptr;
        newui::Button* findCloseButton_ = nullptr;
        bool findShown_ = false;
        newui::ScrollView* scroll_ = nullptr;
        newui::TextFoldingControl* text_ = nullptr;
        newui::text::HistoryTextModel* history_ = nullptr;   // the text control's model
        newui::Button* openButton_ = nullptr;
        newui::Button* saveButton_ = nullptr;
        newui::Button* runButton_ = nullptr;
        std::filesystem::path file_;
        std::filesystem::path scratchFile_;
        bool modified_ = false;
        bool scratchDirty_ = false;
        newui::RunLoop::TimerHandle highlightTimer_ = newui::RunLoop::kInvalidTimerHandle;
        newui::RunLoop::TimerHandle scratchTimer_ = newui::RunLoop::kInvalidTimerHandle;
        newui::RunLoop* loop_ = nullptr;
    };

}
