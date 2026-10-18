#include "curvy/sketchdocument.h"

#include <algorithm>
#include <cctype>

// The document's swatches (a list of saved colors as hex text, shown in the Color panel) and the Eyedropper.

namespace curvy {

    namespace {
        constexpr std::size_t kMaxSwatches = 24;   // two rows of the Color panel grid

        std::string lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        }
    }

    std::vector<std::string> defaultSwatches() {
        return {"#000000", "#555555", "#aaaaaa", "#ffffff", "#e8452c", "#f2b824",
                "#2f7d4f", "#0078d4", "#6a4fc7", "#1b3a6b", "#00aeef", "#ec008c"};
    }

    void SketchDocument::setSwatches(std::vector<std::string> swatches) {
        if (swatches.size() > kMaxSwatches) {
            swatches.resize(kMaxSwatches);
        }
        swatches_ = std::move(swatches);
        updateAllViews();
        onChanged(*this);
    }

    bool SketchDocument::addSwatch(const std::string& hex) {
        std::string value = lower(hex);
        if (value.size() != 7 || value[0] != '#' || swatches_.size() >= kMaxSwatches ||
            std::find(swatches_.begin(), swatches_.end(), value) != swatches_.end()) {
            return false;
        }
        std::vector<std::string> before = swatches_, after = before;
        after.push_back(value);
        undoStack_.push({"Add Swatch", [this, after]() { swatches_ = after; changed(); }, [this, before]() { swatches_ = before; changed(); }});
        return true;
    }

    bool SketchDocument::deleteSwatch(std::size_t index) {
        if (index >= swatches_.size()) {
            return false;
        }
        std::vector<std::string> before = swatches_, after = before;
        after.erase(after.begin() + std::ptrdiff_t(index));
        undoStack_.push({"Delete Swatch", [this, after]() { swatches_ = after; changed(); }, [this, before]() { swatches_ = before; changed(); }});
        return true;
    }

    // The Eyedropper: the look (fills, strokes, effects, opacity, blend mode) of the object under `at` goes onto every selected object -
    // or, when nothing else is selected, becomes what new shapes are drawn with.
    bool SketchDocument::sampleLook(const BLPoint& at, double tolerance) {
        SketchObject* source = hitTest(at, tolerance);
        if (source == nullptr) {
            return false;
        }
        bool others = false;
        for (std::size_t id : selection_) {
            others = others || id != source->id();
        }
        if (!others) {
            setDefaultFill(source->fill());
            setDefaultStroke(source->stroke());
            setDefaultStrokeWidth(source->strokeWidth());
            setDefaultStrokeOptions(source->strokeOptions());
            return true;
        }
        std::vector<StyleState> before = snapshotStyle();
        const std::vector<AppearanceItem> items = source->appearance();
        const double opacity = source->opacity();
        const BlendMode blend = source->blendMode();
        std::size_t fill = source->activeFillIndex(), stroke = source->activeStrokeIndex(), effect = source->activeEffectIndex();
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                if (object != source) {
                    object->setAppearance(items);
                    object->setActiveIndices(fill, stroke, effect);
                    object->setOpacity(opacity);
                    object->setBlendMode(blend);
                }
            }
        }
        changed();
        return commitStyle(before, "Eyedropper");
    }

}
