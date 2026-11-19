#pragma once

// Private to the Settings dialog (settingsdialog.cpp) and its tests: one run of the dialog, which keeps its controls and a working copy of
// the settings in step. It lives in a header so the tests can drive it without showing a window.

#include "curvy/editorfonts.h"
#include "curvy/pythonhighlight.h"
#include "curvy/settings.h"

#include <newui/controls.h>
#include <newui/dialogs.h>
#include <newui/layout.h>
#include <newui/models.h>
#include <newui/rootview.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace curvy::settingsdialog {

    inline std::string narrow(const std::wstring& text) {
        std::string result;
        for (wchar_t ch : text) {
            result.push_back(ch < 128 ? char(ch) : '?');
        }
        return result;
    }

    inline std::wstring widen(const std::string& text) {
        return std::wstring(text.begin(), text.end());
    }

    // @reflect ignore=true
    struct PageInfo {
        const char* nav;
        const char* page;
        SettingsPage id;
    };
    inline const PageInfo kPages[] = {
        {"navAppearance", "pageAppearance", SettingsPage::Appearance},
        {"navWorkspace", "pageWorkspace", SettingsPage::Workspace},
        {"navUnits", "pageUnits", SettingsPage::Units},
        {"navPen", "pagePen", SettingsPage::Pen},
        {"navDrawing", "pageDrawing", SettingsPage::Drawing},
        {"navFiles", "pageFiles", SettingsPage::Files},
        {"navScriptEditor", "pageScriptEditor", SettingsPage::ScriptEditor}};

    constexpr int leftDock = 1, rightDock = 2;   // Settings::docks indexes: top, left, right, bottom

    inline std::string hex(std::uint32_t rgb) {
        char text[16];
        std::snprintf(text, sizeof(text), "#%06X", unsigned(rgb & 0xFFFFFF));
        return text;
    }

    inline bool parseHex(const std::string& text, std::uint32_t& rgb) {
        if (text.size() != 7 || text[0] != '#') {
            return false;
        }
        char* end = nullptr;
        const unsigned long value = std::strtoul(text.c_str() + 1, &end, 16);
        if (*end != '\0') {
            return false;
        }
        rgb = std::uint32_t(value);
        return true;
    }

    // Finds the controls once, keeps them and the working settings in step, and reports every change.
    // @reflect ignore=true
    class Session {
    public:
        Session(newui::RootView& root, Settings& working, std::function<void(const Settings&)> changed)
            : root_(root), working_(working), changed_(std::move(changed)) {}

        newui::RootView& root() { return root_; }

        void wire(const std::function<void(newui::DialogResult)>& closeWith) {
            using namespace newui;
            for (std::size_t i = 0; i < std::size(kPages); ++i) {
                if (auto* nav = find<Button>(kPages[i].nav)) {
                    nav->onClick += [this, i](Control&) { showPage(i); return SyncReturn::Handled; };
                }
            }
            if (auto* reset = find<Button>("settingsReset")) {
                reset->onClick += [this](Control&) {
                    working_.resetPage(kPages[page_].id);
                    refresh();
                    changed_(working_);
                    return SyncReturn::Handled;
                };
            }
            if (auto* cancel = find<Button>("settingsCancel")) {
                cancel->onClick += [closeWith](Control&) { closeWith(DialogResult::Cancel); return SyncReturn::Handled; };
            }
            if (auto* ok = find<Button>("settingsOk")) {
                ok->onClick += [closeWith](Control&) { closeWith(DialogResult::Ok); return SyncReturn::Handled; };
            }
            // Rows for things the app does not have yet: the welcome card and the new-shape colours.
            for (const char* name : {"showWelcome", "defaultFillWell"}) {
                if (View* control = root_.findView(name)) {
                    if (control->parent() != nullptr) {
                        control->parent()->setVisible(false);
                    }
                }
            }
            if (auto* browse = find<Button>("exportFolderBrowse")) {
                browse->setEnabled(false);
            }
            if (auto* search = find<TextField>("settingsSearch")) {
                search->setEnabled(false);
            }

            dropDown("themeChoice", {"Dark", "Light", "Follow Windows"},
                [this] { return int(working_.theme); }, [this](int v) { working_.theme = Settings::ThemeChoice(v); });
            dropDown("scrubAxis", {"Vertical", "Horizontal"},
                [this] { return int(working_.scrubAxis); }, [this](int v) { working_.scrubAxis = Settings::ScrubAxis(v); });
            dropDown("barrelButton", {"Open the rings", "Pan", "Nothing"},
                [this] { return int(working_.barrelButton); }, [this](int v) { working_.barrelButton = Settings::PenButton(v); });
            dropDown("eraserEnd", {"Erase", "Undo", "Nothing"},
                [this] { return int(working_.eraserEnd); }, [this](int v) { working_.eraserEnd = Settings::EraserAction(v); });
            std::vector<std::string> lengthNames;
            for (int i = 0; i < Units::kLengthCount; ++i) {
                lengthNames.push_back(Units::name(LengthUnit(i)));
            }
            dropDown("defaultLength", lengthNames,
                [this] { return int(working_.defaultUnits.length); }, [this](int v) { working_.defaultUnits.length = LengthUnit(v); });
            dropDown("defaultAngle", {"deg", "rad"},
                [this] { return int(working_.defaultUnits.angle); }, [this](int v) { working_.defaultUnits.angle = AngleUnit(v); });
            dropDown("toolDefaultsScope", {"Remember per user", "Store in the document"},
                [this] { return working_.toolDefaultsInDocument ? 1 : 0; }, [this](int v) { working_.toolDefaultsInDocument = v == 1; });

            toggle("canvasFollowsTheme", [this] { return working_.canvasFollowsTheme; }, [this](bool v) { working_.canvasFollowsTheme = v; });
            toggle("toolbarVisible", [this] { return !working_.docks[leftDock].collapsed; }, [this](bool v) { working_.docks[leftDock].collapsed = !v; });
            toggle("propertiesVisible", [this] { return !working_.docks[rightDock].collapsed; }, [this](bool v) { working_.docks[rightDock].collapsed = !v; });
            toggle("rulersVisible", [this] { return working_.rulersVisible; }, [this](bool v) { working_.rulersVisible = v; });
            toggle("hintsFade", [this] { return working_.hintsFade; }, [this](bool v) { working_.hintsFade = v; });
            toggle("snapGrid", [this] { return working_.snapGrid; }, [this](bool v) { working_.snapGrid = v; });
            toggle("snapGuides", [this] { return working_.snapGuides; }, [this](bool v) { working_.snapGuides = v; });
            toggle("autosave", [this] { return working_.autosave; }, [this](bool v) { working_.autosave = v; });

            slider("propertyTextSize", "propertyTextSizeValue", 10, 20, working_.propertyTextSize);
            slider("scrubSensitivity", "scrubSensitivityValue", 25, 400, working_.scrubSensitivity);

            number("recentLength", 0, 50, [this](double v) { working_.recentLength = int(v); }, [this] { return double(working_.recentLength); });
            number("spaceTap", 100, 1000, [this](double v) { working_.spaceTapMs = int(v); }, [this] { return double(working_.spaceTapMs); });
            number("gridSize", 0.1, 1000, [this](double v) { working_.gridSize = v; }, [this] { return working_.gridSize; });
            number("autosaveMinutes", 1, 120, [this](double v) { working_.autosaveMinutes = int(v); }, [this] { return double(working_.autosaveMinutes); });

            wireScriptEditor();

            if (auto* hexField = find<TextField>("canvasBackgroundHex")) {
                auto commit = [this, hexField] {
                    std::uint32_t rgb = 0;
                    if (!updating_ && parseHex(narrow(hexField->text()), rgb)) {
                        working_.canvasBackground = rgb;
                        working_.canvasFollowsTheme = false;
                        refresh();
                        changed_(working_);
                    }
                };
                hexField->onReturnPressed += [commit](TextField&) { commit(); return SyncReturn::Handled; };
                hexField->onLostFocus += [commit](View&) { commit(); return SyncReturn::Handled; };
            }
            if (auto* folder = find<TextField>("exportFolder")) {
                auto commit = [this, folder] {
                    if (!updating_) {
                        working_.exportFolder = narrow(folder->text());
                        changed_(working_);
                    }
                };
                folder->onReturnPressed += [commit](TextField&) { commit(); return SyncReturn::Handled; };
                folder->onLostFocus += [commit](View&) { commit(); return SyncReturn::Handled; };
            }
        }

        void start() {
            showPage(0);
            refresh();
        }

        std::size_t page() const { return page_; }

        // The pages are the cards of one CardLayout: the shown one fills the area, the others are hidden.
        // A page hidden while the dialog loaded was skipped by layout, and its size doesn't change when it is shown, so lay out
        // its subtree explicitly.
        static void layoutTree(newui::View& view) {
            view.updateLayout();
            for (newui::SubView* child : view.childViews()) {
                layoutTree(*child);
            }
        }

        void showPage(std::size_t index) {
            using namespace newui;
            page_ = index;
            if (View* pages = root_.findView("settingsPages")) {
                if (dynamic_cast<CardLayout*>(pages->layout()) == nullptr) {
                    pages->setLayout(std::make_unique<CardLayout>());
                }
                static_cast<CardLayout*>(pages->layout())->show(index);
                for (SubView* page : pages->childViews()) {
                    if (page->isVisible()) {
                        layoutTree(*page);
                    }
                }
            }
            root_.updateLayout();
        }

        // Working settings -> controls.
        void refresh() {
            using namespace newui;
            updating_ = true;
            for (const auto& sync : syncs_) {
                sync();
            }
            if (auto* hexField = find<TextField>("canvasBackgroundHex")) {
                hexField->setText(widen(hex(working_.canvasBackground)));
            }
            if (auto* well = root_.findView("canvasBackgroundWell")) {
                well->style().setBackgroundColor(Color(std::uint32_t(0xFF000000u | working_.canvasBackground), true));
                well->redraw();
            }
            if (auto* folder = find<TextField>("exportFolder")) {
                folder->setText(widen(working_.exportFolder));
            }
            refreshSyntaxColors();
            updating_ = false;
            updatePreview();
        }

    private:
        // The Script editor page: the font (a list of the installed monospace faces, "Automatic" first), its size, the tab width, one hex
        // field and colour well per syntax style, and a preview that follows all of it.
        void wireScriptEditor() {
            using namespace newui;
            auto fonts = std::make_shared<std::vector<std::string>>();
            fonts->push_back("Automatic");
            for (const std::string& font : installedMonospaceFonts()) {
                fonts->push_back(font);
            }
            if (!working_.editorFont.empty() && std::find(fonts->begin(), fonts->end(), working_.editorFont) == fonts->end()) {
                fonts->push_back(working_.editorFont);   // a face from a hand-edited file that the heuristic did not list
            }
            dropDown("editorFont", *fonts,
                [this, fonts] {
                    const auto found = std::find(fonts->begin() + 1, fonts->end(), working_.editorFont);
                    return working_.editorFont.empty() || found == fonts->end() ? 0 : int(found - fonts->begin());
                },
                [this, fonts](int v) { working_.editorFont = v <= 0 ? std::string() : (*fonts)[std::size_t(v)]; });
            number("editorFontSize", 8, 32, [this](double v) { working_.editorFontSize = int(v); }, [this] { return double(working_.editorFontSize); });
            number("editorTabWidth", 2, 8, [this](double v) { working_.editorTabWidth = int(v); }, [this] { return double(working_.editorTabWidth); });
            toggle("scriptTimeout", [this] { return working_.scriptTimeout; }, [this](bool v) { working_.scriptTimeout = v; });
            number("scriptTimeoutSeconds", 1, 86400, [this](double v) { working_.scriptTimeoutSeconds = int(v); },
                   [this] { return double(working_.scriptTimeoutSeconds); });
            number("pythonNodeSeconds", 0, 86400, [this](double v) { working_.pythonNodeSeconds = int(v); },
                   [this] { return double(working_.pythonNodeSeconds); });

            for (const SyntaxStyleInfo& info : editorColorRows()) {
                const std::string style = info.name;
                auto* hex = find<TextField>(("syntaxHex_" + style).c_str());
                if (hex == nullptr) {
                    continue;
                }
                auto commit = [this, hex, style] {
                    if (updating_) {
                        return;
                    }
                    const std::string typed = narrow(hex->text());
                    std::uint32_t rgb = 0;
                    if (typed.empty() || (parseHex(typed, rgb) && rgb == pythonDefaultColor(style, working_.darkTheme()))) {
                        working_.editorColors.erase(style);   // back to the theme's colour
                    } else if (parseHex(typed, rgb)) {
                        working_.editorColors[style] = rgb;
                    }
                    refresh();
                    edited();
                };
                hex->onReturnPressed += [commit](TextField&) { commit(); return SyncReturn::Handled; };
                hex->onLostFocus += [commit](View&) { commit(); return SyncReturn::Handled; };
            }

            if (auto* preview = find<TextControl>("editorPreview")) {
                preview->inputTraits().setReadOnly(true);
                preview->setText(widen(kPreviewCode));
            }
        }

        // Every colour row of the page: the syntax styles, then the text and background colours.
        static std::vector<SyntaxStyleInfo> editorColorRows() {
            std::vector<SyntaxStyleInfo> rows = pythonSyntaxStyles();
            rows.insert(rows.end(), pythonBaseColors().begin(), pythonBaseColors().end());
            return rows;
        }

        static constexpr const char* kPreviewCode =
            "@decorator\n"
            "def draw(shape, count=3):\n"
            "    # one circle per step\n"
            "    for i in range(count):\n"
            "        shape.add(i * 10.5, \"ok\", True)\n"
            "    return None";

        void refreshSyntaxColors() {
            const bool dark = working_.darkTheme();
            for (const SyntaxStyleInfo& info : editorColorRows()) {
                const std::string style = info.name;
                const auto chosen = working_.editorColors.find(style);
                const std::uint32_t rgb = chosen != working_.editorColors.end() ? chosen->second : pythonDefaultColor(style, dark);
                if (auto* field = find<newui::TextField>(("syntaxHex_" + style).c_str())) {
                    field->setText(widen(hex(rgb)));
                }
                if (auto* well = root_.findView(("syntaxWell_" + style).c_str())) {
                    well->style().setBackgroundColor(newui::Color(std::uint32_t(0xFF000000u | rgb), true));
                    well->redraw();
                }
            }
        }

        // The preview shows the editor as the working settings would draw it.
        void updatePreview() {
            auto* preview = find<newui::TextControl>("editorPreview");
            if (preview == nullptr) {
                return;
            }
            const bool dark = working_.darkTheme();
            preview->setFont(scriptEditorFont(working_.editorFont, working_.editorFontSize));
            preview->setStyleSheet(pythonStyleSheet(dark, working_.editorColors));
            preview->setStyledRanges(pythonStyleRanges(widen(kPreviewCode)));
            const auto colorOf = [this, dark](const char* name) {
                const auto chosen = working_.editorColors.find(name);
                return chosen != working_.editorColors.end() ? chosen->second : pythonDefaultColor(name, dark);
            };
            preview->setTextColor(BLRgba32(0xFF000000u | colorOf("text")));
            preview->style().setBackgroundColor(newui::Color(std::uint32_t(0xFF000000u | colorOf("background")), true));
            preview->style().markDirty();
        }

        template <typename T>
        T* find(const char* name) const {
            return dynamic_cast<T*>(root_.findView(name));
        }

        void edited() {
            if (!updating_) {
                updatePreview();
                changed_(working_);
            }
        }

        void dropDown(const char* name, std::vector<std::string> items, std::function<int()> get, std::function<void(int)> set) {
            using namespace newui;
            auto* combo = find<DropDownList>(name);
            if (combo == nullptr) {
                return;
            }
            auto model = std::make_unique<StringListModel>();
            for (const std::string& item : items) {
                model->addItem(item);
            }
            combo->setModel(std::move(model));
            combo->onSelectionChanged += [this, set](DropDownList& changed) {
                std::optional<std::size_t> index = changed.selectedIndex();
                if (!updating_ && index.has_value()) {
                    set(int(*index));
                    edited();
                }
                return SyncReturn::Handled;
            };
            syncs_.push_back([combo, get] { combo->setSelectedIndex(std::size_t(get())); });
        }

        void toggle(const char* name, std::function<bool()> get, std::function<void(bool)> set) {
            using namespace newui;
            auto* box = find<Toggle>(name);
            if (box == nullptr) {
                return;
            }
            box->onCheckedChanged += [this, set](Toggle& changed) {
                if (!updating_) {
                    set(changed.isChecked());
                    edited();
                }
                return SyncReturn::Handled;
            };
            syncs_.push_back([box, get] { box->setChecked(get()); });
        }

        // A slider and the text box beside it, both for one whole number.
        void slider(const char* sliderName, const char* fieldName, int low, int high, int& field) {
            using namespace newui;
            auto* bar = find<Slider>(sliderName);
            auto* text = find<TextField>(fieldName);
            if (bar == nullptr || text == nullptr) {
                return;
            }
            bar->setRange(float(low), float(high));
            bar->setInteger(true);
            bar->onValueChanged += [this, &field, text](Slider& changed) {
                if (!updating_) {
                    field = int(changed.value());
                    updating_ = true;
                    text->setText(widen(std::to_string(field)));
                    updating_ = false;
                    edited();
                }
                return SyncReturn::Handled;
            };
            auto commit = [this, &field, low, high, text] {
                if (updating_) {
                    return;
                }
                const double value = std::atof(narrow(text->text()).c_str());
                if (value >= low && value <= high) {
                    field = int(value);
                }
                refresh();
                edited();
            };
            text->onReturnPressed += [commit](TextField&) { commit(); return SyncReturn::Handled; };
            text->onLostFocus += [commit](View&) { commit(); return SyncReturn::Handled; };
            syncs_.push_back([bar, text, &field] {
                bar->setValue(float(field));
                text->setText(widen(std::to_string(field)));
            });
        }

        void number(const char* name, double low, double high, std::function<void(double)> set, std::function<double()> get) {
            using namespace newui;
            auto* text = find<TextField>(name);
            if (text == nullptr) {
                return;
            }
            auto format = [](double value) {
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%g", value);
                return std::string(buffer);
            };
            auto commit = [this, low, high, set, text] {
                if (updating_) {
                    return;
                }
                const std::string typed = narrow(text->text());
                char* end = nullptr;
                const double value = std::strtod(typed.c_str(), &end);
                if (end != typed.c_str() && *end == '\0' && value >= low && value <= high) {
                    set(value);
                }
                refresh();
                edited();
            };
            text->onReturnPressed += [commit](TextField&) { commit(); return SyncReturn::Handled; };
            text->onLostFocus += [commit](View&) { commit(); return SyncReturn::Handled; };
            syncs_.push_back([text, get, format] { text->setText(widen(format(get()))); });
        }

        newui::RootView& root_;
        Settings& working_;
        std::function<void(const Settings&)> changed_;
        std::vector<std::function<void()>> syncs_;
        std::size_t page_ = 0;
        bool updating_ = false;
    };

}
