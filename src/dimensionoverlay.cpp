#include "curvy/dimensionoverlay.h"

#include "curvy/canvasview.h"
#include "curvy/selectionmodel.h"
#include "curvy/settings.h"
#include "curvy/sketchdocument.h"
#include "curvy/theme.h"
#include "curvy/units.h"

#include <newui/font.h>
#include <newui/fontmanager.h>
#include <newui/keyboard_constants.h>
#include <newui/rootview.h>

#include <algorithm>
#include <cmath>

namespace curvy {

    using namespace newui;

    namespace {

        constexpr float kPillHeight = 18.0f;
        constexpr float kPillPad = 6.0f;
        constexpr float kGap = 8.0f;
        constexpr float kMinBoxPixels = 24.0f;
        constexpr double kPivotGrab = 8.0;
        constexpr double kPivotRadius = 5.0;

        BLRgba32 rgb(std::uint32_t value, std::uint32_t alpha = 255) { return BLRgba32(Theme::argb(value, alpha)); }

        std::string narrowText(const std::wstring& text) {
            return std::string(text.begin(), text.end());
        }

        // A scrub step of one displayed unit is too coarse in metres and too fine in points: the power of ten at or below one point.
        double scrubStep(double pointsPerUnit) {
            return std::pow(10.0, std::floor(std::log10(1.0 / pointsPerUnit)));
        }

    }

    DimensionOverlay::DimensionOverlay(ToolContext context)
        : context_(std::move(context)), model_(context_.document, context_.selection) {
        scrub_.onBegin += [this](NumberScrubber&, double& value) {
            if (!scrubKind_.has_value() || !model_.begin(*scrubKind_)) {
                if (scrubKind_.has_value()) {
                    context_.say(model_.message());
                }
                return SyncReturn(SyncReturn::Ignored);
            }
            for (const DimensionItem& item : model_.compute()) {
                if (item.kind == *scrubKind_) {
                    value = Units::toDisplay(item.value, Quantity::Length, context_.document.units(), double(context_.document.ppi()));
                }
            }
            return SyncReturn(SyncReturn::Handled);
        };
        scrub_.onChange += [this](NumberScrubber&, double value) {
            const double scale = Units::displayScale(Quantity::Length, context_.document.units(), double(context_.document.ppi()));
            if (!model_.preview(value * scale, aboutPivot_)) {
                context_.say(model_.message());
            }
            context_.canvas.style().markDirty();
            return SyncReturn::Handled;
        };
        scrub_.onEnd += [this](NumberScrubber&, bool changed) {
            if (changed && !abandon_) {
                model_.commit();
            } else {
                model_.cancel();
            }
            scrubKind_.reset();
            context_.canvas.style().markDirty();
            return SyncReturn::Handled;
        };
    }

    bool DimensionOverlay::shown() const {
        return context_.documentOpen() && !context_.graphShown() && context_.settings.showDimensions && !context_.selection.empty();
    }

    std::string DimensionOverlay::textFor(const DimensionItem& item) const {
        const DocumentUnits& units = context_.document.units();
        const double shown = Units::toDisplay(item.value, Quantity::Length, units, double(context_.document.ppi()));
        const std::string text = Units::format(shown, units.length, 2);
        switch (item.kind) {
            case DimensionItem::Kind::Radius:
                return "r " + text;
            case DimensionItem::Kind::CurveLength:
                return "len " + text;
            default:
                return text;
        }
    }

    void DimensionOverlay::rebuild() {
        labels_.clear();
        if (!shown()) {
            return;
        }
        BLBox box;
        const std::vector<DimensionItem> items = model_.compute();
        if (items.empty() || !model_.bounds(box)) {
            return;
        }
        const double zoom = double(context_.canvas.zoom());
        const bool wideEnough = (box.x1 - box.x0) * zoom >= kMinBoxPixels;
        const bool tallEnough = (box.y1 - box.y0) * zoom >= kMinBoxPixels;

        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        font.setSize(11.0f);
        for (const DimensionItem& item : items) {
            const bool fits = item.kind == DimensionItem::Kind::Height ? tallEnough
                              : item.kind == DimensionItem::Kind::CurveLength ? (wideEnough || tallEnough)
                                                                              : wideEnough;
            if (!fits) {
                continue;
            }
            Label label;
            label.item = item;
            label.text = textFor(item);
            const float width = font.measureText(label.text).width + 2.0f * kPillPad;
            const BLPoint at = context_.canvas.toView(item.anchor);
            float x = float(at.x) - width * 0.5f, y = float(at.y) - kPillHeight * 0.5f;
            switch (item.kind) {
                case DimensionItem::Kind::Width:
                    y = float(at.y) + kGap;
                    break;
                case DimensionItem::Kind::Height:
                    x = float(at.x) + kGap;
                    break;
                case DimensionItem::Kind::CurveLength:
                    x = float(at.x);
                    y = float(at.y) - kPillHeight - kGap;
                    break;
                case DimensionItem::Kind::Radius:
                    break;
            }
            label.rect = Rect(x, y, width, kPillHeight);
            labels_.push_back(label);
        }
    }

