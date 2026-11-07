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
        list_ = dynamic_cast<BrushList*>(root_.findView("brushList"));
        combo_ = dynamic_cast<newui::DropDownList*>(root_.findView("brushCombo"));
        name_ = dynamic_cast<newui::TextField*>(root_.findView("brushNameField"));
        angle_ = dynamic_cast<newui::TextField*>(root_.findView("brushAngleField"));
        roundness_ = dynamic_cast<newui::TextField*>(root_.findView("brushRoundnessField"));
        spacing_ = dynamic_cast<newui::TextField*>(root_.findView("brushSpacingField"));
        scatter_ = dynamic_cast<newui::TextField*>(root_.findView("brushScatterField"));
        sizeVariation_ = dynamic_cast<newui::TextField*>(root_.findView("brushSizeVariationField"));
        rotation_ = dynamic_cast<newui::TextField*>(root_.findView("brushRotationField"));
        follow_ = dynamic_cast<newui::ToolbarButton*>(root_.findView("brushFollowButton"));
        fit_ = dynamic_cast<newui::DropDownList*>(root_.findView("brushFitCombo"));
        direction_ = dynamic_cast<newui::DropDownList*>(root_.findView("brushDirectionCombo"));
        corner_ = dynamic_cast<newui::DropDownList*>(root_.findView("brushCornerCombo"));
        link_ = dynamic_cast<newui::DropDownList*>(root_.findView("brushLinkCombo"));
        headGuide_ = dynamic_cast<newui::TextField*>(root_.findView("brushHeadGuideField"));
        tailGuide_ = dynamic_cast<newui::TextField*>(root_.findView("brushTailGuideField"));
        cornerRadius_ = dynamic_cast<newui::TextField*>(root_.findView("brushCornerRadiusField"));
        miterLimit_ = dynamic_cast<newui::TextField*>(root_.findView("brushMiterLimitField"));
        breakAngle_ = dynamic_cast<newui::TextField*>(root_.findView("brushBreakAngleField"));
        flipAlong_ = dynamic_cast<newui::ToolbarButton*>(root_.findView("brushFlipAlongButton"));
        flipAcross_ = dynamic_cast<newui::ToolbarButton*>(root_.findView("brushFlipAcrossButton"));

        if (combo_ != nullptr) {
            combo_->onSelectionChanged += [this](newui::DropDownList&) {
                if (!updating_ && combo_->selectedIndex().has_value() && *combo_->selectedIndex() < document_.brushes().size()) {
                    document_.setActiveBrush(document_.brushes()[*combo_->selectedIndex()]->id);
                }
                return newui::SyncReturn::Handled;
            };
        }
        if (list_ != nullptr) {
            list_->onBrushPicked += [this](BrushList& list) {
                if (updating_) {
                    return newui::SyncReturn::Handled;
                }
                if (list.pickedStar()) {   // the star: in or out of Favorites
                    if (std::shared_ptr<const BrushDef> def = document_.findBrush(list.pickedBrush())) {
                        BrushDef edited = *def;
                        edited.favorite = !def->favorite;
                        document_.changeBrush(def->id, edited);
                    }
                } else {
                    document_.setActiveBrush(list.pickedBrush());
                }
                return newui::SyncReturn::Handled;
            };
        }
        onButton("newCalligraphicBrushButton", [this]() { document_.makeBrush(BrushKind::Calligraphic, ""); });
        onButton("newArtBrushButton", [this]() { document_.makeBrush(BrushKind::Art, ""); });
        onButton("newScatterBrushButton", [this]() { document_.makeBrush(BrushKind::Scatter, ""); });
        onButton("applyBrushButton", [this]() { document_.applyBrush(document_.activeBrush()); });
        onButton("removeBrushButton", [this]() { document_.applyBrush(0); });
        onButton("deformBrushButton", [this]() { document_.deformSelectionAlongPath(); });
        onButton("releaseDeformButton", [this]() { document_.releaseDeformAlongPath(); });
        onButton("presetBrushButton", [this]() { document_.addBrushPresets(); });
        onButton("deleteBrushButton", [this]() { document_.deleteBrush(document_.activeBrush()); });
        for (newui::TextField* field : {name_, angle_, roundness_, spacing_, scatter_, sizeVariation_, rotation_, headGuide_, tailGuide_,
                                       cornerRadius_, miterLimit_, breakAngle_}) {
            if (field != nullptr) {
                field->onReturnPressed += [this](newui::TextField&) {
                    applyFields();
                    return newui::SyncReturn::Handled;
                };
            }
        }
        for (newui::DropDownList* list : {fit_, direction_, corner_}) {
            if (list != nullptr) {
                list->onSelectionChanged += [this](newui::DropDownList&) {
                    applyFields();
                    return newui::SyncReturn::Handled;
                };
            }
        }
        if (link_ != nullptr) {
            link_->onSelectionChanged += [this](newui::DropDownList&) {
                if (!updating_ && link_->selectedIndex().has_value()) {
                    const std::size_t index = *link_->selectedIndex();
                    document_.linkBrushToSymbol(document_.activeBrush(), index >= 1 && index <= linkIds_.size() ? linkIds_[index - 1] : 0);
                }
                return newui::SyncReturn::Handled;
            };
        }
        for (newui::ToolbarButton* button : {follow_, flipAlong_, flipAcross_}) {
            if (button == nullptr) {
                continue;
            }
            button->onCheckedChanged += [this](newui::ToolbarButton&) {
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

    bool BrushPanel::reveal() {
        auto* scroll = dynamic_cast<newui::ScrollView*>(root_.findView("sidebar"));
        auto* panel = dynamic_cast<newui::SubView*>(root_.findView("brushesPanel"));
        if (scroll == nullptr || panel == nullptr || scroll->vBar() == nullptr) {
            return false;
        }
        scroll->vBar()->setValue(panel->bounds().top());   // clamped to the scroll range
        return true;
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
        if (list_ != nullptr) {
            // Favorites, the brushes used lately, then one section per category (in the order they first appear).
            std::vector<BrushList::Section> sections;
            BrushList::Section favorites{"Favorites", {}}, recent{"Most Recent", {}};
            for (const std::shared_ptr<const BrushDef>& brush : document_.brushes()) {
                if (brush->favorite) {
                    favorites.brushes.push_back(brush);
                }
                BrushList::Section* into = nullptr;
                for (BrushList::Section& section : sections) {
                    if (section.title == brush->categoryName()) {
                        into = &section;
                    }
                }
                if (into == nullptr) {
                    sections.push_back({brush->categoryName(), {}});
                    into = &sections.back();
                }
                into->brushes.push_back(brush);
            }
            for (std::size_t recentId : document_.recentBrushes()) {
                if (std::shared_ptr<const BrushDef> brush = document_.findBrush(recentId)) {
                    recent.brushes.push_back(brush);
                }
            }
            if (!recent.brushes.empty()) {
                sections.insert(sections.begin(), std::move(recent));
            }
            if (!favorites.brushes.empty()) {
                sections.insert(sections.begin(), std::move(favorites));
            }
            list_->setSections(std::move(sections), id);
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
        const bool art = def && def->kind == BrushKind::Art;
        set(headGuide_, art && def->fit == BrushFit::Guided, def ? def->headGuide : 0.0);
        set(tailGuide_, art && def->fit == BrushFit::Guided, def ? def->tailGuide : 0.0);
        set(cornerRadius_, art && def->corner == BrushCorner::Smooth, def ? def->cornerRadius : 0.0);
        set(miterLimit_, art && def->corner == BrushCorner::Miter, def ? def->miterLimit : 0.0);
        set(breakAngle_, art && def->corner == BrushCorner::Break, def ? def->breakAngle : 0.0);
        auto choose = [art](newui::DropDownList* list, std::size_t index) {
            if (list != nullptr) {
                list->setEnabled(art);
                if (list->selectedIndex() != std::optional<std::size_t>(index)) {
                    list->setSelectedIndex(index);
                }
            }
        };
        choose(fit_, art ? static_cast<std::size_t>(def->fit) : 0);
        choose(direction_, art ? static_cast<std::size_t>(def->direction) : 0);
        choose(corner_, art ? static_cast<std::size_t>(def->corner) : 0);
        for (auto [button, on] : {std::pair{flipAlong_, art && def->flipAlong}, std::pair{flipAcross_, art && def->flipAcross}}) {
            if (button != nullptr) {
                button->setEnabled(art);
                button->setChecked(on);
            }
        }
        if (link_ != nullptr) {
            std::vector<std::string> names{"(none)"};
            std::vector<std::size_t> ids;
            for (const std::shared_ptr<const SymbolDef>& symbol : document_.symbols()) {
                names.push_back(symbol->name);
                ids.push_back(symbol->id);
            }
            if (names != linkShown_) {
                linkShown_ = names;
                linkIds_ = ids;
                auto model = std::make_unique<newui::StringListModel>();
                model->items() = names;
                link_->setModel(std::move(model));
            }
            std::size_t index = 0;
            for (std::size_t i = 0; def && def->sourceSymbol != 0 && i < linkIds_.size(); ++i) {
                if (linkIds_[i] == def->sourceSymbol) {
                    index = i + 1;
                }
            }
            if (link_->selectedIndex() != std::optional<std::size_t>(index)) {
                link_->setSelectedIndex(index);
            }
            link_->setEnabled(def && def->hasArt() && !ids.empty());
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
        enable("deformBrushButton", document_.selection().size() == 2);
        enable("releaseDeformButton", document_.hasDeformAlongPath());
        enable("deleteBrushButton", def != nullptr && document_.brushUseCount(id) == 0);
        fitRows(def.get());
        updating_ = false;
    }

    void BrushPanel::fitRows(const BrushDef* def) {
        auto* body = dynamic_cast<newui::SubView*>(root_.findView("brushPanelBody"));
        auto* panel = dynamic_cast<newui::SubView*>(root_.findView("brushPanel"));
        if (body == nullptr || panel == nullptr) {
            return;   // the older sidebar layout shows every row
        }
        const bool nib = def != nullptr && def->kind == BrushKind::Calligraphic, scatter = def != nullptr && def->kind == BrushKind::Scatter,
                   art = def != nullptr && def->kind == BrushKind::Art;
        struct Row { const char* name; bool shown; float height; };
        const Row rows[] = {
            {"brushList", true, 170.0f}, {"brushActions", true, 26.0f}, {"brushNameRow", def != nullptr, 24.0f},
            {"brushNibRow", nib, 24.0f}, {"brushSpacingRow", scatter, 24.0f}, {"brushVariationRow", scatter, 24.0f}, {"brushFollowRow", scatter, 26.0f},
            {"brushFitRow", art, 24.0f}, {"brushGuideRow", art && def->fit == BrushFit::Guided, 24.0f},
            {"brushCornerRow", art, 24.0f}, {"brushLimitRow", art && (def->corner == BrushCorner::Miter || def->corner == BrushCorner::Break), 24.0f},
            {"brushFlipRow", art, 26.0f}, {"brushLinkRow", art || scatter, 24.0f}};
        float height = 16.0f;   // the body's padding
        int shown = 0;
        for (const Row& row : rows) {
            if (auto* view = root_.findView(row.name)) {
                view->setVisible(row.shown);
            }
            if (row.shown) {
                height += row.height + (shown++ > 0 ? 6.0f : 0.0f);
            }
        }
        if (body->desiredSize().height != height) {
            body->setDesiredSize(newui::Size(body->desiredSize().width, height));
            panel->setDesiredSize(newui::Size(panel->desiredSize().width, height + 24.0f));
            root_.updateLayout();
        }
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
            if (headGuide_ != nullptr && headGuide_->isEnabled() && parseNumber(headGuide_, value)) { edited.headGuide = value; }
            if (tailGuide_ != nullptr && tailGuide_->isEnabled() && parseNumber(tailGuide_, value)) { edited.tailGuide = value; }
            if (cornerRadius_ != nullptr && cornerRadius_->isEnabled() && parseNumber(cornerRadius_, value)) { edited.cornerRadius = value; }
            if (miterLimit_ != nullptr && miterLimit_->isEnabled() && parseNumber(miterLimit_, value)) { edited.miterLimit = value; }
            if (breakAngle_ != nullptr && breakAngle_->isEnabled() && parseNumber(breakAngle_, value)) { edited.breakAngle = value; }
            auto picked = [](const newui::DropDownList* list, std::size_t count, std::size_t keep) {
                return list != nullptr && list->isEnabled() && list->selectedIndex().has_value() && *list->selectedIndex() < count ? *list->selectedIndex() : keep;
            };
            if (def->kind == BrushKind::Art) {
                edited.fit = static_cast<BrushFit>(picked(fit_, 3, static_cast<std::size_t>(def->fit)));
                edited.direction = static_cast<BrushDirection>(picked(direction_, 2, static_cast<std::size_t>(def->direction)));
                edited.corner = static_cast<BrushCorner>(picked(corner_, 4, static_cast<std::size_t>(def->corner)));
                if (flipAlong_ != nullptr) { edited.flipAlong = flipAlong_->isChecked(); }
                if (flipAcross_ != nullptr) { edited.flipAcross = flipAcross_->isChecked(); }
            }
            edited.setParams(edited.params());   // into range
            document_.changeBrush(def->id, edited);
        }
        refresh();
    }

}
