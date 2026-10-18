#pragma once

#include <blend2d/blend2d.h>

#include <vector>

namespace curvy {

    // The path as polylines (curves sampled), one per subpath; `closed` when the subpath was closed (the first point is not repeated).
    struct Polyline {
        std::vector<BLPoint> points;
        bool closed = false;
    };

    std::vector<Polyline> flattenPath(const BLPath& path);

    // A flattened subpath with the running length at each point (a closed one ends where it began, so its last point repeats the first).
    struct MeasuredPath {
        std::vector<BLPoint> points;
        std::vector<double> at;
        double total = 0.0;
    };
    // Every subpath of `path` that has length, measured.
    std::vector<MeasuredPath> measurePath(const BLPath& path);
    // The same, for a path in the other direction.
    MeasuredPath reversedPath(const MeasuredPath& path);
    // The point at length `s` along the subpath and the unit direction of the segment it is on. Beyond the ends `s` is clamped, or
    // (`extend`) the path goes on straight along its first / last segment.
    void sampleMeasured(const MeasuredPath& path, double s, BLPoint& point, BLPoint& dir, bool extend = false);

}
