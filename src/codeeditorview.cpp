#include "curvy/codeeditorview.h"

#include "curvy/editorfonts.h"
#include "curvy/settings.h"
#include "curvy/theme.h"
#include "curvy/utf8.h"

#include <newui/fontmanager.h>
#include <newui/keyboard_constants.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <initializer_list>
#include <regex>

namespace curvy {

    using namespace newui;

    namespace {

        constexpr std::chrono::milliseconds kHighlightDelay(60);
        constexpr std::chrono::milliseconds kSyntaxDelay(400);

        // The editing keys of a code editor, ahead of the text control's own: Ctrl+Enter and F5 execute, Enter keeps (or adds to) the line's
        // indentation, Tab inserts spaces up to the next tab stop (or indents the selected lines), Shift+Tab takes a level off the line or
        // the selected lines. Each is one edit, so one undo step. An indent level and a tab stop are indentWidth() spaces.
        class EditorController : public TextFoldingController {
        public:
            EditorController(Control& owner, std::function<void()> execute, std::shared_ptr<const CodeLanguage> language) :
                TextFoldingController(owner), execute_(std::move(execute)), language_(std::move(language)) {}

            void setIndentWidth(std::size_t width) {
                indentWidth_ = width;
                setTabWidth(width);   // a tab character in the text is drawn as wide as an indent
            }
            std::size_t indentWidth() const { return indentWidth_; }

            std::function<void()> openFind;
            std::function<void()> openReplace;
            std::function<void(bool)> stepFind;
            std::function<bool()> closeFind;   // true when a find bar was open and is closed now

            SyncReturn handleKeyDown(std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t vkCode) override {
                const bool ctrl = (keyMask & kmCtrl) != 0;
                const bool alt = (keyMask & kmAlt) != 0;
                const bool shift = (keyMask & kmShift) != 0;
                if ((vkCode == vkReturn && ctrl && !alt && !shift) || vkCode == vkF5) {
                    if (execute_) {
                        execute_();
                    }
                    return SyncReturn::Handled;
                }
                if (vkCode == vkLetterF && ctrl && !alt && !shift) {
                    openFind();
                    return SyncReturn::Handled;
                }
                if (vkCode == vkLetterH && ctrl && !alt && !shift) {
                    openReplace();
                    return SyncReturn::Handled;
                }
                if (vkCode == vkF3 && !ctrl && !alt) {
                    stepFind(shift);
                    return SyncReturn::Handled;
                }
                if (vkCode == vkEscape && closeFind()) {
                    return SyncReturn::Handled;
                }
                if (vkCode == vkReturn && !ctrl && !alt && !shift) {
                    return newLine();
                }
                if (vkCode == vkTab && !ctrl && !alt) {
                    return indentKey(shift);
                }
                return TextFoldingController::handleKeyDown(keyMask, keyCharVal, repeatCount, vkCode);
            }

            // The tab character that follows the Tab key is not typed: the key already did its work.
            SyncReturn handleKeyPress(std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t vkCode) override {
                if (keyCharVal == L'\t') {
                    return SyncReturn::Handled;
                }
                return TextFoldingController::handleKeyPress(keyMask, keyCharVal, repeatCount, vkCode);
            }

        private:
            // The selected range, or the caret as an empty one.
            text::TextRange current() const {
                if (!selection().isEmpty()) {
                    return selection().ranges()[0];
                }
                const std::size_t at = caret().position().isValid() ? caret().position().offset() : model().length();
                return text::TextRange(at, 0);
            }

            static std::size_t lineStartOf(const std::wstring& text, std::size_t offset) {
                if (offset == 0) {
                    return 0;
                }
                const std::size_t found = text.rfind(L'\n', offset - 1);
                return found == std::wstring::npos ? 0 : found + 1;
            }

            SyncReturn newLine() {
                const text::TextRange range = current();
                const std::wstring& text = model().text();
                const std::size_t start = range.start();
                const std::size_t lineStart = lineStartOf(text, start);
                const std::wstring insert = L"\n" + language_->newLineIndent(text.substr(lineStart, start - lineStart), indentWidth_);
                selection().clear();
                if (range.length() > 0) {
                    model().replace(range, insert);
                } else {
                    model().insert(start, insert);
                }
                caret().setPosition(text::TextPosition(start + insert.size()));
                return SyncReturn::Handled;
            }

