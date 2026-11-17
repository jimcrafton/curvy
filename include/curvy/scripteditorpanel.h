#pragma once

#include "curvy/codeeditorview.h"
#include "curvy/panel.h"
#include "curvy/scriptconsole.h"
#include "curvy/settings.h"

#include <newui/controls.h>
#include <newui/runloop.h>
#include <newui/textfolding.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace curvy {

    // The Python script editor of the right dock: a CodeEditorView (line numbers, syntax colours, folding of indented blocks, undo, find and
    // replace; colours and folds from the lex library, pythonhighlight.h) above Open, Save and Run buttons.
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

        // The find bar (Ctrl+F) between the editor and the buttons; see CodeEditorView for how it behaves.
        void showFind() { editor_->showFind(); }
        void showReplace() { editor_->showReplace(); }
        void hideFind() { editor_->hideFind(); }
        void setReplaceText(const std::string& text) { editor_->setReplaceText(text); }
        std::string replaceText() const { return editor_->replaceText(); }
        bool replaceCurrent() { return editor_->replaceCurrent(); }
        std::size_t replaceAll() { return editor_->replaceAll(); }
        bool findVisible() const { return editor_->findVisible(); }
        void setFindText(const std::string& text) { editor_->setFindText(text); }
        std::string findText() const { return editor_->findText(); }
        bool findNext(bool backward = false) { return editor_->findNext(backward); }
        std::string findStatus() const { return editor_->findStatus(); }
        void setFindOptions(bool matchCase, bool wholeWord, bool regex) { editor_->setFindOptions(matchCase, wholeWord, regex); }
        bool matchCase() const { return editor_->matchCase(); }
        bool wholeWord() const { return editor_->wholeWord(); }
        bool regex() const { return editor_->regex(); }

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
        void checkSyntax() { editor_->checkSyntax(); }
        const std::optional<SyntaxProblem>& syntaxProblem() const { return editor_->syntaxProblem(); }
        // The text under the editor: "Line 4, col 7: invalid syntax", or empty when the script compiles. Run (button, Ctrl+Enter, F5) asks
        // the checker about the code it is about to send, the selected lines or the whole script, and does not send code that does not
        // compile: onMessage gets "Not run - line 4, col 7: invalid syntax". While the whole script has a problem the Run button shows a
        // warning sign.
        std::string problemText() const { return editor_->problemText(); }

        // Called with the file's path after a successful Open or Save, so the owner can remember it for the next session.
        std::function<void(const std::filesystem::path&)> onFileChanged;
        // A message for the user (a file that could not be read or written).
        std::function<void(const std::string&)> onMessage;
        std::function<void()> onDestroyed;

        // What is on show, for tests.
        newui::TextFoldingControl* textControl() const { return editor_->textControl(); }
        std::string runButtonText() const { return runButton_->text(); }
        std::string fontName() const { return editor_->fontName(); }
        // Colours and folds follow the text a moment after an edit; this brings them up to date now.
        void refreshHighlight() { editor_->refreshHighlight(); }

        static constexpr float kPad = CodeEditorView::kPad;
        static constexpr float kButtonHeight = CodeEditorView::kButtonHeight;
        static constexpr float kProblemHeight = CodeEditorView::kProblemHeight;   // the line under the editor that names a syntax problem

    private:
        void layoutParts();
        void historyChanged();
        void updateTitle();
        void updateRunLabel();
        void scheduleScratch();
        void writeScratch();
        // The code Run would send, and its name.
        void codeToRun(std::string& code, std::string& name) const;
        bool hasSelection() const { return editor_->hasSelection(); }

        bool running_ = false;
        CodeEditorView* editor_ = nullptr;
        newui::Button* openButton_ = nullptr;
        newui::Button* saveButton_ = nullptr;
        newui::Button* runButton_ = nullptr;
        std::filesystem::path file_;
        std::filesystem::path scratchFile_;
        bool modified_ = false;
        bool scratchDirty_ = false;
        newui::RunLoop::TimerHandle scratchTimer_ = newui::RunLoop::kInvalidTimerHandle;
        newui::RunLoop* loop_ = nullptr;
    };

}
