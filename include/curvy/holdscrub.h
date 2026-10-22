#pragma once

#include "curvy/sketchdocument.h"

#include <string>
#include <vector>

namespace curvy {

    enum class ScrubPart { None, Stroke, Fill };

    // One number that can be scrubbed on the outline or the fill of an object: hold its key and move the pen. Values are in the unit
    // shown to the user (percent, degrees, points); `rate` is how much the value changes per pixel of travel.
    // @reflect ignore=true
    struct ScrubProp {
        char key;
        const char* name;
        double min, max, rate;
        const char* unit;
        int digits;
        std::vector<double> snaps;   // Shift jumps to the nearest of these
        bool needsColor;             // Hue and Brightness only mean something on a solid paint
    };

    // Hold-key scrubbing of an object's look (docs/ui-design.md, "Hold-key scrubbing"). A session starts when the key goes down over
    // an outline or a fill, follows the pointer's travel live without undo steps, and becomes one undo step when the key is let go
    // (or is put back exactly on Esc).
    // @reflect ignore=true
    class HoldScrub {
    public:
        static const std::vector<ScrubProp>& props(ScrubPart part);
        // The prop for `key` ("W", "O"...) on `part` that this object can take; null when there is none.
        static const ScrubProp* propFor(const SketchObject& object, ScrubPart part, char key);
        // Which part of `object` the point is on: the outline when within its half width plus `tolerance`, else the fill; None otherwise.
        static ScrubPart partAt(const SketchObject& object, const BLPoint& point, double tolerance);
        static std::string format(const ScrubProp& prop, double value);

        // Selects the object and starts scrubbing at the travel `travel` (pixels along the scrub axis). False when the key does nothing here.
        bool begin(SketchDocument& document, std::size_t objectId, ScrubPart part, char key, double travel);
        // `travel` is the pointer's position along the axis now; Shift snaps. Returns the new value.
        double update(SketchDocument& document, double travel, bool snap);
        void finish(SketchDocument& document, bool keep);

        bool active() const { return active_; }
        ScrubPart part() const { return part_; }
        const ScrubProp& prop() const { return *prop_; }
        std::size_t objectId() const { return objectId_; }
        double value() const { return value_; }
        double startValue() const { return startValue_; }

    private:
        double read(const SketchObject& object) const;
        void write(SketchDocument& document, double value);

        bool active_ = false;
        ScrubPart part_ = ScrubPart::None;
        const ScrubProp* prop_ = nullptr;
        std::size_t objectId_ = 0;
        double startTravel_ = 0.0, startValue_ = 0.0, value_ = 0.0;
        SketchPaint startPaint_;
        std::vector<StyleState> before_;
    };

}
