#include "curvy/transformpanel.h"

#include "curvy/numberscrubber.h"

#include <newui/utils.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace curvy {

    namespace {
        constexpr double kPi = 3.14159265358979323846;

        // 12.5 -> "12.5", 100 -> "100", 0.125 -> "0.125" (up to three decimals, trailing zeros dropped).
        std::wstring numberText(double value) {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%.3f", value);
            std::string text = buffer;
            while (!text.empty() && text.back() == '0') { text.pop_back(); }
            if (!text.empty() && text.back() == '.') { text.pop_back(); }
            if (text == "-0" || text.empty()) { text = "0"; }
            return newui::utf8ToWide(text);
        }

        // The number a field holds (a trailing unit such as " pt" is ignored); false when it does not start with one.
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

        const char* kFieldNames[5] = {"transformXField", "transformYField", "transformWField", "transformHField", "transformAngleField"};
    }

    TransformPanel::TransformPanel(newui::RootView& root, SketchDocument& document) : root_(root), document_(document) {
        for (int i = 0; i < 5; ++i) {
            fields_[i] = dynamic_cast<newui::TextField*>(root_.findView(kFieldNames[i]));
            if (fields_[i] != nullptr) {
                Field which = Field(i);
                fields_[i]->onReturnPressed += [this, which](newui::TextField&) {
                    apply(which);
                    return newui::SyncReturn::Handled;
                };
            }
        }
        link_ = dynamic_cast<newui::ToolbarButton*>(root_.findView("linkAspectButton"));
        // Dragging a caption sideways changes its number: positions and sizes by whole points (Shift: tenths, Ctrl+Shift: hundredths),
        // the angle by whole degrees.
        bindScrub(Field::X, "transformXLabel", 1.0, -1.0e6, 1.0e6);
        bindScrub(Field::Y, "transformYLabel", 1.0, -1.0e6, 1.0e6);
        bindScrub(Field::W, "transformWLabel", 1.0, 0.01, 1.0e6);
        bindScrub(Field::H, "transformHLabel", 1.0, 0.01, 1.0e6);
        bindScrub(Field::Angle, "transformAngleLabel", 1.0, -3600.0, 3600.0);
        document_.onChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        document_.onSelectionChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        refresh();
    }

    newui::TextField* TransformPanel::field(Field which) const {
        return fields_[int(which)];
    }

    void TransformPanel::refresh() {
        updating_ = true;
        BLBox box;
        bool any = document_.selectionBounds(box);
        SelectionFrame frame;
        double angle = 0.0;
        if (any && document_.selectionFrame(frame)) {
            angle = frame.angle * 180.0 / kPi;
            angle = std::fmod(angle, 360.0);
            if (angle > 180.0) { angle -= 360.0; }
            if (angle <= -180.0) { angle += 360.0; }
        }
        const double values[5] = {box.x0, box.y0, box.x1 - box.x0, box.y1 - box.y0, angle};
        for (int i = 0; i < 5; ++i) {
            if (fields_[i] != nullptr) {
                fields_[i]->setText(any ? numberText(values[i]) : std::wstring());
                fields_[i]->setEnabled(any);
            }
        }
        if (auto* info = dynamic_cast<newui::Label*>(root_.findView("transformPanelHeaderInfo"))) {
            info->setText(any ? (document_.selection().size() == 1 ? "1 object" : std::to_string(document_.selection().size()) + " objects") : "no selection");
        }
        updating_ = false;
    }

    // The selection's box with the one value `which` replaced (W / H keep the proportions when linked). Angle leaves the box alone.
    BLBox TransformPanel::boxFor(Field which, double value, const BLBox& base, bool linked) const {
        double w = base.x1 - base.x0, h = base.y1 - base.y0;
        switch (which) {
            case Field::X: return BLBox(value, base.y0, value + w, base.y1);
            case Field::Y: return BLBox(base.x0, value, base.x1, value + h);
            case Field::W: {
                double newW = std::max(0.0, value);
                double newH = linked && w > 1e-9 ? h * newW / w : h;
                return BLBox(base.x0, base.y0, base.x0 + newW, base.y0 + newH);
            }
            case Field::H: {
                double newH = std::max(0.0, value);
                double newW = linked && h > 1e-9 ? w * newH / h : w;
                return BLBox(base.x0, base.y0, base.x0 + newW, base.y0 + newH);
            }
            default:
                return base;
        }
    }

    void TransformPanel::bindScrub(Field which, const char* labelName, double unit, double minimum, double maximum) {
        auto* label = dynamic_cast<newui::Label*>(root_.findView(labelName));
        if (label == nullptr) {
            return;
        }
        NumberScrubber::Callbacks callbacks;
        callbacks.begin = [this, which](double& value) {
            scrub_ = Scrub();
            if (updating_ || !document_.selectionBounds(scrub_.box)) {
                return false;
            }
            scrub_.before = document_.snapshotSelection(true);
            scrub_.frameBefore = document_.frameState();
            SelectionFrame frame;
            scrub_.angle = document_.selectionFrame(frame) ? frame.angle : 0.0;
            const double values[5] = {scrub_.box.x0, scrub_.box.y0, scrub_.box.x1 - scrub_.box.x0, scrub_.box.y1 - scrub_.box.y0, scrub_.angle * 180.0 / kPi};
            value = values[int(which)];
            scrub_.active = true;
            return true;
        };
        callbacks.change = [this, which](double value) {
            if (!scrub_.active) {
                return;
            }
            if (which == Field::Angle) {
                scrub_.angleDelta = value * kPi / 180.0 - scrub_.angle;
                double cx = (scrub_.box.x0 + scrub_.box.x1) * 0.5, cy = (scrub_.box.y0 + scrub_.box.y1) * 0.5;
                scrub_.matrix = BLMatrix2D::make_translation(-cx, -cy);
                scrub_.matrix.post_rotate(scrub_.angleDelta);
                scrub_.matrix.post_translate(cx, cy);
            } else {
                scrub_.angleDelta = 0.0;
                scrub_.matrix = SketchDocument::matrixForBox(scrub_.box, boxFor(which, value, scrub_.box, link_ != nullptr && link_->isChecked()));
            }
            document_.previewTransform(scrub_.before, scrub_.matrix, scrub_.angleDelta);
        };
        callbacks.end = [this, which](bool changed) {
            if (!scrub_.active) {
                return;
            }
            scrub_.active = false;
            if (changed) {
                const char* name = which == Field::Angle ? "Rotate" : (which == Field::X || which == Field::Y ? "Move" : "Resize");
                document_.commitGeometry(scrub_.before, name, scrub_.frameBefore);
                document_.noteTransform(scrub_.matrix, scrub_.angleDelta);
            }
            refresh();
        };
        NumberScrubber::attach(*label, unit, minimum, maximum, std::move(callbacks));
    }

    // Enter in a field: that one value changes, the rest stay as the selection has them.
    void TransformPanel::apply(Field which) {
        if (updating_) {
            return;
        }
        BLBox box;
        if (!document_.selectionBounds(box)) {
            return;
        }
        double value = 0.0;
        if (field(which) == nullptr || !parseNumber(field(which)->text(), value)) {
            refresh();   // not a number: put back what the selection has
            return;
        }
        double w = box.x1 - box.x0, h = box.y1 - box.y0;
        bool linked = link_ != nullptr && link_->isChecked();
        switch (which) {
            case Field::X:
                box = BLBox(value, box.y0, value + w, box.y1);
                break;
            case Field::Y:
                box = BLBox(box.x0, value, box.x1, value + h);
                break;
            case Field::W: {
                double newW = std::max(0.0, value);
                double newH = linked && w > 1e-9 ? h * newW / w : h;
                box = BLBox(box.x0, box.y0, box.x0 + newW, box.y0 + newH);
                break;
            }
            case Field::H: {
                double newH = std::max(0.0, value);
                double newW = linked && h > 1e-9 ? w * newH / h : w;
                box = BLBox(box.x0, box.y0, box.x0 + newW, box.y0 + newH);
                break;
            }
            case Field::Angle: {
                SelectionFrame frame;
                double current = document_.selectionFrame(frame) ? frame.angle : 0.0;
                document_.rotateSelectionBy(value * kPi / 180.0 - current);
                refresh();
                return;
            }
        }
        document_.setSelectionBox(box);
        refresh();   // shows what came of it (a rejected or clamped value is put right)
    }

}