    const std::vector<DimensionOverlay::Label>& DimensionOverlay::labels() {
        rebuild();
        return labels_;
    }

    std::optional<std::size_t> DimensionOverlay::labelAt(const Point& view, bool editableOnly) {
        rebuild();
        for (std::size_t i = labels_.size(); i > 0; --i) {
            const Label& label = labels_[i - 1];
            if (label.rect.contains(view) && (!editableOnly || label.item.editable)) {
                return i - 1;
            }
        }
        return std::nullopt;
    }

    const std::vector<DimensionModel::PivotMark>& DimensionOverlay::pivotMarks() {
        marks_.clear();
        if (shown()) {
            marks_ = model_.pivots();   // read live, so a mark being dragged is drawn where the graph now has it
        }
        return marks_;
    }

    std::optional<std::size_t> DimensionOverlay::pivotAt(const Point& view) {
        const std::vector<DimensionModel::PivotMark>& marks = pivotMarks();
        for (std::size_t i = marks.size(); i > 0; --i) {
            const BLPoint at = context_.canvas.toView(marks[i - 1].at);
            if (std::hypot(at.x - double(view.x), at.y - double(view.y)) <= kPivotGrab) {
                return i - 1;
            }
        }
        return std::nullopt;
    }

    bool DimensionOverlay::press(const Point& view) {
        const std::optional<std::size_t> hit = labelAt(view);
        if (!hit.has_value()) {
            const std::optional<std::size_t> mark = pivotAt(view);
            if (!mark.has_value()) {
                return false;
            }
            if (!model_.beginPivotMove(marks_[*mark])) {
                context_.say(model_.message());
                return true;
            }
            pivotDrag_ = true;
            return true;
        }
        const Label& label = labels_[*hit];
        if (label.item.editable) {
            scrubKind_ = label.item.kind;
            scrub_.press(view.x);
        }
        return true;
    }

    void DimensionOverlay::move(const Point& view, std::uint32_t keys) {
        if (pivotDrag_) {
            model_.previewPivotMove(context_.canvas.toDocument(view));
            context_.canvas.style().markDirty();
            return;
        }
        if (!scrub_.active()) {
            return;
        }
        aboutPivot_ = (keys & kmAlt) != 0;
        const double step = scrubStep(Units::displayScale(Quantity::Length, context_.document.units(), double(context_.document.ppi())));
        scrub_.move(view.x, keys, step, 1e-4, 1e9);
    }

    void DimensionOverlay::release() {
        if (pivotDrag_) {
            pivotDrag_ = false;
            if (!abandon_) {
                model_.commit();
            } else {
                model_.cancel();
            }
            context_.canvas.style().markDirty();
            return;
        }
        if (scrub_.active()) {
            scrub_.release();
        }
    }

    bool DimensionOverlay::beginEdit(const Point& view) {
        const std::optional<std::size_t> hit = labelAt(view, true);
        if (!hit.has_value()) {
            return false;
        }
        const Label label = labels_[*hit];
        if (field_ == nullptr) {
            field_ = new TextField();
            field_->setTextColor(BLRgba32(0xFFF0F0F0u));
            field_->caret().setColor(Theme::current().textColor());
            field_->setName("dimensionField");
            field_->setVisible(false);
            context_.canvas.addChild(field_);
            const std::weak_ptr<bool> alive = alive_;
            field_->onReturnPressed += [this, alive](TextField&) {
                if (!alive.expired() && editing_) {
                    commitEdit(narrowText(field_->text()));
                }
                return SyncReturn::Handled;
            };
            field_->onLostFocus += [this, alive](View&) {
                if (!alive.expired() && editing_) {
                    closeField();
                }
                return SyncReturn::Handled;
            };
            field_->onKeyDown += [this, alive](View&, std::uint32_t, int, int, std::uint32_t vk) {
                if (!alive.expired() && editing_ && vk == vkEscape) {
                    closeField();
                    return SyncReturn::Handled;
                }
                return SyncReturn::Ignored;
            };
        }
        const DocumentUnits& units = context_.document.units();
        const double shown = Units::toDisplay(label.item.value, Quantity::Length, units, double(context_.document.ppi()));
        const std::string typed = Units::format(shown, units.length, 3);
        field_->setText(std::wstring(typed.begin(), typed.end()));
        field_->setBounds(Rect(label.rect.left() - 12.0f, label.rect.top() - 2.0f, (std::max)(label.rect.width() + 24.0f, 96.0f), 22.0f));
        field_->setVisible(true);
        editKind_ = label.item.kind;
        editing_ = true;
        if (RootView* root = field_->rootView()) {
            root->setFocusedSubView(field_);
        }
        field_->selectAll();
        context_.canvas.style().markDirty();
        return true;
    }