            SyncReturn indentKey(bool shift) {
                const text::TextRange range = current();
                const std::wstring text = model().text();
                const bool spansLines = range.length() > 0 && text.find(L'\n', range.start()) < range.end();
                if (!shift && !spansLines) {
                    // Spaces up to the next tab stop, replacing a selection within the line.
                    const std::size_t column = range.start() - lineStartOf(text, range.start());
                    const std::wstring spaces(indentWidth_ - column % indentWidth_, L' ');
                    selection().clear();
                    if (range.length() > 0) {
                        model().replace(range, spaces);
                    } else {
                        model().insert(range.start(), spaces);
                    }
                    caret().setPosition(text::TextPosition(range.start() + spaces.size()));
                    return SyncReturn::Handled;
                }

                // Whole lines: those the selection touches (not the one it ends at the very start of), or the caret's.
                const std::size_t blockStart = lineStartOf(text, range.start());
                std::size_t last = range.end();
                if (range.length() > 0 && text[last - 1] == L'\n') {
                    --last;
                }
                const std::size_t newline = text.find(L'\n', last);
                const std::size_t blockEnd = newline == std::wstring::npos ? text.size() : newline;
                const std::wstring block = text.substr(blockStart, blockEnd - blockStart);

                std::wstring changed;
                std::size_t from = 0;
                while (from <= block.size()) {
                    std::size_t to = block.find(L'\n', from);
                    if (to == std::wstring::npos) {
                        to = block.size();
                    }
                    std::wstring line = block.substr(from, to - from);
                    if (!shift) {
                        if (!line.empty()) {
                            line.insert(0, indentWidth_, L' ');
                        }
                    } else if (!line.empty() && line[0] == L'\t') {
                        line.erase(0, 1);
                    } else {
                        std::size_t spaces = 0;
                        while (spaces < indentWidth_ && spaces < line.size() && line[spaces] == L' ') {
                            ++spaces;
                        }
                        line.erase(0, spaces);
                    }
                    changed += line;
                    if (to < block.size()) {
                        changed += L'\n';
                    }
                    from = to + 1;
                }
                if (changed == block) {
                    return SyncReturn::Handled;
                }
                selection().clear();
                model().replace(text::TextRange(blockStart, block.size()), changed);
                if (range.length() > 0) {
                    selection().setRange(text::TextRange(blockStart, changed.size()));
                    caret().setPosition(text::TextPosition(blockStart + changed.size()));
                } else {
                    const std::size_t removed = block.size() > changed.size() ? block.size() - changed.size() : 0;
                    const std::size_t at = shift ? (std::max)(blockStart, range.start() - removed) : range.start();
                    caret().setPosition(text::TextPosition(at));
                }
                return SyncReturn::Handled;
            }

            std::function<void()> execute_;
            std::shared_ptr<const CodeLanguage> language_;
            std::size_t indentWidth_ = 4;
        };

        // The find field's keys that a single-line field does not give the editor: F3 and Shift+F3 (next, previous) and Esc.
        class FindFieldController : public TextController {
        public:
            FindFieldController(Control& owner, std::function<void(bool)> step, std::function<void()> close) :
                TextController(owner), step_(std::move(step)), close_(std::move(close)) {}

            SyncReturn handleKeyDown(std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t vkCode) override {
                const bool shift = (keyMask & kmShift) != 0;
                if (vkCode == vkEscape) {
                    close_();
                    return SyncReturn::Handled;
                }
                if (vkCode == vkF3) {
                    step_(shift);
                    return SyncReturn::Handled;
                }
                return TextController::handleKeyDown(keyMask, keyCharVal, repeatCount, vkCode);
            }

        private:
            std::function<void(bool)> step_;
            std::function<void()> close_;
        };

        std::wstring lowered(std::wstring text) {
            for (wchar_t& c : text) {
                c = wchar_t(std::towlower(wint_t(c)));
            }
            return text;
        }

        bool hasUpper(const std::wstring& text) {
            return std::any_of(text.begin(), text.end(), [](wchar_t c) { return std::towlower(wint_t(c)) != wint_t(c); });
        }

    }

