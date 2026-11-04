#pragma once

#include "curvy/shapetools.h"
#include "curvy/sketchobject.h"

#include <cstddef>

namespace curvy {

    // A star, polygon, rounded rectangle or arc recognised from its outline alone (shapetools.h), so nothing extra is stored and files and
    // shapes made before are covered. The canvas handles and the Shape box of the property dock edit the same numbers.
    enum class LiveKind { None, RoundRect, Polygon, Star, Arc };

    // @reflect ignore=true
    struct LiveShape {
        LiveKind kind = LiveKind::None;
        std::size_t object = 0;
        RegularShape regular;   // polygon, star
        RoundRectShape rect;    // rounded rectangle
        ArcShape arc;           // arc
    };

    // True (and fills `out`) when `object` is one of those shapes in its parameter form. A squashed or hand-edited shape is not.
    bool recogniseLiveShape(const SketchObject& object, LiveShape& out);
    // The outline the numbers of `shape` make.
    BLPath liveShapePath(const LiveShape& shape);

}
