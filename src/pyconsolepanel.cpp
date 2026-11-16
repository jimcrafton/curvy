#include "curvy/pyconsolepanel.h"

#include "curvy/theme.h"

#include <newui/keyboard_constants.h>

#include <algorithm>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr float kPad = 6.0f;
        constexpr float kInputHeight = 26.0f;

        // A single-line field handles Up and Down itself (the caret jumps to either end), before any listener of the field sees them,
        // so the history keys are taken here.
        class HistoryController : public TextController {
        public:
            HistoryController(Control& owner, std::function<void(int)> recall) : TextController(owner), recall_(std::move(recall)) {}

            SyncReturn handleKeyDown(std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t vkCode) override {
                if ((vkCode == vkUpArrow || vkCode == vkDownArrow) && (keyMask & (kmCtrl | kmAlt | kmShift)) == 0) {
                    recall_(vkCode == vkUpArrow ? -1 : 1);
                    return SyncReturn::Handled;
                }
                return TextController::handleKeyDown(keyMask, keyCharVal, repeatCount, vkCode);
            }

        private:
            std::function<void(int)> recall_;
        };
    }

    std::wstring PyConsolePanel::widen(const std::string& text) {
        return std::wstring(text.begin(), text.end());
    }

    PyConsolePanel::PyConsolePanel(ScriptConsole& console, SketchDocument* document) : Panel("pyconsole", "Python"), console_(console), document_(document) {
        const Theme& theme = Theme::current();
        output_ = new TextControl();
        output_->setName("pyConsoleOutput");
        output_->inputTraits().setReadOnly(true);
        output_->setTextColor(theme.textColor());
        body().addChild(output_);

        input_ = new TextField();
        input_->setName("pyConsoleInput");
        input_->setController(std::make_unique<HistoryController>(*input_, [this](int direction) { recall(direction); }));
        input_->setTextColor(BLRgba32(0xFFF0F0F0u));
        input_->caret().setColor(theme.textColor());
        body().addChild(input_);

        input_->onReturnPressed += [this](TextField&) {
            const std::string code = input();
            setInput("");
            execute(code);
            return SyncReturn::Handled;
        };
        body().onSizeChanged += [this](View&, const Size&) {
            layoutParts();
            return SyncReturn::Ignored;
        };
        setPreferredBodyHeight(220.0f);
        layoutParts();
    }

    PyConsolePanel::~PyConsolePanel() {
        if (onDestroyed) {
            onDestroyed();
        }
    }

    void PyConsolePanel::layoutParts() {
        const Rect own = body().bounds();
        body().setContentSize(Size(own.width(), own.height()));
        const float width = (std::max)(own.width() - 2.0f * kPad, 40.0f);
        const float outputHeight = (std::max)(own.height() - kInputHeight - 3.0f * kPad, 20.0f);
        output_->setBounds(Rect(kPad, kPad, width, outputHeight));
        input_->setBounds(Rect(kPad, kPad * 2.0f + outputHeight, width, kInputHeight));
        output_->controller().setScrollOffsetY((std::max)(0.0f, output_->controller().contentHeight() - outputHeight));
    }

    void PyConsolePanel::setTranscript(const std::wstring& text) {
        output_->inputTraits().setReadOnly(false);   // read-only vetoes every change, this one included
        output_->setText(text);
        output_->inputTraits().setReadOnly(true);
        layoutParts();
    }

    void PyConsolePanel::append(const std::string& text) {
        setTranscript(output_->text() + widen(text));
    }

    void PyConsolePanel::execute(const std::string& code) {
        if (code.find_first_not_of(" \t\r\n") == std::string::npos) {
            return;
        }
        if (history_.empty() || history_.back() != code) {
            history_.push_back(code);
        }
        recallAt_ = history_.size();
        std::string echo = ">>> " + code + "\n";
        const ScriptResult result = console_.run(code, document_);
        append(echo + result.output + (result.ok ? "" : result.error + (result.error.empty() || result.error.back() != '\n' ? "\n" : "")));
        if (onExecuted) {
            onExecuted();
        }
    }

    void PyConsolePanel::clear() {
        setTranscript(L"");
    }

    void PyConsolePanel::setInput(const std::string& text) {
        input_->setText(widen(text));
    }

    std::string PyConsolePanel::input() const {
        const std::wstring& text = input_->text();
        return std::string(text.begin(), text.end());
    }

    void PyConsolePanel::recall(int direction) {
        if (history_.empty()) {
            return;
        }
        const long next = long(recallAt_) + direction;
        if (next < 0) {
            return;
        }
        if (next >= long(history_.size())) {
            recallAt_ = history_.size();
            setInput("");
            return;
        }
        recallAt_ = std::size_t(next);
        setInput(history_[recallAt_]);
    }

}