    CodeEditorView::CodeEditorView(std::shared_ptr<const CodeLanguage> language, const std::string& namePrefix) :
        language_(language ? std::move(language) : std::make_shared<const CodeLanguage>()) {
        const Theme& theme = Theme::current();
        const auto named = [&namePrefix](const char* part) { return namePrefix + part; };
        scroll_ = new ScrollView();
        scroll_->setName(named("Scroll"));
        addChild(scroll_);

        // Line numbers, folding and colours come with the control, which starts in the monospace font. The controller is swapped in
        // first and the model after it, so the control subscribes to the model it keeps.
        text_ = new TextFoldingControl();
        text_->setName(named("Text"));
        text_->setVisible(true);
        auto controller = std::make_unique<EditorController>(*text_, [this]() {
            if (onExecuteKey) {
                onExecuteKey();
            }
        }, language_);
        controller->openFind = [this]() { showFind(); };
        controller->openReplace = [this]() { showReplace(); };
        controller->stepFind = [this](bool backward) { findNext(backward); };
        controller->closeFind = [this]() {
            const bool wasShown = findShown_;
            hideFind();
            return wasShown;
        };
        text_->setController(std::move(controller));
        auto history = std::make_unique<text::HistoryTextModel>();
        history_ = history.get();
        text_->setModel(std::move(history));
        text_->setHighlightsCurrentLine(true);
        text_->setTextColor(theme.textColor());
        text_->caret().setColor(theme.textColor());
        scroll_->addChild(text_);
        applySettings(Settings());   // the defaults until the owner passes the user's

        findField_ = new TextField();
        findField_->setName(named("Find"));
        findField_->setController(std::make_unique<FindFieldController>(*findField_, [this](bool backward) { findNext(backward); }, [this]() { hideFind(); }));
        findField_->setTextColor(theme.textColor());
        findField_->caret().setColor(theme.textColor());
        findField_->setPlaceholder("Find");
        addChild(findField_);
        findLabel_ = new Label();
        findLabel_->setName(named("FindStatus"));
        findLabel_->setTextAlignment(TextAlignment::Right);
        addChild(findLabel_);
        findPrevButton_ = new Button();
        findPrevButton_->setName(named("FindPrev"));
        findPrevButton_->setText("Prev");
        addChild(findPrevButton_);
        findNextButton_ = new Button();
        findNextButton_->setName(named("FindNext"));
        findNextButton_->setText("Next");
        addChild(findNextButton_);
        findCloseButton_ = new Button();
        findCloseButton_->setName(named("FindClose"));
        findCloseButton_->setText("X");
        addChild(findCloseButton_);
        problemLabel_ = new Label();
        problemLabel_->setName(named("Problem"));
        problemLabel_->setTextColor(BLRgba32(0xFFE86A6Au));
        problemLabel_->setVisible(false);
        addChild(problemLabel_);
        const auto makeSwitch = [this](const std::string& name, const char* label) {
            Button* button = new Button();
            button->setName(name);
            button->setText(label);
            button->setToggleButton(true);
            addChild(button);
            button->onCheckedChanged += [this](Button&) {
                findOptionsChanged();
                return SyncReturn::Handled;
            };
            return button;
        };
        matchCaseButton_ = makeSwitch(named("MatchCase"), "Aa");
        wholeWordButton_ = makeSwitch(named("WholeWord"), "W");
        regexButton_ = makeSwitch(named("Regex"), ".*");
        replaceField_ = new TextField();
        replaceField_->setName(named("Replace"));
        replaceField_->setController(std::make_unique<FindFieldController>(*replaceField_, [this](bool backward) { findNext(backward); }, [this]() { hideFind(); }));
        replaceField_->setTextColor(theme.textColor());
        replaceField_->caret().setColor(theme.textColor());
        replaceField_->setPlaceholder("Replace with");
        addChild(replaceField_);
        replaceButton_ = new Button();
        replaceButton_->setName(named("ReplaceOne"));
        replaceButton_->setText("Replace");
        addChild(replaceButton_);
        replaceAllButton_ = new Button();
        replaceAllButton_->setName(named("ReplaceAll"));
        replaceAllButton_->setText("All");
        addChild(replaceAllButton_);
        applyFindVisibility();
        replaceField_->onReturnPressed += [this](TextField&) {
            replaceCurrent();
            return SyncReturn::Handled;
        };
        replaceButton_->onClick += [this](Control&) {
            replaceCurrent();
            return SyncReturn::Handled;
        };
        replaceAllButton_->onClick += [this](Control&) {
            replaceAll();
            return SyncReturn::Handled;
        };
        findField_->onReturnPressed += [this](TextField&) {
            // A TextField takes Enter before its controller sees the key (and its modifiers), so Shift is read from the keyboard.
            findNext((GetKeyState(VK_SHIFT) & 0x8000) != 0);
            return SyncReturn::Handled;
        };
        findField_->model().onChanged.add([this](Model&) {
            // Typing keeps the current match when it still fits and moves on to the next one when it does not.
            const text::TextRange range = text_->selection().isEmpty() ? text::TextRange(0, 0) : text_->selection().ranges()[0];
            if (!findText().empty()) {
                selectMatch(range.start(), false);
            }
            updateFindStatus();
            return SyncReturn::Ignored;
        });
        findPrevButton_->onClick += [this](Control&) {
            findNext(true);
            return SyncReturn::Handled;
        };
        findNextButton_->onClick += [this](Control&) {
            findNext(false);
            return SyncReturn::Handled;
        };
        findCloseButton_->onClick += [this](Control&) {
            hideFind();
            return SyncReturn::Handled;
        };

        text_->model().onChanged.add([this](Model&) {
            scheduleHighlight();
            scheduleSyntaxCheck();
            return SyncReturn::Ignored;
        });
        // After the edit is in the history: onChanged comes first and would still see the old clean state.
        text_->model().onHistoryChanged.add([this](Model&) {
            if (onHistoryChanged) {
                onHistoryChanged();
            }
            return SyncReturn::Ignored;
        });
        text_->selection().onAfterSelectionChanged += [this](text::TextSelection&, const text::TextRange&) {
            if (onSelectionChanged) {
                onSelectionChanged();
            }
            if (findShown_) {
                updateFindStatus();   // which match is the current one
            }
            return SyncReturn::Ignored;
        };
        onSizeChanged += [this](View&, const Size&) {
            layoutParts();
            return SyncReturn::Ignored;
        };
        layoutParts();
    }

