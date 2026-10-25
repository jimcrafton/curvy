#pragma once

#include <blend2d/blend2d.h>
#include <jitnodes/geo/geometry.h>

#include <string>
#include <utility>
#include <vector>

namespace jitnodes {
    class Node;
}

namespace curvy {

    class SketchObject;

    // The graph node type that holds one path object's curves in its geometry parameter "curve": one primitive per sub path, a polyline for a
    // figure of straight lines and a cubic (jitnodes PrimKind) for any other. The node cooks that geometry as it is.
    inline constexpr const char* kPathNodeType = "curvyPath";
    // Text, symbol and image objects keep their outline in "curve" too, plus what shapes it: the words, font and style (text), the definition
    // (symbol) or the picture's name (image). The pixels and definitions stay in the document; a node refers to them.
    inline constexpr const char* kTextNodeType = "curvyText";
    inline constexpr const char* kSymbolNodeType = "curvySymbol";
    inline constexpr const char* kImageNodeType = "curvyImage";

    // Where an object sits: the six numbers of an affine matrix (a b c d e f, as in SVG), wired after the shape node and before the object's
    // appearance nodes. The shape node holds the outline as it was drawn; this node carries it to the page (its output is the shape moved,
    // turned, scaled or sheared by the matrix) and the paints are laid out on the shape before it, so they follow every one of those.
    inline constexpr const char* kTransformNodeType = "curvyTransform";

    bool isShapeNodeType(const std::string& typeName);
    BLMatrix2D transformOf(const jitnodes::Node& node);
    void storeTransform(jitnodes::Node& node, const BLMatrix2D& matrix);
    // A copy of `geometry` with every point (handles included) moved by `matrix`.
    jitnodes::GeometryHandle placeGeometry(const jitnodes::Geometry& geometry, const BLMatrix2D& matrix);
    // The node type that mirrors this object.
    const char* shapeNodeType(const SketchObject& object);
    // The parameters besides "curve" that the object's node holds, as name and text.
    std::vector<std::pair<std::string, std::string>> shapeNodeParams(const SketchObject& object);
    // Gives a text object the words, font, size, placement, style and warp its node holds; false when nothing differed or the text was bad.
    bool applyTextNodeParams(SketchObject& object, const jitnodes::Node& node);

    // Registers curvy's node types with jitnodes' registry; safe to call any number of times.
    void registerCurvyNodes();

    // The path as a curve geometry (float precision), and back. pathFromGeometry takes straight cubic segments back as lines and returns false for
    // a geometry that is not well formed. keepAttributes: point attributes of an older geometry carried over when the shape's layout is unchanged.
    jitnodes::GeometryHandle geometryFromPath(const BLPath& path, const jitnodes::Geometry* keepAttributes = nullptr);
    bool pathFromGeometry(const jitnodes::Geometry& geometry, BLPath& path);
    // Same primitives and, when `positions` is set, x and y within `tolerance` (relative to 1 + the coordinate; 0 = identical); other
    // attributes are ignored.
    bool sameCurveShape(const jitnodes::Geometry& a, const jitnodes::Geometry& b, double tolerance = 0.0, bool positions = true);

    // The path as text (M, L, Q, C and Z), numbers to six significant digits, the precision the SVG and .skb files keep.
    std::string pathToText(const BLPath& path);
    // False when `text` is not in the form pathToText() writes.
    bool pathFromText(const std::string& text, BLPath& path);

}
