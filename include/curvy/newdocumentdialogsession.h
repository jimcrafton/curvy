#pragma once

// Private to the New Document dialog (newdocumentdialog.cpp) and its tests: one run of the dialog, which keeps its controls and the
// model in step. It lives in a header so the tests can drive it without showing a window.

#include "curvy/newdocumentmodel.h"
#include "curvy/pagepreview.h"

#include <newui/controls.h>
#include <newui/dialogs.h>
#include <newui/models.h>
#include <newui/rootview.h>
#include <newui/segmentedcontrol.h>

#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace curvy::newdocdialog {

    using namespace newui;

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

    inline bool parseNumber(const std::wstring& text, double& out) {
        std::string s = narrow(text);
        char* end = nullptr;
        double value = std::strtod(s.c_str(), &end);
        if (end == s.c_str()) {
            return false;
        }
        out = value;
        return true;
    }

    inline const double kPpiPresets[] = {72.0, 96.0, 150.0, 300.0, 600.0};

    // Finds the controls once, then keeps them and the model in step.
    // @reflect ignore=true
    class Session {
    public:
        Session(RootView& root, NewDocumentModel& model) : root_(root), model_(model) {
            category_ = find<SegmentedControl>("categoryControl");
            list_ = find<ListView>("presetList");
            preview_ = find<PagePreview>("pagePreview");
            nameField_ = find<TextField>("newDocumentName");
            widthField_ = find<TextField>("widthField");
            heightField_ = find<TextField>("heightField");
            unitCombo_ = find<DropDownList>("unitCombo");
            linkButton_ = find<ToolbarButton>("linkProportionsButton");
            portraitButton_ = find<ToolbarButton>("portraitButton");
            landscapeButton_ = find<ToolbarButton>("landscapeButton");
            modeControl_ = find<SegmentedControl>("colorModeControl");
            ppiField_ = find<TextField>("ppiField");
            ppiCombo_ = find<DropDownList>("ppiPresetCombo");
            backgroundCombo_ = find<DropDownList>("backgroundCombo");
            summary_ = find<Label>("summaryLabel");
        }

        // Hooks every control; `closeWith` ends the dialog.
        void wire(const std::function<void(DialogResult)>& closeWith) {
            if (category_ != nullptr) {
                category_->onSelectionChanged += [this](SegmentedControl& changed) {
                    if (!updating_) {
                        showCategory(NewDocumentModel::categories()[changed.selectedIndex()], true);
                    }
                    return SyncReturn::Handled;
                };
            }
            if (list_ != nullptr) {
                list_->onSelectionChanged += [this](ListView& changed) {
                    std::optional<std::size_t> index = changed.selectedIndex();
                    if (!updating_ && index.has_value() && *index < rows_.size()) {
                        model_.loadPreset(NewDocumentModel::presets()[rows_[*index]]);
                        refresh();
                    }
                    return SyncReturn::Handled;
                };
            }
            auto commitOnEnterOrLeave = [this](TextField* field) {
                if (field == nullptr) {
                    return;
                }
                field->onReturnPressed += [this](TextField&) { commitFields(); return SyncReturn::Ignored; };
                field->onLostFocus += [this](View&) { commitFields(); return SyncReturn::Handled; };
            };
            commitOnEnterOrLeave(nameField_);
            commitOnEnterOrLeave(widthField_);
            commitOnEnterOrLeave(heightField_);
            commitOnEnterOrLeave(ppiField_);

            if (unitCombo_ != nullptr) {
                unitCombo_->onSelectionChanged += [this](DropDownList& changed) {
                    std::optional<std::size_t> index = changed.selectedIndex();
                    if (!updating_ && index.has_value()) {
                        commitFields(false);  // typed-but-uncommitted numbers convert too
                        model_.setUnit(LengthUnit(*index));
                        refresh();
                    }
                    return SyncReturn::Handled;
                };
            }
            if (ppiCombo_ != nullptr) {
                ppiCombo_->onSelectionChanged += [this](DropDownList& changed) {
                    std::optional<std::size_t> index = changed.selectedIndex();
                    if (!updating_ && index.has_value() && *index < 5) {
                        model_.setPpi(kPpiPresets[*index]);
                        refresh();
                    }
                    return SyncReturn::Handled;
                };
            }
            if (backgroundCombo_ != nullptr) {
                backgroundCombo_->onSelectionChanged += [this](DropDownList& changed) {
                    std::optional<std::size_t> index = changed.selectedIndex();
                    if (!updating_ && index.has_value()) {
                        model_.setBackground(PageBackground(*index));
                    }
                    return SyncReturn::Handled;
                };
            }
            if (modeControl_ != nullptr) {
                modeControl_->onSelectionChanged += [this](SegmentedControl& changed) {
                    if (!updating_) {
                        model_.setColorMode(changed.selectedIndex() == 1 ? ColorSpace::CMYK : ColorSpace::RGB);
                        refresh();
                    }
                    return SyncReturn::Handled;
                };
            }
            if (linkButton_ != nullptr) {
                linkButton_->onCheckedChanged += [this](ToolbarButton& changed) {
                    if (!updating_) {
                        commitFields(false);
                        model_.setKeepProportions(changed.isChecked());
                        refresh();
                    }
                    return SyncReturn::Handled;
                };
            }
            // Orientation is a pair of toggles that behave like radio buttons.
            auto wireOrientation = [this](ToolbarButton* button, bool landscape) {
                if (button == nullptr) {
                    return;
                }
                button->onCheckedChanged += [this, landscape](ToolbarButton& changed) {
                    if (!updating_ && changed.isChecked()) {
                        commitFields(false);
                        model_.setLandscape(landscape);
                    }
                    if (!updating_) {
                        refresh();
                    }
                    return SyncReturn::Handled;
                };
            };
            wireOrientation(portraitButton_, false);
            wireOrientation(landscapeButton_, true);

            if (auto* create = find<Button>("createButton")) {
                create->onClick += [this, closeWith](Control&) {
                    commitFields();
                    closeWith(DialogResult::Ok);
                    return SyncReturn::Handled;
                };
            }
            if (auto* cancel = find<Button>("cancelButton")) {
                cancel->onClick += [closeWith](Control&) {
                    closeWith(DialogResult::Cancel);
                    return SyncReturn::Handled;
                };
            }
        }

        RootView& root() { return root_; }

        // Opens on the Print list with the model's own settings (nothing reloaded from a preset).
        void start() {
            showCategory("Print", false);
            refresh();
        }

    private:
        template <typename T>
        T* find(const char* name) const {
            return dynamic_cast<T*>(root_.findView(name));
        }

        // Reads the text boxes into the model (name, width, height, resolution).
        void commitFields(bool refreshAfter = true) {
            if (updating_) {
                return;
            }
            double number = 0.0;
            if (nameField_ != nullptr) {
                std::string name = narrow(nameField_->text());
                if (!name.empty()) {
                    model_.setName(name);
                }
            }
            if (ppiField_ != nullptr && parseNumber(ppiField_->text(), number)) {
                model_.setPpi(number);
            }
            // Whichever of width/height changed drives the other when proportions are linked.
            double w = 0.0, h = 0.0;
            bool haveW = widthField_ != nullptr && parseNumber(widthField_->text(), w);
            bool haveH = heightField_ != nullptr && parseNumber(heightField_->text(), h);
            if (haveW && NewDocumentModel::format(w) != NewDocumentModel::format(model_.width())) {
                model_.setWidth(w);
            } else if (haveH && NewDocumentModel::format(h) != NewDocumentModel::format(model_.height())) {
                model_.setHeight(h);
            }
            if (refreshAfter) {
                refresh();
            }
        }

        // Fills the list with one category. `loadFirst` also takes the first preset's settings.
        void showCategory(const std::string& category, bool loadFirst) {
            bool wasUpdating = updating_;
            updating_ = true;
            rows_ = NewDocumentModel::presetsIn(category);
            if (list_ != nullptr) {
                auto items = std::make_unique<StringListModel>();
                for (std::size_t index : rows_) {
                    items->addItem(NewDocumentModel::rowText(NewDocumentModel::presets()[index]));
                }
                list_->setModel(std::move(items));
                if (!rows_.empty()) {
                    list_->setSelectedIndex(std::size_t(0));
                }
            }
            if (category_ != nullptr) {
                std::vector<std::string> categories = NewDocumentModel::categories();
                for (std::size_t i = 0; i < categories.size(); ++i) {
                    if (categories[i] == category && category_->selectedIndex() != i) {
                        category_->setSelectedIndex(i);
                    }
                }
            }
            updating_ = wasUpdating;
            if (loadFirst && !rows_.empty()) {
                model_.loadPreset(NewDocumentModel::presets()[rows_.front()]);
                refresh();
            }
        }

        // Model -> controls.
        void refresh() {
            if (updating_) {
                return;
            }
            updating_ = true;
            if (nameField_ != nullptr) {
                nameField_->setText(widen(model_.name()));
            }
            if (widthField_ != nullptr) {
                widthField_->setText(widen(NewDocumentModel::format(model_.width())));
            }
            if (heightField_ != nullptr) {
                heightField_->setText(widen(NewDocumentModel::format(model_.height())));
            }
            if (unitCombo_ != nullptr) {
                unitCombo_->setSelectedIndex(std::size_t(model_.unit()));
            }
            if (ppiField_ != nullptr) {
                ppiField_->setText(widen(NewDocumentModel::format(model_.ppi())));
            }
            if (ppiCombo_ != nullptr) {
                std::optional<std::size_t> match;
                for (std::size_t i = 0; i < 5; ++i) {
                    if (kPpiPresets[i] == model_.ppi()) {
                        match = i;
                    }
                }
                ppiCombo_->setSelectedIndex(match);
            }
            if (backgroundCombo_ != nullptr) {
                backgroundCombo_->setSelectedIndex(std::size_t(model_.background()));
            }
            if (modeControl_ != nullptr) {
                modeControl_->setSelectedIndex(model_.colorMode() == ColorSpace::CMYK ? 1 : 0);
            }
            if (linkButton_ != nullptr && linkButton_->isChecked() != model_.keepProportions()) {
                linkButton_->setChecked(model_.keepProportions());
            }
            if (portraitButton_ != nullptr && portraitButton_->isChecked() == model_.isLandscape()) {
                portraitButton_->setChecked(!model_.isLandscape());
            }
            if (landscapeButton_ != nullptr && landscapeButton_->isChecked() != model_.isLandscape()) {
                landscapeButton_->setChecked(model_.isLandscape());
            }
            if (preview_ != nullptr) {
                preview_->setPageWidth(float(model_.width()));
                preview_->setPageHeight(float(model_.height()));
                preview_->setWidthLabel(model_.widthLabel());
                preview_->setHeightLabel(model_.heightLabel());
            }
            if (summary_ != nullptr) {
                summary_->setText(model_.summary());
            }
            updating_ = false;
        }

        RootView& root_;
        NewDocumentModel& model_;
        bool updating_ = false;
        std::vector<std::size_t> rows_;  // list row -> index into NewDocumentModel::presets()

        SegmentedControl* category_ = nullptr;
        ListView* list_ = nullptr;
        PagePreview* preview_ = nullptr;
        TextField* nameField_ = nullptr;
        TextField* widthField_ = nullptr;
        TextField* heightField_ = nullptr;
        DropDownList* unitCombo_ = nullptr;
        ToolbarButton* linkButton_ = nullptr;
        ToolbarButton* portraitButton_ = nullptr;
        ToolbarButton* landscapeButton_ = nullptr;
        SegmentedControl* modeControl_ = nullptr;
        TextField* ppiField_ = nullptr;
        DropDownList* ppiCombo_ = nullptr;
        DropDownList* backgroundCombo_ = nullptr;
        Label* summary_ = nullptr;
    };

}
