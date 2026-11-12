#include "curvy/graphpanel.h"

#include <jitnodes/graph/network.h>

#include "curvy/theme.h"

#include <newui/cursor.h>
#include <newui/mouse_constants.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>

namespace curvy {

    using namespace newui;

    GraphPanel::GraphPanel(View& root, SketchDocument& document) : root_(root), document_(document) {
        for (const char* id : {"graphNameField", "graphTagsField"}) {
            if (auto* f = field(id)) {
                f->caret().setColor(Theme::current().textColor());
            }
        }
        if (auto* name = field("graphNameField")) {
            name->onReturnPressed += [this](TextField&) {
                commitName();
                return SyncReturn::Handled;
            };
        }
        if (auto* tags = field("graphTagsField")) {
            tags->onReturnPressed += [this](TextField&) {
                commitTags();
                return SyncReturn::Handled;
            };
        }
        makePicker();
        scroll_ =dynamic_cast<ScrollView*>(root_.findView("graphParamScroll"));
        viewport_ = dynamic_cast<SubView*>(root_.findView("graphParamViewport"));
        if (scroll_ != nullptr) {
            scroll_->onSizeChanged += [this](View&, const Size&) {
                layoutRows();
                return SyncReturn::Handled;
            };
        }
        const std::pair<const char*, const char*> buttons[] = {{"graphDisplayButton", "graph.display"}, {"graphBypassButton", "graph.bypass"}, {"graphFreezeButton", "graph.freeze"}};
        for (const auto& [id, command] : buttons) {
            if (auto* button = dynamic_cast<Button*>(root_.findView(id))) {
                button->onClick += [this, command = std::string(command)](Control&) {
                    if (onCommand) {
                        onCommand(command);
                    }
                    return SyncReturn::Handled;
                };
            }
        }
        show(NodeRef());
    }

    Label* GraphPanel::label(const std::string& name) const {
        return dynamic_cast<Label*>(root_.findView(name));
    }

    TextField* GraphPanel::field(const std::string& name) const {
        return dynamic_cast<TextField*>(root_.findView(name));
    }

    std::string GraphPanel::narrow(const std::wstring& text) {
        return std::string(text.begin(), text.end());
    }

    std::wstring GraphPanel::widen(const std::string& text) {
        return std::wstring(text.begin(), text.end());
    }