    CodeEditorView::~CodeEditorView() {
        if (loop_ != nullptr) {
            if (highlightTimer_ != RunLoop::kInvalidTimerHandle) {
                loop_->cancelDelayed(highlightTimer_);
            }
            if (syntaxTimer_ != RunLoop::kInvalidTimerHandle) {
                loop_->cancelDelayed(syntaxTimer_);
            }
        }
    }

    std::string CodeEditorView::text() const {
        return wideToUtf8(text_->text());
    }

    void CodeEditorView::setText(const std::string& text) {
        text_->setText(utf8ToWide(text));
        history_->markClean();
        refreshHighlight();
        layoutParts();
        scheduleSyntaxCheck();
    }

    bool CodeEditorView::isClean() const {
        return history_->isClean();
    }

    void CodeEditorView::markClean() {
        history_->markClean();
    }

    void CodeEditorView::applySettings(const Settings& settings) {
        text_->setFont(scriptEditorFont(settings.editorFont, settings.editorFontSize));
        if (auto sheet = language_->styleSheet(settings.darkTheme(), settings.editorColors)) {
            text_->setStyleSheet(std::move(sheet));
        }

        // The plain-text and background colours are overrides on top of the app theme's look; when one is cleared the look that was there is
        // put back (kept when the override first replaced it).
        const Theme& theme = Theme::current();
        const auto textColor = settings.editorColors.find("text");
        const BLRgba32 text = textColor != settings.editorColors.end() ? BLRgba32(0xFF000000u | textColor->second) : theme.textColor();
        text_->setTextColor(text);
        text_->caret().setColor(text);
        const auto background = settings.editorColors.find("background");
        if (background != settings.editorColors.end()) {
            if (!themeBackground_) {
                const auto saved = [](View& view) {
                    const gfx::Fill& fill = view.style().backgroundFill();
                    return SavedFill{fill.kind(), fill.color()};
                };
                themeBackground_ = std::make_pair(saved(*text_), saved(*scroll_));
            }
            const Color color(0xFF000000u | background->second, true);
            text_->style().setBackgroundColor(color);
            scroll_->style().setBackgroundColor(color);
        } else if (themeBackground_) {
            const auto restore = [](View& view, const SavedFill& saved) {
                gfx::Fill& fill = view.style().backgroundFill();
                fill.setColor(saved.color);
                fill.setKind(saved.kind);
            };
            restore(*text_, themeBackground_->first);
            restore(*scroll_, themeBackground_->second);
            themeBackground_.reset();
        }
        text_->style().markDirty();
        scroll_->style().markDirty();
        if (auto* controller = dynamic_cast<EditorController*>(&text_->controller())) {
            controller->setIndentWidth(std::size_t(settings.editorTabWidth));
        }
        text_->controller().ensureLayoutUpToDate();
        scroll_->updateLayout();
    }