    bool DimensionOverlay::commitEdit(const std::string& text) {
        if (!editing_ || !editKind_.has_value()) {
            return false;
        }
        const DimensionItem::Kind kind = *editKind_;
        closeField();
        double points = 0.0;
        if (!Units::parseQuantity(text, Quantity::Length, context_.document.units(), double(context_.document.ppi()), points)) {
            context_.say("'" + text + "' is not a length");
            return false;
        }
        if (!model_.apply(kind, points)) {
            context_.say(model_.message().empty() ? "Nothing changed" : model_.message());
            return false;
        }
        context_.canvas.style().markDirty();
        return true;
    }

    void DimensionOverlay::closeField() {
        editing_ = false;
        editKind_.reset();
        if (field_ != nullptr) {
            field_->setVisible(false);
        }
        context_.canvas.style().markDirty();
    }

    void DimensionOverlay::cancelEdit() {
        if (editing_) {
            closeField();
        }
    }

    std::string DimensionOverlay::editText() const {
        return field_ != nullptr ? narrowText(field_->text()) : std::string();
    }

    void DimensionOverlay::cancel() {
        if (scrub_.active() || pivotDrag_) {
            abandon_ = true;   // the release below puts the graph back instead of making the undo step
            scrub_.release();
            abandon_ = false;
        }
        cancelEdit();
    }

    void DimensionOverlay::paintPivots(BLContext& ctx) {
        const BLRgba32 ink = rgb(Theme::current().dimension);
        const BLRgba32 halo = rgb(Theme::current().onAccent, 0xE0);
        ctx.set_stroke_width(3.0);
        for (const DimensionModel::PivotMark& mark : pivotMarks()) {
            const BLPoint at = context_.canvas.toView(mark.at);
            const double r = kPivotRadius;
            if (mark.level == DimensionModel::PivotMark::Level::Object) {
                BLPath diamond;
                diamond.move_to(at.x, at.y - r - 1.0);
                diamond.line_to(at.x + r + 1.0, at.y);
                diamond.line_to(at.x, at.y + r + 1.0);
                diamond.line_to(at.x - r - 1.0, at.y);
                diamond.close();
                ctx.stroke_path(diamond, halo);
                ctx.fill_path(diamond, ink);
            } else {
                ctx.stroke_circle(BLCircle(at.x, at.y, r), halo);
                ctx.set_stroke_width(1.0);
                ctx.stroke_circle(BLCircle(at.x, at.y, r), ink);
                ctx.stroke_line(BLPoint(at.x - r - 3.0, at.y), BLPoint(at.x + r + 3.0, at.y), ink);
                ctx.stroke_line(BLPoint(at.x, at.y - r - 3.0), BLPoint(at.x, at.y + r + 3.0), ink);
                ctx.set_stroke_width(3.0);
            }
        }
        ctx.set_stroke_width(1.0);
    }

    void DimensionOverlay::paint(BLContext& ctx) {
        rebuild();
        paintPivots(ctx);
        if (labels_.empty()) {
            return;
        }
        BLBox box;
        if (model_.bounds(box)) {
            const BLPoint a = context_.canvas.toView(BLPoint(box.x0, box.y0));
            const BLPoint b = context_.canvas.toView(BLPoint(box.x1, box.y1));
            ctx.set_stroke_width(1.0);
            ctx.stroke_rect(BLRect(a.x, a.y, b.x - a.x, b.y - a.y), rgb(Theme::current().dimension, 0xB0));
        }
        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        font.setSize(11.0f);
        BLFont* blFont = font.blFont();
        for (const Label& label : labels_) {
            const Rect& r = label.rect;
            ctx.set_fill_style(rgb(Theme::current().dimension, label.item.editable ? 0xE6 : 0xB0));
            ctx.fill_round_rect(BLRoundRect(double(r.left()), double(r.top()), double(r.width()), double(r.height()), 4.0));
            if (blFont != nullptr && blFont->is_valid()) {
                ctx.set_fill_style(rgb(Theme::current().onAccent));
                ctx.fill_utf8_text(BLPoint(double(r.left() + kPillPad), double(r.top()) + 13.0), *blFont, label.text.c_str(), label.text.size());
            }
        }
    }

}