    void GraphPanel::show(const NodeRef& node) {
        if (!(node == node_)) {
            closeColorPicker();
        }
        node_ = node;
        const bool have = node.valid() && document_.hasNode(node);
        const jitnodes::Node* shown = have ? &document_.node(node) : nullptr;
        const auto set = [&](const char* labelName, const std::string& text) {
            if (Label* l = label(labelName)) {
                l->setText(text);
            }
        };
        set("graphPropsTitle", shown != nullptr ? shown->name() : "Nothing selected");
        set("graphPropsKind", shown != nullptr ? shown->typeName() : "");
        if (TextField* name = field("graphNameField")) {
            name->setText(widen(shown != nullptr ? shown->name() : ""));
            name->setEnabled(shown != nullptr);
        }
        if (TextField* tags = field("graphTagsField")) {
            std::string text;
            if (shown != nullptr) {
                for (const std::string& tag : shown->tags()) {
                    text += (text.empty() ? "" : " ") + tag;
                }
            }
            tags->setText(widen(text));
            tags->setEnabled(shown != nullptr);
        }
        const auto flag = [&](const char* id, const std::string& text) {
            if (auto* button = dynamic_cast<Button*>(root_.findView(id))) {
                button->setText(text);
                button->setEnabled(shown != nullptr);
            }
        };
        const bool displayed = have && node.scope == 0 && document_.network().displayNode() == node.id;
        flag("graphDisplayButton", displayed ? "Displayed" : "Display");
        flag("graphBypassButton", shown != nullptr && shown->bypassed() ? "Enable" : "Bypass");
        flag("graphFreezeButton", shown != nullptr && shown->frozen() ? "Unfreeze" : "Freeze");

        entries_.clear();
        if (shown != nullptr) {
            std::vector<std::string> names = shown->floatNames();
            std::sort(names.begin(), names.end());
            std::vector<Entry> colors, plain;
            const auto addPlain = [&](const std::string& n) {
                Entry entry;
                entry.name = n;
                if (const auto unit = shown->paramTagValue(n, "unit")) {
                    entry.unit = std::strtod(unit->c_str(), nullptr);
                }
                if (!(entry.unit > 0.0)) {
                    entry.unit = 1.0;
                }
                if (const auto low = shown->paramTagValue(n, "min")) {
                    entry.minimum = std::strtod(low->c_str(), nullptr);
                }
                if (const auto high = shown->paramTagValue(n, "max")) {
                    entry.maximum = std::strtod(high->c_str(), nullptr);
                }
                plain.push_back(entry);
            };
            for (const std::string& n : names) {
                const auto group = shown->paramTagValue(n, "group");
                if (!group || shown->paramTagValue(n, "kind").value_or("") != "color") {
                    addPlain(n);
                    continue;
                }
                auto at = std::find_if(colors.begin(), colors.end(), [&](const Entry& e) { return e.name == *group; });
                if (at == colors.end()) {
                    Entry color;
                    color.name = *group;
                    color.color = true;
                    colors.push_back(color);
                    at = colors.end() - 1;
                }
                const std::string channel = shown->paramTagValue(n, "channel").value_or("");
                (channel == "r" ? at->r : channel == "g" ? at->g : channel == "b" ? at->b : at->a) = n;
            }
            // A colour needs all of r, g and b; the parameters of one that does not are shown as plain numbers.
            for (const Entry& color : colors) {
                if (!color.r.empty() && !color.g.empty() && !color.b.empty()) {
                    entries_.push_back(color);
                } else {
                    for (const std::string& n : {color.r, color.g, color.b, color.a}) {
                        if (!n.empty()) {
                            addPlain(n);
                        }
                    }
                }
            }
            entries_.insert(entries_.end(), plain.begin(), plain.end());
        }
        while (viewport_ != nullptr && rows_.size() < entries_.size()) {
            makeRow();
        }
        for (std::size_t row = 0; row < rows_.size(); ++row) {
            fillRow(row);
        }
        layoutRows();
        syncPicker();

        const jitnodes::Network* net = have ? document_.findNetwork(node.scope) : nullptr;
        const auto link = [&](const char* id, const std::string& text) {
            if (Label* l = label(id)) {
                l->setText(text);
            }
        };
        std::vector<std::string> inputs, users;
        if (net != nullptr) {
            for (int i = 0; i < shown->inputCount(); ++i) {
                const jitnodes::NodeId from = net->inputOf(node.id, i);
                inputs.push_back(std::to_string(i) + "  " + (from == jitnodes::invalidNodeId ? "(empty)" : net->node(from).name()));
            }
            for (const jitnodes::NodeId id : net->nodeIds()) {
                for (int i = 0; i < net->node(id).inputCount(); ++i) {
                    if (net->inputOf(id, i) == node.id) {
                        users.push_back(std::to_string(i) + "  " + net->node(id).name());
                    }
                }
            }
        }
        link("graphInput0", inputs.size() > 0 ? inputs[0] : "");
        link("graphInput1", inputs.size() > 1 ? inputs[1] : "");
        link("graphUsed0", users.size() > 0 ? users[0] : "");
        root_.style().markDirty();
    }

    void GraphPanel::focusName() {
        if (TextField* name = field("graphNameField")) {
            if (RootView* rootView = name->rootView()) {
                rootView->setFocusedSubView(name);
            }
        }
    }

    void GraphPanel::commitName() {
        if (TextField* name = field("graphNameField")) {
            rename(narrow(name->text()));
        }
    }

    void GraphPanel::commitTags() {
        if (TextField* tags = field("graphTagsField")) {
            retag(narrow(tags->text()));
        }
    }

    void GraphPanel::retag(const std::string& text) {
        if (!node_.valid() || !document_.hasNode(node_)) {
            show(node_);
            return;
        }
        std::vector<std::string> words;
        std::string spaced = text;
        std::replace(spaced.begin(), spaced.end(), ',', ' ');
        std::istringstream in(spaced);
        for (std::string word; in >> word;) {
            if (std::find(words.begin(), words.end(), word) == words.end()) {
                words.push_back(word);
            }
        }
        try {
            document_.setGraphTags(node_, words);
            if (onEdited) {
                onEdited();
            }
        } catch (const std::exception& error) {
            if (onRefused) {
                onRefused(error.what());
            }
        }
        show(node_);
    }

