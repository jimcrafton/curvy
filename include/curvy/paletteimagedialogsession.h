#pragma once

// Private to the "New palette from image" dialog (paletteimagedialog.cpp) and its tests: one run of the dialog, which keeps its controls and the
// model in step. It lives in a header so the tests can drive it without showing a window.

#include "curvy/paletteimagemodel.h"
#include "curvy/theme.h"

#include <blend2d/blend2d.h>

#include <newui/controls.h>
#include <newui/dialogs.h>
#include <newui/models.h>
#include <newui/rootview.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace curvy::paletteimagedialog {

    using namespace newui;

    inline std::string narrow(const std::wstring& text) {
        return std::string(text.begin(), text.end());
    }

    inline std::wstring widen(const std::string& text) {
        return std::wstring(text.begin(), text.end());
    }

    // The picture the colours were taken from, scaled to fit and centred.
    // @reflect ignore=true
    class ImagePreview : public SubView {
    public:
        bool setImage(const std::string& path) {
            BLImage loaded;
            if (loaded.read_from_file(path.c_str()) != BL_SUCCESS) {
                return false;
            }
            image_ = loaded;
            redraw();
            return true;
        }

        bool hasImage() const { return image_.width() > 0; }

        void paint(BLContext& ctx) override {
            if (!hasImage()) {
                return;
            }
            const Rect client = getClientBounds();
            const double scale = (std::min)(double(client.width()) / double(image_.width()), double(client.height()) / double(image_.height()));
            const double width = double(image_.width()) * scale;
            const double height = double(image_.height()) * scale;
            const double x = double(client.left()) + (double(client.width()) - width) * 0.5;
            const double y = double(client.top()) + (double(client.height()) - height) * 0.5;
            ctx.blit_image(BLRect(x, y, width, height), image_);
        }

    private:
        BLImage image_;
    };

    // @reflect ignore=true
    class Session {
    public:
        static constexpr std::size_t kMaxChips = PaletteImageModel::kMaxCount;

        // `choose` asks the user for another image and returns its path, or nothing.
        Session(RootView& root, PaletteImageModel& model, std::function<std::string()> choose) : root_(root), model_(model), choose_(std::move(choose)) {
            picture_ = find<SubView>("picture");
            chooseButton_ = find<Button>("chooseImage");
            imageName_ = find<Label>("imageName");
            count_ = find<Slider>("count");
            countText_ = find<Label>("countText");
            order_ = find<DropDownList>("order");
            merge_ = find<Toggle>("merge");
            skip_ = find<Toggle>("skip");
            found_ = find<SubView>("found");
            foundText_ = find<Label>("foundText");
            nameField_ = find<TextField>("newName");
            ok_ = find<Button>("ok");
            cancel_ = find<Button>("cancel");
        }

        // Hooks every control; `closeWith` ends the dialog.
        void wire(const std::function<void(DialogResult)>& closeWith) {
            if (count_ != nullptr) {
                count_->setRange(float(PaletteImageModel::kMinCount), float(PaletteImageModel::kMaxCount));
                count_->setInteger(true);
                count_->onValueChanged += [this](Slider& changed) {
                    if (!updating_) {
                        model_.setCount(int(changed.value()));
                        refresh();
                    }
                    return SyncReturn::Handled;
                };
            }
            if (order_ != nullptr) {
                auto items = std::make_unique<StringListModel>();
                for (const char* item : {"Most used", "Hue", "Lightness"}) {
                    items->addItem(item);
                }
                order_->setModel(std::move(items));
                order_->onSelectionChanged += [this](DropDownList& changed) {
                    const std::optional<std::size_t> index = changed.selectedIndex();
                    if (!updating_ && index.has_value()) {
                        model_.setOrder(*index == 1 ? PaletteExtractOptions::Order::Hue
                                        : *index == 2 ? PaletteExtractOptions::Order::Lightness
                                                      : PaletteExtractOptions::Order::MostUsed);
                        refresh();
                    }
                    return SyncReturn::Handled;
                };
            }
            if (merge_ != nullptr) {
                merge_->onCheckedChanged += [this](Toggle& changed) {
                    if (!updating_) {
                        model_.setMergeNearDuplicates(changed.isChecked());
                        refresh();
                    }
                    return SyncReturn::Handled;
                };
            }
            if (skip_ != nullptr) {
                skip_->onCheckedChanged += [this](Toggle& changed) {
                    if (!updating_) {
                        model_.setIgnoreNearWhiteAndBlack(changed.isChecked());
                        refresh();
                    }
                    return SyncReturn::Handled;
                };
            }
            if (nameField_ != nullptr) {
                nameField_->onReturnPressed += [this](TextField&) {
                    commitName();
                    return SyncReturn::Ignored;
                };
                nameField_->onLostFocus += [this](View&) {
                    commitName();
                    return SyncReturn::Handled;
                };
            }
            if (chooseButton_ != nullptr) {
                chooseButton_->onClick += [this](Control&) {
                    chooseAnother();
                    return SyncReturn::Handled;
                };
            }
            if (ok_ != nullptr) {
                ok_->onClick += [this, closeWith](Control&) {
                    commitName();
                    if (model_.canCreate()) {
                        closeWith(DialogResult::Ok);
                    }
                    return SyncReturn::Handled;
                };
            }
            if (cancel_ != nullptr) {
                cancel_->onClick += [closeWith](Control&) {
                    closeWith(DialogResult::Cancel);
                    return SyncReturn::Handled;
                };
            }
        }

        // Builds what is made in code (the chips and the picture), then shows the model.
        void start() {
            if (found_ != nullptr) {
                found_->setLayout(nullptr);   // the chips are placed by hand: wrapping rows are not something the flex layout does
                for (SubView* sample : std::vector<SubView*>(found_->childViews())) {   // the mockup's sample colours
                    found_->removeChild(sample);
                    delete sample;
                }
                for (std::size_t i = 0; i < kMaxChips; ++i) {
                    auto* chip = new SubView();
                    chip->style().setBorderWidth(1.0f);
                    chip->style().setBorderFill(Color(0.0f, 0.0f, 0.0f, 1.0f));
                    chip->setVisible(false);
                    found_->addChild(chip);
                    chips_.push_back(chip);
                }
            }
            if (picture_ != nullptr) {
                picture_->setLayout(nullptr);
                for (SubView* child : std::vector<SubView*>(picture_->childViews())) {
                    picture_->removeChild(child);
                    delete child;
                }
                preview_ = new ImagePreview();
                preview_->setVisible(true);   // a view made in code starts hidden
                picture_->addChild(preview_);
                const Rect box = picture_->bounds();
                preview_->setBounds(Rect(0.0f, 0.0f, box.width(), box.height()));
            }
            if (!model_.path().empty() && preview_ != nullptr) {
                preview_->setImage(model_.path());
            }
            syncName();
            refresh();
        }

        // The state of the controls, for tests and for the owner.
        std::size_t chipCount() const {
            return std::size_t(std::count_if(chips_.begin(), chips_.end(), [](const SubView* chip) { return chip->isVisible(); }));
        }
        Color chipColor(std::size_t index) const {
            return index < chips_.size() ? chips_[index]->style().backgroundFill().color() : Color();
        }
        bool pictureShown() const { return preview_ != nullptr && preview_->hasImage(); }
        std::string foundText() const { return foundText_ != nullptr ? foundText_->text() : std::string(); }
        std::string imageNameText() const { return imageName_ != nullptr ? imageName_->text() : std::string(); }
        std::string countText() const { return countText_ != nullptr ? countText_->text() : std::string(); }
        bool createEnabled() const { return ok_ != nullptr && ok_->isEnabled(); }
        // Picks another image as the Choose image button does.
        void chooseAnother() {
            const std::string path = choose_ ? choose_() : std::string();
            if (path.empty()) {
                return;
            }
            if (model_.load(path)) {
                if (preview_ != nullptr) {
                    preview_->setImage(path);
                }
                syncName();
            }
            refresh();
        }

    private:
        template <typename T>
        T* find(const char* name) const {
            return dynamic_cast<T*>(root_.findView(name));
        }

        void commitName() {
            if (!updating_ && nameField_ != nullptr) {
                model_.setName(narrow(nameField_->text()));
                refresh();
            }
        }

        void syncName() {
            if (nameField_ != nullptr) {
                updating_ = true;
                nameField_->setText(widen(model_.name()));
                updating_ = false;
            }
        }

        void refresh() {
            updating_ = true;
            if (count_ != nullptr) {
                count_->setValue(float(model_.count()));
            }
            if (countText_ != nullptr) {
                countText_->setText(std::to_string(model_.count()));
            }
            if (order_ != nullptr) {
                const auto order = model_.order();
                order_->setSelectedIndex(order == PaletteExtractOptions::Order::Hue ? 1 : order == PaletteExtractOptions::Order::Lightness ? 2 : 0);
            }
            if (merge_ != nullptr) {
                merge_->setChecked(model_.mergeNearDuplicates());
            }
            if (skip_ != nullptr) {
                skip_->setChecked(model_.ignoreNearWhiteAndBlack());
            }
            if (imageName_ != nullptr) {
                imageName_->setText(model_.path().empty() ? "No image chosen" : model_.fileName());
            }
            layoutChips();
            if (foundText_ != nullptr) {
                const std::size_t count = model_.found().size();
                foundText_->setText(!model_.error().empty() ? model_.error() : count == 0 ? "Choose an image" : std::to_string(count) + (count == 1 ? " colour found" : " colours found"));
            }
            if (ok_ != nullptr) {
                ok_->setEnabled(model_.canCreate());
            }
            updating_ = false;
        }

        void layoutChips() {
            if (found_ == nullptr) {
                return;
            }
            constexpr float kChip = 20.0f;
            constexpr float kGap = 3.0f;
            const float width = (std::max)(found_->bounds().width(), kChip);
            const std::size_t columns = (std::max<std::size_t>)(1, std::size_t((width + kGap) / (kChip + kGap)));
            const std::vector<Swatch>& colors = model_.found();
            for (std::size_t i = 0; i < chips_.size(); ++i) {
                SubView* chip = chips_[i];
                const bool used = i < colors.size();
                chip->setVisible(used);
                if (used) {
                    const Swatch& swatch = colors[i];
                    const auto byte = [](float v) { return std::uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
                    chip->style().setBackgroundColor(BLRgba32(byte(swatch.r), byte(swatch.g), byte(swatch.b), 255u));
                    chip->setBounds(Rect(float(i % columns) * (kChip + kGap), float(i / columns) * (kChip + kGap), kChip, kChip));
                    chip->style().markDirty();
                }
            }
        }

        RootView& root_;
        PaletteImageModel& model_;
        std::function<std::string()> choose_;
        SubView* picture_ = nullptr;
        ImagePreview* preview_ = nullptr;
        Button* chooseButton_ = nullptr;
        Label* imageName_ = nullptr;
        Slider* count_ = nullptr;
        Label* countText_ = nullptr;
        DropDownList* order_ = nullptr;
        Toggle* merge_ = nullptr;
        Toggle* skip_ = nullptr;
        SubView* found_ = nullptr;
        Label* foundText_ = nullptr;
        TextField* nameField_ = nullptr;
        Button* ok_ = nullptr;
        Button* cancel_ = nullptr;
        std::vector<SubView*> chips_;
        bool updating_ = false;
    };

}
