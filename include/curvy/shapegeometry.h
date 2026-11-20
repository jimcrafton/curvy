#pragma once

#include <jitnodes/geo/geometry.h>

#include <blend2d/blend2d.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace curvy {

    // @reflect ignore=true
    struct ShapeHit {
        std::size_t shape = 0;       // ordinal in geometry.shapes()
        std::uint32_t first = 0;     // its first primitive
        std::string key;             // empty for an anonymous shape
    };

    // What a marquee takes: Inside = every point of the shape's outline lies in the region; Touches = any point of the outline lies in the
    // region, or the region's edge crosses the shape's fill or stroke (so a region wholly inside a big filled shape touches it).
    enum class RegionMode { Inside, Touches };

    // A cooked geometry seen as shapes (the primitives with one key, a compound shape has holes): their paths, what lies under a point, and what a
    // region takes. It only looks at the geometry, which must outlive it; drawing, bounds, hit testing and marquee picking all go through it.
    // @reflect ignore=true
    class ShapeGeometry {
    public:
        explicit ShapeGeometry(const jitnodes::Geometry& geometry) : geometry_(geometry) {}

        const jitnodes::Geometry& geometry() const { return geometry_; }

        // The path of one shape (all its primitives), with the points mapped to origin + point * scale.
        BLPath path(const jitnodes::ShapeRange& shape, double originX, double originY, double scale) const;

        // The topmost shape (drawn last) that covers (x, y), in the geometry's own units: inside its fill, or within `tolerance` of its stroke
        // (a stroke thinner than twice the tolerance counts as that wide). A shape with neither a fill nor a stroke is not hit.
        std::optional<ShapeHit> hitAt(float x, float y, float tolerance) const;

        // The keys of the shapes a region takes, in drawing order, each once. `polygon` is the region in the geometry's units (a rectangle, an
        // ellipse, a freehand loop and a polygon are all this), filled with the even-odd rule and closed by itself. A shape with neither a fill
        // nor a stroke (nothing to see) and an anonymous shape are skipped.
        std::vector<std::string> keysInRegion(const std::vector<BLPoint>& polygon, RegionMode mode) const;

        // The region of a rectangle given by two opposite corners.
        static std::vector<BLPoint> rectRegion(double x0, double y0, double x1, double y1);
        // The region of an ellipse inscribed in the rectangle given by two opposite corners.
        static std::vector<BLPoint> ellipseRegion(double x0, double y0, double x1, double y1);

    private:
        const jitnodes::Geometry& geometry_;
    };

}
