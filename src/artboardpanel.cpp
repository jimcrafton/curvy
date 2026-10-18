#include "curvy/artboardpanel.h"

#include <newui/models.h>
#include <newui/utils.h>

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

        bool parseNumber(const std::wstring& text, double& value) {
            std::string narrow = newui::wideToUtf8(text);
            char* end = nullptr;
            double parsed = std::strtod(narrow.c_str(), &end);
            if (end == narrow.c_str() || !std::isfinite(parsed)) {
                return false;
            }
            value = parsed;
            return true;
        }
    }

    ArtboardPanel::ArtboardPanel(newui::RootView& root, SketchDocument& document) : root_(root), document_(document) {
        combo_ = dynamic_cast<newui::DropDownList*>(root_.findView("artboardCombo"));
        name_ = dynamic_cast<newui::TextField*>(root_.findView("artboardNameField"));
        x_ = dynamic_cast<newui::TextField*>(root_.findView("artboardXField"));
        y_ = dynamic_cast<newui::TextField*>(root_.findView("artboardYField"));
        width_ = dynamic_cast<newui::TextField*>(root_.findView("artboardWidthField"));
        height_ = dynamic_cast<newui::TextField*>(root_.findView("artboardHeightField"));
        add_ = dynamic_cast<newui::ToolbarButton*>(root_.findView("addArtboardButton"));
        delete_ = dynamic_cast<newui::ToolbarButton*>(root_.findView("deleteArtboardButton"));

        if (combo_ != nullptr) {
            combo_->onSelectionChanged += [this](newui::DropDownList&) {
                if (!updating_ && combo_->selectedIndex().has_value()) {
                    document_.setActiveArtboard(*combo_->selectedIndex());
                }
                return newui::SyncReturn::Handled;
            };
        }
        if (add_ != nullptr) {
            add_->onClick += [this](newui::Control&) {
                document_.addArtboardBesideLast();
                return newui::SyncReturn::Handled;
            };
        }
        if (delete_ != nullptr) {
            delete_->onClick += [this](newui::Control&) {
                document_.deleteArtboard(document_.activeArtboard());
                return newui::SyncReturn::Handled;
            };
        }
        auto bind = [this](newui::TextField* field, double Artboard::*member) {
            if (field != nullptr) {
                field->onReturnPressed += [this, field, member](newui::TextField&) {
                    applyField(field, member);
                    return newui::SyncReturn::Handled;
                };
            }
        };
        bind(x_, &Artboard::x);
        bind(y_, &Artboard::y);
        bind(width_, &Artboard::width);
        bind(height_, &Artboard::height);
        if (name_ != nullptr) {
            name_->onReturnPressed += [this](newui::TextField&) {
                applyName();
                return newui::SyncReturn::Handled;
            };
        }
        document_.onChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        refresh();
    }

    void ArtboardPanel::refresh() {
        updating_ = true;
        const Artboard& board = document_.activeArtboardRect();
        if (combo_ != nullptr) {
            std::vector<std::string> names;
            for (const Artboard& each : document_.artboards()) {
                names.push_back(each.name);
            }
            if (names != names_) {
                names_ = names;
                auto model = std::make_unique<newui::StringListModel>();
                model->items() = names_;
                combo_->setModel(std::move(model));
            }
            std::optional<std::size_t> index = document_.activeArtboard();
            if (combo_->selectedIndex() != index) {
                combo_->setSelectedIndex(*index);
            }
        }
        if (name_ != nullptr) { name_->setText(newui::utf8ToWide(board.name)); }
        if (x_ != nullptr) { x_->setText(numberText(board.x)); }
        if (y_ != nullptr) { y_->setText(numberText(board.y)); }
        if (width_ != nullptr) { width_->setText(numberText(board.width)); }
        if (height_ != nullptr) { height_->setText(numberText(board.height)); }
        if (delete_ != nullptr) { delete_->setEnabled(document_.artboards().size() > 1); }
        updating_ = false;
    }

    void ArtboardPanel::applyField(newui::TextField* field, double Artboard::*member) {
        double value;
        if (!updating_ && field != nullptr && parseNumber(field->text(), value)) {
            Artboard board = document_.activeArtboardRect();
            board.*member = value;
            document_.setArtboard(document_.activeArtboard(), board);
        }
        refresh();
    }

    void ArtboardPanel::applyName() {
        if (!updating_ && name_ != nullptr) {
            Artboard board = document_.activeArtboardRect();
            board.name = newui::wideToUtf8(name_->text());
            document_.setArtboard(document_.activeArtboard(), board);
        }
        refresh();
    }

}
