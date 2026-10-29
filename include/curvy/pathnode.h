#pragma once

#include <blend2d/blend2d.h>
#include <jitnodes/geo/geometry.h>
#include <jitnodes/graph/network.h>

#include <string>
#include <utility>
#include <vector>

namespace jitnodes {
    class Node;
}

namespace curvy {

    class SketchObject;
    struct PaintWarp;

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

    // A group or layer: a jitnodes subnet whose child network holds its members' node chains and whose display node is what it shows. `role` is
    // "group" or "layer". The placement numbers are those of the transform node and are promoted, so a member reads them as `parent.rotation`;
    // the cook carries the contents by that matrix.
    inline constexpr const char* kSubnetNodeType = "curvySubnet";

    // Tags some of a shape's points: it passes its input through and adds the point group `group` (jitnodes' "group.<name>" int attribute).
    // Who is in it comes from the selectors: `ids` (point ids as "3,7,10-14"), `every`/`offset` (every nth anchor), `box` (inside x0,y0,x1,y1 of
    // the shape's own space) - a point matching any enabled one - narrowed by `kind` (any, anchors, handles, corners, smooth), then joined with a
    // group of the same name already on the input by `operation` (replace, union, subtract, intersect). A handle follows its anchor's tag.
    inline constexpr const char* kTagNodeType = "curvyTag";
    // Moves some of a shape's points: the transform node's translate, rotate, scale, shear and pivot (in the shape's own space), applied to the points
    // of the point group `group` (every point when it is empty; none when the input has no such group). It sits before the placement node, which
    // still places the whole result, so an object with one is *driven*: its outline is what the chain cooks, not what the object last drew.
    inline constexpr const char* kMovePointsNodeType = "curvyMovePoints";
    // The int point attribute that names every point for good: it travels with the point through the nodes, so a tag by id survives edits that
    // renumber the points. geometryFromPath gives new points fresh ids and keeps those of points that stayed.
    inline constexpr const char* kPointIdAttrib = "id";

    // A tag node's `ids` text as a sorted list ("3,7,10-12" -> 3 7 10 11 12) and back (runs written as ranges).
    std::vector<int> pointIdList(const std::string& text);
    std::string pointIdText(std::vector<int> ids);

    bool isShapeNodeType(const std::string& typeName);
    // The shape node behind `node`, looking through tag and move-points nodes (they sit between a shape and its transform).
    jitnodes::NodeId shapeBehind(const jitnodes::Network& network, jitnodes::NodeId node);
    // The node's own placement (relative to its parent, when it has one); worldTransformOf follows the parent links to the page.
    BLMatrix2D transformOf(const jitnodes::Node& node);
    // The same from the values the node last resolved (an expression or keys give their value at the document's time); what a cook uses.
    BLMatrix2D resolvedTransformOf(const jitnodes::Node& node);
    // Within one network: the parent links of transform nodes only. The matrices of the subnets holding the network come on top (see SketchDocument::scopeMatrix).
    BLMatrix2D worldTransformOf(const jitnodes::Network& network, jitnodes::NodeId place);
    void storeTransform(jitnodes::Node& node, const BLMatrix2D& matrix);
    // The perspective map of the object's paints (identity when never warped), kept on the transform node beside its placement as text at the
    // precision of a saved file (six digits); a node that already holds the text is left alone. paintWarpDiffers: the node's text is not what
    // this map would be written as, i.e. the node is the newer.
    void storePaintWarp(jitnodes::Node& node, const PaintWarp& warp);
    bool paintWarpDiffers(const jitnodes::Node& node, const PaintWarp& warp);
    PaintWarp paintWarpOf(const jitnodes::Node& node);
    // Puts the pivot at the middle of `shape`'s bounds, so rotating or scaling in the graph turns the shape about its centre.
    // Moves the pivot (a point in the shape's own space) without moving the shape: translate takes up the difference.
    void setTransformPivot(jitnodes::Node& node, double x, double y);
    // The pivot as the dock shows and edits it: where it lands in the node's output space (the parent's, or the page), pivot + translate.
    // Editing one axis moves it along that axis of the parent space, whatever the node's rotation; the node still stores the local pivot.
    bool pivotShownInParent(const jitnodes::Node& node, const std::string& name);
    double shownParam(const jitnodes::Node& node, const std::string& name);
    void setPivotInParent(jitnodes::Node& node, const std::string& name, double value);
    void centerTransformPivot(jitnodes::Node& node, const jitnodes::Geometry& shape);
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
    // a geometry that is not well formed. keepAttributes: point attributes of an older geometry carried over - all of them when the shape's layout
    // is unchanged, otherwise the id and group flags of the points that are still where they were (found in order, so inserting or deleting
    // points leaves the others their ids), with fresh ids for the rest.
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
