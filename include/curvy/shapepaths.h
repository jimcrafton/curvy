#pragma once

#include <jitnodes/geo/geometry.h>

#include <blend2d/blend2d.h>

#include <cstddef>
#include <optional>
#include <string>

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

}
