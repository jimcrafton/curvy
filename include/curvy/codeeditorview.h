#pragma once

#include "curvy/codelanguage.h"
#include "curvy/settings.h"
#include "curvy/syntaxproblem.h"

#include <newui/controls.h>
#include <newui/runloop.h>
#include <newui/subview.h>
#include <newui/textfolding.h>
#include <newui/texthistory.h>
#include <newui/textstyle.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace curvy {

    // A code editor view: a text control in a scroll view (line numbers, colours and folds from a CodeLanguage, undo, the editing keys of a
    // code editor), a line under it that names the first problem the language's checker finds, and a find / replace bar (Ctrl+F, Ctrl+H).
    // The Python Script Editor panel and the wrangle code section of the Node panel are made of it. It only edits text; what the text is
    // for (a file, a node's parameter) is the owner's business, told through the callbacks. The owner gives it its bounds and it lays out
    // its parts inside them.
    //
    // Keys: Ctrl+Enter and F5 call onExecuteKey; Enter keeps (or adds to) the line's indentation; Tab inserts spaces up to the next tab
    // stop (or indents the selected lines), Shift+Tab takes a level off; Ctrl+F opens the find bar, Ctrl+H adds the replace row, F3 and
    // Shift+F3 step through the matches, Esc closes the bar. Each edit is one undo step. Text is UTF-8 outside, wide inside.
    //
    // Find: typing selects the first match at or after the caret; Enter or F3 goes to the next one, Shift+Enter or Shift+F3 to the previous
    // one, both wrapping round. The search ignores case unless the text searched for has a capital letter. Every match is marked in the text
    // (the current one with a frame), because the editor does not draw its selection while the find field has the keyboard focus. Replace
    // replaces the current match and goes to the next; All replaces every match as one undo step.
    // @reflect ignore=true
    class CodeEditorView : public newui::SubView {
    public:
        static constexpr float kPad = 6.0f;
        static constexpr float kButtonHeight = 24.0f;
        static constexpr float kProblemHeight = 20.0f;   // the line under the editor that names a syntax problem

        // `language` gives the colours, folds and indentation (null: plain text). `namePrefix` names the parts ("pyEditor" gives
        // pyEditorText, pyEditorFind, ...), so each editor's parts can be told apart.
        CodeEditorView(std::shared_ptr<const CodeLanguage> language, const std::string& namePrefix);
        ~CodeEditorView() override;

        std::string text() const;
        // Replaces the text, forgets the undo history and counts it as unmodified.
        void setText(const std::string& text);
        // False after an edit; true again after setText(), markClean() or undoing back to that state.
        bool isClean() const;
        void markClean();

        // The user's font, size, tab width and syntax colours (and the light or dark colour set, from the theme choice). Applies at once.
        void applySettings(const Settings& settings);

        bool hasSelection() const;
        // The model's history: the owner can group edits.
        newui::text::HistoryTextModel& history() const { return *history_; }

        // Colours and folds follow the text a moment after an edit; this brings them up to date now.
        void refreshHighlight();
        // Lays out the text, the problem line and the find bar in the view's bounds. Called when the bounds change.
        void layoutParts();

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

        // Asks for the text's first problem. With it set, a moment after typing stops the problem is marked in the text with a squiggle and
        // named in a line under the editor ("Line 4, col 7: invalid syntax"); without it only the language's own lexical squiggles show.
        std::function<std::optional<SyntaxProblem>(const std::string&)> onCheckSyntax;
        // Checks now instead of after the pause (what the timer calls); clears the problem when onCheckSyntax is not set.
        void checkSyntax();
        const std::optional<SyntaxProblem>& syntaxProblem() const { return problem_; }
        std::string problemText() const { return problemLabel_->text(); }

        // Ctrl+Enter or F5 in the text.
        std::function<void()> onExecuteKey;
        // After an edit (or undo or redo) is in the history, so isClean() is current.
        std::function<void()> onHistoryChanged;
        // After the selection or the problem changed.
        std::function<void()> onSelectionChanged;
        std::function<void()> onProblemChanged;

        // What is on show, for tests.
        newui::TextFoldingControl* textControl() const { return text_; }
        std::string fontName() const { return text_->font().name(); }

    private:
        struct FindMatch {
            std::size_t start = 0;
            std::size_t length = 0;
            std::wstring replacement;   // only when asked for
        };
        // The theme's backgrounds of the text and its scroll view, kept while the user's background colour replaces them.
        struct SavedFill {
            newui::gfx::PaintKind kind = newui::gfx::PaintKind::None;
            newui::Color color;
        };

        void scheduleHighlight();
        void scheduleSyntaxCheck();
        // Sets the text's marks: the find marks given plus the syntax problem's squiggle.
        void applyMarks(std::vector<newui::text::TextDecoration> findMarks);
        // Every match of the query in the text under the current switches. False with `badPattern` set when a regular expression does not parse.
        bool collectMatches(std::vector<FindMatch>& matches, bool withReplacements, bool& badPattern) const;
        // Selects the match at or after (or, backward, before) `from`, wrapping round; false when the text has none.
        bool selectMatch(std::size_t from, bool backward);
        void updateFindStatus();
        void revealCaret();
        void openFindBar(bool withReplace);
        void applyFindVisibility();
        void findOptionsChanged();

        std::shared_ptr<const CodeLanguage> language_;
        std::optional<std::pair<SavedFill, SavedFill>> themeBackground_;
        std::optional<SyntaxProblem> problem_;
        newui::Label* problemLabel_ = nullptr;
        std::vector<newui::text::TextDecoration> findMarks_;
        newui::RunLoop::TimerHandle highlightTimer_ = newui::RunLoop::kInvalidTimerHandle;
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
        newui::RunLoop* loop_ = nullptr;
    };

}
