#pragma once

#include "curvy/graphdocument.h"

#include <jitnodes/geo/geometry.h>

#include <cstddef>
#include <string>
#include <vector>

namespace curvy {

    // What the Select panel (and a script) can choose shapes by.
    enum class ShapeProperty { Tag, Fill, Stroke, Width, Type, Layer };

    const char* shapePropertyName(ShapeProperty property);

    // The text a shape shows for each property. An absent colour or width is kNoValue.
    // @reflect ignore=true
    struct ShapeValues {
        std::string key;
        std::vector<std::string> tags;   // the node's own tags and those of the layers and groups around it, without the internal ones
        std::string fill;                // "#RRGGBB"
        std::string stroke;              // "#RRGGBB"
        std::string width;               // "2 px" (rounded to half a pixel)
        std::string type;                // the node type that made the shape
        std::string layer;               // the name of the layer around it
    };

    // @reflect ignore=true
    struct ValueCount {
        std::string text;
        std::size_t count = 0;
    };

    inline const char* const kNoValue = "(none)";

    // One entry per keyed shape of the geometry (what the canvas draws), read from the primitive attributes fill.* and stroke.* and from the
    // graph (the node a key names, its tags, the layers around it). Tags with a ':' (the document's own, such as object:xform) and the bare
    // "layer" and "group" are not shown to the user.
    // (The document is not const: finding a node by key refreshes the document's uid index when the graph changed.)
    std::vector<ShapeValues> describeShapes(GraphDocument& document, const jitnodes::Geometry& geometry);

    // The values one shape has for a property: several for Tag (none: kNoValue), one for the others.
    std::vector<std::string> valuesOf(const ShapeValues& shape, ShapeProperty property);

    // Each distinct value over the shapes and how many shapes have it, the most common first.
    std::vector<ValueCount> countValues(const std::vector<ShapeValues>& shapes, ShapeProperty property);

    // The keys of the shapes that have the value, in the order given.
    std::vector<std::string> shapesWithValue(const std::vector<ShapeValues>& shapes, ShapeProperty property, const std::string& value);

}