    void CodeEditorView::layoutParts() {
        const Rect own = bounds();
        const float width = (std::max)(own.width() - 2.0f * kPad, 40.0f);
        const float findRow = findShown_ ? (kButtonHeight + kPad) * (replaceShown_ ? 3.0f : 2.0f) : 0.0f;
        const float problemRow = problem_ ? kProblemHeight : 0.0f;
        const float textHeight = (std::max)(own.height() - 2.0f * kPad - findRow - problemRow, 20.0f);
        scroll_->setBounds(Rect(kPad, kPad, width, textHeight));
        problemLabel_->setBounds(Rect(kPad, kPad + textHeight + 2.0f, width, kProblemHeight - 2.0f));
        if (findShown_) {
            const float y = kPad * 2.0f + textHeight + problemRow;
            const float closeWidth = kButtonHeight;
            const float buttonWidth = 44.0f;
            const float statusWidth = 64.0f;
            float right = kPad + width;
            findCloseButton_->setBounds(Rect(right - closeWidth, y, closeWidth, kButtonHeight));
            right -= closeWidth + kPad;
            findNextButton_->setBounds(Rect(right - buttonWidth, y, buttonWidth, kButtonHeight));
            right -= buttonWidth + kPad;
            findPrevButton_->setBounds(Rect(right - buttonWidth, y, buttonWidth, kButtonHeight));
            right -= buttonWidth + kPad;
            findLabel_->setBounds(Rect(right - statusWidth, y, statusWidth, kButtonHeight));
            right -= statusWidth + kPad;
            findField_->setBounds(Rect(kPad, y, (std::max)(right - kPad, 30.0f), kButtonHeight));
            const float switchY = y + kButtonHeight + kPad;
            const float switchWidth = 34.0f;
            float switchX = kPad;
            for (Button* button : {matchCaseButton_, wholeWordButton_, regexButton_}) {
                button->setBounds(Rect(switchX, switchY, switchWidth, kButtonHeight));
                switchX += switchWidth + kPad;
            }
            if (replaceShown_) {
                const float y2 = switchY + kButtonHeight + kPad;
                const float allWidth = 36.0f;
                const float oneWidth = 60.0f;
                float edge = kPad + width - closeWidth - kPad;   // the right edge of the Next button
                replaceAllButton_->setBounds(Rect(edge - allWidth, y2, allWidth, kButtonHeight));
                edge -= allWidth + kPad;
                replaceButton_->setBounds(Rect(edge - oneWidth, y2, oneWidth, kButtonHeight));
                edge -= oneWidth + kPad;
                replaceField_->setBounds(Rect(kPad, y2, (std::max)(edge - kPad, 30.0f), kButtonHeight));
            }
        }
        text_->controller().ensureLayoutUpToDate();
        scroll_->updateLayout();
    }

    bool CodeEditorView::hasSelection() const {
        for (const text::TextRange& range : text_->selection().ranges()) {
            if (range.isValid() && range.length() > 0) {
                return true;
            }
        }
        return false;
    }

    void CodeEditorView::refreshHighlight() {
        if (loop_ != nullptr && highlightTimer_ != RunLoop::kInvalidTimerHandle) {
            loop_->cancelDelayed(highlightTimer_);
            highlightTimer_ = RunLoop::kInvalidTimerHandle;
        }
        const std::wstring& text = text_->text();
        text_->setStyledRanges(language_->styleRanges(text));

        // The folds the text has now, keeping closed the ones that were closed (matched by where they start).
        std::vector<text::TextFold> folds = language_->folds(text);
        const std::vector<text::TextFold>& current = text_->folds();
        for (text::TextFold& fold : folds) {
            fold.collapsed = std::any_of(current.begin(), current.end(), [&fold](const text::TextFold& old) {
                return old.collapsed && old.start == fold.start;
            });
        }
        if (folds != current) {
            text_->setFolds(std::move(folds));
        }
        if (findShown_) {
            updateFindStatus();   // the marks and the count follow the edit
        }
    }

    void CodeEditorView::scheduleHighlight() {
        RunLoop& loop = RunLoop::current();
        if (!loop) {
            refreshHighlight();   // no run loop (a test): there is nothing to wait for
            return;
        }
        loop_ = &loop;
        if (highlightTimer_ != RunLoop::kInvalidTimerHandle) {
            loop.cancelDelayed(highlightTimer_);
        }
        highlightTimer_ = loop.postDelayed(kHighlightDelay, [this]() {
            highlightTimer_ = RunLoop::kInvalidTimerHandle;
            refreshHighlight();
            return true;   // once
        });
    }

