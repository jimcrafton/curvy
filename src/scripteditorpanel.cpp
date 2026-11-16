#include "curvy/scripteditorpanel.h"

#include "curvy/editorfonts.h"
#include "curvy/pythonhighlight.h"
#include "curvy/settings.h"
#include "curvy/theme.h"
#include "curvy/utf8.h"

#include <newui/fontmanager.h>
#include <newui/keyboard_constants.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <sstream>

namespace curvy {

    using namespace newui;

    namespace {

        constexpr const char* kBaseTitle = "Python Script Editor";
        constexpr std::chrono::milliseconds kHighlightDelay(60);
        constexpr std::chrono::milliseconds kScratchDelay(800);

        // The editing keys of a Python editor, ahead of the text control's own: Ctrl+Enter and F5 run the script, Enter keeps (or adds
        // to) the line's indentation, Tab inserts spaces up to the next tab stop (or indents the selected lines), Shift+Tab takes a
        // level off the line or the selected lines. Each is one edit, so one undo step. An indent level and a tab stop are indentWidth()
        // spaces.
        class EditorController : public TextFoldingController {
        public:
            EditorController(Control& owner, std::function<void()> run) : TextFoldingController(owner), run_(std::move(run)) {}

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
                    run_();
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
                const std::wstring insert = L"\n" + pythonNewLineIndent(text.substr(lineStart, start - lineStart), indentWidth_);
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

            std::function<void()> run_;
            std::size_t indentWidth_ = 4;
        };

        // The find field's keys that a single-line field does not give the panel: F3 and Shift+F3 (next, previous) and Esc.
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

        // A file's bytes as text: no BOM, no carriage returns.
        std::string normalized(std::string bytes) {
            if (bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) {
                bytes.erase(0, 3);
            }
            bytes.erase(std::remove(bytes.begin(), bytes.end(), '\r'), bytes.end());
            return bytes;
        }

        // Removes the indentation every non-blank line has in common, so a block copied from deep inside a script still runs.
        std::string dedented(const std::string& code) {
            std::vector<std::string> lines;
            std::istringstream in(code);
            for (std::string line; std::getline(in, line);) {
                lines.push_back(line);
            }
            std::size_t common = std::string::npos;
            for (const std::string& line : lines) {
                const std::size_t first = line.find_first_not_of(" \t");
                if (first != std::string::npos) {
                    common = (std::min)(common, first);
                }
            }
            if (common == std::string::npos || common == 0) {
                return code;
            }
            std::string out;
            for (const std::string& line : lines) {
                out += line.size() >= common ? line.substr(common) : std::string();
                out += '\n';
            }
            return out;
        }

    }

