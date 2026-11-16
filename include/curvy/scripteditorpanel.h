#pragma once

#include "curvy/panel.h"

#include <newui/controls.h>

#include <filesystem>
#include <functional>
#include <string>

namespace curvy {

    // The Python script editor of the right dock: a multi-line text area above Open, Save and Run buttons. It only edits and stores text; Run
    // hands the whole text to onRun (the owner sends it to the console, which shows the output) and Open and Save ask the owner for a file
    // name through onChooseOpen / onChooseSave. Files are UTF-8.
    // @reflect ignore=true
    class ScriptEditorPanel : public Panel {
    public:
        ScriptEditorPanel();
        ~ScriptEditorPanel() override;

        std::string script() const;
        void setScript(const std::string& text);

        // The file the text came from or went to; empty for a script that was never saved.
        const std::filesystem::path& file() const { return file_; }

        // False with `error` set when the file cannot be read or written.
        bool load(const std::filesystem::path& path, std::string& error);
        bool save(const std::filesystem::path& path, std::string& error);

        // The same as the buttons.
        void run();
        void open();
        void saveToFile();

        // Gets the script text and the name to show for it (the file name, or "script").
        std::function<void(const std::string&, const std::string&)> onRun;
        // Return an empty path when the user cancels.
        std::function<std::filesystem::path()> onChooseOpen;
        std::function<std::filesystem::path(const std::filesystem::path& current)> onChooseSave;
        // A message for the user (a file that could not be read or written).
        std::function<void(const std::string&)> onMessage;
        std::function<void()> onDestroyed;

        static constexpr float kPad = 6.0f;
        static constexpr float kButtonHeight = 24.0f;

    private:
        void layoutParts();

        newui::ScrollView* scroll_ = nullptr;
        newui::TextControl* text_ = nullptr;
        newui::Button* openButton_ = nullptr;
        newui::Button* saveButton_ = nullptr;
        newui::Button* runButton_ = nullptr;
        std::filesystem::path file_;
    };

}