    void CodeEditorView::scheduleSyntaxCheck() {
        if (!onCheckSyntax) {
            return;
        }
        RunLoop& loop = RunLoop::current();
        if (!loop) {
            checkSyntax();   // no run loop (a test): there is nothing to wait for
            return;
        }
        loop_ = &loop;
        if (syntaxTimer_ != RunLoop::kInvalidTimerHandle) {
            loop.cancelDelayed(syntaxTimer_);
        }
        syntaxTimer_ = loop.postDelayed(kSyntaxDelay, [this]() {
            syntaxTimer_ = RunLoop::kInvalidTimerHandle;
            checkSyntax();
            return true;   // once
        });
    }

    void CodeEditorView::checkSyntax() {
        if (loop_ != nullptr && syntaxTimer_ != RunLoop::kInvalidTimerHandle) {
            loop_->cancelDelayed(syntaxTimer_);
            syntaxTimer_ = RunLoop::kInvalidTimerHandle;
        }
        std::optional<SyntaxProblem> found;
        if (onCheckSyntax) {
            found = onCheckSyntax(text());
        }
        problem_ = found;
        problemLabel_->setText(found ? "Line " + std::to_string(found->line) + ", col " + std::to_string(found->column) + ": " + found->message : std::string());
        problemLabel_->setVisible(found.has_value());
        if (onProblemChanged) {
            onProblemChanged();
        }
        layoutParts();
        applyMarks(findMarks_);
    }

    void CodeEditorView::applyMarks(std::vector<text::TextDecoration> findMarks) {
        findMarks_ = findMarks;
        if (problem_) {
            // From the reported column to its end (or the end of that line), at least one character and never past the text.
            const std::wstring& text = text_->text();
            const auto lineStart = [&text](int line) {
                std::size_t at = 0;
                for (int i = 1; i < line && at != std::wstring::npos; ++i) {
                    const std::size_t newline = text.find(L'\n', at);
                    at = newline == std::wstring::npos ? std::wstring::npos : newline + 1;
                }
                return at;
            };
            const std::size_t first = lineStart(problem_->line);
            if (first != std::wstring::npos) {
                const std::size_t lineEnd = (std::min)(text.find(L'\n', first), text.size());
                const std::size_t start = (std::min)(first + std::size_t(problem_->column - 1), lineEnd > first ? lineEnd - 1 : first);
                std::size_t end = lineEnd;
                const std::size_t lastStart = lineStart(problem_->endLine);
                if (problem_->endLine == problem_->line && lastStart != std::wstring::npos && problem_->endColumn - 1 > problem_->column - 1) {
                    end = (std::min)(lastStart + std::size_t(problem_->endColumn - 1), lineEnd);
                }
                text::TextDecoration mark;
                mark.start = start;
                mark.length = (std::max)(end, start + 1) - start;
                mark.kind = text::TextDecorationKind::Squiggle;
                mark.color = Color(BLRgba32(0xFFE86A6Au));
                mark.thickness = 1.5f;
                findMarks.push_back(mark);
            }
        }
        if (!findMarks.empty() || !text_->decorations().empty()) {
            text_->setDecorations(std::move(findMarks));
        }
    }

    void CodeEditorView::showFind() {
        openFindBar(replaceShown_);
    }

    void CodeEditorView::showReplace() {
        openFindBar(true);
    }

    void CodeEditorView::openFindBar(bool withReplace) {
        // A selection within one line is what the user wants to look for.
        if (hasSelection()) {
            const text::TextRange range = text_->selection().ranges()[0];
            const std::wstring picked = text_->text().substr(range.start(), range.length());
            if (picked.find(L'\n') == std::wstring::npos) {
                findField_->setText(picked);
            }
        }
        findShown_ = true;
        replaceShown_ = withReplace;
        applyFindVisibility();
        layoutParts();
        updateFindStatus();
        if (RootView* root = rootView()) {
            root->setFocusedSubView(findField_);
        }
        findField_->selectAll();
    }

    void CodeEditorView::applyFindVisibility() {
        for (View* view : std::initializer_list<View*>{findField_, findLabel_, findPrevButton_, findNextButton_, findCloseButton_, matchCaseButton_,
                                                       wholeWordButton_, regexButton_}) {
            view->setVisible(findShown_);
        }
        for (View* view : std::initializer_list<View*>{replaceField_, replaceButton_, replaceAllButton_}) {
            view->setVisible(findShown_ && replaceShown_);
        }
    }

