#include "curvy/brushpanel.h"

#include <newui/models.h>
#include <newui/utils.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace curvy {

    namespace {
        // 12.5 -> "12.5", 24 -> "24" (up to two decimals, trailing zeros dropped).
        std::wstring numberText(double value) {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%.2f", value);
            std::string text = buffer;
            while (!text.empty() && text.back() == '0') { text.pop_back(); }
            if (!text.empty() && text.back() == '.') { text.pop_back(); }
            if (text == "-0" || text.empty()) { text = "0"; }
            return newui::utf8ToWide(text);
        }

        bool parseNumber(const newui::TextField* field, double& value) {
            if (field == nullptr) {
                return false;
            }
            std::string narrow = newui::wideToUtf8(field->text());
            char* end = nullptr;
            double parsed = std::strtod(narrow.c_str(), &end);
            if (end == narrow.c_str() || !std::isfinite(parsed)) {
                return false;
            }
            value = parsed;
            return true;
        }
    }

    void BrushPanel::onButton(const char* name, std::function<void()> action) {
        if (auto* button = dynamic_cast<newui::Control*>(root_.findView(name))) {
            button->onClick += [action](newui::Control&) {
                action();
                return newui::SyncReturn::Handled;
            };
        }
    }

    BrushPanel::BrushPanel(newui::RootView& root, SketchDocument& document) : root_(root), document_(document) {
        combo_ = dynamic_cast<newui::DropDownList*>(root_.findView("brushCombo"));
        name_ = dynamic_cast<newui::TextField*>(root_.findView("brushNameField"));
        angle_ = dynamic_cast<newui::TextField*>(root_.findView("brushAngleField"));
        roundness_ = dynamic_cast<newui::TextField*>(root_.findView("brushRoundnessField"));
        spacing_ = dynamic_cast<newui::TextField*>(root_.findView("brushSpacingField"));
        scatter_ = dynamic_cast<newui::TextField*>(root_.findView("brushScatterField"));
        sizeVariation_ = dynamic_cast<newui::TextField*>(root_.findView("brushSizeVariationField"));
        rotation_ = dynamic_cast<newui::TextField*>(root_.findView("brushRotationField"));
        follow_ = dynamic_cast<newui::ToolbarButton*>(root_.findView("brushFollowButton"));

        if (combo_ != nullptr) {
            combo_->onSelectionChanged += [this](newui::DropDownList&) {
                if (!updating_ && combo_->selectedIndex().has_value() && *combo_->selectedIndex() < document_.brushes().size()) {
                    document_.setActiveBrush(document_.brushes()[*combo_->selectedIndex()]->id);
                }
                return newui::SyncReturn::Handled;
            };
        }
        onButton("newCalligraphicBrushButton", [this]() { document_.makeBrush(BrushKind::Calligraphic, ""); });
        onButton("newArtBrushButton", [this]() { document_.makeBrush(BrushKind::Art, ""); });
        onButton("newScatterBrushButton", [this]() { document_.makeBrush(BrushKind::Scatter, ""); });
        onButton("applyBrushButton", [this]() { document_.applyBrush(document_.activeBrush()); });
        onButton("removeBrushButton", [this]() { document_.applyBrush(0); });
        onButton("presetBrushButton", [this]() { document_.addBrushPresets(); });
        onButton("deleteBrushButton", [this]() { document_.deleteBrush(document_.activeBrush()); });
        for (newui::TextField* field : {name_, angle_, roundness_, spacing_, scatter_, sizeVariation_, rotation_}) {
            if (field != nullptr) {
                field->onReturnPressed += [this](newui::TextField&) {
                    applyFields();
                    return newui::SyncReturn::Handled;
                };
            }
        }
        if (follow_ != nullptr) {
            follow_->onCheckedChanged += [this](newui::ToolbarButton&) {
                if (!updating_) {
                    applyFields();
                }
                return newui::SyncReturn::Handled;
            };
        }
        document_.onChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        document_.onSelectionChanged += [this](SketchDocument&) {
            if (std::shared_ptr<const BrushDef> brush = document_.selectedBrush()) {
                document_.setActiveBrush(brush->id);   // choosing a brushed object shows its brush
            }
            refresh();
            return newui::SyncReturn::Handled;
        };
        refresh();
    }

    void BrushPanel::refresh() {
        updating_ = true;
        const std::size_t id = document_.activeBrush();
        std::shared_ptr<const BrushDef> def = document_.findBrush(id);
        if (combo_ != nullptr) {
            std::vector<std::string> names;
            for (const std::shared_ptr<const BrushDef>& brush : document_.brushes()) {
                names.push_back(brush->name);
            }
            if (names != shown_) {
                shown_ = names;
                auto model = std::make_unique<newui::StringListModel>();
                model->items() = names.empty() ? std::vector<std::string>{"(none)"} : names;
                combo_->setModel(std::move(model));
            }
            std::size_t index = 0;
            for (std::size_t i = 0; i < document_.brushes().size(); ++i) {
                if (document_.brushes()[i]->id == id) {
                    index = i;
                }
            }
            if (combo_->selectedIndex() != std::optional<std::size_t>(index)) {
                combo_->setSelectedIndex(index);
            }
            combo_->setEnabled(!document_.brushes().empty());
        }
        const bool nib = def && def->kind == BrushKind::Calligraphic, scattered = def && def->kind == BrushKind::Scatter;
        auto set = [](newui::TextField* field, bool enabled, double value) {
            if (field != nullptr) {
                field->setEnabled(enabled);
                field->setText(enabled ? numberText(value) : std::wstring());
            }
        };
        if (name_ != nullptr) {
            name_->setText(def ? newui::utf8ToWide(def->name) : std::wstring());
            name_->setEnabled(def != nullptr);
        }
        set(angle_, nib, def ? def->angle : 0.0);
        set(roundness_, nib, def ? def->roundness : 0.0);
        set(spacing_, scattered, def ? def->spacing : 0.0);
        set(scatter_, scattered, def ? def->scatter : 0.0);
        set(sizeVariation_, scattered, def ? def->sizeVariation : 0.0);
        set(rotation_, scattered, def ? def->rotation : 0.0);
        if (follow_ != nullptr) {
            follow_->setEnabled(scattered);
            follow_->setChecked(scattered && def->followPath);
        }
        auto enable = [this](const char* name, bool on) {
            if (auto* control = dynamic_cast<newui::Control*>(root_.findView(name))) {
                control->setEnabled(on);
            }
        };
        const bool selected = !document_.selection().empty();
        enable("newArtBrushButton", selected);
        enable("newScatterBrushButton", selected);
        enable("applyBrushButton", def != nullptr && selected);
        enable("removeBrushButton", document_.selectedBrush() != nullptr);
        enable("deleteBrushButton", def != nullptr && document_.brushUseCount(id) == 0);
        updating_ = false;
    }

    void BrushPanel::applyFields() {
        std::shared_ptr<const BrushDef> def = document_.findBrush(document_.activeBrush());
        if (!updating_ && def) {
            BrushDef edited = *def;
            edited.name = name_ != nullptr ? newui::wideToUtf8(name_->text()) : def->name;
            double value = 0.0;
            if (parseNumber(angle_, value)) { edited.angle = value; }
            if (parseNumber(roundness_, value)) { edited.roundness = value; }
            if (parseNumber(spacing_, value)) { edited.spacing = value; }
            if (parseNumber(scatter_, value)) { edited.scatter = value; }
            if (parseNumber(sizeVariation_, value)) { edited.sizeVariation = value; }
            if (parseNumber(rotation_, value)) { edited.rotation = value; }
            if (follow_ != nullptr && follow_->isEnabled()) { edited.followPath = follow_->isChecked(); }
            edited.setParams(edited.params());   // into range
            document_.changeBrush(def->id, edited);
        }
        refresh();
    }

}