    ScriptEditorPanel::ScriptEditorPanel() : Panel("pyeditor", kBaseTitle) {
        const Theme& theme = Theme::current();
        scroll_ = new ScrollView();
        scroll_->setName("pyEditorScroll");
        body().addChild(scroll_);

        // Line numbers, folding and colours come with the control, which starts in the monospace font. The controller is swapped in
        // first and the model after it, so the control subscribes to the model it keeps.
        text_ = new TextFoldingControl();
        text_->setName("pyEditorText");
        text_->setVisible(true);
        auto controller = std::make_unique<EditorController>(*text_, [this]() { run(); });
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

        openButton_ = new Button();
        openButton_->setName("pyEditorOpen");
        openButton_->setText("Open...");
        body().addChild(openButton_);
        saveButton_ = new Button();
        saveButton_->setName("pyEditorSave");
        saveButton_->setText("Save...");
        body().addChild(saveButton_);
        runButton_ = new Button();
        runButton_->setName("pyEditorRun");
        runButton_->setText("Run");
        body().addChild(runButton_);

        findField_ = new TextField();
        findField_->setName("pyEditorFind");
        findField_->setController(std::make_unique<FindFieldController>(*findField_, [this](bool backward) { findNext(backward); }, [this]() { hideFind(); }));
        findField_->setTextColor(theme.textColor());
        findField_->caret().setColor(theme.textColor());
        findField_->setPlaceholder("Find");
        body().addChild(findField_);
        findLabel_ = new Label();
        findLabel_->setName("pyEditorFindStatus");
        findLabel_->setTextAlignment(TextAlignment::Right);
        body().addChild(findLabel_);
        findPrevButton_ = new Button();
        findPrevButton_->setName("pyEditorFindPrev");
        findPrevButton_->setText("Prev");
        body().addChild(findPrevButton_);
        findNextButton_ = new Button();
        findNextButton_->setName("pyEditorFindNext");
        findNextButton_->setText("Next");
        body().addChild(findNextButton_);
        findCloseButton_ = new Button();
        findCloseButton_->setName("pyEditorFindClose");
        findCloseButton_->setText("X");
        body().addChild(findCloseButton_);
        replaceField_ = new TextField();
        replaceField_->setName("pyEditorReplace");
        replaceField_->setController(std::make_unique<FindFieldController>(*replaceField_, [this](bool backward) { findNext(backward); }, [this]() { hideFind(); }));
        replaceField_->setTextColor(theme.textColor());
        replaceField_->caret().setColor(theme.textColor());
        replaceField_->setPlaceholder("Replace with");
        body().addChild(replaceField_);
        replaceButton_ = new Button();
        replaceButton_->setName("pyEditorReplaceOne");
        replaceButton_->setText("Replace");
        body().addChild(replaceButton_);
        replaceAllButton_ = new Button();
        replaceAllButton_->setName("pyEditorReplaceAll");
        replaceAllButton_->setText("All");
        body().addChild(replaceAllButton_);
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

        openButton_->onClick += [this](Control&) {
            open();
            return SyncReturn::Handled;
        };
        saveButton_->onClick += [this](Control&) {
            saveToFile();
            return SyncReturn::Handled;
        };
        runButton_->onClick += [this](Control&) {
            run();
            return SyncReturn::Handled;
        };
        text_->model().onChanged.add([this](Model&) {
            scheduleHighlight();
            return SyncReturn::Ignored;
        });
        // After the edit is in the history: onChanged comes first and would still see the old clean state.
        text_->model().onHistoryChanged.add([this](Model&) {
            historyChanged();
            return SyncReturn::Ignored;
        });
        text_->selection().onAfterSelectionChanged += [this](text::TextSelection&, const text::TextRange&) {
            updateRunLabel();
            if (findShown_) {
                updateFindStatus();   // which match is the current one
            }
            return SyncReturn::Ignored;
        };
        body().onSizeChanged += [this](View&, const Size&) {
            layoutParts();
            return SyncReturn::Ignored;
        };
        setPreferredBodyHeight(260.0f);
        updateTitle();
        layoutParts();
    }

    ScriptEditorPanel::~ScriptEditorPanel() {
        if (loop_ != nullptr) {
            if (highlightTimer_ != RunLoop::kInvalidTimerHandle) {
                loop_->cancelDelayed(highlightTimer_);
            }
            if (scratchTimer_ != RunLoop::kInvalidTimerHandle) {
                loop_->cancelDelayed(scratchTimer_);
            }
        }
        if (scratchDirty_) {
            writeScratch();   // a pause was still running: do not lose what was typed in it
        }
        if (onDestroyed) {
            onDestroyed();
        }
    }

    void ScriptEditorPanel::applySettings(const Settings& settings) {
        text_->setFont(scriptEditorFont(settings.editorFont, settings.editorFontSize));
        text_->setStyleSheet(pythonStyleSheet(settings.darkTheme(), settings.editorColors));
        if (auto* controller = dynamic_cast<EditorController*>(&text_->controller())) {
            controller->setIndentWidth(std::size_t(settings.editorTabWidth));
        }
        text_->controller().ensureLayoutUpToDate();
        scroll_->updateLayout();
    }

