#pragma once

#include "curvy/panelsmenu.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // One Python file a menu can run.
    // @reflect ignore=true
    struct ScriptFile {
        std::string name;                 // the file name without ".py"
        std::filesystem::path path;
        bool sample = false;              // one of the scripts that come with the app
    };

    // The top bar's Scripts button: the user's own scripts (the `scripts` folder next to the settings file), then the samples that come with
    // the app, then "Open scripts folder". Built as data, like the Panels menu, so the lines can be tested; PanelsMenu::popup shows them.
    // @reflect ignore=true
    class ScriptsMenu {
    public:
        // The .py files directly in `folder`, ordered by name without regard to case. Empty when the folder does not exist or cannot be read.
        static std::vector<ScriptFile> scriptsIn(const std::filesystem::path& folder, bool sample = false);

        // `run` gets the file of the line that was picked; `openFolder` the user folder (the caller creates it if needed). A folder with no
        // scripts shows one line that says so and does nothing.
        static std::vector<PanelMenuEntry> entries(const std::filesystem::path& userFolder, const std::filesystem::path& samplesFolder,
                                                   std::function<void(const ScriptFile&)> run, std::function<void()> openFolder);

        // The text of a script file as Python should get it: a UTF-8 byte order mark dropped. False when the file cannot be read.
        static bool readScript(const std::filesystem::path& path, std::string& code);
    };

}
