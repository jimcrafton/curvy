#include "curvy/scripteditorpanel.h"

#include "curvy/theme.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace curvy {

    using namespace newui;

    namespace {

        std::wstring widen(const std::string& text) {
            return std::wstring(text.begin(), text.end());
        }

        std::string narrow(const std::wstring& text) {
            return std::string(text.begin(), text.end());
        }

        // The text control holds wide characters one per UTF-8 byte (as the console does), so a file's bytes pass through unchanged.
        std::string withoutBom(std::string bytes) {
            if (bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) {
                bytes.erase(0, 3);
            }
            return bytes;
        }

    }

    ScriptEditorPanel::ScriptEditorPanel() : Panel("pyeditor", "Python Script Editor") {
        const Theme& theme = Theme::current();
        scroll_ = new ScrollView();
        scroll_->setName("pyEditorScroll");
        body().addChild(scroll_);
        text_ = new TextControl();
        text_->setName("pyEditorText");
        text_->setTextColor(theme.textColor());
        // Consolas is on every Windows install; a proportional font would misalign Python's indentation.
        text_->setFont(Font("Consolas", 13.0f));
        text_->caret().setColor(theme.textColor());
        scroll_->addChild(text_);

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
            run();
            return SyncReturn::Handled;
        };
        body().onSizeChanged += [this](View&, const Size&) {
            layoutParts();
            return SyncReturn::Ignored;
        };
        setPreferredBodyHeight(220.0f);
        layoutParts();
    }

    ScriptEditorPanel::~ScriptEditorPanel() {
        if (onDestroyed) {
            onDestroyed();
        }
    }

    void ScriptEditorPanel::layoutParts() {
        const Rect own = body().bounds();
        body().setContentSize(Size(own.width(), own.height()));
        const float width = (std::max)(own.width() - 2.0f * kPad, 40.0f);
        const float textHeight = (std::max)(own.height() - kButtonHeight - 3.0f * kPad, 20.0f);
        scroll_->setBounds(Rect(kPad, kPad, width, textHeight));
        const float buttonWidth = (width - 2.0f * kPad) / 3.0f;
        const float top = kPad * 2.0f + textHeight;
        openButton_->setBounds(Rect(kPad, top, buttonWidth, kButtonHeight));
        saveButton_->setBounds(Rect(kPad * 2.0f + buttonWidth, top, buttonWidth, kButtonHeight));
        runButton_->setBounds(Rect(kPad * 3.0f + buttonWidth * 2.0f, top, buttonWidth, kButtonHeight));
        text_->controller().ensureLayoutUpToDate();
        scroll_->updateLayout();
    }

    std::string ScriptEditorPanel::script() const {
        return narrow(text_->text());
    }

    void ScriptEditorPanel::setScript(const std::string& text) {
        text_->setText(widen(text));
        layoutParts();
    }

    bool ScriptEditorPanel::load(const std::filesystem::path& path, std::string& error) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            error = "Could not open " + path.filename().string();
            return false;
        }
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string cleaned = withoutBom(bytes);
        cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '\r'), cleaned.end());
        setScript(cleaned);
        file_ = path;
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
        return true;
    }

    void ScriptEditorPanel::run() {
        if (onRun) {
            onRun(script(), file_.empty() ? std::string("script") : file_.filename().string());
        }
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
