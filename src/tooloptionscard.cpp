#include "curvy/tooloptionscard.h"

#include "curvy/theme.h"

#include <newui/bundle.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace curvy {

    using namespace newui;

    namespace {
        constexpr float kCardWidth = 236.0f;
        constexpr float kHeadHeight = 24.0f;
        constexpr float kRowHeight = 28.0f;   // 24 and the 4 between rows
        constexpr float kFootHeight = 24.0f;
        constexpr float kPadding = 12.0f;

        template <typename T>
        T* find(View& root, const char* name) {
            return dynamic_cast<T*>(root.findView(name));
        }

        std::wstring widen(const std::string& text) { return std::wstring(text.begin(), text.end()); }

        struct ToolInfo {
            SketchTool tool;
            const char* command;
            const char* name;
        };
        const ToolInfo kTools[] = {{SketchTool::Polygon, "options.polygon", "Polygon"}, {SketchTool::Star, "options.star", "Star"},
                                   {SketchTool::RoundRect, "options.roundRect", "Rounded rectangle"}, {SketchTool::Pencil, "options.pencil", "Pencil"}};
    }

    bool ToolOptionsCard::hasOptions(SketchTool tool) {
        return std::any_of(std::begin(kTools), std::end(kTools), [&](const ToolInfo& info) { return info.tool == tool; });
    }

    std::string ToolOptionsCard::commandFor(SketchTool tool) {
        for (const ToolInfo& info : kTools) {
            if (info.tool == tool) {
                return info.command;
            }
        }
        return std::string();
    }

    bool ToolOptionsCard::toolFromCommand(const std::string& command, SketchTool& tool) {
        for (const ToolInfo& info : kTools) {
            if (command == info.command) {
                tool = info.tool;
                return true;
            }
        }
        return false;
    }

    ToolOptionsCard::ToolOptionsCard(SubView& parent, CanvasView& canvas) : parent_(parent), canvas_(canvas) {
        card_ = dynamic_cast<SubView*>(Bundle::instance().loadView("toolOptionsCard"));
        if (card_ == nullptr) {
            return;
        }
        card_->setVisible(false);
        parent_.addChild(card_);
        title_ = find<Label>(*card_, "optionsTitle");
        reset_ = find<Button>(*card_, "optionsReset");
        close_ = find<ToolbarButton>(*card_, "optionsClose");
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
        for (std::size_t i = 0; i < 2; ++i) {
            const std::string n = std::to_string(i + 1);
            rows_[i] = find<SubView>(*card_, ("optionRow" + n).c_str());
            labels_[i] = find<Label>(*card_, ("optionLabel" + n).c_str());
            sliders_[i] = find<Slider>(*card_, ("optionSlider" + n).c_str());
            fields_[i] = find<TextField>(*card_, ("optionValue" + n).c_str());
            if (sliders_[i] != nullptr) {
                sliders_[i]->setInteger(true);
                sliders_[i]->onValueChanged += [this, i](Slider& changed) {
                    if (!updating_) {
                        apply(i, double(changed.value()));
                    }
                    return SyncReturn::Handled;
                };
            }
            if (fields_[i] != nullptr) {
                fields_[i]->onReturnPressed += [this, i](TextField& field) {
                    std::string text;
                    for (wchar_t ch : field.text()) {
                        text.push_back(ch < 128 ? char(ch) : '?');
                    }
                    char* end = nullptr;
                    const double parsed = std::strtod(text.c_str(), &end);
                    if (end != text.c_str()) {
                        apply(i, parsed);
                    }
                    refresh();   // a refused value goes back to the kept one
                    return SyncReturn::Handled;
                };
            }
        }
        if (reset_ != nullptr) {
            reset_->onClick += [this](Control&) {
                for (std::size_t i = 0; i < active_.size(); ++i) {
                    apply(i, active_[i].reset);
                }
                return SyncReturn::Handled;
            };
        }
        canvas_.onToolDefaultsChanged += [this](CanvasView&) {
            if (open_) {
                refresh();   // the arrows while dragging a shape change the same values
            }
            return SyncReturn::Handled;
        };
        restyle();
    }

    std::vector<ToolOptionsCard::Row> ToolOptionsCard::rowsFor(SketchTool tool) const {
        CanvasView* canvas = &canvas_;
        switch (tool) {
            case SketchTool::Polygon:
                return {{"Sides", 3, 24, 5, [canvas] { return double(canvas->polygonSides()); }, [canvas](double v) { canvas->setPolygonSides(int(std::lround(v))); }}};
            case SketchTool::Star:
                return {{"Points", 3, 24, 5, [canvas] { return double(canvas->starPointCount()); }, [canvas](double v) { canvas->setStarPoints(int(std::lround(v))); }},
                        {"Inner radius", 10, 90, 50, [canvas] { return double(canvas->starInnerPercent()); }, [canvas](double v) { canvas->setStarInnerPercent(int(std::lround(v))); }}};
            case SketchTool::RoundRect:
                return {{"Corner radius", 0, 100, 12, [canvas] { return canvas->cornerRadius(); }, [canvas](double v) { canvas->setCornerRadius(v); }}};
            case SketchTool::Pencil:
                return {{"Smoothing", 0, 100, 40, [canvas] { return double(canvas->pencilSmoothing()); }, [canvas](double v) { canvas->setPencilSmoothing(int(std::lround(v))); }}};
            default:
                return {};
        }
    }

    void ToolOptionsCard::show(SketchTool tool, const Point& at) {
        close();
        std::vector<Row> rows = rowsFor(tool);
        if (card_ == nullptr || rows.empty()) {
            return;
        }
        tool_ = tool;
        open_ = true;
        active_ = std::move(rows);
        for (const ToolInfo& info : kTools) {
            if (info.tool == tool && title_ != nullptr) {
                title_->setText(info.name);
            }
        }
        updating_ = true;   // setRange() re-clamps the slider and fires its change: that must not rewrite the default
        for (std::size_t i = 0; i < 2; ++i) {
            const bool used = i < active_.size();
            if (rows_[i] != nullptr) {
                rows_[i]->setVisible(used);
            }
            if (used) {
                if (labels_[i] != nullptr) {
                    labels_[i]->setText(active_[i].label);
                }
                if (sliders_[i] != nullptr) {
                    sliders_[i]->setRange(float(active_[i].min), float(active_[i].max));
                }
            }
        }
        updating_ = false;
        refresh();

        const float height = kHeadHeight + kPadding + kRowHeight * float(active_.size()) + kFootHeight;
        const Rect client = parent_.getClientBounds();
        const float x = std::clamp(at.x, 0.0f, std::max(0.0f, client.width() - kCardWidth));
        const float y = std::clamp(at.y, 0.0f, std::max(0.0f, client.height() - height));
        card_->setBounds(Rect(x, y, kCardWidth, height));
        if (View* inner = card_->findView("optionsCard")) {
            inner->setBounds(Rect(0.0f, 0.0f, kCardWidth, height));
        }
        card_->setVisible(true);
        card_->updateLayout();
        parent_.style().markDirty();
    }

    void ToolOptionsCard::close() {
        if (!open_) {
            return;
        }
        open_ = false;
        tool_ = SketchTool::Select;
        active_.clear();
        card_->setVisible(false);
        parent_.style().markDirty();
    }

    bool ToolOptionsCard::contains(const Point& pt) const {
        return open_ && card_->bounds().contains(pt);
    }

    double ToolOptionsCard::value(std::size_t row) const {
        return row < active_.size() ? active_[row].get() : 0.0;
    }

    void ToolOptionsCard::apply(std::size_t row, double value) {
        if (row >= active_.size()) {
            return;
        }
        active_[row].set(std::clamp(value, active_[row].min, active_[row].max));
        refresh();
    }

    void ToolOptionsCard::refresh() {
        updating_ = true;
        for (std::size_t i = 0; i < active_.size() && i < 2; ++i) {
            const double v = active_[i].get();
            if (sliders_[i] != nullptr) {
                sliders_[i]->setValue(float(v));
            }
            if (fields_[i] != nullptr) {
                fields_[i]->setText(widen(std::to_string(int(std::lround(v)))));
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
        fill(card_->findView("optionsCard"), theme.bgSurface, 0xF2);
        fill(card_->findView("optionsHead"), theme.slot, 0xF2);
        text(title_, theme.textPrimary);
        text(card_->findView("optionsInfo"), theme.textMuted);
        text(card_->findView("optionsNote"), theme.accent);
        for (const char* name : {"optionLabel1", "optionLabel2"}) {
            text(card_->findView(name), theme.textMuted);
        }
        for (const char* name : {"optionValue1", "optionValue2"}) {
            fill(card_->findView(name), theme.bgApp, 0xFF);
        }
    }

}
