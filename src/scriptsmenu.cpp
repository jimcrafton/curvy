#include "curvy/scriptsmenu.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace curvy {

    namespace {

        std::string lowered(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        }

        bool isPython(const std::filesystem::path& path) {
            return lowered(path.extension().string()) == ".py";
        }

    }

    std::vector<ScriptFile> ScriptsMenu::scriptsIn(const std::filesystem::path& folder, bool sample) {
        std::vector<ScriptFile> files;
        std::error_code error;
        std::filesystem::directory_iterator it(folder, error);
        if (error) {
            return files;
        }
        for (const std::filesystem::directory_entry& entry : it) {
            std::error_code ignored;
            if (entry.is_regular_file(ignored) && isPython(entry.path())) {
                ScriptFile file;
                file.name = entry.path().stem().u8string();
                file.path = entry.path();
                file.sample = sample;
                files.push_back(std::move(file));
            }
        }
        std::sort(files.begin(), files.end(), [](const ScriptFile& a, const ScriptFile& b) {
            const std::string left = lowered(a.name);
            const std::string right = lowered(b.name);
            return left != right ? left < right : a.name < b.name;
        });
        return files;
    }

    std::vector<PanelMenuEntry> ScriptsMenu::entries(const std::filesystem::path& userFolder, const std::filesystem::path& samplesFolder,
                                                     std::function<void(const ScriptFile&)> run, std::function<void()> openFolder) {
        std::vector<PanelMenuEntry> lines;
        const auto addScripts = [&](const std::vector<ScriptFile>& files) {
            for (const ScriptFile& file : files) {
                PanelMenuEntry line;
                line.text = file.sample ? file.name + " (sample)" : file.name;
                line.run = [run, file]() {
                    if (run) {
                        run(file);
                    }
                };
                lines.push_back(std::move(line));
            }
        };

        const std::vector<ScriptFile> own = scriptsIn(userFolder, false);
        if (own.empty()) {
            PanelMenuEntry none;
            none.text = "No scripts in your scripts folder yet";
            lines.push_back(std::move(none));
        } else {
            addScripts(own);
        }

        const std::vector<ScriptFile> samples = scriptsIn(samplesFolder, true);
        if (!samples.empty()) {
            PanelMenuEntry separator;
            separator.separator = true;
            lines.push_back(std::move(separator));
            addScripts(samples);
        }

        PanelMenuEntry separator;
        separator.separator = true;
        lines.push_back(std::move(separator));
        PanelMenuEntry open;
        open.text = "Open scripts folder";
        open.run = [openFolder]() {
            if (openFolder) {
                openFolder();
            }
        };
        lines.push_back(std::move(open));
        return lines;
    }

    bool ScriptsMenu::readScript(const std::filesystem::path& path, std::string& code) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return false;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        code = buffer.str();
        if (code.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            code.erase(0, 3);
        }
        return true;
    }

}
