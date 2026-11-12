#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "curvy/settings.h"

#include <newui/utils.h>

#include <json5/json5.hpp>
#include <json5/json5_builder.hpp>
#include <json5/json5_input.hpp>
#include <json5/json5_output.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace curvy {

    namespace {

        constexpr const char* themeNames[] = {"dark", "light", "system"};
        constexpr const char* axisNames[] = {"vertical", "horizontal"};
        constexpr const char* buttonNames[] = {"rings", "pan", "nothing"};
        constexpr const char* eraserNames[] = {"erase", "undo", "nothing"};
        constexpr float defaultDockSize[Settings::dockCount] = {64.0f, 220.0f, 268.0f, 120.0f};

        bool find(const json5::value& object, const char* key, json5::value& out) {
            if (!object.is_object()) {
                return false;
            }
            for (auto [name, child] : json5::object_view(object)) {
                if (std::string_view(name) == key) {
                    out = child;
                    return true;
                }
            }
            return false;
        }

        bool group(const json5::value& root, const char* key, json5::value& out) {
            return find(root, key, out) && out.is_object();
        }

        void readInt(const json5::value& from, const char* key, int& field, int low, int high) {
            json5::value v;
            if (find(from, key, v) && v.is_number()) {
                const double value = v.get<double>();
                if (std::isfinite(value) && value >= low && value <= high) {
                    field = int(std::lround(value));
                }
            }
        }

        void readReal(const json5::value& from, const char* key, double& field, double low, double high) {
            json5::value v;
            if (find(from, key, v) && v.is_number()) {
                const double value = v.get<double>();
                if (std::isfinite(value) && value >= low && value <= high) {
                    field = value;
                }
            }
        }

        void readBool(const json5::value& from, const char* key, bool& field) {
            json5::value v;
            if (find(from, key, v) && v.is_boolean()) {
                field = v.get_bool();
            }
        }

        template <std::size_t N>
        void readChoice(const json5::value& from, const char* key, int& field, const char* const (&names)[N]) {
            json5::value v;
            if (find(from, key, v) && v.is_string()) {
                for (std::size_t i = 0; i < N; ++i) {
                    if (std::string_view(v.get_c_str()) == names[i]) {
                        field = int(i);
                        return;
                    }
                }
            }
        }

        std::string hexColor(std::uint32_t rgb) {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "#%06X", rgb & 0xFFFFFFu);
            return buffer;
        }

        void readColor(const json5::value& from, const char* key, std::uint32_t& field) {
            json5::value v;
            if (find(from, key, v) && v.is_string()) {
                const std::string text = v.get_c_str();
                char* end = nullptr;
                const unsigned long parsed = text.size() == 7 && text[0] == '#' ? std::strtoul(text.c_str() + 1, &end, 16) : 0;
                if (end != nullptr && *end == '\0') {
                    field = std::uint32_t(parsed);
                }
            }
        }
    }

    Settings::Settings() {
        for (int i = 0; i < dockCount; ++i) {
            docks[i].size = defaultDockSize[i];
        }
    }

    const char* Settings::dockName(int index) {
        static constexpr const char* names[dockCount] = {"top", "left", "right", "bottom"};
        return names[index];
    }

    void Settings::resetPage(SettingsPage page) {
        const Settings defaults;
        switch (page) {
            case SettingsPage::Appearance:
                theme = defaults.theme;
                canvasBackground = defaults.canvasBackground;
                canvasFollowsTheme = defaults.canvasFollowsTheme;
                propertyTextSize = defaults.propertyTextSize;
                break;
            case SettingsPage::Workspace:
                rulersVisible = defaults.rulersVisible;
                hintsFade = defaults.hintsFade;
                recentLength = defaults.recentLength;
                for (int i = 0; i < dockCount; ++i) {
                    docks[i] = defaults.docks[i];
                }
                break;
            case SettingsPage::Pen:
                scrubAxis = defaults.scrubAxis;
                scrubSensitivity = defaults.scrubSensitivity;
                spaceTapMs = defaults.spaceTapMs;
                barrelButton = defaults.barrelButton;
                eraserEnd = defaults.eraserEnd;
                break;
            case SettingsPage::Drawing:
                snapGrid = defaults.snapGrid;
                gridSize = defaults.gridSize;
                snapGuides = defaults.snapGuides;
                toolDefaultsInDocument = defaults.toolDefaultsInDocument;
                polygonSides = defaults.polygonSides;
                starPoints = defaults.starPoints;
                cornerRadius = defaults.cornerRadius;
                starInner = defaults.starInner;
                pencilSmoothing = defaults.pencilSmoothing;
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
        json5::document out;
        json5::builder b(out);
        auto number = [&b](const char* key, double value) { b[key] = json5::value(value); };
        auto flag = [&b](const char* key, bool value) { b[key] = json5::value(value); };
        auto text = [&b](const char* key, const std::string& value) { b[key] = b.new_string(value); };

        b.push_object();
        b.push_object();
        text("theme", themeNames[int(theme)]);
        text("canvasBackground", hexColor(canvasBackground));
        flag("canvasFollowsTheme", canvasFollowsTheme);
        number("propertyTextSize", propertyTextSize);
        b["appearance"] = b.pop();

        b.push_object();
        flag("rulersVisible", rulersVisible);
        flag("hintsFade", hintsFade);
        number("recentLength", recentLength);
        b["workspace"] = b.pop();

        b.push_object();
        text("scrubAxis", axisNames[int(scrubAxis)]);
        number("scrubSensitivity", scrubSensitivity);
        number("spaceTapMs", spaceTapMs);
        text("barrelButton", buttonNames[int(barrelButton)]);
        text("eraserEnd", eraserNames[int(eraserEnd)]);
        b["pen"] = b.pop();

        b.push_object();
        flag("snapGrid", snapGrid);
        number("gridSize", gridSize);
        flag("snapGuides", snapGuides);
        flag("toolDefaultsInDocument", toolDefaultsInDocument);
        number("polygonSides", polygonSides);
        number("starPoints", starPoints);
        number("cornerRadius", cornerRadius);
        number("starInner", starInner);
        number("pencilSmoothing", pencilSmoothing);
        b["drawing"] = b.pop();

        b.push_object();
        flag("autosave", autosave);
        number("autosaveMinutes", autosaveMinutes);
        text("exportFolder", exportFolder);
        b["files"] = b.pop();

        if (window.known()) {
            b.push_object();
            number("x", window.x);
            number("y", window.y);
            number("width", window.width);
            number("height", window.height);
            flag("maximized", window.maximized);
            text("monitor", window.monitor);
            b["window"] = b.pop();
        }

        b.push_object();
        for (int i = 0; i < dockCount; ++i) {
            b.push_object();
            number("size", docks[i].size);
            flag("collapsed", docks[i].collapsed);
            b[dockName(i)] = b.pop();
        }
        b["docks"] = b.pop();
        b.pop();

        json5::writer_params params;
        std::ostringstream os;
        os << std::setprecision(9);
        json5::to_stream(os, out, params);
        return os.str();
    }

    void Settings::fromText(const std::string& text) {
        json5::document parsed;
        if (json5::from_string(text, parsed)) {
            return;
        }
        const json5::value root = parsed;
        if (!root.is_object()) {
            return;
        }
        json5::value g;
        int choice = 0;

        if (group(root, "appearance", g)) {
            choice = int(theme);
            readChoice(g, "theme", choice, themeNames);
            theme = ThemeChoice(choice);
            readColor(g, "canvasBackground", canvasBackground);
            readBool(g, "canvasFollowsTheme", canvasFollowsTheme);
            readInt(g, "propertyTextSize", propertyTextSize, 10, 20);
        }
        if (group(root, "workspace", g)) {
            readBool(g, "rulersVisible", rulersVisible);
            readBool(g, "hintsFade", hintsFade);
            readInt(g, "recentLength", recentLength, 0, 50);
        }
        if (group(root, "pen", g)) {
            choice = int(scrubAxis);
            readChoice(g, "scrubAxis", choice, axisNames);
            scrubAxis = ScrubAxis(choice);
            readInt(g, "scrubSensitivity", scrubSensitivity, 25, 400);
            readInt(g, "spaceTapMs", spaceTapMs, 100, 1000);
            choice = int(barrelButton);
            readChoice(g, "barrelButton", choice, buttonNames);
            barrelButton = PenButton(choice);
            choice = int(eraserEnd);
            readChoice(g, "eraserEnd", choice, eraserNames);
            eraserEnd = EraserAction(choice);
        }
        if (group(root, "drawing", g)) {
            readBool(g, "snapGrid", snapGrid);
            readReal(g, "gridSize", gridSize, 0.001, 1000.0);
            readBool(g, "snapGuides", snapGuides);
            readBool(g, "toolDefaultsInDocument", toolDefaultsInDocument);
            readInt(g, "polygonSides", polygonSides, 3, 60);
            readInt(g, "starPoints", starPoints, 3, 40);
            readInt(g, "cornerRadius", cornerRadius, 0, 200);
            readInt(g, "starInner", starInner, 10, 90);
            readInt(g, "pencilSmoothing", pencilSmoothing, 0, 100);
        }
        if (group(root, "files", g)) {
            readBool(g, "autosave", autosave);
            readInt(g, "autosaveMinutes", autosaveMinutes, 1, 120);
            json5::value folder;
            if (find(g, "exportFolder", folder) && folder.is_string()) {
                exportFolder = folder.get_c_str();
            }
        }
        if (group(root, "window", g)) {
            WindowPlacement placed;
            readInt(g, "x", placed.x, -32000, 32000);
            readInt(g, "y", placed.y, -32000, 32000);
            readInt(g, "width", placed.width, 200, 32000);
            readInt(g, "height", placed.height, 150, 32000);
            readBool(g, "maximized", placed.maximized);
            json5::value monitor;
            if (find(g, "monitor", monitor) && monitor.is_string()) {
                placed.monitor = monitor.get_c_str();
            }
            if (placed.known()) {
                window = placed;
            }
        }
        json5::value docksGroup;
        if (group(root, "docks", docksGroup)) {
            for (int i = 0; i < dockCount; ++i) {
                if (group(docksGroup, dockName(i), g)) {
                    double size = docks[i].size;
                    readReal(g, "size", size, minDockSize, 32000.0);
                    docks[i].size = float(size);
                    readBool(g, "collapsed", docks[i].collapsed);
                }
            }
        }
    }

    std::string Settings::defaultPath() {
        const std::string base = newui::SpecialFolders::localAppData();
        return base.empty() ? std::string() : base + "/curvy/settings.json5";
    }

    Settings Settings::load(const std::string& path) {
        Settings settings;
        if (path.empty()) {
            return settings;
        }
        std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
        if (in) {
            std::ostringstream buffer;
            buffer << in.rdbuf();
            settings.fromText(buffer.str());
        }
        return settings;
    }

    bool Settings::save(const std::string& path) const {
        if (path.empty()) {
            return false;
        }
        const std::filesystem::path file = std::filesystem::u8path(path);
        std::error_code problem;
        std::filesystem::create_directories(file.parent_path(), problem);
        const std::filesystem::path temp = file.u8string() + ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out << toText();
            if (!out) {
                return false;
            }
        }
        std::filesystem::rename(temp, file, problem);
        return !problem;
    }

}