    void GraphPanel::rename(const std::string& text) {
        if (!node_.valid() || !document_.hasNode(node_) || text.empty() || text == document_.node(node_).name()) {
            show(node_);
            return;
        }
        try {
            document_.renameGraphNode(node_, text);
            if (onEdited) {
                onEdited();
            }
        } catch (const std::exception& error) {
            if (onRefused) {
                onRefused(error.what());
            }
        }
        show(node_);
    }

    void GraphPanel::makeRow() {
        if (viewport_ == nullptr) {
            return;
        }
        const std::size_t index = rows_.size();
        Row row;
        row.caption = new Label();
        row.caption->setTextAlignment(TextAlignment::Left);
        row.caption->setTextColor(BLRgba32(0xFFA0A0A0u));
        viewport_->addChild(row.caption);
        row.field = new TextField();
        row.field->setTextColor(BLRgba32(0xFFF0F0F0u));
        row.field->caret().setColor(Theme::current().textColor());
        viewport_->addChild(row.field);
        row.swatch = new SubView();
        row.swatch->setVisible(false);
        row.swatch->setCursor(Cursor(CursorKind::Hand));
        row.swatch->onMouseDown += [this, index](View&, const Point&, std::uint32_t buttons, std::uint32_t) {
            if ((buttons & mbmLeftButton) == 0) {
                return SyncReturn(SyncReturn::Ignored);
            }
            toggleColorPicker(index);
            return SyncReturn(SyncReturn::Handled);
        };
        viewport_->addChild(row.swatch);

        row.field->onReturnPressed += [this, index](TextField&) {
            commitParam(index);
            return SyncReturn::Handled;
        };
        row.scrubber = NumberScrubber::attach(*row.caption, 1.0, -1e9, 1e9);
        row.scrubber->onBegin += [this, index](NumberScrubber&, double& value) {
            if (index >= entries_.size() || entries_[index].color || !node_.valid() || !document_.hasNode(node_)) {
                return SyncReturn(SyncReturn::Ignored);
            }
            scrubBefore_ = document_.snapshotGraph();
            value = double(document_.node(node_).getFloat(entries_[index].name));
            return SyncReturn(SyncReturn::Handled);
        };
        row.scrubber->onChange += [this, index](NumberScrubber&, double value) {
            if (index < entries_.size() && node_.valid() && document_.hasNode(node_)) {
                document_.setGraphFloatLive(node_, entries_[index].name, float(clampTo(entries_[index], value)));
                fillRow(index);
                if (onEdited) {
                    onEdited();
                }
            }
            return SyncReturn::Handled;
        };
        row.scrubber->onEnd += [this, index](NumberScrubber&, bool changed) {
            if (changed && scrubBefore_ && index < entries_.size()) {
                document_.commitGraphEdit(*scrubBefore_, "Change " + entries_[index].name);
            }
            scrubBefore_.reset();
            return SyncReturn::Handled;
        };
        row.scrubber->onUnit += [this, index](NumberScrubber&, double& unit) {
            if (index >= entries_.size()) {
                return SyncReturn(SyncReturn::Ignored);
            }
            unit = entries_[index].unit;
            return SyncReturn(SyncReturn::Handled);
        };
        rows_.push_back(row);
    }

