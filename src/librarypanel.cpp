#include "curvy/librarypanel.h"

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

    void LibraryPanel::onButton(const char* name, std::function<void()> action) {
        if (auto* button = dynamic_cast<newui::Control*>(root_.findView(name))) {
            button->onClick += [action](newui::Control&) {
                action();
                return newui::SyncReturn::Handled;
            };
        }
    }

    LibraryPanel::LibraryPanel(newui::RootView& root, SketchDocument& document) : root_(root), document_(document) {
        symbolCombo_ = dynamic_cast<newui::DropDownList*>(root_.findView("symbolCombo"));
        patternCombo_ = dynamic_cast<newui::DropDownList*>(root_.findView("patternCombo"));
        symbolName_ = dynamic_cast<newui::TextField*>(root_.findView("symbolNameField"));
        patternName_ = dynamic_cast<newui::TextField*>(root_.findView("patternNameField"));
        scale_ = dynamic_cast<newui::TextField*>(root_.findView("patternScaleField"));
        angle_ = dynamic_cast<newui::TextField*>(root_.findView("patternAngleField"));

        if (symbolCombo_ != nullptr) {
            symbolCombo_->onSelectionChanged += [this](newui::DropDownList&) {
                if (!updating_ && symbolCombo_->selectedIndex().has_value() && *symbolCombo_->selectedIndex() < document_.symbols().size()) {
                    document_.setActiveSymbol(document_.symbols()[*symbolCombo_->selectedIndex()]->id);
                }
                return newui::SyncReturn::Handled;
            };
        }
        if (patternCombo_ != nullptr) {
            patternCombo_->onSelectionChanged += [this](newui::DropDownList&) {
                if (!updating_ && patternCombo_->selectedIndex().has_value() && *patternCombo_->selectedIndex() < document_.patterns().size()) {
                    document_.setActivePattern(document_.patterns()[*patternCombo_->selectedIndex()]->id);
                }
                return newui::SyncReturn::Handled;
            };
        }
        onButton("newSymbolButton", [this]() { document_.makeSymbol(""); });
        onButton("placeSymbolButton", [this]() { document_.placeActiveSymbol(); });
        onButton("redefineSymbolButton", [this]() { document_.redefineSymbol(document_.activeSymbol()); });
        onButton("breakSymbolButton", [this]() { document_.breakSymbolLink(); });
        onButton("deleteSymbolButton", [this]() { document_.deleteSymbol(document_.activeSymbol()); });
        onButton("newPatternButton", [this]() { document_.makePattern(""); });
        onButton("patternFillButton", [this]() { document_.applyPattern(document_.activePattern(), false); });
        onButton("patternStrokeButton", [this]() { document_.applyPattern(document_.activePattern(), true); });
        onButton("deletePatternButton", [this]() { document_.deletePattern(document_.activePattern()); });
        if (symbolName_ != nullptr) {
            symbolName_->onReturnPressed += [this](newui::TextField&) {
                if (!updating_) { document_.renameSymbol(document_.activeSymbol(), newui::wideToUtf8(symbolName_->text())); }
                refresh();
                return newui::SyncReturn::Handled;
            };
        }
        if (patternName_ != nullptr) {
            patternName_->onReturnPressed += [this](newui::TextField&) {
                if (!updating_) { document_.renamePattern(document_.activePattern(), newui::wideToUtf8(patternName_->text())); }
                refresh();
                return newui::SyncReturn::Handled;
            };
        }
        for (newui::TextField* field : {scale_, angle_}) {
            if (field != nullptr) {
                field->onReturnPressed += [this](newui::TextField&) {
                    applyPatternTransform();
                    return newui::SyncReturn::Handled;
                };
            }
        }
        document_.onChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        document_.onSelectionChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        refresh();
    }

    void LibraryPanel::fillCombo(newui::DropDownList* combo, const std::vector<std::shared_ptr<const SymbolDef>>& library, std::vector<std::string>& shown, std::size_t active) {
        if (combo == nullptr) {
            return;
        }
        std::vector<std::string> names;
        for (const std::shared_ptr<const SymbolDef>& def : library) {
            names.push_back(def->name);
        }
        if (names != shown) {
            shown = names;
            auto model = std::make_unique<newui::StringListModel>();
            model->items() = names.empty() ? std::vector<std::string>{"(none)"} : names;
            combo->setModel(std::move(model));
        }
        std::optional<std::size_t> index = 0;
        for (std::size_t i = 0; i < library.size(); ++i) {
            if (library[i]->id == active) {
                index = i;
            }
        }
        if (combo->selectedIndex() != index) {
            combo->setSelectedIndex(*index);
        }
        combo->setEnabled(!library.empty());
    }

    void LibraryPanel::refresh() {
        updating_ = true;
        const std::size_t symbol = document_.activeSymbol(), pattern = document_.activePattern();
        fillCombo(symbolCombo_, document_.symbols(), symbolNames_, symbol);
        fillCombo(patternCombo_, document_.patterns(), patternNames_, pattern);
        auto nameOf = [](const std::shared_ptr<const SymbolDef>& def) { return def ? def->name : std::string(); };
        if (symbolName_ != nullptr) {
            symbolName_->setText(newui::utf8ToWide(nameOf(document_.findSymbol(symbol))));
            symbolName_->setEnabled(symbol != 0);
        }
        if (patternName_ != nullptr) {
            patternName_->setText(newui::utf8ToWide(nameOf(document_.findPattern(pattern))));
            patternName_->setEnabled(pattern != 0);
        }
        const SketchPaint* paint = document_.selectedPatternPaint();
        if (scale_ != nullptr) {
            scale_->setEnabled(paint != nullptr);
            scale_->setText(paint != nullptr ? numberText(paint->patternScale() * 100.0) : std::wstring());
        }
        if (angle_ != nullptr) {
            angle_->setEnabled(paint != nullptr);
            angle_->setText(paint != nullptr ? numberText(paint->patternAngle()) : std::wstring());
        }
        auto enable = [this](const char* name, bool on) {
            if (auto* control = dynamic_cast<newui::Control*>(root_.findView(name))) {
                control->setEnabled(on);
            }
        };
        const bool selected = !document_.selection().empty();
        enable("newSymbolButton", selected);
        enable("placeSymbolButton", symbol != 0);
        enable("redefineSymbolButton", symbol != 0 && selected);
        enable("breakSymbolButton", document_.canBreakSymbolLink());
        enable("deleteSymbolButton", symbol != 0 && document_.symbolUseCount(symbol) == 0);
        enable("newPatternButton", selected);
        enable("patternFillButton", pattern != 0 && selected);
        enable("patternStrokeButton", pattern != 0 && selected);
        enable("deletePatternButton", pattern != 0 && document_.patternUseCount(pattern) == 0);
        updating_ = false;
    }

    void LibraryPanel::applyPatternTransform() {
        double scale = 0.0, angle = 0.0;
        const SketchPaint* paint = document_.selectedPatternPaint();
        if (!updating_ && paint != nullptr && scale_ != nullptr && angle_ != nullptr && parseNumber(scale_->text(), scale) && parseNumber(angle_->text(), angle)) {
            document_.setPatternTransform(std::max(1.0, std::min(10000.0, scale)) / 100.0, angle);
        }
        refresh();
    }

}