    void CodeEditorView::hideFind() {
        if (!findShown_) {
            return;
        }
        findShown_ = false;
        applyFindVisibility();
        layoutParts();
        updateFindStatus();   // takes the marks off the text
        if (RootView* root = rootView()) {
            root->setFocusedSubView(text_);
        }
    }

    void CodeEditorView::setReplaceText(const std::string& text) {
        replaceField_->setText(utf8ToWide(text));
    }

    std::string CodeEditorView::replaceText() const {
        return wideToUtf8(replaceField_->text());
    }

    bool CodeEditorView::collectMatches(std::vector<FindMatch>& matches, bool withReplacements, bool& badPattern) const {
        matches.clear();
        badPattern = false;
        const std::wstring query = findField_->text();
        if (query.empty()) {
            return true;
        }
        const std::wstring& text = text_->text();
        const std::wstring with = replaceField_->text();
        const auto isWord = [](wchar_t c) { return c == L'_' || std::iswalnum(wint_t(c)) != 0; };
        // Whole word: the edges of the match that are word characters must not touch another one.
        const auto wordOk = [&](std::size_t start, std::size_t length) {
            if (!wholeWord_) {
                return true;
            }
            const bool leftOk = !isWord(text[start]) || start == 0 || !isWord(text[start - 1]);
            const std::size_t end = start + length;
            const bool rightOk = !isWord(text[end - 1]) || end >= text.size() || !isWord(text[end]);
            return leftOk && rightOk;
        };

        if (regex_) {
            try {
                std::wregex pattern(query, matchCase_ ? std::regex::ECMAScript : std::regex::ECMAScript | std::regex::icase);
                for (std::wsregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
                    const std::wsmatch& found = *it;
                    if (found.length(0) == 0 || !wordOk(std::size_t(found.position(0)), std::size_t(found.length(0)))) {
                        continue;
                    }
                    FindMatch match;
                    match.start = std::size_t(found.position(0));
                    match.length = std::size_t(found.length(0));
                    if (withReplacements) {
                        match.replacement = found.format(with);
                    }
                    matches.push_back(std::move(match));
                }
            } catch (const std::regex_error&) {
                matches.clear();
                badPattern = true;
                return false;
            }
            return true;
        }

        const bool exact = matchCase_ || hasUpper(query);
        const std::wstring hay = exact ? text : lowered(text);
        const std::wstring needle = exact ? query : lowered(query);
        for (std::size_t at = hay.find(needle); at != std::wstring::npos; at = hay.find(needle, at + needle.size())) {
            if (!wordOk(at, needle.size())) {
                continue;
            }
            FindMatch match;
            match.start = at;
            match.length = needle.size();
            if (withReplacements) {
                match.replacement = with;
            }
            matches.push_back(std::move(match));
        }
        return true;
    }

    void CodeEditorView::setFindOptions(bool matchCase, bool wholeWord, bool regex) {
        matchCase_ = matchCase;
        wholeWord_ = wholeWord;
        regex_ = regex;
        matchCaseButton_->setChecked(matchCase);
        wholeWordButton_->setChecked(wholeWord);
        regexButton_->setChecked(regex);
        findOptionsChanged();
    }

    void CodeEditorView::findOptionsChanged() {
        matchCase_ = matchCaseButton_->isChecked();
        wholeWord_ = wholeWordButton_->isChecked();
        regex_ = regexButton_->isChecked();
        if (!findText().empty() && findShown_) {
            const text::TextRange range = text_->selection().isEmpty() ? text::TextRange(0, 0) : text_->selection().ranges()[0];
            selectMatch(range.start(), false);
        }
        updateFindStatus();
    }

    bool CodeEditorView::replaceCurrent() {
        if (findField_->text().empty()) {
            return false;
        }
        std::vector<FindMatch> matches;
        bool bad = false;
        collectMatches(matches, true, bad);
        if (hasSelection()) {
            const text::TextRange range = text_->selection().ranges()[0];
            const auto current = std::find_if(matches.begin(), matches.end(), [&](const FindMatch& match) {
                return match.start == range.start() && match.length == range.length();
            });
            if (current != matches.end()) {
                text_->selection().clear();
                text_->model().replace(range, current->replacement);
                text_->caret().setPosition(text::TextPosition(range.start() + current->replacement.size()));
            }
        }
        return findNext(false);
    }