    void GraphPanel::fillRow(std::size_t index) {
        Row& row = rows_[index];
        const bool used = index < entries_.size() && node_.valid() && document_.hasNode(node_);
        row.caption->setVisible(used);
        row.field->setVisible(used);
        row.swatch->setVisible(used && entries_[index].color);
        if (!used) {
            return;
        }
        const Entry& entry = entries_[index];
        const jitnodes::Node& shown = document_.node(node_);
        row.caption->setText(entry.name);
        row.caption->setCursor(Cursor(entry.color ? CursorKind::Arrow : CursorKind::SizeWE));
        if (entry.color) {
            const float r = shown.getFloat(entry.r), g = shown.getFloat(entry.g), b = shown.getFloat(entry.b);
            row.field->setText(widen(entry.a.empty() ? colorText(r, g, b) : colorText(r, g, b, shown.getFloat(entry.a))));
            const auto byte = [](float v) { return std::uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            row.swatch->style().setBackgroundColor(BLRgba32(0xFF000000u | (byte(r) << 16) | (byte(g) << 8) | byte(b)));
        } else {
            char text[32];
            std::snprintf(text, sizeof(text), "%g", double(shown.getFloat(entry.name)));
            row.field->setText(widen(text));
        }
    }

    void GraphPanel::layoutRows() {
        if (viewport_ == nullptr || scroll_ == nullptr) {
            return;
        }
        constexpr float rowHeight = 24.0f, margin = 8.0f, captionWidth = 90.0f, swatchWidth = 22.0f, scrollBar = 16.0f;
        const float width = (std::max)(120.0f, scroll_->getClientBounds().width() - scrollBar);
        const float height = (std::max)(rowHeight, rowHeight * float(entries_.size()));
        viewport_->setBounds(Rect(0.0f, 0.0f, width, height));
        scroll_->setContentSize(Size(width, height));
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const float y = rowHeight * float(i);
            Row& row = rows_[i];
            row.caption->setBounds(Rect(margin, y + 3.0f, captionWidth, 18.0f));
            float fieldLeft = margin + captionWidth + 6.0f;
            if (i < entries_.size() && entries_[i].color) {
                row.swatch->setBounds(Rect(fieldLeft, y + 4.0f, swatchWidth, 16.0f));
                fieldLeft += swatchWidth + 6.0f;
            }
            row.field->setBounds(Rect(fieldLeft, y + 2.0f, (std::max)(40.0f, width - fieldLeft - margin), 20.0f));
        }
        scroll_->updateLayout();
        scroll_->style().markDirty();
    }

    double GraphPanel::clampTo(const Entry& entry, double value) {
        if (entry.minimum && value < *entry.minimum) {
            return *entry.minimum;
        }
        if (entry.maximum && value > *entry.maximum) {
            return *entry.maximum;
        }
        return value;
    }

    std::string GraphPanel::limitText(const Entry& entry) {
        char low[32], high[32];
        std::snprintf(low, sizeof(low), "%g", entry.minimum.value_or(0.0));
        std::snprintf(high, sizeof(high), "%g", entry.maximum.value_or(0.0));
        if (entry.minimum && entry.maximum) {
            return std::string(low) + " to " + high;
        }
        return entry.minimum ? "at least " + std::string(low) : "at most " + std::string(high);
    }

    // The card lies over the node area, at its top right, so it never covers the property panel it was opened from.
    void GraphPanel::makePicker() {
        auto* nodes = dynamic_cast<SubView*>(root_.findView("graphNodes"));
        if (nodes == nullptr) {
            return;
        }
        picker_ = new ColorPickerCard();
        picker_->setHexVisible(false);   // the colour row already shows the hex
        picker_->setVisible(false);
        nodes->addChild(picker_);
        picker_->onDragStarted += [this](ColorPickerCard&) {
            pickerBefore_ = document_.snapshotGraph();
            return SyncReturn::Handled;
        };
        picker_->onChanged += [this](ColorPickerCard&) {
            applyPicker();
            swatches_->setCurrent(Swatch{float(picker_->red()), float(picker_->green()), float(picker_->blue()), float(picker_->alpha()), ""});
            return SyncReturn::Handled;
        };
        picker_->onDragFinished += [this](ColorPickerCard&) {
            if (pickerBefore_ && pickerRow_ < entries_.size()) {
                if (pendingLink_ != 0 && node_.valid() && document_.hasNode(node_)) {
                    document_.node(node_).addTag(SwatchLinks::tag(pendingLink_, entries_[pickerRow_].name));
                }
                document_.commitGraphEdit(*pickerBefore_, "Change " + entries_[pickerRow_].name);
            }
            pickerBefore_.reset();
            document_.noteRecentColor(Swatch{float(picker_->red()), float(picker_->green()), float(picker_->blue()), float(picker_->alpha()), ""});
            return SyncReturn::Handled;
        };

        picker_->setSwatchesVisible(true);
        swatches_ = std::make_unique<SwatchPaletteController>(document_, picker_->swatchStrip());
        swatches_->onUse = [this](const Swatch& swatch) {
            pendingLink_ = swatch.link;
            picker_->applyColor(swatch.r, swatch.g, swatch.b, swatch.a);
            pendingLink_ = 0;
        };
        swatches_->onMessage = [this](const std::string& message) {
            if (onRefused) {
                onRefused(message);
            }
        };
    }

