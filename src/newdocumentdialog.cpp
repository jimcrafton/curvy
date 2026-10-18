#include "curvy/newdocumentdialog.h"

#include "curvy/pagepreview.h"

#include <newui/bundle.h>
#include <newui/controls.h>
#include <newui/dialogs.h>
#include <newui/models.h>
#include <newui/segmentedcontrol.h>

#include <cstdio>
#include <cstdlib>
#include <optional>

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

        bool parseNumber(const std::wstring& text, double& out) {
            std::string s = narrow(text);
            char* end = nullptr;
            double value = std::strtod(s.c_str(), &end);
            if (end == s.c_str()) {
                return false;
            }
            out = value;
            return true;
        }

        const double kPpiPresets[] = {72.0, 96.0, 150.0, 300.0, 600.0};

        // One run of the dialog: finds the controls once, then keeps them and the model in step.
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
                    field->onReturnPressed += [this](TextField&) { commitFields(); return SyncReturn::Handled; };
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

            // For NewDocumentDialog::check().
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

    namespace {
        bool showDialog(Frame& owner, NewDocumentModel& model, bool setup) {
            Dialog dialog;
            dialog.setName("newdocument");
            if (!Bundle::instance().loadDialog(dialog)) {
                return false;
            }
            NewDocumentModel working = model;
            Session session(dialog.rootView(), working);
            session.wire([&dialog](DialogResult result) { dialog.close(result); });
            session.start();
            if (setup) {
                dialog.setTitle("Document Setup");
                if (auto* ok = dynamic_cast<Button*>(dialog.rootView().findView("createButton"))) {
                    ok->setText("OK");
                }
                for (const char* name : {"categoryControl", "presetList", "newDocumentName"}) {
                    if (auto* control = dynamic_cast<Control*>(dialog.rootView().findView(name))) {
                        control->setEnabled(false);
                    }
                }
            }
            if (dialog.showModal(&owner) != DialogResult::Ok) {
                return false;
            }
            model = working;
            return true;
        }
    }

    bool NewDocumentDialog::runSetup(Frame& owner, NewDocumentModel& model) {
        return showDialog(owner, model, true);
    }

    bool NewDocumentDialog::run(Frame& owner, NewDocumentModel& model) {
        return showDialog(owner, model, false);
    }



}

namespace curvy {

    int NewDocumentDialog::check() {
        using namespace newui;
        int failures = 0;
        auto expect = [&failures](bool ok, const char* what) {
            std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
            failures += ok ? 0 : 1;
        };

        Dialog dialog;
        dialog.setName("newdocument");
        expect(Bundle::instance().loadDialog(dialog), "newdocument.newui loads into a Dialog");
        expect(dialog.position() == DialogPosition::CenterOnRootView, "dialog position read from the file");
        NewDocumentModel model;
        Session session(dialog.rootView(), model);
        session.wire([](DialogResult) {});
        session.start();
        dialog.rootView().updateLayout();

        auto find = [&session](const char* name) { return session.root().findView(name); };
        auto* category = dynamic_cast<SegmentedControl*>(find("categoryControl"));
        auto* list = dynamic_cast<ListView*>(find("presetList"));
        auto* widthField = dynamic_cast<TextField*>(find("widthField"));
        auto* heightField = dynamic_cast<TextField*>(find("heightField"));
        auto* unitCombo = dynamic_cast<DropDownList*>(find("unitCombo"));
        auto* mode = dynamic_cast<SegmentedControl*>(find("colorModeControl"));
        auto* landscape = dynamic_cast<ToolbarButton*>(find("landscapeButton"));
        auto* portrait = dynamic_cast<ToolbarButton*>(find("portraitButton"));
        auto* link = dynamic_cast<ToolbarButton*>(find("linkProportionsButton"));
        auto* ppiCombo = dynamic_cast<DropDownList*>(find("ppiPresetCombo"));
        auto* summary = dynamic_cast<Label*>(find("summaryLabel"));
        auto* preview = dynamic_cast<PagePreview*>(find("pagePreview"));
        expect(category && list && widthField && heightField && unitCombo && mode && landscape && portrait && link && ppiCombo && summary && preview,
            "all dialog controls found");
        if (!(category && list && widthField && heightField && unitCombo && mode && landscape && portrait && link && ppiCombo && summary && preview)) {
            return failures;
        }
        expect(category->segments().size() == 4, "category control has four segments");
        expect(list->model() != nullptr && list->model()->size() == 10, "Print list has ten presets");
        expect(narrow(widthField->text()) == "8.5" && narrow(heightField->text()) == "11", "opens on Letter, 8.5 x 11");
        expect(mode->selectedIndex() == 1, "Letter is a CMYK preset");
        expect(portrait->isChecked() && !landscape->isChecked(), "portrait is checked for Letter");

        category->setSelectedIndex(1);  // Web
        expect(list->model()->size() == 6, "Web list has six presets");
        expect(narrow(widthField->text()) == "1920" && narrow(heightField->text()) == "1080", "choosing Web loads Full HD");
        expect(mode->selectedIndex() == 0, "Full HD is an RGB preset");
        expect(landscape->isChecked() && !portrait->isChecked(), "Full HD is landscape");
        expect(preview->pageWidth() == 1920.0f && preview->widthLabel() == "1920 px", "preview follows the preset");

        list->setSelectedIndex(std::size_t(5));  // Banner 728 x 90
        expect(narrow(widthField->text()) == "728" && narrow(heightField->text()) == "90", "picking a list row loads it");

        unitCombo->setSelectedIndex(std::size_t(4));  // inches
        expect(narrow(widthField->text()) == "10.11", "px -> in at 72 ppi keeps the physical size (728 px = 10.11 in)");
        expect(summary->text().find("728 x 90 pt") != std::string::npos, "summary reports the size in points");

        portrait->setChecked(true);
        expect(narrow(widthField->text()) == "1.25" && narrow(heightField->text()) == "10.11", "portrait swaps width and height");
        expect(!landscape->isChecked(), "landscape unchecks");

        link->setChecked(true);
        widthField->setText(L"2.5");
        widthField->onReturnPressed(*widthField);
        expect(narrow(heightField->text()) == "20.22", "linked proportions scale the height (2.5 x 20.22)");
        link->setChecked(false);

        ppiCombo->setSelectedIndex(std::size_t(1));  // 96 ppi
        expect(narrow(dynamic_cast<TextField*>(find("ppiField"))->text()) == "96", "ppi preset fills the ppi field");

        mode->setSelectedIndex(1);
        expect(model.colorMode() == ColorSpace::CMYK, "color mode button changes the model");
        dynamic_cast<DropDownList*>(find("backgroundCombo"))->setSelectedIndex(std::size_t(1));
        expect(model.background() == PageBackground::Transparent, "background combo changes the model");
        return failures;
    }

}