    std::size_t CodeEditorView::replaceAll() {
        std::vector<FindMatch> matches;
        bool bad = false;
        collectMatches(matches, true, bad);
        if (matches.empty()) {
            return 0;
        }
        text_->selection().clear();
        history_->beginGroup();
        for (auto it = matches.rbegin(); it != matches.rend(); ++it) {   // from the end, so the earlier offsets stay right
            text_->model().replace(text::TextRange(it->start, it->length), it->replacement);
        }
        history_->endGroup();
        updateFindStatus();
        return matches.size();
    }

    void CodeEditorView::setFindText(const std::string& text) {
        findField_->setText(utf8ToWide(text));
    }

    std::string CodeEditorView::findText() const {
        return wideToUtf8(findField_->text());
    }

    std::string CodeEditorView::findStatus() const {
        return findLabel_->text();
    }

    bool CodeEditorView::selectMatch(std::size_t from, bool backward) {
        std::vector<FindMatch> matches;
        bool bad = false;
        collectMatches(matches, false, bad);
        if (matches.empty()) {
            return false;
        }
        const FindMatch* chosen = nullptr;
        if (backward) {
            for (const FindMatch& match : matches) {
                if (match.start < from) {
                    chosen = &match;
                }
            }
            if (chosen == nullptr) {
                chosen = &matches.back();
            }
        } else {
            for (const FindMatch& match : matches) {
                if (match.start >= from) {
                    chosen = &match;
                    break;
                }
            }
            if (chosen == nullptr) {
                chosen = &matches.front();
            }
        }
        text_->selection().setRange(text::TextRange(chosen->start, chosen->length));
        text_->caret().setPosition(text::TextPosition(chosen->start + chosen->length));
        revealCaret();
        return true;
    }

    bool CodeEditorView::findNext(bool backward) {
        std::size_t from = 0;
        if (hasSelection()) {
            const text::TextRange range = text_->selection().ranges()[0];
            from = backward ? range.start() : range.end();
        } else if (text_->caret().position().isValid()) {
            from = text_->caret().position().offset();
        }
        const bool found = selectMatch(from, backward);
        updateFindStatus();
        return found;
    }

    void CodeEditorView::updateFindStatus() {
        const std::wstring query = findField_->text();
        if (query.empty() || !findShown_) {
            findLabel_->setText("");
            applyMarks({});
            return;
        }
        std::vector<FindMatch> matches;
        bool bad = false;
        collectMatches(matches, false, bad);
        if (bad) {
            applyMarks({});
            findLabel_->setText("Bad pattern");
            return;
        }
        const std::size_t selected = hasSelection() ? text_->selection().ranges()[0].start() : std::wstring::npos;
        std::size_t total = 0;
        std::size_t index = 0;
        std::vector<text::TextDecoration> marks;
        for (const FindMatch& found : matches) {
            ++total;
            const bool current = found.start == selected;
            if (current) {
                index = total;
            }
            text::TextDecoration mark;
            mark.start = found.start;
            mark.length = found.length;
            mark.kind = text::TextDecorationKind::Background;
            mark.color = Color(BLRgba32(current ? 0xB0FF9A1Au : 0x70E6C229u));
            marks.push_back(mark);
            if (current) {
                text::TextDecoration frame = mark;
                frame.kind = text::TextDecorationKind::Box;
                frame.color = Color(BLRgba32(0xFFFF9A1Au));
                frame.thickness = 1.5f;
                marks.push_back(frame);
            }
        }
        applyMarks(std::move(marks));
        if (total == 0) {
            findLabel_->setText("No match");
        } else if (index == 0) {
            findLabel_->setText(std::to_string(total) + " found");
        } else {
            findLabel_->setText(std::to_string(index) + " of " + std::to_string(total));
        }
    }

    // Scrolls the view just enough to show the caret's line (centred when it was off screen).
    void CodeEditorView::revealCaret() {
        text_->controller().ensureLayoutUpToDate();
        const Rect caret = text_->controller().caretDocumentRect();
        if (caret.height() <= 0.0f) {
            return;
        }
        ScrollBar* bar = scroll_->vBar();
        const float view = scroll_->bounds().height();
        if (caret.top() < bar->value() || caret.bottom() > bar->value() + view) {
            bar->setValue((std::max)(caret.top() - view * 0.5f, 0.0f));
        }
    }

}
