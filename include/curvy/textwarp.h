#pragma once

#include "curvy/pathflatten.h"
#include "curvy/sketchobject.h"

namespace curvy {

    // Carries text outlines from the flat page of the text (the first baseline on y = 0, x running along it, y down) onto paths:
    //  - OnPath: the baseline follows the path from `start` percent of its length, the text standing on its left (up, for a path that
    //    runs to the right); `flip` puts it on the other side. Past the end of the path the text goes on straight.
    //  - Between: the text is stretched between two paths - its length over the whole length of both, its height from the top path
    //    (the top of the letters) to the bottom one (just under the baseline of the last line) - like an envelope.
    // The measures (`x0`..`x1` the text across, `yTop`..`yBottom` its height) come from the text, see SketchObject::rebuildText.
    // @reflect ignore=true
    class TextWarper {
    public:
        TextWarper(const TextWarp& warp, double x0, double x1, double yTop, double yBottom);
        // False when there is nothing to follow (a path with no length, no path at all).
        bool valid() const { return valid_; }
        BLPoint map(double x, double y) const;
        // The outline with every point carried over; curves are cut into short pieces first so that they bend with the path.
        BLPath apply(const BLPath& outline) const;

    private:
        TextWarp::Kind kind_ = TextWarp::Kind::None;
        MeasuredPath first_, second_;
        double start_ = 0.0, x0_ = 0.0, x1_ = 1.0, yTop_ = 0.0, yBottom_ = 1.0;
        bool valid_ = false;
    };

}
