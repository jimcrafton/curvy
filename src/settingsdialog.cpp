#include "curvy/settingsdialog.h"

#ifdef CURVY_GUI_AUTOMATION
#include <newui/automation.h>
#endif
#include <newui/bundle.h>
#include <newui/controls.h>
#include <newui/dialogs.h>
#include <newui/layout.h>
#include <newui/models.h>
#include <newui/uiinputmanager.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

namespace curvy {

    using namespace newui;

    namespace {
        std::string narrow(const std::wstring& text) {
            std::string result;
            for (wchar_t ch : text) {
                result.push_back(ch < 128 ? char(ch) : '?');
            }
            return result;
        }

        std::wstring widen(const std::string& text) {
            return std::wstring(text.begin(), text.end());
        }

        struct PageInfo {
            const char* nav;
            const char* page;
            SettingsPage id;
        };
        const PageInfo kPages[] = {
            {"navAppearance", "pageAppearance", SettingsPage::Appearance},
            {"navWorkspace", "pageWorkspace", SettingsPage::Workspace},
            {"navPen", "pagePen", SettingsPage::Pen},
            {"navDrawing", "pageDrawing", SettingsPage::Drawing},
            {"navFiles", "pageFiles", SettingsPage::Files}};

        std::string hex(std::uint32_t rgb) {
            char text[16];
            std::snprintf(text, sizeof(text), "#%06X", unsigned(rgb & 0xFFFFFF));
            return text;
        }