    bool GraphPanel::colorPickerOpen() const {
        return picker_ != nullptr && picker_->isVisible();
    }

    bool GraphPanel::useRecentColor(std::size_t index) {
        const std::vector<Swatch>& recent = document_.recentColors();
        if (!colorPickerOpen() || index >= recent.size()) {
            return false;
        }
        const Swatch swatch = recent[index];   // applying it reorders the list
        picker_->applyColor(swatch.r, swatch.g, swatch.b, swatch.a);
        return true;
    }

    void GraphPanel::closeColorPicker() {
        if (picker_ != nullptr && picker_->isVisible()) {
            picker_->setVisible(false);
            if (View* parent = picker_->parent()) {
                parent->style().markDirty();
            }
        }
        pickerBefore_.reset();
    }

    void GraphPanel::toggleColorPicker(std::size_t row) {
        if (picker_ == nullptr || row >= entries_.size() || !entries_[row].color || !node_.valid() || !document_.hasNode(node_)) {
            return;
        }
        if (colorPickerOpen() && pickerRow_ == row) {
            closeColorPicker();
            return;
        }
        pickerRow_ = row;
        if (View* parent = picker_->parent()) {
            const Rect bounds = picker_->bounds();
            picker_->setBounds(Rect((std::max)(10.0f, parent->bounds().width() - bounds.width() - 10.0f), 10.0f, bounds.width(), bounds.height()));
            parent->style().markDirty();
        }
        picker_->setVisible(true);
        swatches_->refresh();   // New and Open change the palettes without telling the strip
        syncPicker();
    }

    void GraphPanel::syncPicker() {
        if (!colorPickerOpen()) {
            return;
        }
        if (pickerRow_ >= entries_.size() || !entries_[pickerRow_].color || !node_.valid() || !document_.hasNode(node_)) {
            closeColorPicker();
            return;
        }
        const Entry& entry = entries_[pickerRow_];
        const jitnodes::Node& shown = document_.node(node_);
        const bool hasAlpha = !entry.a.empty();
        picker_->setColor(shown.getFloat(entry.r), shown.getFloat(entry.g), shown.getFloat(entry.b), hasAlpha ? shown.getFloat(entry.a) : 1.0, hasAlpha);
        swatches_->setCurrent(Swatch{float(picker_->red()), float(picker_->green()), float(picker_->blue()), float(picker_->alpha()), ""});
    }

    void GraphPanel::dropLinkTags(const std::string& group) {
        jitnodes::Node& node = document_.node(node_);
        const std::vector<std::string> tags = node.tags();
        for (const std::string& tag : tags) {
            std::uint32_t link = 0;
            std::string tied;
            if (SwatchLinks::parse(tag, link, tied) && tied == group) {
                node.removeTag(tag);
            }
        }
    }

    void GraphPanel::applyPicker() {
        if (pickerRow_ >= entries_.size() || !entries_[pickerRow_].color || !node_.valid() || !document_.hasNode(node_)) {
            return;
        }
        const Entry& entry = entries_[pickerRow_];
        dropLinkTags(entry.name);   // a linked swatch's use ties the colour again when the gesture ends
        document_.setGraphFloatLive(node_, entry.r, float(picker_->red()));
        document_.setGraphFloatLive(node_, entry.g, float(picker_->green()));
        document_.setGraphFloatLive(node_, entry.b, float(picker_->blue()));
        if (!entry.a.empty()) {
            document_.setGraphFloatLive(node_, entry.a, float(picker_->alpha()));
        }
        fillRow(pickerRow_);
        if (onEdited) {
            onEdited();
        }
    }

