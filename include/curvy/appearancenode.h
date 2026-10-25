#pragma once

#include "curvy/sketchobject.h"

#include <jitnodes/graph/network.h>
#include <jitnodes/graph/node.h>

#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace curvy {

    // One entry of an object's appearance stack is one graph node, wired after the object's shape node and the entry below it:
    //   curvyFill   - a paint (none, solid, gradient or pattern), opacity, blend mode, visibility
    //   curvyStroke - the same, plus weight, cap, join, miter, dashes, width profile, alignment, arrows and brush
    //   curvyEffect - a live effect with its amount, tint, softness, offset and radius
    // The nodes pass their input geometry on unchanged: they carry the look as parameters. Colours are held in their own colour space, the
    // gradient stops, dashes and width points as small geometry tables (one point per row).
    inline constexpr const char* kFillNodeType = "curvyFill";
    inline constexpr const char* kStrokeNodeType = "curvyStroke";
    inline constexpr const char* kEffectNodeType = "curvyEffect";

    bool isAppearanceNodeType(const std::string& typeName);
    const char* appearanceNodeType(AppearanceKind kind);
    // Registers the three node types with jitnodes' registry (registerCurvyNodes() calls it).
    void registerAppearanceNodes();

    // What a node holds for an item, as plain values: every float parameter that applies to the item (a solid paint has no gradient
    // parameters) and the tables, whose rows are the columns of the table in order. Two items are the same look exactly when their params
    // are equal, which is how the document tells which of the object and its node changed.
    struct AppearanceParams {
        std::map<std::string, float> numbers;
        std::map<std::string, std::vector<std::vector<float>>> tables;

        bool operator==(const AppearanceParams& other) const { return numbers == other.numbers && tables == other.tables; }
        bool operator!=(const AppearanceParams& other) const { return !(*this == other); }
    };

    // Finds the library entries an item refers to by id.
    struct AppearanceLibrary {
        std::function<std::shared_ptr<const SymbolDef>(std::size_t)> pattern;
        std::function<std::shared_ptr<const BrushDef>(std::size_t)> brush;
    };

    // The appearance nodes of a network by the node they take their input from, and the chain that follows a shape node through them.
    std::unordered_map<jitnodes::NodeId, jitnodes::NodeId> appearanceSuccessors(const jitnodes::Network& network, const std::vector<jitnodes::NodeId>& ids);
    std::vector<jitnodes::NodeId> appearanceChain(const std::unordered_map<jitnodes::NodeId, jitnodes::NodeId>& successors, jitnodes::NodeId shape);

    AppearanceParams paramsOf(const AppearanceItem& item);
    AppearanceParams paramsOf(const jitnodes::Node& node);
    void store(jitnodes::Node& node, const AppearanceParams& params);
    // `base` with what `params` changed relative to `baseParams` (paramsOf(base)): untouched values keep their full precision.
    AppearanceItem itemFrom(const AppearanceParams& params, const AppearanceParams& baseParams, const AppearanceItem& base, const AppearanceLibrary& library);

}