        bool parseHex(const std::string& text, std::uint32_t& rgb) {
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

        // One run of the dialog: finds the controls once, keeps them and the working settings in step, and reports every change.
        class Session {
        public:
            Session(RootView& root, Settings& working, std::function<void(const Settings&)> changed)
                : root_(root), working_(working), changed_(std::move(changed)) {}

            RootView& root() { return root_; }

            void wire(const std::function<void(DialogResult)>& closeWith, std::function<void()> onCancel = {}) {
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
                    cancel->onClick += [closeWith, onCancel](Control&) {
                        if (onCancel) {
                            onCancel();
                        }
                        closeWith(DialogResult::Cancel);
                        return SyncReturn::Handled;
                    };
                }
                if (auto* ok = find<Button>("settingsOk")) {
                    ok->onClick += [closeWith](Control&) { closeWith(DialogResult::Ok); return SyncReturn::Handled; };
                }
                if (auto* welcome = find<Button>("showWelcome")) {
                    welcome->onClick += [this, welcome](Control&) {
                        working_.welcomeShown = false;   // the workspace shows the card once the dialog is closed with OK
                        welcome->setText("Shows after OK");
                        welcome->setEnabled(false);
                        return SyncReturn::Handled;
                    };
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
                    [this] { return int(working_.scrubAxis); }, [this](int v) { working_.scrubAxis = WorkspaceModel::ScrubAxis(v); });
                dropDown("barrelButton", {"Open the rings", "Pan", "Nothing"},
                    [this] { return int(working_.barrelButton); }, [this](int v) { working_.barrelButton = Settings::PenButton(v); });
                dropDown("eraserEnd", {"Erase", "Undo", "Nothing"},
                    [this] { return int(working_.eraserEnd); }, [this](int v) { working_.eraserEnd = Settings::EraserAction(v); });
                dropDown("toolDefaultsScope", {"Remember per user", "Store in the document"},
                    [this] { return working_.toolDefaultsInDocument ? 1 : 0; }, [this](int v) { working_.toolDefaultsInDocument = v == 1; });

                toggle("canvasFollowsTheme", working_.canvasFollowsTheme);
                toggle("toolbarVisible", working_.toolbarVisible);
                toggle("propertiesVisible", working_.propertiesVisible);
                toggle("rulersVisible", working_.rulersVisible);
                toggle("hintsFade", working_.hintsFade);
                toggle("snapGrid", working_.snapGrid);
                toggle("snapGuides", working_.snapGuides);
                toggle("autosave", working_.autosave);

                slider("propertyTextSize", "propertyTextSizeValue", 10, 20, working_.propertyTextSize);
                slider("scrubSensitivity", "scrubSensitivityValue", 25, 400, working_.scrubSensitivity);

                number("recentLength", 0, 50, [this](double v) { working_.recentLength = int(v); }, [this] { return double(working_.recentLength); });
                number("spaceTap", 100, 1000, [this](double v) { working_.spaceTapMs = int(v); }, [this] { return double(working_.spaceTapMs); });
                number("gridSize", 0.1, 1000, [this](double v) { working_.gridSize = v; }, [this] { return working_.gridSize; });
                number("autosaveMinutes", 1, 120, [this](double v) { working_.autosaveMinutes = int(v); }, [this] { return double(working_.autosaveMinutes); });

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
            static void layoutTree(View& view) {
                view.updateLayout();
                for (SubView* child : view.childViews()) {
                    layoutTree(*child);
                }
            }

            void showPage(std::size_t index) {
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
                updating_ = false;
            }

        private:
            template <typename T>
            T* find(const char* name) const {
                return dynamic_cast<T*>(root_.findView(name));
            }

            void edited() {
                if (!updating_) {
                    changed_(working_);
                }
            }

            void dropDown(const char* name, std::vector<std::string> items, std::function<int()> get, std::function<void(int)> set) {
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

            void toggle(const char* name, bool& field) {
                auto* box = find<Toggle>(name);
                if (box == nullptr) {
                    return;
                }
                box->onCheckedChanged += [this, &field](Toggle& changed) {
                    if (!updating_) {
                        field = changed.isChecked();
                        edited();
                    }
                    return SyncReturn::Handled;
                };
                syncs_.push_back([box, &field] { box->setChecked(field); });
            }

            // A slider and the text box beside it, both for one whole number.
            void slider(const char* sliderName, const char* fieldName, int low, int high, int& field) {
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
                auto commit = [this, &field, low, high, bar, text] {
                    if (updating_) {
                        return;
                    }
                    const double value = std::atof(narrow(text->text()).c_str());
                    if (value >= low && value <= high) {
                        field = int(value);
                    }
                    refresh();
                    edited();
                    (void)bar;
                };
                text->onReturnPressed += [commit](TextField&) { commit(); return SyncReturn::Handled; };
                text->onLostFocus += [commit](View&) { commit(); return SyncReturn::Handled; };
                syncs_.push_back([bar, text, &field] {
                    bar->setValue(float(field));
                    text->setText(widen(std::to_string(field)));
                });
            }

            void number(const char* name, double low, double high, std::function<void(double)> set, std::function<double()> get) {
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

            RootView& root_;
            Settings& working_;
            std::function<void(const Settings&)> changed_;
            std::vector<std::function<void()>> syncs_;
            std::size_t page_ = 0;
            bool updating_ = false;
        };
    }

    bool SettingsDialog::run(Frame& owner, Settings& settings, const std::function<void(const Settings&)>& apply, const std::function<void()>& onCancel) {
        Dialog dialog;
        dialog.setName("settings");
        if (!Bundle::instance().loadDialog(dialog)) {
            return false;
        }
        const Settings original = settings;
        Settings working = settings;
        Session session(dialog.rootView(), working, apply);
        session.wire([&dialog](DialogResult result) { dialog.close(result); }, onCancel);
        session.start();
#ifdef CURVY_GUI_AUTOMATION
        Automation::expose(dialog.rootView());   // so a GUI test can reach the dialog's views while it is open
#endif
        if (dialog.showModal(&owner) != DialogResult::Ok) {
            apply(original);
            return false;
        }
        settings = working;
        return true;
    }

    int SettingsDialog::check() {
        int failures = 0;
        auto expect = [&failures](bool ok, const char* what) {
            std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
            failures += ok ? 0 : 1;
        };

        Dialog dialog;
        dialog.setName("settings");
        expect(Bundle::instance().loadDialog(dialog), "settings.newui loads into a Dialog");

        Settings working;
        int applied = 0;
        Settings last;
        Session session(dialog.rootView(), working, [&applied, &last](const Settings& s) { ++applied; last = s; });
        int closedOk = 0;
        int closedCancel = 0;
        session.wire([&closedOk, &closedCancel](DialogResult result) { (result == DialogResult::Ok ? closedOk : closedCancel)++; });
        session.start();
        dialog.rootView().updateLayout();

        auto find = [&session](const char* name) { return session.root().findView(name); };
        auto* theme = dynamic_cast<DropDownList*>(find("themeChoice"));
        auto* rulers = dynamic_cast<Toggle*>(find("rulersVisible"));
        auto* size = dynamic_cast<Slider*>(find("propertyTextSize"));
        auto* sizeText = dynamic_cast<TextField*>(find("propertyTextSizeValue"));
        auto* hold = dynamic_cast<TextField*>(find("spaceTap"));
        auto* navPen = dynamic_cast<Button*>(find("navPen"));
        auto* reset = dynamic_cast<Button*>(find("settingsReset"));
        expect(theme && rulers && size && sizeText && hold && navPen && reset, "all settings controls found");
        if (!(theme && rulers && size && sizeText && hold && navPen && reset)) {
            return failures;
        }

        expect(find("pageAppearance")->isVisible() && !find("pagePen")->isVisible(), "opens on Appearance only");
        expect(theme->selectedIndex() == std::optional<std::size_t>(0), "theme shows Dark");
        expect(narrow(sizeText->text()) == "11" && size->value() == 11.0f, "property text size shows 11");
        expect(rulers->isChecked(), "rulers at start is on");

        theme->setSelectedIndex(std::size_t(1));
        expect(working.theme == Settings::ThemeChoice::Light && applied == 1 && last.theme == Settings::ThemeChoice::Light, "choosing Light applies live");
        size->setValue(14.0f);
        expect(working.propertyTextSize == 14 && narrow(sizeText->text()) == "14", "slider moves the number and the text");
        rulers->setChecked(false);
        expect(!working.rulersVisible, "unticking rulers changes the setting");

        navPen->onClick(*navPen);
        expect(!find("pageAppearance")->isVisible() && find("pagePen")->isVisible(), "Pen nav shows the pen page");
        {
            View* pen = find("pagePen");
            expect(pen->parent() != nullptr && pen->parent()->name() == "settingsPages", "the pages are siblings, not nested in the first one");
            expect(pen->bounds().top() < 4.0f && pen->bounds().height() > 100.0f, "the shown page fills the pages area");
        }
        hold->setText(L"650");
        hold->onReturnPressed(*hold);
        expect(working.spaceTapMs == 650, "typed tap threshold is taken");
        hold->setText(L"5");
        hold->onReturnPressed(*hold);
        expect(working.spaceTapMs == 300 || working.spaceTapMs == 650, "out-of-range threshold is refused");
        expect(narrow(hold->text()) == std::to_string(working.spaceTapMs), "the box goes back to the kept value");

        reset->onClick(*reset);
        expect(working.spaceTapMs == 300 && working.theme == Settings::ThemeChoice::Light, "Reset page only resets the shown page");

        RootView& root = dialog.rootView();
        root.setFocusedSubView(hold);
        UIInputManager::instance().routeDialogKeyDown(root, 0, 0, 1, vkReturn);
        expect(closedOk == 0, "Return in a field with its own handler does not press OK");
        root.setFocusedSubView(reset);
        UIInputManager::instance().routeDialogKeyDown(root, 0, 0, 1, vkSpaceBar);
        expect(closedOk == 0 && closedCancel == 0, "Space on a focused button clicks that button, not OK");
        root.setFocusedSubView(rulers);
        UIInputManager::instance().routeDialogKeyDown(root, 0, 0, 1, vkReturn);
        expect(closedOk == 1, "Return elsewhere presses the default OK button");
        UIInputManager::instance().routeDialogKeyDown(root, 0, 0, 1, vkEscape);
        expect(closedCancel == 1, "Escape presses Cancel");
        UIInputManager::instance().routeDialogKeyDown(root, kmCtrl, 0, 1, vkEscape);
        expect(closedCancel == 1, "Ctrl+Escape is left alone");

        Settings roundTrip;
        roundTrip.fromText(working.toText());
        expect(roundTrip.toText() == working.toText(), "settings text round-trips");
        Settings junk;
        junk.fromText("spaceTapMs=99999\nunknown=1\nscrubAxis=x\ntheme=2\n");
        expect(junk.spaceTapMs == 300 && junk.theme == Settings::ThemeChoice::FollowWindows && junk.scrubAxis == WorkspaceModel::ScrubAxis::Vertical,
            "bad values are skipped, good ones kept");
        return failures;
    }

}
