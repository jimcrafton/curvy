#include "curvy/selectpanel.h"

#include "curvy/theme.h"

#include <newui/bundle.h>
#include <newui/fontmanager.h>
#include <newui/uicolormanager.h>

#include <windows.h>

#include <algorithm>
#include <any>
#include <cstdlib>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr float kPad = 10.0f;
        constexpr float kRow = 24.0f;
        constexpr float kListRow = 24.0f;
        constexpr float kMinList = 80.0f;
        constexpr float kPreferredHeight = 340.0f;
        constexpr float kStatusHeight = 34.0f;

        const ShapeProperty kProperties[] = {ShapeProperty::Tag, ShapeProperty::Fill, ShapeProperty::Stroke, ShapeProperty::Width, ShapeProperty::Type, ShapeProperty::Layer};

        bool isColour(ShapeProperty property) {
            return property == ShapeProperty::Fill || property == ShapeProperty::Stroke;
        }

        bool parseHex(const std::string& text, std::uint32_t& rgb) {
            if (text.size() != 7 || text[0] != '#') {
                return false;
            }
            char* end = nullptr;
            rgb = std::uint32_t(std::strtoul(text.c_str() + 1, &end, 16));
            return end != nullptr && *end == '\0';
        }

        bool keyDown(int vk) {
            return (::GetKeyState(vk) & 0x8000) != 0;
        }

        double textWidth(const BLFont& font, const std::string& text) {
            BLGlyphBuffer glyphs;
            glyphs.set_utf8_text(text.c_str(), text.size());
            font.shape(glyphs);
            BLTextMetrics metrics;
            font.get_text_metrics(glyphs, metrics);
            return metrics.advance.x;
        }

        double baselineFor(const BLFont& font, double centerY) {
            const BLFontMetrics& metrics = font.metrics();
            return centerY - (metrics.ascent + metrics.descent) * 0.5 + metrics.ascent;
        }
    }

    void SelectValueModel::setRows(std::vector<SelectValueRow> rows) {
        rows_ = std::move(rows);
        onChanged(*this);
    }

    void SelectValueModel::clear() {
        rows_.clear();
        newui::ListModel::clear();
    }

    std::any SelectValueModel::value(const std::any& key) {
        const std::size_t* index = std::any_cast<std::size_t>(&key);
        if (index == nullptr || *index >= rows_.size()) {
            return std::any();
        }
        return rows_[*index].text;
    }

    void SelectValueItem::paint(BLContext& ctx, const newui::Rect& rect, std::size_t index, newui::ListController& controller) {
        Item::paint(ctx, rect);
        auto* model = dynamic_cast<SelectValueModel*>(controller.model());
        const SelectValueRow* row = model != nullptr ? model->row(index) : nullptr;
        newui::Font font = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
        BLFont* base = font.blFont();
        if (row == nullptr || base == nullptr || !base->is_valid()) {
            return;
        }
        const newui::Rect& bounds = clientBounds();
        const double centerY = bounds.top() + bounds.size().height * 0.5;
        const double left = bounds.left() + 6.0;
        double textLeft = left;
        ctx.save();
        if (row->own) {
            ctx.set_stroke_style(BLRgba32(Theme::argb(Theme::current().accent)));
            ctx.set_stroke_width(1.0);
            ctx.stroke_rect(BLRect(bounds.left() + 0.5, bounds.top() + 0.5, bounds.size().width - 1.0, bounds.size().height - 1.0));
        }
        if (row->colour) {
            ctx.set_fill_style(BLRgba32(Theme::argb(row->rgb)));
            ctx.fill_rect(BLRect(left, centerY - 7.0, 14.0, 14.0));
            ctx.set_stroke_style(BLRgba32(0xFF000000u));
            ctx.set_stroke_width(1.0);
            ctx.stroke_rect(BLRect(left + 0.5, centerY - 6.5, 13.0, 13.0));
            textLeft = left + 22.0;
        }
        const BLRgba32 textColor = newui::UIColorManager::colorFor(isSelected() ? newui::UIColorRole::HighlightText : newui::UIColorRole::ControlText).toBLRgba32();
        const std::string count = std::to_string(row->count);
        const double countLeft = bounds.left() + bounds.size().width - 8.0 - textWidth(*base, count);
        ctx.set_fill_style(isSelected() ? textColor : BLRgba32(0xFFA0A0A0u));
        ctx.fill_utf8_text(BLPoint(countLeft, baselineFor(*base, centerY)), *base, count.c_str(), count.size());
        ctx.clip_to_rect(BLRect(textLeft, bounds.top(), (std::max)(countLeft - 6.0 - textLeft, 0.0), bounds.size().height));
        ctx.set_fill_style(textColor);
        ctx.fill_utf8_text(BLPoint(textLeft, baselineFor(*base, centerY)), *base, row->text.c_str(), row->text.size());
        ctx.restore();
    }

    SelectPanel::SelectPanel(SketchDocument& document, SelectionModel& selection) : Panel("select", "Select by"), document_(document), selection_(selection) {
        View* loaded = Bundle::instance().loadView("selectPanel");
        root_ = dynamic_cast<SubView*>(loaded);
        if (root_ == nullptr) {
            delete loaded;
            throw std::runtime_error("SelectPanel: Resources/selectPanel.newui did not load");
        }
        body().addChild(root_);
        root_->setLayout(nullptr);

        const auto find = [this](const char* name) { return root_->findView(name); };
        title_ = dynamic_cast<Label*>(find("selectTitle"));
        tabsRow_ = dynamic_cast<SubView*>(find("selectTabsRow"));
        tabs_ = dynamic_cast<SegmentedControl*>(find("selectTabs"));
        valuesBox_ = dynamic_cast<SubView*>(find("selectValuesBox"));
        withinRow_ = dynamic_cast<SubView*>(find("selectWithinRow"));
        withinBox_ = dynamic_cast<SubView*>(find("selectWithinBox"));
        withinTabs_ = dynamic_cast<SegmentedControl*>(find("selectWithin"));
        buttons_ = dynamic_cast<SubView*>(find("selectButtons"));
        status_ = dynamic_cast<Label*>(find("selectStatus"));
        if (title_ == nullptr || tabsRow_ == nullptr || tabs_ == nullptr || valuesBox_ == nullptr || withinRow_ == nullptr || withinBox_ == nullptr ||
            withinTabs_ == nullptr || buttons_ == nullptr || status_ == nullptr) {
            throw std::runtime_error("SelectPanel: Resources/selectPanel.newui lacks a view the panel needs");
        }
        tabsRow_->setLayout(nullptr);
        withinRow_->setLayout(nullptr);
        withinBox_->setLayout(nullptr);
        buttons_->setLayout(nullptr);
        valuesBox_->setLayout(nullptr);

        updating_ = true;
        tabs_->setSegments({"Tag", "Fill", "Stroke", "Width", "Type", "Layer"});
        tabs_->setSelectedIndex(1);
        withinTabs_->setSegments({"Document", "Selection"});
        updating_ = false;
        tabs_->onSelectionChanged += [this](SegmentedControl& changed) {
            if (!updating_ && changed.selectedIndex() < std::size(kProperties)) {
                property_ = kProperties[changed.selectedIndex()];
                chosen_.clear();
                refresh();
            }
            return SyncReturn::Handled;
        };
        withinTabs_->onSelectionChanged += [this](SegmentedControl& changed) {
            if (!updating_) {
                within_ = changed.selectedIndex() == 1 ? Within::Selection : Within::Document;
                refresh();
            }
            return SyncReturn::Handled;
        };

        for (SubView* child : std::vector<SubView*>(valuesBox_->childViews())) {   // the mockup's placeholder list
            valuesBox_->removeChild(child);
            delete child;
        }
        auto* scroll = new ScrollView();
        valuesBox_->addChild(scroll);
        list_ = new ListView();
        list_->setName("selectValues");   // a GUI test finds the list by it
        scroll->addChild(list_);
        auto controller = std::make_shared<ListController>();
        controller->setDefaultItemClassName("SelectValueItem");
        list_->setController(controller);
        auto rows = std::make_unique<SelectValueModel>();
        model_ = rows.get();
        list_->setModel(std::move(rows));
        list_->setRowHeight(kListRow);
        list_->onSelectionChanged += [this](ListView& changed) {
            if (updating_) {
                return SyncReturn(SyncReturn::Handled);
            }
            const std::optional<std::size_t> index = changed.selectedIndex();
            const SelectValueRow* picked = index.has_value() ? model_->row(*index) : nullptr;
            if (picked != nullptr) {
                SelectionModel::Combine how = SelectionModel::Combine::Replace;
                if (keyDown(VK_MENU)) {
                    how = SelectionModel::Combine::Subtract;
                } else if (keyDown(VK_SHIFT) || keyDown(VK_CONTROL)) {
                    how = SelectionModel::Combine::Add;
                }
                const std::string value = picked->text;
                choose(value, how);
                updating_ = true;
                list_->setSelectedIndex(std::nullopt);   // the list is a menu of values, not a selection of its own
                updating_ = false;
            }
            return SyncReturn(SyncReturn::Handled);
        };

        const auto click = [this](const char* name, std::function<void()> action) {
            if (auto* button = dynamic_cast<Button*>(root_->findView(name))) {
                button->onClick += [action](Control&) {
                    action();
                    return SyncReturn::Handled;
                };
            }
        };
        click("selectAddButton", [this]() { applyChosen(SelectionModel::Combine::Add); });
        click("selectSubtractButton", [this]() { applyChosen(SelectionModel::Combine::Subtract); });
        click("selectSimilarButton", [this]() { selectSimilar(); });

        body().onSizeChanged += [this](View&, const Size&) {
            layoutAll();
            return SyncReturn::Ignored;
        };
        setPreferredBodyHeight(kPreferredHeight);
        refresh();
    }

    SelectPanel::~SelectPanel() {
        if (onDestroyed) {
            onDestroyed();
        }
    }

    void SelectPanel::layoutAll() {
        const Rect own = body().bounds();
        const float width = own.width();
        const float height = (std::max)(own.height(), kPreferredHeight * 0.75f);
        const float inner = width - 2.0f * kPad;
        root_->setBounds(Rect(0.0f, 0.0f, width, height));
        body().setContentSize(Size(width, height));

        float y = 8.0f;
        title_->setBounds(Rect(kPad, y, inner, 16.0f));
        y += 20.0f;
        tabsRow_->setBounds(Rect(kPad, y, inner, kRow));
        tabs_->setBounds(Rect(0.0f, 0.0f, inner, kRow));
        y += kRow + 6.0f;
        const float fixedBelow = kRow + 6.0f + kRow + 6.0f + kStatusHeight + 6.0f;
        const float listHeight = (std::max)(height - y - fixedBelow, kMinList);
        valuesBox_->setBounds(Rect(kPad, y, inner, listHeight));
        if (!valuesBox_->childViews().empty()) {
            valuesBox_->childViews().front()->setBounds(Rect(1.0f, 1.0f, inner - 2.0f, listHeight - 2.0f));
        }
        y += listHeight + 6.0f;
        withinRow_->setBounds(Rect(kPad, y, inner, kRow));
        if (auto* label = root_->findView("withinLabel")) {
            label->setBounds(Rect(0.0f, 0.0f, 52.0f, kRow));
        }
        withinBox_->setBounds(Rect(56.0f, 0.0f, inner - 56.0f, kRow));
        withinTabs_->setBounds(Rect(0.0f, 0.0f, inner - 56.0f, kRow));
        y += kRow + 6.0f;
        buttons_->setBounds(Rect(kPad, y, inner, kRow));
        const char* names[] = {"selectAddButton", "selectSubtractButton", "selectSimilarButton"};
        const float cell = (inner - 2.0f * 5.0f) / 3.0f;
        for (std::size_t i = 0; i < 3; ++i) {
            if (auto* button = root_->findView(names[i])) {
                button->setBounds(Rect(float(i) * (cell + 5.0f), 0.0f, cell, kRow));
            }
        }
        y += kRow + 6.0f;
        status_->setBounds(Rect(kPad, y, inner, kStatusHeight));
        root_->updateLayout();
    }

    void SelectPanel::setProperty(ShapeProperty property) {
        property_ = property;
        chosen_.clear();
        updating_ = true;
        for (std::size_t i = 0; i < std::size(kProperties); ++i) {
            if (kProperties[i] == property) {
                tabs_->setSelectedIndex(i);
            }
        }
        updating_ = false;
        refresh();
    }

    void SelectPanel::setWithin(Within within) {
        within_ = within;
        updating_ = true;
        withinTabs_->setSelectedIndex(within == Within::Selection ? 1 : 0);
        updating_ = false;
        refresh();
    }

    void SelectPanel::rebuildShapes() {
        shapes_.clear();
        const jitnodes::GeometryHandle artwork = document_.artwork();
        if (artwork != nullptr) {
            shapes_ = describeShapes(document_, *artwork);
        }
    }

    std::vector<std::string> SelectPanel::ownValues() const {
        if (selection_.items().empty()) {
            return {};
        }
        std::vector<std::string> out;
        std::size_t shapes = 0;
        for (const ShapeValues& shape : shapes_) {
            if (selection_.isShapeKeySelected(shape.key)) {
                ++shapes;
                for (const std::string& value : valuesOf(shape, property_)) {
                    if (std::find(out.begin(), out.end(), value) == out.end()) {
                        out.push_back(value);
                    }
                }
            }
        }
        return shapes == 1 ? out : std::vector<std::string>();
    }

    void SelectPanel::refresh() {
        rebuildShapes();
        std::vector<ShapeValues> pool;
        for (const ShapeValues& shape : shapes_) {
            if (within_ == Within::Document || selection_.isShapeKeySelected(shape.key)) {
                pool.push_back(shape);
            }
        }
        const std::vector<std::string> own = ownValues();
        std::vector<SelectValueRow> rows;
        for (const ValueCount& value : countValues(pool, property_)) {
            SelectValueRow row;
            row.text = value.text;
            row.count = value.count;
            row.own = std::find(own.begin(), own.end(), value.text) != own.end();
            row.colour = isColour(property_) && parseHex(value.text, row.rgb);
            rows.push_back(std::move(row));
        }
        updating_ = true;
        model_->setRows(std::move(rows));
        list_->setSelectedIndex(std::nullopt);
        updating_ = false;
        if (statusText_.empty() || selection_.items().empty()) {
            showStatus(selection_.items().empty() ? "Nothing selected. Click a value to select every shape that has it." : "");
        }
        layoutAll();
    }

    void SelectPanel::showStatus(const std::string& text) {
        statusText_ = text;
        status_->setText(text);
    }

    void SelectPanel::applyKeys(const std::vector<std::string>& keys, SelectionModel::Combine how) {
        selection_.selectShapeKeys(keys, how);
        const std::string verb = how == SelectionModel::Combine::Add ? "Added " : (how == SelectionModel::Combine::Subtract ? "Removed " : "Selected ");
        showStatus(verb + std::to_string(keys.size()) + (keys.size() == 1 ? " shape" : " shapes"));
        if (onMessage) {
            onMessage(statusText_);
        }
    }

    bool SelectPanel::choose(const std::string& value, SelectionModel::Combine how) {
        rebuildShapes();
        std::vector<ShapeValues> pool;
        for (const ShapeValues& shape : shapes_) {
            if (within_ == Within::Document || selection_.isShapeKeySelected(shape.key)) {
                pool.push_back(shape);
            }
        }
        const std::vector<std::string> keys = shapesWithValue(pool, property_, value);
        if (keys.empty()) {
            return false;
        }
        chosen_ = value;
        applyKeys(keys, how);
        return true;
    }

    bool SelectPanel::applyChosen(SelectionModel::Combine how) {
        if (chosen_.empty()) {
            showStatus("Click a value first");
            return false;
        }
        return choose(chosen_, how);
    }

    bool SelectPanel::selectSimilar() {
        rebuildShapes();
        const std::vector<std::string> own = ownValues();
        if (own.empty()) {
            showStatus("Select one shape first");
            return false;
        }
        std::vector<std::string> keys;
        for (const std::string& value : own) {
            for (const std::string& key : shapesWithValue(shapes_, property_, value)) {
                if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                    keys.push_back(key);
                }
            }
        }
        chosen_ = own.front();
        applyKeys(keys, SelectionModel::Combine::Replace);
        return true;
    }

}
