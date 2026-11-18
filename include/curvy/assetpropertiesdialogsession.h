#pragma once

// Private to the Asset Properties dialog (assetpropertiesdialog.cpp) and its tests: one run of the dialog, which keeps its controls and an
// AssetPropertiesSession in step. It lives in a header so the tests can drive it without showing a window.

#include "curvy/assetpropertiessession.h"
#include "curvy/codeeditorview.h"
#include "curvy/pythonhighlight.h"

#include <newui/controls.h>
#include <newui/cursor.h>
#include <newui/dialogs.h>
#include <newui/groupedlist.h>
#include <newui/layout.h>
#include <newui/models.h>
#include <newui/mouse_constants.h>
#include <newui/rootview.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace curvy::assetpropsdialog {

    inline std::string narrow(const std::wstring& text) {
        return std::string(text.begin(), text.end());
    }

    inline std::wstring widen(const std::string& text) {
        return std::wstring(text.begin(), text.end());
    }

    // The pages are the cards of one CardLayout, in this order; each has a tab button.
    inline const char* const kPages[] = {"Basic", "Parameters", "Contents", "Python", "Help", "Files"};

    // @reflect ignore=true
    struct EventInfo {
        const char* name;
        const char* group;
        const char* when;
        const char* args;
    };

    // When each script runs (docs/asset-manager-plan.md, section 5a). The same sentences are in docs/mockups/assetProperties.html.
    inline const EventInfo kEvents[] = {
        {"module", "SHARED CODE",
         "Once per session, the first time the asset is used (an instance is made, a file with one opens, or another script imports it). Defines helper "
         "functions; it is not tied to any node.",
         "none"},
        {"onCreated", "EACH INSTANCE",
         "An instance exists: Add, paste, undo of a delete, or opening a document (then after the whole document is built, so other nodes can be looked "
         "at). reason is added, pasted, undo or loaded; set defaults only for added and pasted.",
         "node, reason"},
        {"onVersionUpdated", "EACH INSTANCE",
         "An older instance is rebuilt from a newer version of the asset (Update, or opening a document saved with an older one). For changes the renames "
         "list cannot express, such as a new unit. Must only read old and write fresh: it cannot touch the document, and it may run twice.",
         "old, fresh, inputs, from_version, to_version"},
        {"onInputChanged", "EACH INSTANCE", "An input of the instance was wired, rewired or disconnected.", "node, index"},
        {"onParamChanged", "EACH INSTANCE",
         "A promoted parameter was edited by the user, a script or undo. Not run for animated values while the timeline plays or scrubs (see "
         "onFrameChanged).",
         "node, name, value"},
        {"onFrameChanged", "EACH INSTANCE",
         "The playhead moved, after the new frame is shown. Skipped while playing if the previous call has not finished; at most once per displayed frame. Use it to drive other things from the time. It cannot change this asset or anything it reads from: Apply and Save refuse a script that tries to, and it is stopped when it runs.",
         "node, frame, playing"},
        {"onDeleted", "EACH INSTANCE", "The instance is about to be deleted (it still exists). Not run when the whole document closes.", "node"},
        {"onInstalled", "LIBRARY",
         "The asset is registered in the library (app start, Load, Reload, Save). Not run for an asset that only arrives embedded in a document until that "
         "document is trusted.",
         "name, version"}};

    // What the dialog asks of its owner (the editor), each optional.
    // @reflect ignore=true
    struct Callbacks {
        // The file to save to, asked for when the asset has none; empty when the user cancels.
        std::function<std::string(const std::string& suggestedName)> chooseSavePath;
        // Takes the contents and parameters from the selected subnet of the graph (Match node); returns an error message or "".
        std::function<std::string(AssetPropertiesSession&)> matchSubnet;
        // Makes the contents editable in the graph (adds an instance and unlocks it); the dialog applies its changes first and closes.
        std::function<std::string(const std::string& name)> editContents;
        // A file to add to the asset: its name and text; false when the user cancels.
        std::function<bool(std::string& name, std::string& text)> chooseFile;
        // After Apply or Save changed the library.
        std::function<void()> changed;
    };

    // Finds the controls once, keeps them and the session in step, and reports every change.
    // @reflect ignore=true
    class Session {
    public:
        Session(newui::RootView& root, AssetPropertiesSession& state, Callbacks callbacks)
            : root_(root), state_(state), callbacks_(std::move(callbacks)) {}

        void wire(const std::function<void(newui::DialogResult)>& closeWith) {
            using namespace newui;
            closeWith_ = closeWith;
            for (std::size_t i = 0; i < std::size(kPages); ++i) {
                if (auto* tab = find<Button>((std::string("tab") + kPages[i]).c_str())) {
                    tab->onClick += [this, i](Control&) {
                        showPage(i);
                        return SyncReturn::Handled;
                    };
                }
            }
            button("propsCancel", [this]() { closeWith_(DialogResult::Cancel); });
            button("propsRevert", [this]() {
                state_.revert();
                param_ = -1;
                refresh();
            });
            button("propsApply", [this]() {
                const std::string error = state_.apply();
                if (!error.empty()) {
                    setFooter(error);
                    return;
                }
                if (callbacks_.changed) {
                    callbacks_.changed();
                }
                refresh();
            });
            button("propsSave", [this]() { save(); });
            button("paramAdd", [this]() {
                param_ = int(state_.addParam());
                refresh();
            });
            button("paramRemove", [this]() {
                if (param_ >= 0) {
                    state_.removeParam(std::size_t(param_));
                    param_ = (std::min)(param_, int(state_.paramCount()) - 1);
                    refresh();
                }
            });
            button("matchCurrent", [this]() {
                if (callbacks_.matchSubnet) {
                    const std::string problem = callbacks_.matchSubnet(state_);
                    refresh();
                    if (!problem.empty()) {
                        setFooter(problem);
                    }
                }
            });
            button("editContents", [this]() {
                if (!callbacks_.editContents) {
                    return;
                }
                const std::string applied = state_.apply();
                if (!applied.empty()) {
                    setFooter(applied);
                    return;
                }
                const std::string error = callbacks_.editContents(state_.working().name);
                if (!error.empty()) {
                    setFooter(error);
                    return;
                }
                if (callbacks_.changed) {
                    callbacks_.changed();
                }
                closeWith_(DialogResult::Ok);
            });
            button("fileAdd", [this]() {
                std::string name;
                std::string text;
                if (callbacks_.chooseFile && callbacks_.chooseFile(name, text)) {
                    state_.setFile(name, text);
                    refresh();
                }
            });
            button("fileRemove", [this]() {
                if (!file_.empty()) {
                    state_.removeFile(file_);
                    file_.clear();
                    refresh();
                }
            });

            text("typeName", [this] { return state_.working().name; }, [this](const std::string& v) { state_.setName(v); });
            text("typeLabel", [this] { return state_.working().label; }, [this](const std::string& v) { state_.setLabel(v); });
            text("typeCategory", [this] { return state_.working().category; }, [this](const std::string& v) { state_.setCategory(v); });
            text("typeAuthor", [this] { return state_.working().author; }, [this](const std::string& v) { state_.setAuthor(v); });
            text("helpSummary", [this] { return state_.working().description; }, [this](const std::string& v) { state_.setDescription(v); });
            text("inputCount", [this] { return std::to_string(state_.working().inputs); },
                 [this](const std::string& v) {
                     char* end = nullptr;
                     const long value = std::strtol(v.c_str(), &end, 10);
                     if (end != v.c_str() && *end == '\0') {
                         state_.setInputs(int(value));
                     }
                 });
            if (auto* help = find<TextControl>("helpText")) {
                help->model().onChanged.add([this, help](Model&) {
                    if (!updating_) {
                        state_.setHelp(narrow(help->text()));
                        refreshState();
                    }
                    return SyncReturn::Ignored;
                });
                syncs_.push_back([this, help] {
                    if (narrow(help->text()) != state_.working().help) {
                        help->setText(widen(state_.working().help));
                    }
                });
            }

            // The parameter editor: name on Enter or when leaving the field (a rename rewrites the contents), the rest as it is typed.
            paramText("paramName", true, [](const std::string& v, jitnodes::AssetParam& p) { p.name = v; return true; },
                      [](const jitnodes::AssetParam& p) { return p.name; });
            paramText("paramLabel", false, [](const std::string& v, jitnodes::AssetParam& p) { p.label = v; return true; },
                      [](const jitnodes::AssetParam& p) { return p.label; });
            paramText("paramGroup", false, [](const std::string& v, jitnodes::AssetParam& p) { p.group = v; return true; },
                      [](const jitnodes::AssetParam& p) { return p.group; });
            paramText("paramTooltip", false, [](const std::string& v, jitnodes::AssetParam& p) { p.tooltip = v; return true; },
                      [](const jitnodes::AssetParam& p) { return p.tooltip; });
            paramText("paramDefault", false,
                      [](const std::string& v, jitnodes::AssetParam& p) {
                          char* end = nullptr;
                          const float value = std::strtof(v.c_str(), &end);
                          if (end == v.c_str() || *end != '\0') {
                              return false;
                          }
                          p.defaultValue = value;
                          return true;
                      },
                      [](const jitnodes::AssetParam& p) { return number(p.defaultValue); });
            paramText("paramMin", false, [](const std::string& v, jitnodes::AssetParam& p) { return optionalNumber(v, p.minimum); },
                      [](const jitnodes::AssetParam& p) { return p.minimum ? number(*p.minimum) : std::string(); });
            paramText("paramMax", false, [](const std::string& v, jitnodes::AssetParam& p) { return optionalNumber(v, p.maximum); },
                      [](const jitnodes::AssetParam& p) { return p.maximum ? number(*p.maximum) : std::string(); });

            wirePython();
        }

        void start() {
            using namespace newui;
            buildEventRows();
            buildParamList();
            buildFilesList();
            showPage(0);
            refresh();
        }

        std::size_t page() const { return page_; }
        int selectedParam() const { return param_; }
        const std::string& selectedEvent() const { return event_; }

        // Dialog state -> controls.
        void refresh() {
            updating_ = true;
            for (const auto& sync : syncs_) {
                sync();
            }
            refreshParams();
            refreshEvents();
            refreshFiles();
            updating_ = false;
            refreshState();
        }

        // The header, version, contents line and footer: what changes with every edit.
        void refreshState() {
            const bool wasUpdating = updating_;
            updating_ = true;
            const jitnodes::AssetDefinition& working = state_.working();
            setLabel("propsTitle", working.name.empty() ? std::string("New asset") : working.name);
            setLabel("propsKind", state_.kind() == AssetPropertiesSession::Kind::asset
                                      ? "asset, version " + std::to_string(working.version)
                                      : std::string("not saved yet"));
            setLabel("versionText", state_.versionText());
            const std::size_t nodes = state_.nodeCount();
            setLabel("contentsInfo", nodes == 0 ? std::string("Empty: no nodes yet. Create it, then add it to a graph and press Edit to build the contents.")
                                                : std::to_string(nodes) + " nodes. Instances cannot be entered.");
            if (auto* note = find<newui::Label>("brushNote")) {
                note->setText(state_.isBrush() ? "referenceWidth and guideLength together make this asset a brush." : std::string());
            }
            const std::string problem = state_.problem();
            setFooter(state_.footerNote());
            for (const char* name : {"propsApply", "propsSave"}) {
                if (auto* b = find<newui::Button>(name)) {
                    b->setEnabled(problem.empty());
                }
            }
            if (auto* save = find<newui::Button>("propsSave")) {
                save->setText(state_.saveLabel());
            }
            if (auto* b = find<newui::Button>("matchCurrent")) {
                b->setEnabled(bool(callbacks_.matchSubnet));
            }
            if (auto* b = find<newui::Button>("editContents")) {
                b->setEnabled(bool(callbacks_.editContents) && state_.kind() == AssetPropertiesSession::Kind::asset);
            }
            updating_ = wasUpdating;
            showEventDetail();
        }

        // The pages are the cards of one CardLayout: the shown one fills the area, the others are hidden.
        void showPage(std::size_t index) {
            using namespace newui;
            page_ = index;
            if (View* pages = root_.findView("propsPages")) {
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
            for (std::size_t i = 0; i < std::size(kPages); ++i) {
                if (auto* tab = find<Button>((std::string("tab") + kPages[i]).c_str())) {
                    tab->setEnabled(i != index);   // the shown page's tab is the pressed-looking one
                }
            }
            root_.updateLayout();
            if (editor_ != nullptr) {
                fitEditor();
            }
        }

        // Selects an event or a parameter, as a click on its row does.
        void selectEvent(const std::string& name) {
            if (name == event_) {
                return;
            }
            event_ = name;
            syncEventSelection();
            updating_ = true;
            if (editor_ != nullptr) {
                editor_->setText(state_.script(event_));
                editor_->checkSyntax();
            }
            updating_ = false;
            refreshEvents();
            showEventDetail();
        }

        void selectParam(int index) {
            param_ = index;
            refresh();
        }

        // Types into the script editor as the user does (one history step).
        void typeScript(const std::string& source) {
            if (editor_ != nullptr) {
                editor_->setText(source);
                state_.setScript(event_, source);
                editor_->checkSyntax();
                refresh();
            }
        }

        // The problem line under the script editor, and the whole footer, for tests.
        std::string editorProblemText() const { return editor_ == nullptr ? std::string() : editor_->problemText(); }
        std::string footerText() const {
            const auto* label = find<newui::Label>("propsFooterNote");
            return label == nullptr ? std::string() : label->text();
        }
        std::size_t eventRowCount() const { return std::size(kEvents); }
        std::string eventWhen() const {
            const auto* label = find<newui::Label>("eventWhen");
            return label == nullptr ? std::string() : label->text();
        }
        bool eventHasScriptMark(const std::string& name) const {
            if (eventList_ == nullptr || eventList_->model() == nullptr) {
                return false;
            }
            for (std::size_t row = 0; row < rowEvents_.size(); ++row) {
                if (rowEvents_[row] >= 0 && name == kEvents[rowEvents_[row]].name) {
                    return std::any_cast<std::string>(eventList_->model()->valueAt(row)).rfind("* ", 0) == 0;
                }
            }
            return false;
        }

        void save() {
            std::string path = state_.working().source;
            if (path.empty()) {
                if (!callbacks_.chooseSavePath) {
                    setFooter("Choose a file to save to");
                    return;
                }
                path = callbacks_.chooseSavePath(state_.suggestedFileName());
                if (path.empty()) {
                    return;
                }
            }
            const std::string error = state_.save(path);
            if (!error.empty()) {
                setFooter(error);
                return;
            }
            if (callbacks_.changed) {
                callbacks_.changed();
            }
            closeWith_(newui::DialogResult::Ok);
        }

    private:
        static std::string number(float value) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", double(value));
            return buffer;
        }

        static bool optionalNumber(const std::string& text, std::optional<float>& out) {
            if (text.empty()) {
                out.reset();
                return true;
            }
            char* end = nullptr;
            const float value = std::strtof(text.c_str(), &end);
            if (end == text.c_str() || *end != '\0') {
                return false;
            }
            out = value;
            return true;
        }

        static void layoutTree(newui::View& view) {
            view.updateLayout();
            for (newui::SubView* child : view.childViews()) {
                layoutTree(*child);
            }
        }

        template <typename T>
        T* find(const char* name) const {
            return dynamic_cast<T*>(root_.findView(name));
        }

        newui::SubView* listOf(const char* name) const { return find<newui::SubView>(name); }

        void setLabel(const char* name, const std::string& text) {
            if (auto* label = find<newui::Label>(name)) {
                if (label->text() != text) {
                    label->setText(text);
                }
            }
        }

        void setFooter(const std::string& text) { setLabel("propsFooterNote", text); }

        void button(const char* name, std::function<void()> action) {
            if (auto* b = find<newui::Button>(name)) {
                b->onClick += [action](newui::Control&) {
                    action();
                    return newui::SyncReturn::Handled;
                };
            }
        }

        // A text field of the working definition: written as it is typed, shown again by refresh().
        void text(const char* name, std::function<std::string()> get, std::function<void(const std::string&)> set) {
            auto* field = find<newui::TextField>(name);
            if (field == nullptr) {
                return;
            }
            field->model().onChanged.add([this, field, set](newui::Model&) {
                if (!updating_) {
                    set(narrow(field->text()));
                    refreshState();
                }
                return newui::SyncReturn::Ignored;
            });
            syncs_.push_back([field, get] {
                if (narrow(field->text()) != get()) {
                    field->setText(widen(get()));
                }
            });
        }

        // A field of the selected parameter. `set` returns false for text that is not valid (the field is shown again from the parameter).
        void paramText(const char* name, bool onCommit, std::function<bool(const std::string&, jitnodes::AssetParam&)> set,
                       std::function<std::string(const jitnodes::AssetParam&)> get) {
            using namespace newui;
            auto* field = find<TextField>(name);
            if (field == nullptr) {
                return;
            }
            auto apply = [this, field, set]() {
                if (updating_ || param_ < 0 || std::size_t(param_) >= state_.paramCount()) {
                    return;
                }
                jitnodes::AssetParam changed = state_.param(std::size_t(param_));
                if (!set(narrow(field->text()), changed)) {
                    return;
                }
                const std::string error = state_.setParam(std::size_t(param_), changed);
                if (!error.empty()) {
                    refresh();
                    setFooter(error);
                    return;
                }
                updateParamRows();
                refreshState();
            };
            if (onCommit) {
                field->onReturnPressed += [apply](TextField&) {
                    apply();
                    return SyncReturn::Handled;
                };
                field->onLostFocus += [apply](View&) {
                    apply();
                    return SyncReturn::Handled;
                };
            } else {
                field->model().onChanged.add([apply](Model&) {
                    apply();
                    return SyncReturn::Ignored;
                });
            }
            syncs_.push_back([this, field, get] {
                const bool has = param_ >= 0 && std::size_t(param_) < state_.paramCount();
                field->setEnabled(has);
                const std::string shown = has ? get(state_.param(std::size_t(param_))) : std::string();
                if (narrow(field->text()) != shown) {
                    field->setText(widen(shown));
                }
            });
        }

        void refreshParams() {
            updateParamRows();
            for (const auto& sync : syncs_) {
                sync();
            }
        }

        // The rows of the parameter list: one per parameter, the selected one highlighted.
        void updateParamRows() {
            using namespace newui;
            if (param_ >= int(state_.paramCount())) {
                param_ = int(state_.paramCount()) - 1;
            }
            if (param_ < 0 && state_.paramCount() > 0) {
                param_ = 0;
            }
            if (paramList_ == nullptr) {
                return;
            }
            std::vector<std::string> rows;
            for (std::size_t i = 0; i < state_.paramCount(); ++i) {
                const jitnodes::AssetParam& p = state_.param(i);
                rows.push_back(p.name + "    " + number(p.defaultValue));
            }
            setListRows(*paramList_, rows, param_ < 0 ? std::optional<std::size_t>() : std::optional<std::size_t>(std::size_t(param_)));
        }

        // Fills a ListView's rows. The same count keeps the model and only rewrites the text, so a list being clicked is not rebuilt under the mouse.
        void setListRows(newui::ListView& list, const std::vector<std::string>& rows, std::optional<std::size_t> selected) {
            using namespace newui;
            syncingLists_ = true;
            if (list.model() == nullptr || list.model()->size() != rows.size()) {
                auto items = std::make_unique<StringListModel>();
                for (const std::string& row : rows) {
                    items->addItem(row);
                }
                list.setModel(std::move(items));
            } else {
                for (std::size_t i = 0; i < rows.size(); ++i) {
                    list.model()->setValueAt(i, rows[i]);
                }
            }
            list.setSelectedIndex(selected);
            syncingLists_ = false;
        }

        void buildParamList() {
            using namespace newui;
            paramList_ = find<ListView>("paramList");
            if (paramList_ == nullptr) {
                return;
            }
            paramList_->setRowHeight(26.0f);
            paramList_->onSelectionChanged += [this](ListView& changed) {
                if (syncingLists_) {
                    return SyncReturn(SyncReturn::Handled);
                }
                const std::optional<std::size_t> index = changed.selectedIndex();
                if (index.has_value() && *index < state_.paramCount()) {
                    selectParam(int(*index));
                } else {
                    refreshParams();   // Ctrl+click can empty the selection; a parameter stays chosen
                }
                return SyncReturn(SyncReturn::Handled);
            };
        }

        void buildFilesList() {
            using namespace newui;
            filesList_ = find<ListView>("filesList");
            if (filesList_ == nullptr) {
                return;
            }
            filesList_->setRowHeight(26.0f);
            filesList_->onSelectionChanged += [this](ListView& changed) {
                if (syncingLists_) {
                    return SyncReturn(SyncReturn::Handled);
                }
                const std::optional<std::size_t> index = changed.selectedIndex();
                file_ = index.has_value() && *index < fileNames_.size() ? fileNames_[*index] : std::string();
                return SyncReturn(SyncReturn::Handled);
            };
        }

        void buildEventRows() {
            using namespace newui;
            eventList_ = find<ListView>("eventList");
            if (eventList_ == nullptr) {
                return;
            }
            // A row per event with a heading row before each group; rowEvents_ maps a row to its kEvents index (-1 for a heading).
            std::vector<bool> headings;
            auto items = std::make_unique<StringListModel>();
            std::string group;
            for (std::size_t i = 0; i < std::size(kEvents); ++i) {
                if (group != kEvents[i].group) {
                    group = kEvents[i].group;
                    items->addItem(group);
                    headings.push_back(true);
                    rowEvents_.push_back(-1);
                }
                items->addItem(kEvents[i].name);
                headings.push_back(false);
                rowEvents_.push_back(int(i));
            }
            eventController_ = std::make_shared<GroupedListController>();
            eventController_->setHeadings(std::move(headings));
            eventList_->setController(eventController_);
            eventList_->setModel(std::move(items));
            eventList_->setRowHeight(24.0f);
            eventList_->onSelectionChanged += [this](ListView& changed) {
                if (syncingEventList_) {
                    return SyncReturn(SyncReturn::Handled);
                }
                const std::optional<std::size_t> index = changed.selectedIndex();
                if (index.has_value() && *index < rowEvents_.size()) {
                    std::size_t row = *index;
                    if (rowEvents_[row] < 0) {
                        row = eventController_->nextSelectable(row, selectedEventRow());   // a heading cannot be chosen
                    }
                    if (rowEvents_[row] >= 0) {
                        selectEvent(kEvents[rowEvents_[row]].name);
                    }
                }
                syncEventSelection();   // also puts the selection back when Ctrl+click emptied it
                return SyncReturn(SyncReturn::Handled);
            };
            event_ = kEvents[0].name;
            syncEventSelection();
        }

        std::size_t selectedEventRow() const {
            for (std::size_t row = 0; row < rowEvents_.size(); ++row) {
                if (rowEvents_[row] >= 0 && event_ == kEvents[rowEvents_[row]].name) {
                    return row;
                }
            }
            return 0;
        }

        void syncEventSelection() {
            if (eventList_ == nullptr) {
                return;
            }
            syncingEventList_ = true;
            eventList_->setSelectedIndex(selectedEventRow());
            syncingEventList_ = false;
        }

        void refreshEvents() {
            using namespace newui;
            if (eventList_ != nullptr && eventList_->model() != nullptr) {
                for (std::size_t row = 0; row < rowEvents_.size(); ++row) {
                    if (rowEvents_[row] >= 0) {
                        const char* name = kEvents[rowEvents_[row]].name;
                        eventList_->model()->setValueAt(row, std::string(state_.hasScript(name) ? "* " : "  ") + name);
                    }
                }
            }
            if (editor_ != nullptr && !updating_) {
                editor_->checkSyntax();
            }
        }

        void showEventDetail() {
            for (const EventInfo& info : kEvents) {
                if (event_ == info.name) {
                    setLabel("eventTitle", info.name);
                    setLabel("eventWhen", info.when);
                    setLabel("eventArgs", info.args);
                }
            }
            setLabel("pythonProblem", state_.scriptProblem(event_));
        }

        void refreshFiles() {
            using namespace newui;
            if (filesList_ == nullptr) {
                return;
            }
            fileNames_.clear();
            std::vector<std::string> rows;
            std::optional<std::size_t> selected;
            for (const auto& [name, text] : state_.working().files) {
                if (name == file_) {
                    selected = fileNames_.size();
                }
                fileNames_.push_back(name);
                rows.push_back(name + "    " + std::to_string(text.size()) + " bytes");
            }
            if (!selected.has_value()) {
                file_.clear();
            }
            setListRows(*filesList_, rows, selected);
        }

        // The script editor sits inside the mockup's placeholder and follows its size.
        void wirePython() {
            using namespace newui;
            SubView* placeholder = listOf("pythonEditor");
            if (placeholder == nullptr) {
                return;
            }
            // The "runs when" text starts at the top of its label, and the editor names the problem under itself, so the label that repeats it is hidden.
            if (auto* when = find<Label>("eventWhen")) {
                when->setVerticalTextAlignment(VerticalTextAlignment::Top);
            }
            if (auto* problem = find<Label>("pythonProblem")) {
                problem->setVisible(false);
            }
            editor_ = new CodeEditorView(PythonLanguage::instance(), "assetScript");
            editor_->setVisible(true);
            placeholder->setLayout(nullptr);
            placeholder->addChild(editor_);
            placeholder->onSizeChanged += [this](View&, const Size&) {
                fitEditor();
                return SyncReturn::Ignored;
            };
            fitEditor();
            editor_->onCheckSyntax = [this](const std::string&) -> std::optional<SyntaxProblem> {
                for (const ScriptIssue& issue : state_.scriptProblems()) {
                    if (issue.script == event_) {
                        SyntaxProblem problem;
                        problem.line = issue.line;
                        problem.column = issue.column;
                        problem.endLine = issue.line;
                        problem.endColumn = issue.column;
                        problem.message = issue.message;
                        return problem;
                    }
                }
                return std::nullopt;
            };
            editor_->onHistoryChanged = [this]() {
                if (!updating_ && editor_ != nullptr) {
                    state_.setScript(event_, editor_->text());
                    refreshEvents();
                    refreshState();
                }
            };
        }

        void fitEditor() {
            if (editor_ == nullptr || editor_->parent() == nullptr) {
                return;
            }
            const newui::Rect own = editor_->parent()->bounds();
            editor_->setBounds(newui::Rect(0.0f, 0.0f, own.width(), own.height()));
            editor_->layoutParts();
        }

        newui::RootView& root_;
        AssetPropertiesSession& state_;
        Callbacks callbacks_;
        std::function<void(newui::DialogResult)> closeWith_;
        std::vector<std::function<void()>> syncs_;
        newui::ListView* paramList_ = nullptr;
        newui::ListView* filesList_ = nullptr;
        std::vector<std::string> fileNames_;   // list row -> file name
        bool syncingLists_ = false;
        newui::ListView* eventList_ = nullptr;
        std::shared_ptr<newui::GroupedListController> eventController_;
        std::vector<int> rowEvents_;
        bool syncingEventList_ = false;
        CodeEditorView* editor_ = nullptr;
        std::size_t page_ = 0;
        int param_ = -1;
        std::string event_;
        std::string file_;
        bool updating_ = false;
    };

}
