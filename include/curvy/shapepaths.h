#pragma once

#include <jitnodes/geo/geometry.h>

#include <blend2d/blend2d.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace curvy {

    // The path of one shape (all its primitives, a compound shape has holes), with the points mapped to origin + point * scale.
    BLPath shapePath(const jitnodes::Geometry& geometry, const jitnodes::ShapeRange& shape, double originX, double originY, double scale);

    // @reflect ignore=true
    struct ShapeHit {
        std::size_t shape = 0;       // ordinal in geometry.shapes()
        std::uint32_t first = 0;     // its first primitive
        std::string key;             // empty for an anonymous shape
    };

    // The topmost shape (drawn last) that covers (x, y), in the geometry's own units: inside its fill, or within `tolerance` of its stroke
    // (a stroke thinner than twice the tolerance counts as that wide). A shape with neither a fill nor a stroke is not hit.
    std::optional<ShapeHit> shapeAt(const jitnodes::Geometry& geometry, float x, float y, float tolerance);

    // What a marquee takes: Inside = every point of the shape's outline lies in the region; Touches = any point of the outline lies in the
    // region, or the region's edge crosses the shape's fill or stroke (so a region wholly inside a big filled shape touches it).
    enum class RegionMode { Inside, Touches };

    // The keys of the shapes a region takes, in drawing order, each once. `polygon` is the region in the geometry's units (a rectangle, an
    // ellipse, a freehand loop and a polygon are all this), filled with the even-odd rule and closed by itself. A shape with neither a fill
    // nor a stroke (nothing to see) and an anonymous shape are skipped.
    std::vector<std::string> shapesInRegion(const jitnodes::Geometry& geometry, const std::vector<BLPoint>& polygon, RegionMode mode);

    // The region of a rectangle given by two opposite corners.
    std::vector<BLPoint> rectRegion(double x0, double y0, double x1, double y1);
    // The region of an ellipse inscribed in the rectangle given by two opposite corners.
    std::vector<BLPoint> ellipseRegion(double x0, double y0, double x1, double y1);

}
