#include "curvy/scripteditorpanel.h"

#include "curvy/pythonhighlight.h"
#include "curvy/settings.h"
#include "curvy/utf8.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>

namespace curvy {

    using namespace newui;

    namespace {

        constexpr const char* kBaseTitle = "Python Script Editor";
        constexpr std::chrono::milliseconds kScratchDelay(800);

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
        editor_ = new CodeEditorView(PythonLanguage::instance(), "pyEditor");
        editor_->setVisible(true);
        body().addChild(editor_);
        editor_->onExecuteKey = [this]() { run(); };
        editor_->onCheckSyntax = [this](const std::string& code) -> std::optional<SyntaxProblem> {
            if (onCheckSyntax) {
                return onCheckSyntax(code);
            }
            return std::nullopt;
        };
        editor_->onHistoryChanged = [this]() { historyChanged(); };
        editor_->onSelectionChanged = [this]() { updateRunLabel(); };
        editor_->onProblemChanged = [this]() { updateRunLabel(); };

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

        openButton_->onClick += [this](Control&) {
            open();
            return SyncReturn::Handled;
        };
        saveButton_->onClick += [this](Control&) {
            saveToFile();
            return SyncReturn::Handled;
        };
        runButton_->onClick += [this](Control&) {
            if (running_) {
                if (onStop) {
                    onStop();
                }
                return SyncReturn::Handled;
            }
            run();
            return SyncReturn::Handled;
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
        if (loop_ != nullptr && scratchTimer_ != RunLoop::kInvalidTimerHandle) {
            loop_->cancelDelayed(scratchTimer_);
        }
        if (scratchDirty_) {
            writeScratch();   // a pause was still running: do not lose what was typed in it
        }
        if (onDestroyed) {
            onDestroyed();
        }
    }

    void ScriptEditorPanel::applySettings(const Settings& settings) {
        editor_->applySettings(settings);
    }

    void ScriptEditorPanel::layoutParts() {
        const Rect own = body().bounds();
        body().setContentSize(Size(own.width(), own.height()));
        const float width = (std::max)(own.width() - 2.0f * kPad, 40.0f);
        // The editor takes everything above the button row; the row sits kPad above the bottom edge.
        const float editorHeight = (std::max)(own.height() - kButtonHeight - kPad, 2.0f * kPad + 20.0f);
        editor_->setBounds(Rect(0.0f, 0.0f, own.width(), editorHeight));
        // Run is the wide one: its label can be "Run Selection".
        const float room = width - 2.0f * kPad;
        const float quarter = room * 0.25f;
        openButton_->setBounds(Rect(kPad, editorHeight, quarter, kButtonHeight));
        saveButton_->setBounds(Rect(kPad * 2.0f + quarter, editorHeight, quarter, kButtonHeight));
        runButton_->setBounds(Rect(kPad * 3.0f + quarter * 2.0f, editorHeight, room - quarter * 2.0f, kButtonHeight));
    }

    std::string ScriptEditorPanel::script() const {
        return editor_->text();
    }

    void ScriptEditorPanel::setScript(const std::string& text) {
        editor_->setText(text);
        modified_ = false;
        updateTitle();
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
        // A warning sign (U+26A0) while the whole script does not compile: Run will refuse it and say why.
        const bool selected = hasSelection();
        if (running_) {
            runButton_->setText("Stop");
        } else if (selected) {
            runButton_->setText("Run Selection");
        } else {
            runButton_->setText(editor_->syntaxProblem() ? "\xE2\x9A\xA0 Run" : "Run");
        }
    }

    void ScriptEditorPanel::historyChanged() {
        const bool now = !editor_->isClean();
        if (now != modified_) {
            modified_ = now;
            updateTitle();
        }
        scheduleScratch();
        updateRunLabel();
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
        if (scratchFile_.empty() || !editor_->textControl()->text().empty() || !std::filesystem::exists(scratchFile_, ignored)) {
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

    void ScriptEditorPanel::restoreFile(const std::filesystem::path& path) {
        if (path.empty()) {
            return;
        }
        if (modified_ && file_.empty()) {
            file_ = path;   // the text that came back from the scratch copy was this file's
            updateTitle();
            return;
        }
        std::error_code ignored;
        if (editor_->textControl()->text().empty() && std::filesystem::exists(path, ignored)) {
            std::string error;
            load(path, error);
        }
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
        if (onFileChanged) {
            onFileChanged(file_);
        }
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
        editor_->markClean();
        modified_ = false;
        updateTitle();
        scheduleScratch();
        if (onFileChanged) {
            onFileChanged(file_);
        }
        return true;
    }

    void ScriptEditorPanel::codeToRun(std::string& code, std::string& name) const {
        name = file_.empty() ? std::string("script") : file_.filename().string();
        if (!hasSelection()) {
            code = script();
            return;
        }
        // Whole lines, so a selection that starts after a line's indentation still runs.
        TextFoldingControl* control = editor_->textControl();
        const std::wstring& text = control->text();
        std::size_t start = text.size();
        std::size_t end = 0;
        for (const text::TextRange& range : control->selection().ranges()) {
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

    void ScriptEditorPanel::setRunning(bool running) {
        running_ = running;
        updateRunLabel();
    }

    void ScriptEditorPanel::run() {
        if (!onRun || running_) {
            return;
        }
        std::string code;
        std::string name;
        codeToRun(code, name);
        // Python would only print the error in the console; saying it here, before anything runs, keeps the console's history for real runs.
        if (onCheckSyntax) {
            if (const std::optional<SyntaxProblem> problem = onCheckSyntax(code)) {
                if (onMessage) {
                    onMessage("Not run - " + std::string(hasSelection() ? "selection " : "") + "line " + std::to_string(problem->line) + ", col " +
                              std::to_string(problem->column) + ": " + problem->message);
                }
                return;
            }
        }
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
