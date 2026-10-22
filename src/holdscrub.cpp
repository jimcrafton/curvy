#include "curvy/holdscrub.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace curvy {

    namespace {
        const std::vector<double> kPercents = {0, 25, 50, 75, 100};
        const std::vector<double> kHues = {0, 60, 120, 180, 240, 300, 360};

        AppearanceKind kindOf(ScrubPart part) { return part == ScrubPart::Stroke ? AppearanceKind::Stroke : AppearanceKind::Fill; }
    }

    const std::vector<ScrubProp>& HoldScrub::props(ScrubPart part) {
        static const std::vector<ScrubProp> stroke = {
            {'W', "Width", 0.5, 40.0, 0.08, " pt", 1, {1, 2, 4, 8, 16, 32}, false},
            {'O', "Opacity", 0.0, 100.0, 0.4, "%", 0, kPercents, false},
            {'H', "Hue", 0.0, 360.0, 1.0, "\xC2\xB0", 0, kHues, true}};
        static const std::vector<ScrubProp> fill = {
            {'O', "Opacity", 0.0, 100.0, 0.4, "%", 0, kPercents, false},
            {'H', "Hue", 0.0, 360.0, 1.0, "\xC2\xB0", 0, kHues, true},
            {'B', "Brightness", 0.0, 100.0, 0.3, "%", 0, {25, 50, 75, 90}, true}};
        static const std::vector<ScrubProp> none;
        return part == ScrubPart::Stroke ? stroke : part == ScrubPart::Fill ? fill : none;
    }

    const ScrubProp* HoldScrub::propFor(const SketchObject& object, ScrubPart part, char key) {
        for (const ScrubProp& prop : props(part)) {
            if (prop.key != key) {
                continue;
            }
            if (prop.needsColor) {
                const SketchPaint& paint = part == ScrubPart::Stroke ? object.stroke() : object.fill();
                if (paint.kind() != SketchPaint::Kind::Solid) {
                    return nullptr;
                }
            }
            return &prop;
        }
        return nullptr;
    }

    ScrubPart HoldScrub::partAt(const SketchObject& object, const BLPoint& point, double tolerance) {
        double t = 0.0, distance = 0.0;
        if (!object.stroke().isNone() && object.nearestOnPath(point, t, distance) && distance <= object.strokeWidth() * 0.5 + tolerance) {
            return ScrubPart::Stroke;
        }
        if (object.isClosed() && !object.fill().isNone() && object.geometry().hit_test(point, BL_FILL_RULE_NON_ZERO) == BL_HIT_TEST_IN) {
            return ScrubPart::Fill;
        }
        return ScrubPart::None;
    }

    std::string HoldScrub::format(const ScrubProp& prop, double value) {
        char text[48];
        std::snprintf(text, sizeof(text), "%.*f%s", prop.digits, value, prop.unit);
        return text;
    }

    double HoldScrub::read(const SketchObject& object) const {
        const AppearanceKind kind = kindOf(part_);
        switch (prop_->key) {
            case 'W': return object.strokeWidth();
            case 'O': return object.activeItem(kind).opacity * 100.0;
            case 'H': return startPaint_.color().convertedTo(ColorSpace::HSB).component(0) * 360.0;
            default: return startPaint_.color().convertedTo(ColorSpace::HSB).component(2) * 100.0;
        }
    }

    void HoldScrub::write(SketchDocument& document, double value) {
        const AppearanceKind kind = kindOf(part_);
        switch (prop_->key) {
            case 'W':
                document.setSelectionStrokeWidth(value);
                break;
            case 'O':
                document.setSelectionItemOpacity(kind, value / 100.0);
                break;
            default: {
                const SketchColor hsb = startPaint_.color().convertedTo(ColorSpace::HSB);
                const bool hue = prop_->key == 'H';
                const SketchPaint paint = SketchPaint::solid(SketchColor::hsb(hue ? float(value / 360.0) : hsb.component(0), hsb.component(1),
                    hue ? hsb.component(2) : float(value / 100.0), hsb.alpha()));
                if (part_ == ScrubPart::Stroke) {
                    document.setSelectionStroke(paint);
                } else {
                    document.setSelectionFill(paint);
                }
                break;
            }
        }
    }

    bool HoldScrub::begin(SketchDocument& document, std::size_t objectId, ScrubPart part, char key, double travel) {
        const SketchObject* object = document.findById(objectId);
        if (object == nullptr) {
            return false;
        }
        const ScrubProp* prop = propFor(*object, part, key);
        if (prop == nullptr) {
            return false;
        }
        document.setSelection({objectId});
        active_ = true;
        part_ = part;
        prop_ = prop;
        objectId_ = objectId;
        startTravel_ = travel;
        startPaint_ = part == ScrubPart::Stroke ? object->stroke() : object->fill();
        startValue_ = value_ = read(*object);
        before_ = document.snapshotStyle();
        return true;
    }

    double HoldScrub::update(SketchDocument& document, double travel, bool snap) {
        double value = startValue_ + (travel - startTravel_) * prop_->rate;
        if (snap && !prop_->snaps.empty()) {
            value = *std::min_element(prop_->snaps.begin(), prop_->snaps.end(), [&](double a, double b) { return std::fabs(a - value) < std::fabs(b - value); });
        }
        value_ = std::clamp(value, prop_->min, prop_->max);
        write(document, value_);
        return value_;
    }

    void HoldScrub::finish(SketchDocument& document, bool keep) {
        if (!active_) {
            return;
        }
        active_ = false;
        if (keep) {
            document.commitStyle(before_, std::string("Scrub ") + prop_->name);
        } else {
            write(document, startValue_);
        }
        before_.clear();
    }

}
