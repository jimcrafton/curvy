#pragma once

#include "curvy/graphdocument.h"

#include <jitnodes/geo/geometry.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace curvy {

    // What the Select panel (and a script) can choose shapes by.
    enum class ShapeProperty { Tag, Fill, Stroke, Width, Type, Layer };

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

    // The shapes of the artwork as rows of values to choose by: one row per keyed shape, read from the primitive attributes fill.* and stroke.*
    // and from the graph (the node a key names, its tags, the layers around it). Tags with a ':' (the document's own, such as object:xform) and
    // the bare "layer" and "group" are not shown to the user. A table is a snapshot: build a new one after the document changes. It is what the
    // Select panel lists and what a script's shapes_where filters; narrowing gives another table, so criteria combine.
    // @reflect ignore=true
    class ShapeTable {
    public:
        ShapeTable() = default;
        // (The document is not const: finding a node by key refreshes the document's uid index when the graph changed.)
        ShapeTable(GraphDocument& document, const jitnodes::Geometry& geometry);

        static const char* propertyName(ShapeProperty property);
        // The values one shape has for a property: several for Tag (none: kNoValue), one for the others.
        static std::vector<std::string> valuesOf(const ShapeValues& shape, ShapeProperty property);

        const std::vector<ShapeValues>& rows() const { return rows_; }
        bool empty() const { return rows_.empty(); }
        void clear() { rows_.clear(); }

        // The rows `keep` accepts, in the same order.
        ShapeTable filtered(const std::function<bool(const ShapeValues&)>& keep) const;
        // The rows that have the value.
        ShapeTable having(ShapeProperty property, const std::string& value) const;
        // Each distinct value over the rows and how many rows have it, the most common first.
        std::vector<ValueCount> counts(ShapeProperty property) const;
        // The keys of the rows that have the value, in the order given.
        std::vector<std::string> keysWith(ShapeProperty property, const std::string& value) const;
        // The keys of all the rows.
        std::vector<std::string> keys() const;

    private:
        std::vector<ShapeValues> rows_;
    };

}
