#include "curvy/settings.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace curvy {

    void Settings::resetPage(SettingsPage page) {
        const Settings defaults;
        switch (page) {
            case SettingsPage::Appearance:
                theme = defaults.theme;
                canvasBackground = defaults.canvasBackground;
                canvasFollowsTheme = defaults.canvasFollowsTheme;
                propertyTextSize = defaults.propertyTextSize;
                blurPanels = defaults.blurPanels;
                break;
            case SettingsPage::Workspace:
                toolbarVisible = defaults.toolbarVisible;
                propertiesVisible = defaults.propertiesVisible;
                rulersVisible = defaults.rulersVisible;
                hintsFade = defaults.hintsFade;
                recentLength = defaults.recentLength;
                break;
            case SettingsPage::Pen:
                scrubAxis = defaults.scrubAxis;
                scrubSensitivity = defaults.scrubSensitivity;
                holdDelayMs = defaults.holdDelayMs;
                spaceTapMs = defaults.spaceTapMs;
                barrelButton = defaults.barrelButton;
                eraserEnd = defaults.eraserEnd;
                break;
            case SettingsPage::Drawing:
                snapGrid = defaults.snapGrid;
                gridSize = defaults.gridSize;
                snapGuides = defaults.snapGuides;
                toolDefaultsInDocument = defaults.toolDefaultsInDocument;
                break;
            case SettingsPage::Files:
                autosave = defaults.autosave;
                autosaveMinutes = defaults.autosaveMinutes;
                exportFolder = defaults.exportFolder;
                break;
        }
    }

    bool Settings::darkTheme() const {
        if (theme != ThemeChoice::FollowWindows) {
            return theme == ThemeChoice::Dark;
        }
#ifdef _WIN32
        DWORD light = 0, size = sizeof(light);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_SUCCESS) {
            return light == 0;
        }
#endif
        return true;
    }

    std::string Settings::toText() const {
        std::ostringstream out;
        out << "theme=" << int(theme) << "\n"
            << "canvasBackground=" << canvasBackground << "\n"
            << "canvasFollowsTheme=" << canvasFollowsTheme << "\n"
            << "propertyTextSize=" << propertyTextSize << "\n"
            << "blurPanels=" << blurPanels << "\n"
            << "toolbarVisible=" << toolbarVisible << "\n"
            << "propertiesVisible=" << propertiesVisible << "\n"
            << "rulersVisible=" << rulersVisible << "\n"
            << "hintsFade=" << hintsFade << "\n"
            << "recentLength=" << recentLength << "\n"
            << "scrubAxis=" << int(scrubAxis) << "\n"
            << "scrubSensitivity=" << scrubSensitivity << "\n"
            << "holdDelayMs=" << holdDelayMs << "\n"
            << "spaceTapMs=" << spaceTapMs << "\n"
            << "barrelButton=" << int(barrelButton) << "\n"
            << "eraserEnd=" << int(eraserEnd) << "\n"
            << "snapGrid=" << snapGrid << "\n"
            << "gridSize=" << gridSize << "\n"
            << "snapGuides=" << snapGuides << "\n"
            << "toolDefaultsInDocument=" << toolDefaultsInDocument << "\n"
            << "welcomeShown=" << welcomeShown << "\n"
            << "autosave=" << autosave << "\n"
            << "autosaveMinutes=" << autosaveMinutes << "\n"
            << "exportFolder=" << exportFolder << "\n";
        return out.str();
    }

    void Settings::fromText(const std::string& text) {
        std::map<std::string, std::string> values;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const std::size_t eq = line.find('=');
            if (eq != std::string::npos && eq > 0) {
                values[line.substr(0, eq)] = line.substr(eq + 1);
            }
        }

        auto number = [&values](const char* key, long& out) {
            auto it = values.find(key);
            if (it == values.end() || it->second.empty()) {
                return false;
            }
            char* end = nullptr;
            const long parsed = std::strtol(it->second.c_str(), &end, 10);
            if (*end != '\0') {
                return false;
            }
            out = parsed;
            return true;
        };
        auto integer = [&number](const char* key, int& field, int low, int high) {
            long value = 0;
            if (number(key, value) && value >= low && value <= high) {
                field = int(value);
            }
        };
        auto flag = [&number](const char* key, bool& field) {
            long value = 0;
            if (number(key, value) && (value == 0 || value == 1)) {
                field = value == 1;
            }
        };

        int choice = int(theme);
        integer("theme", choice, 0, 2);
        theme = ThemeChoice(choice);
        long color = 0;
        if (number("canvasBackground", color) && color >= 0 && color <= 0xFFFFFF) {
            canvasBackground = std::uint32_t(color);
        }
        flag("canvasFollowsTheme", canvasFollowsTheme);
        integer("propertyTextSize", propertyTextSize, 10, 20);
        flag("blurPanels", blurPanels);
        flag("toolbarVisible", toolbarVisible);
        flag("propertiesVisible", propertiesVisible);
        flag("rulersVisible", rulersVisible);
        flag("hintsFade", hintsFade);
        integer("recentLength", recentLength, 0, 50);
        choice = int(scrubAxis);
        integer("scrubAxis", choice, 0, 1);
        scrubAxis = WorkspaceModel::ScrubAxis(choice);
        integer("scrubSensitivity", scrubSensitivity, 25, 400);
        integer("holdDelayMs", holdDelayMs, 100, 2000);
        integer("spaceTapMs", spaceTapMs, 100, 1000);
        choice = int(barrelButton);
        integer("barrelButton", choice, 0, 2);
        barrelButton = PenButton(choice);
        choice = int(eraserEnd);
        integer("eraserEnd", choice, 0, 2);
        eraserEnd = EraserAction(choice);
        flag("snapGrid", snapGrid);
        auto grid = values.find("gridSize");
        if (grid != values.end()) {
            const double parsed = std::atof(grid->second.c_str());
            if (parsed > 0.0 && parsed <= 1000.0) {
                gridSize = parsed;
            }
        }
        flag("snapGuides", snapGuides);
        flag("toolDefaultsInDocument", toolDefaultsInDocument);
        flag("welcomeShown", welcomeShown);
        flag("autosave", autosave);
        integer("autosaveMinutes", autosaveMinutes, 1, 120);
        auto folder = values.find("exportFolder");
        if (folder != values.end()) {
            exportFolder = folder->second;
        }
    }

    std::string Settings::defaultPath() {
        const char* base = std::getenv("APPDATA");
        return (base != nullptr ? std::string(base) + "\\curvy" : std::string(".")) + "\\settings.ini";
    }

    Settings Settings::load(const std::string& path) {
        Settings settings;
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::stringstream buffer;
            buffer << in.rdbuf();
            settings.fromText(buffer.str());
        }
        return settings;
    }

    bool Settings::save(const std::string& path) const {
#ifdef _WIN32
        const std::size_t slash = path.find_last_of("\\/");
        if (slash != std::string::npos) {
            CreateDirectoryA(path.substr(0, slash).c_str(), nullptr);
        }
#endif
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << toText();
        return bool(out);
    }

}
