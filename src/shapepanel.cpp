#include "curvy/shapepanel.h"

#include <newui/utils.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace curvy {

    namespace {
        constexpr double kPi = 3.14159265358979323846;

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
            const double parsed = std::strtod(narrow.c_str(), &end);
            if (end == narrow.c_str() || !std::isfinite(parsed)) {
                return false;
            }
            value = parsed;
            return true;
        }

        double wrapSweep(double radians) {   // magnitude into (0, 2 pi], as the canvas handles keep it
            radians = std::fabs(radians);
            return std::max(1e-3, std::min(2.0 * kPi, radians));
        }
    }

    ShapePanel::ShapePanel(newui::RootView& root, SketchDocument& document) : root_(root), document_(document) {
        info_ = dynamic_cast<newui::Label*>(root_.findView("shapePanelHeaderInfo"));
        countLabel_ = dynamic_cast<newui::Label*>(root_.findView("shapeCountLabel"));
        count_ = dynamic_cast<newui::TextField*>(root_.findView("shapeCountField"));
        inner_ = dynamic_cast<newui::TextField*>(root_.findView("shapeInnerField"));
        radius_ = dynamic_cast<newui::TextField*>(root_.findView("shapeRadiusField"));
        start_ = dynamic_cast<newui::TextField*>(root_.findView("shapeStartField"));
        sweep_ = dynamic_cast<newui::TextField*>(root_.findView("shapeSweepField"));
        bind(count_, Field::Count);
        bind(inner_, Field::Inner);
        bind(radius_, Field::Radius);
        bind(start_, Field::Start);
        bind(sweep_, Field::Sweep);
        document_.onChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        document_.onSelectionChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        refresh();
    }

    void ShapePanel::bind(newui::TextField* field, Field which) {
        if (field == nullptr) {
            return;
        }
        field->onReturnPressed += [this, which](newui::TextField&) { apply(which); return newui::SyncReturn::Handled; };
        field->onLostFocus += [this, which](newui::View&) { apply(which); return newui::SyncReturn::Handled; };
    }

    void ShapePanel::show(newui::View* view, bool visible) {
        if (view != nullptr) {
            view->setVisible(visible);
        }
    }

    bool ShapePanel::current(LiveShape& out) const {
        if (document_.selection().size() != 1) {
            return false;
        }
        const SketchObject* object = document_.findById(document_.selection().front());
        return object != nullptr && recogniseLiveShape(*object, out);
    }

    void ShapePanel::refresh() {
        LiveShape shape;
        const bool live = current(shape);
        updating_ = true;
        const bool regular = live && (shape.kind == LiveKind::Star || shape.kind == LiveKind::Polygon);
        show(root_.findView("shapeCountRow"), regular);
        show(root_.findView("shapeInnerRow"), live && shape.kind == LiveKind::Star);
        show(root_.findView("shapeRadiusRow"), live && shape.kind == LiveKind::RoundRect);
        show(root_.findView("shapeStartRow"), live && shape.kind == LiveKind::Arc);
        show(root_.findView("shapeSweepRow"), live && shape.kind == LiveKind::Arc);
        if (live) {
            const char* name = shape.kind == LiveKind::Star ? "Star" : shape.kind == LiveKind::Polygon ? "Polygon" : shape.kind == LiveKind::RoundRect ? "Rounded rectangle" : "Arc";
            if (info_ != nullptr) { info_->setText(name); }
            if (countLabel_ != nullptr) { countLabel_->setText(shape.kind == LiveKind::Star ? "Points" : "Sides"); }
            if (count_ != nullptr) { count_->setText(numberText(shape.regular.points)); }
            if (inner_ != nullptr) { inner_->setText(numberText(shape.regular.ratio() * 100.0)); }
            if (radius_ != nullptr) { radius_->setText(numberText(shape.rect.radius)); }
            if (start_ != nullptr) { start_->setText(numberText(shape.arc.start * 180.0 / kPi)); }
            if (sweep_ != nullptr) { sweep_->setText(numberText(std::fabs(shape.arc.sweep) * 180.0 / kPi)); }
        }
        updating_ = false;
    }

    void ShapePanel::apply(Field which) {
        LiveShape shape;
        if (updating_ || !current(shape)) {
            return;
        }
        newui::TextField* field = which == Field::Count ? count_ : which == Field::Inner ? inner_ : which == Field::Radius ? radius_ : which == Field::Start ? start_ : sweep_;
        double value = 0.0;
        if (field == nullptr || !parseNumber(field->text(), value)) {
            refresh();
            return;
        }
        const LiveShape original = shape;
        const char* what = "Change Shape";
        switch (which) {
            case Field::Count:
                shape.regular.points = std::max(3, std::min(shape.kind == LiveKind::Star ? 40 : 60, int(std::lround(value))));
                what = shape.kind == LiveKind::Star ? "Change Star Points" : "Change Polygon Sides";
                break;
            case Field::Inner:
                shape.regular.inner = std::max(0.05, std::min(0.95, value / 100.0)) * shape.regular.outer;
                what = "Change Star Inner Radius";
                break;
            case Field::Radius: {
                const double limit = std::min(shape.rect.box.x1 - shape.rect.box.x0, shape.rect.box.y1 - shape.rect.box.y0) * 0.5;
                shape.rect.radius = std::max(0.0, std::min(limit, value));
                what = "Change Corner Radius";
                break;
            }
            case Field::Start: {
                const double end = shape.arc.start + shape.arc.sweep;   // the far end stays put, as with the canvas handle
                shape.arc.start = value * kPi / 180.0;
                const double direction = shape.arc.sweep < 0.0 ? -1.0 : 1.0;
                shape.arc.sweep = wrapSweep(end - shape.arc.start) * direction;
                what = "Change Arc";
                break;
            }
            case Field::Sweep:
                shape.arc.sweep = wrapSweep(value * kPi / 180.0) * (shape.arc.sweep < 0.0 ? -1.0 : 1.0);
                what = "Change Arc";
                break;
        }
        // The fields show two decimals, so a number the user did not change comes back slightly rounded: that is not an edit.
        const auto same = [](double a, double b, double tolerance) { return std::fabs(a - b) < tolerance; };
        const double degree = kPi / 180.0;
        if (shape.regular.points == original.regular.points && same(shape.regular.ratio(), original.regular.ratio(), 0.00006) &&
            same(shape.rect.radius, original.rect.radius, 0.006) && same(shape.arc.start, original.arc.start, 0.006 * degree) &&
            same(std::fabs(shape.arc.sweep), std::fabs(original.arc.sweep), 0.006 * degree)) {
            refresh();   // Return and then losing focus both arrive here: the second finds nothing to change
            return;
        }
        SketchObject* object = document_.findById(shape.object);
        if (object != nullptr) {
            const auto before = document_.snapshotSelection();
            const FrameState frame = document_.frameState();
            const jitnodes::GraphSnapshot graphBefore = document_.snapshotGraph();
            const BLPath path = liveShapePath(shape);
            if (document_.isParametric(object->id()) && document_.setParametricOutline(*object, path)) {
                document_.commitGraphEdit(graphBefore, what);
            } else {
                document_.setObjectOutline(*object, path);
                document_.commitGeometry(before, what, frame);
            }
        }
        refresh();
    }

}