    std::string GraphPanel::colorText(float r, float g, float b) {
        const auto byte = [](float v) { return unsigned(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        char text[16];
        std::snprintf(text, sizeof(text), "#%02X%02X%02X", byte(r), byte(g), byte(b));
        return text;
    }

    std::string GraphPanel::colorText(float r, float g, float b, float a) {
        const auto byte = [](float v) { return unsigned(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        char text[16];
        std::snprintf(text, sizeof(text), "#%02X%02X%02X%02X", byte(r), byte(g), byte(b), byte(a));
        return text;
    }

    bool GraphPanel::parseColor(const std::string& text, float& r, float& g, float& b, float& a, bool& hasAlpha) {
        std::string digits = text;
        digits.erase(std::remove_if(digits.begin(), digits.end(), [](unsigned char c) { return std::isspace(c) != 0; }), digits.end());
        if (!digits.empty() && digits[0] == '#') {
            digits.erase(0, 1);
        }
        if ((digits.size() != 6 && digits.size() != 8) || !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) {
            return false;
        }
        hasAlpha = digits.size() == 8;
        const unsigned long value = std::strtoul(digits.c_str(), nullptr, 16);
        const unsigned long rgb = hasAlpha ? value >> 8 : value;
        r = float((rgb >> 16) & 0xFFu) / 255.0f;
        g = float((rgb >> 8) & 0xFFu) / 255.0f;
        b = float(rgb & 0xFFu) / 255.0f;
        if (hasAlpha) {
            a = float(value & 0xFFu) / 255.0f;
        }
        return true;
    }

    void GraphPanel::setParam(std::size_t row, const std::string& text) {
        if (row < rows_.size()) {
            rows_[row].field->setText(widen(text));
        }
        commitParam(row);
    }

    void GraphPanel::commitParam(std::size_t row) {
        if (row >= rows_.size() || row >= entries_.size() || !node_.valid() || !document_.hasNode(node_)) {
            return;
        }
        const std::string text = narrow(rows_[row].field->text());
        if (entries_[row].color) {
            const Entry& entry = entries_[row];
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
            bool hasAlpha = false;
            if (!parseColor(text, r, g, b, a, hasAlpha)) {
                if (onRefused) {
                    onRefused("'" + text + "' is not a colour (use #RRGGBB or #RRGGBBAA)");
                }
                show(node_);
                return;
            }
            std::map<std::string, float> values = {{entry.r, r}, {entry.g, g}, {entry.b, b}};
            if (hasAlpha && !entry.a.empty()) {
                values[entry.a] = a;
            }
            const jitnodes::GraphSnapshot before = document_.snapshotGraph();
            for (const auto& [name, value] : values) {
                document_.setGraphFloatLive(node_, name, value);
            }
            dropLinkTags(entry.name);   // a colour typed by hand is no longer the swatch's
            document_.commitGraphEdit(before, "Change " + entry.name);
        } else {
            char* end = nullptr;
            const double number = std::strtod(text.c_str(), &end);
            if (end == text.c_str()) {
                if (onRefused) {
                    onRefused("'" + text + "' is not a number");
                }
                show(node_);
                return;
            }
            const double limited = clampTo(entries_[row], number);
            if (limited != number && onRefused) {
                onRefused(entries_[row].name + " is limited to " + limitText(entries_[row]));
            }
            document_.setGraphParam(node_, entries_[row].name, float(limited));
        }
        if (onEdited) {
            onEdited();
        }
        show(node_);
    }

    std::string GraphPanel::title() const {
        const Label* l = label("graphPropsTitle");
        return l != nullptr ? l->text() : "";
    }

    std::string GraphPanel::kind() const {
        const Label* l = label("graphPropsKind");
        return l != nullptr ? l->text() : "";
    }

    std::string GraphPanel::name() const {
        const TextField* f = field("graphNameField");
        return f != nullptr ? narrow(f->text()) : "";
    }

    std::string GraphPanel::paramCaption(std::size_t row) const {
        return row < entries_.size() ? entries_[row].name : "";
    }

    std::string GraphPanel::paramValue(std::size_t row) const {
        return row < entries_.size() && row < rows_.size() ? narrow(rows_[row].field->text()) : "";
    }

    bool GraphPanel::paramShown(std::size_t row) const {
        return row < entries_.size() && row < rows_.size() && rows_[row].field->isVisible();
    }

}