    void ScriptEditorPanel::layoutParts() {
        const Rect own = body().bounds();
        body().setContentSize(Size(own.width(), own.height()));
        const float width = (std::max)(own.width() - 2.0f * kPad, 40.0f);
        const float findRow = findShown_ ? (kButtonHeight + kPad) * (replaceShown_ ? 2.0f : 1.0f) : 0.0f;
        const float textHeight = (std::max)(own.height() - kButtonHeight - 3.0f * kPad - findRow, 20.0f);
        scroll_->setBounds(Rect(kPad, kPad, width, textHeight));
        if (findShown_) {
            const float y = kPad * 2.0f + textHeight;
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
            if (replaceShown_) {
                const float y2 = y + kButtonHeight + kPad;
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
        const float top0 = kPad * 3.0f + textHeight + findRow - kPad;
        // Run is the wide one: its label can be "Run Selection".
        const float room = width - 2.0f * kPad;
        const float quarter = room * 0.25f;
        const float top = top0;
        openButton_->setBounds(Rect(kPad, top, quarter, kButtonHeight));
        saveButton_->setBounds(Rect(kPad * 2.0f + quarter, top, quarter, kButtonHeight));
        runButton_->setBounds(Rect(kPad * 3.0f + quarter * 2.0f, top, room - quarter * 2.0f, kButtonHeight));
        text_->controller().ensureLayoutUpToDate();
        scroll_->updateLayout();
    }

    std::string ScriptEditorPanel::script() const {
        return wideToUtf8(text_->text());
    }

    void ScriptEditorPanel::setScript(const std::string& text) {
        text_->setText(utf8ToWide(text));
        history_->markClean();
        modified_ = false;
        updateTitle();
        refreshHighlight();
        layoutParts();
    }

    void ScriptEditorPanel::updateTitle() {
        std::string title = kBaseTitle;
        title += " - " + (file_.empty() ? std::string("untitled") : file_.filename().string());
        if (modified_) {
            title += " *";
        }
        setTitle(title);
    }

    void ScriptEditorPanel::updateRunLabel() {
        runButton_->setText(hasSelection() ? "Run Selection" : "Run");
    }

    bool ScriptEditorPanel::hasSelection() const {
        for (const text::TextRange& range : text_->selection().ranges()) {
            if (range.isValid() && range.length() > 0) {
                return true;
            }
        }
        return false;
    }

    void ScriptEditorPanel::historyChanged() {
        const bool now = !history_->isClean();
        if (now != modified_) {
            modified_ = now;
            updateTitle();
        }
        scheduleScratch();
        updateRunLabel();
    }

    void ScriptEditorPanel::refreshHighlight() {
        if (loop_ != nullptr && highlightTimer_ != RunLoop::kInvalidTimerHandle) {
            loop_->cancelDelayed(highlightTimer_);
            highlightTimer_ = RunLoop::kInvalidTimerHandle;
        }
        const std::wstring& text = text_->text();
        text_->setStyledRanges(pythonStyleRanges(text));

        // The folds the text has now, keeping closed the ones that were closed (matched by where they start).
        std::vector<text::TextFold> folds = pythonFolds(text);
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

    void ScriptEditorPanel::scheduleHighlight() {
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

    void ScriptEditorPanel::scheduleScratch() {
        if (scratchFile_.empty()) {
            return;
        }
        scratchDirty_ = true;
        RunLoop& loop = RunLoop::current();
        if (!loop) {
            writeScratch();
            return;
        }
        loop_ = &loop;
        if (scratchTimer_ != RunLoop::kInvalidTimerHandle) {
            loop.cancelDelayed(scratchTimer_);
        }
        scratchTimer_ = loop.postDelayed(kScratchDelay, [this]() {
            scratchTimer_ = RunLoop::kInvalidTimerHandle;
            writeScratch();
            return true;
        });
    }

    void ScriptEditorPanel::writeScratch() {
        scratchDirty_ = false;
        if (scratchFile_.empty()) {
            return;
        }
        std::error_code ignored;
        if (!modified_) {
            std::filesystem::remove(scratchFile_, ignored);   // saved or loaded: nothing left to restore
            return;
        }
        std::filesystem::create_directories(scratchFile_.parent_path(), ignored);
        std::ofstream out(scratchFile_, std::ios::binary | std::ios::trunc);
        const std::string bytes = script();
        out.write(bytes.data(), std::streamsize(bytes.size()));
    }

    void ScriptEditorPanel::setScratchFile(const std::filesystem::path& path) {
        scratchFile_ = path;
        std::error_code ignored;
        if (scratchFile_.empty() || !text_->text().empty() || !std::filesystem::exists(scratchFile_, ignored)) {
            return;
        }
        std::ifstream in(scratchFile_, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::string restored = normalized(bytes);
        if (restored.empty()) {
            return;
        }
        setScript(restored);
        modified_ = true;   // it is an unsaved script
        updateTitle();
    }

    bool ScriptEditorPanel::load(const std::filesystem::path& path, std::string& error) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            error = "Could not open " + path.filename().string();
            return false;
        }
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        file_ = path;
        setScript(normalized(bytes));
        scheduleScratch();   // removes a stale scratch copy
        return true;
    }

    bool ScriptEditorPanel::save(const std::filesystem::path& path, std::string& error) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Could not write " + path.filename().string();
            return false;
        }
        const std::string bytes = script();
        out.write(bytes.data(), std::streamsize(bytes.size()));
        out.close();
        if (!out) {
            error = "Could not write " + path.filename().string();
            return false;
        }
        file_ = path;
        history_->markClean();
        modified_ = false;
        updateTitle();
        scheduleScratch();
        return true;
    }

    void ScriptEditorPanel::showFind() {
        openFindBar(replaceShown_);
    }

    void ScriptEditorPanel::showReplace() {
        openFindBar(true);
    }

    void ScriptEditorPanel::openFindBar(bool withReplace) {
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

    void ScriptEditorPanel::applyFindVisibility() {
        for (View* view : std::initializer_list<View*>{findField_, findLabel_, findPrevButton_, findNextButton_, findCloseButton_}) {
            view->setVisible(findShown_);
        }
        for (View* view : std::initializer_list<View*>{replaceField_, replaceButton_, replaceAllButton_}) {
            view->setVisible(findShown_ && replaceShown_);
        }
    }

    void ScriptEditorPanel::hideFind() {
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

    void ScriptEditorPanel::setReplaceText(const std::string& text) {
        replaceField_->setText(utf8ToWide(text));
    }

    std::string ScriptEditorPanel::replaceText() const {
        return wideToUtf8(replaceField_->text());
    }

    bool ScriptEditorPanel::selectionIsMatch() const {
        const std::wstring query = findField_->text();
        if (query.empty() || !hasSelection()) {
            return false;
        }
        const text::TextRange range = text_->selection().ranges()[0];
        if (range.length() != query.size()) {
            return false;
        }
        const std::wstring picked = text_->text().substr(range.start(), range.length());
        return hasUpper(query) ? picked == query : lowered(picked) == lowered(query);
    }

    bool ScriptEditorPanel::replaceCurrent() {
        if (findField_->text().empty()) {
            return false;
        }
        if (selectionIsMatch()) {
            const text::TextRange range = text_->selection().ranges()[0];
            const std::wstring with = replaceField_->text();
            text_->selection().clear();
            text_->model().replace(range, with);
            text_->caret().setPosition(text::TextPosition(range.start() + with.size()));
        }
        return findNext(false);
    }

    std::size_t ScriptEditorPanel::replaceAll() {
        const std::wstring query = findField_->text();
        if (query.empty()) {
            return 0;
        }
        const bool exact = hasUpper(query);
        const std::wstring hay = exact ? text_->text() : lowered(text_->text());
        const std::wstring needle = exact ? query : lowered(query);
        std::vector<std::size_t> hits;
        for (std::size_t at = hay.find(needle); at != std::wstring::npos; at = hay.find(needle, at + needle.size())) {
            hits.push_back(at);
        }
        if (hits.empty()) {
            return 0;
        }
        const std::wstring with = replaceField_->text();
        text_->selection().clear();
        history_->beginGroup();
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) {   // from the end, so the earlier offsets stay right
            text_->model().replace(text::TextRange(*it, needle.size()), with);
        }
        history_->endGroup();
        updateFindStatus();
        return hits.size();
    }

    void ScriptEditorPanel::setFindText(const std::string& text) {
        findField_->setText(utf8ToWide(text));
    }

    std::string ScriptEditorPanel::findText() const {
        return wideToUtf8(findField_->text());
    }

    std::string ScriptEditorPanel::findStatus() const {
        return findLabel_->text();
    }

    bool ScriptEditorPanel::selectMatch(std::size_t from, bool backward) {
        const std::wstring query = findField_->text();
        if (query.empty()) {
            return false;
        }
        const bool exact = hasUpper(query);
        const std::wstring hay = exact ? text_->text() : lowered(text_->text());
        const std::wstring needle = exact ? query : lowered(query);
        std::size_t at = std::wstring::npos;
        if (backward) {
            if (from > 0) {
                at = hay.rfind(needle, from - 1);
            }
            if (at == std::wstring::npos) {
                at = hay.rfind(needle);
            }
        } else {
            at = hay.find(needle, from);
            if (at == std::wstring::npos) {
                at = hay.find(needle);
            }
        }
        if (at == std::wstring::npos) {
            return false;
        }
        text_->selection().setRange(text::TextRange(at, needle.size()));
        text_->caret().setPosition(text::TextPosition(at + needle.size()));
        revealCaret();
        return true;
    }

    bool ScriptEditorPanel::findNext(bool backward) {
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

    void ScriptEditorPanel::updateFindStatus() {
        const std::wstring query = findField_->text();
        if (query.empty() || !findShown_) {
            findLabel_->setText("");
            if (!text_->decorations().empty()) {
                text_->setDecorations({});
            }
            return;
        }
        const bool exact = hasUpper(query);
        const std::wstring hay = exact ? text_->text() : lowered(text_->text());
        const std::wstring needle = exact ? query : lowered(query);
        const std::size_t selected = hasSelection() ? text_->selection().ranges()[0].start() : std::wstring::npos;
        std::size_t total = 0;
        std::size_t index = 0;
        std::vector<text::TextDecoration> marks;
        for (std::size_t at = hay.find(needle); at != std::wstring::npos; at = hay.find(needle, at + needle.size())) {
            ++total;
            const bool current = at == selected;
            if (current) {
                index = total;
            }
            text::TextDecoration mark;
            mark.start = at;
            mark.length = needle.size();
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
        text_->setDecorations(std::move(marks));
        if (total == 0) {
            findLabel_->setText("No match");
        } else if (index == 0) {
            findLabel_->setText(std::to_string(total) + " found");
        } else {
            findLabel_->setText(std::to_string(index) + " of " + std::to_string(total));
        }
    }

    // Scrolls the view just enough to show the caret's line (centred when it was off screen).
    void ScriptEditorPanel::revealCaret() {
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

    void ScriptEditorPanel::codeToRun(std::string& code, std::string& name) const {
        name = file_.empty() ? std::string("script") : file_.filename().string();
        if (!hasSelection()) {
            code = script();
            return;
        }
        // Whole lines, so a selection that starts after a line's indentation still runs.
        const std::wstring& text = text_->text();
        std::size_t start = text.size();
        std::size_t end = 0;
        for (const text::TextRange& range : text_->selection().ranges()) {
            if (range.isValid() && range.length() > 0) {
                start = (std::min)(start, range.start());
                end = (std::max)(end, range.end());
            }
        }
        const std::size_t lineStart = start == 0 ? 0 : text.rfind(L'\n', start - 1) + 1;
        const std::size_t newline = text.find(L'\n', end);
        const std::size_t lineEnd = newline == std::wstring::npos ? text.size() : newline;
        code = dedented(wideToUtf8(text.substr(lineStart == std::wstring::npos ? 0 : lineStart, lineEnd - lineStart)));
        name += " (selection)";
    }

    void ScriptEditorPanel::run() {
        if (!onRun) {
            return;
        }
        std::string code;
        std::string name;
        codeToRun(code, name);
        onRun(code, name);
    }

    void ScriptEditorPanel::open() {
        if (!onChooseOpen) {
            return;
        }
        const std::filesystem::path path = onChooseOpen();
        if (path.empty()) {
            return;
        }
        std::string error;
        if (!load(path, error) && onMessage) {
            onMessage(error);
        }
    }

    void ScriptEditorPanel::saveToFile() {
        if (!onChooseSave) {
            return;
        }
        const std::filesystem::path path = onChooseSave(file_);
        if (path.empty()) {
            return;
        }
        std::string error;
        if (!save(path, error) && onMessage) {
            onMessage(error);
        }
    }

}
