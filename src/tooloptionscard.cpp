#include "curvy/tooloptionscard.h"

#include "curvy/theme.h"

#include <newui/bundle.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr float kMinWidth = 236.0f;
        constexpr float kHeadHeight = 24.0f;
        constexpr float kFootHeight = 24.0f;
        constexpr float kPadding = 7.0f;
        constexpr float kSpacing = 4.0f;
        constexpr float kLineHeight = 17.0f;
        constexpr float kControlHeight = 24.0f;
        constexpr float kToggleHeight = 34.0f;

        template <typename T>
        T* find(View& root, const char* name) {
            return dynamic_cast<T*>(root.findView(name));
        }

        std::wstring widen(const std::string& text) { return std::wstring(text.begin(), text.end()); }

        std::string narrow(const std::wstring& text) {
            std::string out;
            for (wchar_t ch : text) {
                out.push_back(ch < 128 ? char(ch) : '?');
            }
            return out;
        }

        Label* makeLabel(const std::string& text) {
            auto* label = new Label();
            label->setText(text);
            label->setTextAlignment(TextAlignment::Left);
            return label;
        }
    }

    ToolOptionsCard::ToolOptionsCard(SubView& parent) : parent_(parent) {
        card_ = dynamic_cast<SubView*>(Bundle::instance().loadView("toolOptionsCard"));
        if (card_ == nullptr) {
            return;
        }
        card_->setVisible(false);
        parent_.addChild(card_);
        inner_ = find<SubView>(*card_, "optionsCard");
        body_ = find<SubView>(*card_, "optionsBody");
        foot_ = find<SubView>(*card_, "optionsFoot");
        title_ = find<Label>(*card_, "optionsTitle");
        note_ = find<Label>(*card_, "optionsNote");
        reset_ = find<Button>(*card_, "optionsReset");
        close_ = find<ToolbarButton>(*card_, "optionsClose");
        if (View* info = card_->findView("optionsInfo")) {
            info->setVisible(false);
        }
        if (body_ != nullptr) {
            for (const char* name : {"optionRow1", "optionRow2"}) {   // the mockup's slider rows: the rows are made from the tool's options
                if (auto* row = dynamic_cast<SubView*>(body_->findView(name))) {
                    body_->removeChild(row);
                    delete row;
                }
            }
        }
        if (close_ != nullptr) {
            close_->setIcon("icons/close.svg");
            close_->setIconSize(14.0f);
            close_->onClick += [this](Control&) {
                close();
                if (onDismissed) {
                    onDismissed();
                }
                return SyncReturn::Handled;
            };
        }
        if (reset_ != nullptr) {
            reset_->onClick += [this](Control&) {
                for (std::size_t i = 0; i < rows_.size(); ++i) {
                    apply(i, rows_[i].option.reset);
                }
                return SyncReturn::Handled;
            };
        }
        restyle();
    }

    void ToolOptionsCard::clearRows() {
        if (body_ != nullptr) {
            for (SubView* child : std::vector<SubView*>(body_->childViews())) {
                if (child != foot_) {
                    body_->removeChild(child);
                    delete child;
                }
            }
        }
        rows_.clear();
    }

    bool ToolOptionsCard::show(Tool& tool, const Point& at) {
        close();
        std::vector<ToolOption> options = tool.options();
        if (card_ == nullptr || body_ == nullptr || foot_ == nullptr || options.empty()) {
            return false;
        }
        toolId_ = tool.id();
        if (title_ != nullptr) {
            title_->setText(tool.title());
        }
        if (note_ != nullptr) {
            note_->setText(tool.optionsNote());
        }

        body_->removeChild(foot_);   // the rows go in before it
        float width = kMinWidth;
        for (ToolOption& option : options) {
            Row row;
            row.option = std::move(option);
            rows_.push_back(std::move(row));
        }
        for (Row& row : rows_) {
            if (row.option.kind == ToolOption::Kind::Choice) {
                row.segments = new SegmentedControl();
                row.segments->setSegments(row.option.choices);
                width = std::max(width, row.segments->naturalSize().width + 2.0f * kPadding);
            }
        }
        const float inside = width - 2.0f * kPadding;
        float rowsHeight = 0.0f;
        for (std::size_t index = 0; index < rows_.size(); ++index) {
            Row& row = rows_[index];
            auto* holder = new SubView();
            float height = kControlHeight;
            row.label = makeLabel(row.option.label);
            switch (row.option.kind) {
                case ToolOption::Kind::Choice: {
                    height = kLineHeight + 3.0f + kControlHeight;
                    row.label->setBounds(Rect(0.0f, 0.0f, inside, kLineHeight));
                    const Size natural = row.segments->naturalSize();
                    row.segments->setBounds(Rect(0.0f, kLineHeight + 3.0f, natural.width, kControlHeight));
                    holder->addChild(row.label);
                    holder->addChild(row.segments);
                    row.segments->onSelectionChanged += [this, index](SegmentedControl& changed) {
                        if (!updating_) {
                            apply(index, double(changed.selectedIndex()));
                        }
                        return SyncReturn::Handled;
                    };
                    break;
                }
                case ToolOption::Kind::Toggle: {
                    height = kToggleHeight;
                    row.toggle = new Toggle();
                    row.toggle->setBounds(Rect(0.0f, 2.0f, 20.0f, 20.0f));
                    row.label->setBounds(Rect(26.0f, 0.0f, inside - 26.0f, kToggleHeight));
                    holder->addChild(row.toggle);
                    holder->addChild(row.label);
                    row.toggle->onCheckedChanged += [this, index](Toggle& changed) {
                        if (!updating_) {
                            apply(index, changed.isChecked() ? 1.0 : 0.0);
                        }
                        return SyncReturn::Handled;
                    };
                    break;
                }
                case ToolOption::Kind::Number: {
                    row.label->setBounds(Rect(0.0f, 3.0f, 90.0f, kLineHeight));
                    row.slider = new Slider();
                    row.slider->setRange(float(row.option.min), float(row.option.max));
                    row.slider->setBounds(Rect(96.0f, 0.0f, std::max(40.0f, inside - 96.0f - 50.0f), kControlHeight));
                    row.field = new TextField();
                    row.field->setBounds(Rect(inside - 44.0f, 2.0f, 44.0f, 20.0f));
                    holder->addChild(row.label);
                    holder->addChild(row.slider);
                    holder->addChild(row.field);
                    row.slider->onValueChanged += [this, index](Slider& changed) {
                        if (!updating_) {
                            apply(index, double(changed.value()));
                        }
                        return SyncReturn::Handled;
                    };
                    row.field->onReturnPressed += [this, index](TextField& field) {
                        char* end = nullptr;
                        const std::string text = narrow(field.text());
                        const double parsed = std::strtod(text.c_str(), &end);
                        if (end != text.c_str()) {
                            apply(index, parsed);
                        }
                        refresh();   // a refused value goes back to the kept one
                        return SyncReturn::Handled;
                    };
                    break;
                }
            }
            if (View* control = controlOf(index)) {
                control->setName("option_" + row.option.id);   // a GUI test finds the control by it
            }
            holder->setDesiredSize(Size(inside, height));
            holder->setBounds(Rect(kPadding, kPadding, inside, height));
            for (View* view : std::initializer_list<View*>{holder, row.label, row.segments, row.toggle, row.slider, row.field}) {
                if (view != nullptr) {
                    view->setVisible(true);   // a view made in code starts hidden
                }
            }
            body_->addChild(holder);
            rowsHeight += height + kSpacing;
        }
        body_->addChild(foot_);
        foot_->setDesiredSize(Size(inside, kFootHeight));

        open_ = true;
        restyle();
        refresh();

        const float bodyHeight = 2.0f * kPadding + rowsHeight + kFootHeight;
        const float height = kHeadHeight + bodyHeight;
        const Rect client = parent_.getClientBounds();
        // Beside the anchor, never over it: to its right and a little above, or to its left when there is no room on the right; then pulled
        // back inside the parent (a tall card near the bottom slides up).
        constexpr float gap = 14.0f;
        float x = at.x + gap;
        if (x + width > client.width()) {
            x = at.x - gap - width;
        }
        x = std::clamp(x, 0.0f, std::max(0.0f, client.width() - width));
        const float y = std::clamp(at.y - 18.0f, 0.0f, std::max(0.0f, client.height() - height));
        card_->setBounds(Rect(x, y, width, height));
        if (inner_ != nullptr) {
            inner_->setBounds(Rect(0.0f, 0.0f, width, height));
            // The head and body are placed by hand: the frame's vertical layout keeps the body at the mockup's height, which clips the rows.
            inner_->setLayout(nullptr);
        }
        if (View* head = card_->findView("optionsHead")) {
            head->setBounds(Rect(0.0f, 0.0f, width, kHeadHeight));
            head->setDesiredSize(Size(width, kHeadHeight));
        }
        body_->setBounds(Rect(0.0f, kHeadHeight, width, bodyHeight));
        body_->setDesiredSize(Size(width, bodyHeight));
        card_->setVisible(true);
        card_->updateLayout();
        body_->updateLayout();
        parent_.style().markDirty();
        return true;
    }

    void ToolOptionsCard::close() {
        if (!open_) {
            return;
        }
        open_ = false;
        toolId_.clear();
        clearRows();
        card_->setVisible(false);
        parent_.style().markDirty();
    }

    bool ToolOptionsCard::contains(const Point& pt) const {
        return open_ && card_->bounds().contains(pt);
    }

    double ToolOptionsCard::value(std::size_t row) const {
        return row < rows_.size() ? rows_[row].option.get() : 0.0;
    }

    View* ToolOptionsCard::controlOf(std::size_t row) const {
        if (row >= rows_.size()) {
            return nullptr;
        }
        const Row& r = rows_[row];
        if (r.segments != nullptr) {
            return r.segments;
        }
        if (r.toggle != nullptr) {
            return r.toggle;
        }
        return r.slider;
    }

    void ToolOptionsCard::apply(std::size_t row, double value) {
        if (row >= rows_.size()) {
            return;
        }
        const ToolOption& option = rows_[row].option;
        if (option.enabled && !option.enabled()) {
            refresh();
            return;
        }
        option.set(std::clamp(value, option.min, option.kind == ToolOption::Kind::Choice ? double(option.choices.size()) - 1.0 : option.max));
        refresh();
    }

    void ToolOptionsCard::refresh() {
        updating_ = true;
        for (Row& row : rows_) {
            const double v = row.option.get();
            const bool enabled = !row.option.enabled || row.option.enabled();
            if (row.segments != nullptr) {
                row.segments->setSelectedIndex(std::size_t(std::max(0.0, v)));
            }
            if (row.toggle != nullptr) {
                row.toggle->setChecked(v >= 0.5);
                row.toggle->setEnabled(enabled);
            }
            if (row.slider != nullptr) {
                row.slider->setValue(float(v));
                row.slider->setEnabled(enabled);
            }
            if (row.field != nullptr) {
                row.field->setText(widen(std::to_string(int(std::lround(v)))));
            }
            if (row.segments != nullptr) {
                for (std::size_t i = 0; i < row.option.choices.size(); ++i) {
                    row.segments->setSegmentEnabled(i, enabled || i == std::size_t(std::max(0.0, v)));
                }
            }
            if (row.label != nullptr) {
                row.label->setEnabled(enabled);
            }
        }
        updating_ = false;
    }

    void ToolOptionsCard::restyle() {
        if (card_ == nullptr) {
            return;
        }
        const Theme& theme = Theme::current();
        auto fill = [](View* view, std::uint32_t rgb, std::uint32_t alpha) {
            if (view != nullptr) {
                view->style().setBackgroundColor(Color(Theme::argb(rgb, alpha), true));
            }
        };
        auto text = [](View* view, std::uint32_t rgb) {
            if (auto* label = dynamic_cast<Label*>(view)) {
                label->setTextColor(BLRgba32(Theme::argb(rgb)));
            }
        };
        fill(inner_, theme.bgSurface, 0xF2);
        fill(card_->findView("optionsHead"), theme.slot, 0xF2);
        text(title_, theme.textPrimary);
        text(note_, theme.accent);
        for (Row& row : rows_) {
            text(row.label, theme.textMuted);
            fill(row.field, theme.bgApp, 0xFF);
        }
    }

}
